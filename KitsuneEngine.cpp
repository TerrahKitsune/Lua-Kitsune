#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#endif

#include <cassert>  // assert() is a no-op in release builds (NDEBUG defined by MSVC /MD /MT)
#include <cstdint>  // int64_t
#include <atomic>   // std::atomic
#include <chrono>   // portable timing fallback
#include <condition_variable> // std::condition_variable
#include <mutex>    // std::mutex
#include <new>      // std::nothrow
#include <thread>   // std::thread (used in Task 7; included here with atomic)

#ifdef _WIN32
// WinSock2 must be included before windows.h or any headers that include it
#include <WinSock2.h>
#endif
#include "platform.h"

#ifdef KITSUNE_HTTP
#include "HttpCurlMain.h"
#endif

#include "mem.h"
#include "lua_main_incl.h"

#ifdef KITSUNE_MYSQL
#include "MySQLMain.h"
#endif
#ifdef KITSUNE_POSTGRES
#include "PostgresMain.h"
#endif
#include "ProcessMain.h"

#ifdef KITSUNE_KAFKA
#include "luakafkamain.h"
#endif
#ifdef KITSUNE_ARCHIVE
#include "LuaArchiveMain.h"
#endif
#ifdef KITSUNE_REDIS
#include "RedisMain.h"
#endif

#include "LuaMutexMain.h"

#include "lua_misc.h"
#include "MD5Main.h"
#include "LuaAesMain.h"
#include "LuaSQLiteMain.h"
#include "TimerMain.h"
#include "LuaFileSystemMain.h"
#include "StreamMain.h"
#include "streamshmemory.h"
#include "Sha256Main.h"
#include "luajsonmain.h"
#include "luajson.h"
#include "base64.h"
#include "wcharmain.h"
#include "luawchar.h"
#include "LuaCsvMain.h"
#include "SHA1Main.h"

#include "KitsuneEngine.h"
#include "LuaEngineBuiltins.h"

// Unique address used as the Lua registry key for the shared bridge LuaJson instance.
// Defined in luajson.cpp; accessed via lua_json_bridge_registry_key().

// ── Portable auto-reset event (replaces Win32 HANDLE-based WinEvent) ─────────
// Uses std::condition_variable so it works on all platforms without any
// OS handle.  The default constructor initialises the event to un-signaled;
// no separate Create() call is required.
struct PlatformEvent {
	std::mutex              mtx;
	std::condition_variable cv;
	bool                    signaled = false;

	void Set() {
		{ std::lock_guard<std::mutex> lk(mtx); signaled = true; }
		cv.notify_one();
	}
	void Wait() {
		std::unique_lock<std::mutex> lk(mtx);
		cv.wait(lk, [this] { return signaled; });
		signaled = false;
	}
	bool WaitFor(uint32_t ms) {
		std::unique_lock<std::mutex> lk(mtx);
		bool r = cv.wait_for(lk, std::chrono::milliseconds(ms), [this] { return signaled; });
		if (r)
			signaled = false;
		return r;
	}
};

// ── Per-coroutine slot ────────────────────────────────────────────────────────
struct KitsuneCoroutine {
	int           id;
	int           threadRef;    // LUA_REGISTRYINDEX anchor; keeps the thread alive for GC
	lua_State* thread;       // cached lua_State*; valid iff threadRef != LUA_NOREF.
	// Must be NULLed whenever threadRef is unref'd.
	// Written only under AcquireLuaAccess; read only by the scheduler.
	int               argsRef;        // LUA_REGISTRYINDEX anchor for the ARGS table; retrieved by GetArgs()
	std::atomic<long> fireAndForget{0};
	std::atomic<long> done{0};        // 0 = still running / yielded, 1 = finished
	std::atomic<long> released{0};    // 1 = slot should be freed; scheduler zeros it on next compaction
	std::atomic<long> interrupted{0}; // set to 1 by KitsuneCancel; observed by the scheduler before resuming this coroutine
	char* error;
	KitsuneVariable  result;
	double        sleepUntil;   // GetCounter deadline (ms) before which the coroutine must not be resumed; 0 = not sleeping
	double        startTime;    // GetCounter value recorded when the coroutine was created
	int           initialNArgs; // number of args already on the thread stack for the first lua_resume; 0 for file/string coroutines
};

#define KITSUNE_MAX_COROUTINES 256
// Maximum depth for recursive Lua-table → linked-list conversion; prevents stack overflow on deeply nested or circular tables.
#define KITSUNE_MAX_TABLE_DEPTH 32

// ── Engine state ──────────────────────────────────────────────────────────────
struct KitsuneState {
	// ── Lua ──────────────────────────────────────────────────────────────────
	lua_State* L;
	double           PCFreq;
	int64_t          CounterStart;
	lua_State* DelegateState; // calling coroutine's state during a RegisterFunction call
	char* lastCallError;  // deferred KITSUNE_TERROR message; freed after args cleanup

	// ── Interrupt / pause ────────────────────────────────────────────────────
	std::atomic<long> interrupt{0};   // set by KitsuneInterrupt; cleared by scheduler when all done
	std::atomic<long> pauseFlag{0};   // set by AcquireLuaAccess; serviced by hook + scheduler
	PlatformEvent pausedEvent;   // hook signals this when it parks
	PlatformEvent resumeEvent;   // AcquireLuaAccess signals this to let hook continue

	// ── SetVariable/GetVariable serialisation ────────────────────────────────
	std::mutex       accessLock; // serialises concurrent external callers

	// ── Scheduler thread ─────────────────────────────────────────────────────
	std::thread           schedulerThread;
	std::atomic<uint32_t> schedulerThreadId{0}; // OS thread ID set at scheduler startup; guards are scoped to this thread only
	std::atomic<long>     schedulerStop{0}; // set to 1 by KitsuneCleanup
	PlatformEvent         workEvent;     // signaled when a new coroutine is ready to run

	// ── Active coroutine slots (written only by scheduler; read by callers) ──
	KitsuneCoroutine* slots[KITSUNE_MAX_COROUTINES];
	int               slotCount;
	std::mutex        slotsLock; // guards add/remove of slots[] entries

	// ── Counters ─────────────────────────────────────────────────────────────
	std::atomic<long> nextId{0};             // monotonically increasing coroutine ID
	std::atomic<long> runningCount{0};       // number of slots where done == 0
	std::atomic<long> currentCoroutineId{0}; // ID of the coroutine currently inside lua_resume, or 0
};

#ifdef _WIN32
BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	return TRUE;
}
#endif

#ifdef _WIN32
static void StartCounter(KitsuneState* state) {
	LARGE_INTEGER li;
	QueryPerformanceFrequency(&li);
	state->PCFreq = double(li.QuadPart) / 1000.0;
	QueryPerformanceCounter(&li);
	state->CounterStart = li.QuadPart;
}
static double GetCounter(KitsuneState* state) {
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return double(li.QuadPart - state->CounterStart) / state->PCFreq;
}
#else
// Fallback using std::chrono::steady_clock; CounterStart stores microseconds since epoch,
// PCFreq = 1000 converts to milliseconds via (now - CounterStart) / PCFreq.
static void StartCounter(KitsuneState* state) {
	state->PCFreq = 1000.0;
	state->CounterStart = std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}
static double GetCounter(KitsuneState* state) {
	const int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
	return double(now - state->CounterStart) / state->PCFreq;
}
#endif

static void SetSlotError(KitsuneCoroutine* slot, const char* msg) {
	gff_free(slot->error);
	slot->error = NULL;
	if (msg) {
		size_t len = strlen(msg);
		slot->error = (char*)gff_malloc(len + 1);
		if (slot->error)
			memcpy(slot->error, msg, len + 1);
	}
}

// Forward declaration — defined after FillKitsuneVariableFromStack to allow mutual recursion for nested tables.
static KeyValuePairKitsuneVariableNode* TableToLinkedList(lua_State* L, int idx, int depth);

// Recursively frees a KeyValuePairKitsuneVariableNode linked list produced by TableToLinkedList.
static void FreeKVNode(KeyValuePairKitsuneVariableNode* node) {
	while (node) {
		if ((node->key.type == LUA_TSTRING || node->key.type == KITSUNE_TJSON || node->key.type == KITSUNE_TCHAR16 || node->key.type == KITSUNE_TERROR || node->key.type == LUA_TUSERDATA) && node->key.data)
			gff_free(node->key.data);
		else if (node->key.type == LUA_TTABLE && node->key.table)
			FreeKVNode(node->key.table);
		if ((node->value.type == LUA_TSTRING || node->value.type == KITSUNE_TJSON || node->value.type == KITSUNE_TCHAR16 || node->value.type == KITSUNE_TERROR || node->value.type == LUA_TUSERDATA) && node->value.data)
			gff_free(node->value.data);
		else if (node->value.type == LUA_TTABLE && node->value.table)
			FreeKVNode(node->value.table);
		KeyValuePairKitsuneVariableNode* next = node->next;
		gff_free(node);
		node = next;
	}
}

// Frees the heap data owned by a KitsuneVariable (string bytes or table linked list).
// Nulls the data pointer after freeing to prevent double-free. Does NOT free var itself.
static void FreeVariableData(KitsuneVariable* var) {
	if (!var) return;
	if ((var->type == LUA_TSTRING || var->type == KITSUNE_TJSON || var->type == KITSUNE_TERROR || var->type == LUA_TUSERDATA) && var->data) {
		gff_free(var->data);
		var->data = NULL;
	}
	else if (var->type == KITSUNE_TCHAR16 && var->char16data) {
		gff_free(var->char16data);
		var->char16data = NULL;
	}
	else if (var->type == LUA_TTABLE && var->table) {
		FreeKVNode(var->table);
		var->table = NULL;
	}
	else if (var->type == KITSUNE_TSTREAM) {
		// Signal the global-list sweeper that this slot's accessor reference is released.
		if (var->stream)
			var->stream->flags |= KITSUNE_SHARED_MEMORY_FLAG_ACCESSOR_DISPOSED;
		var->stream = NULL;
	}
}

// ── char16_t / wchar_t boundary helpers ─────────────────────────────────────────────────────
// All casting between the public ABI type (char16_t, stored in KitsuneVariable) and the
// internal Lua representation (wchar_t, used by LuaWChar) is confined here.
// On Windows, wchar_t is 2 bytes (UTF-16 LE), so both helpers are zero-cost operations.
// A future non-Windows port replaces these two functions with real UTF-32 <-> UTF-16
// converters and adds the appropriate #ifdef guard — nothing outside these helpers changes.

// Allocates a char16_t* copy of a wchar_t* src (len code units, excluding null terminator).
// The caller owns the result; free with gff_free.
static char16_t* AllocChar16FromWchar(const wchar_t* src, size_t len, size_t* outChar16Len) {
	return wchar_alloc_as_char16(src, len, outChar16Len);
}

// Returns a wchar_t* view of a char16_t* for passing to Lua APIs.
// On Windows this is a no-op reinterpret cast. A future non-Windows port that stores UTF-32
// internally must allocate and convert here (and update callers to free the result).
static inline const wchar_t* Char16AsWchar(const char16_t* p) {
	return reinterpret_cast<const wchar_t*>(p);
}

// Fills a KitsuneVariable from the Lua stack at the given index.
// When shallow=true (default), tables are left opaque (type=KITSUNE_TTABLE, table=NULL).
// When shallow=false, KITSUNE_TTABLE values are converted to a linked list via TableToLinkedList
// at KITSUNE_MAX_TABLE_DEPTH. Do NOT pass shallow=false inside TableToLinkedList itself —
// that path controls depth explicitly via its own recursion. Caller owns any allocated data.
static void FillKitsuneVariableFromStack(lua_State* L, int idx, KitsuneVariable* out, bool shallow = true) {
	memset(out, 0, sizeof(KitsuneVariable));
	int abs_idx = lua_absindex(L, idx);
	int t = lua_type(L, abs_idx);
	switch (t) {
	case LUA_TNUMBER:
		if (lua_isinteger(L, abs_idx)) {
			out->type = KITSUNE_TINTEGER;
			out->integer = (long long)lua_tointeger(L, abs_idx);
		}
		else {
			out->type = LUA_TNUMBER;
			out->number = lua_tonumber(L, abs_idx);
		}
		break;
	case LUA_TBOOLEAN:
		out->type = LUA_TBOOLEAN;
		out->boolean = lua_toboolean(L, abs_idx) != 0;
		break;
	case LUA_TSTRING: {
		size_t len;
		const char* s = lua_tolstring(L, abs_idx, &len);
		if (s) {
			out->data = (unsigned char*)gff_malloc(len + 1);
			if (out->data) {
				memcpy(out->data, s, len + 1);
				out->length = len;
				out->type = LUA_TSTRING;
			}
			else {
				out->type = LUA_TNONE;  // OOM sentinel: lua_type() never returns LUA_TNONE for valid indices
			}
		}
		break;
	}
	case LUA_TUSERDATA: {
		// Wchar is bridged as KITSUNE_TCHAR16: the internal wchar_t* is converted to char16_t*
		// at the boundary via AllocChar16FromWchar so the native object can be reconstructed on push.
		if (lua_iswchar(L, abs_idx)) {
			LuaWChar* wch = (LuaWChar*)lua_touserdata(L, abs_idx);
			if (wch && wch->str && wch->len > 0) {
				out->char16data = AllocChar16FromWchar(wch->str, wch->len, &out->length);
			}
			out->type = KITSUNE_TCHAR16;
			break;
		}
		// All other userdata types: read __name from the metatable and store it as a string.
		// __tostring is intentionally not called (may execute arbitrary Lua code).
		if (lua_getmetatable(L, abs_idx)) {
			lua_getfield(L, -1, "__name");
			if (lua_type(L, -1) == LUA_TSTRING) {
				size_t typeNameLen;
				const char* typeName = lua_tolstring(L, -1, &typeNameLen);
				if (typeName && typeNameLen > 0) {
					out->data = (unsigned char*)gff_malloc(typeNameLen + 1);
					if (out->data) {
						memcpy(out->data, typeName, typeNameLen + 1);
						out->length = typeNameLen;
					}
				}
			}
			lua_pop(L, 2);  // pop __name and metatable
		}
		out->type = LUA_TUSERDATA;
		break;
	}
	case LUA_TTABLE:
		out->type = LUA_TTABLE;
		if (!shallow)
			out->table = TableToLinkedList(L, abs_idx, KITSUNE_MAX_TABLE_DEPTH);
		break;
	default:
		out->type = t;  // preserve actual type (function, thread, etc.); data/table remain NULL
		break;
	}
}

// Converts a Lua table at stack index idx into a heap-allocated KeyValuePairKitsuneVariableNode
// linked list. Nested tables are recursed up to depth levels deep.
static KeyValuePairKitsuneVariableNode* TableToLinkedList(lua_State* L, int idx, int depth) {
	if (depth <= 0) return NULL;
	KeyValuePairKitsuneVariableNode* head = NULL;
	KeyValuePairKitsuneVariableNode** tail = &head;
	int abs_idx = lua_absindex(L, idx);
	lua_pushnil(L);  // first key
	while (lua_next(L, abs_idx)) {
		KeyValuePairKitsuneVariableNode* node = (KeyValuePairKitsuneVariableNode*)gff_malloc(sizeof(KeyValuePairKitsuneVariableNode));
		if (!node) {
			lua_pop(L, 2);
			break;  // OOM: abort iteration with partial list
		}
		memset(node, 0, sizeof(KeyValuePairKitsuneVariableNode));
		FillKitsuneVariableFromStack(L, -2, &node->key);
		if (lua_type(L, -1) == LUA_TTABLE) {
			// Recurse directly rather than through FillKitsuneVariableFromStack so the depth
			// limit is honoured across all levels instead of resetting to MAX each time.
			node->value.type = LUA_TTABLE;
			node->value.table = TableToLinkedList(L, -1, depth - 1);
		}
		else {
			FillKitsuneVariableFromStack(L, -1, &node->value);
		}
		node->next = NULL;
		*tail = node;
		tail = &node->next;
		lua_pop(L, 1);  // pop value, keep key for next lua_next
	}
	return head;
}

static void PushKitsuneVariable(lua_State* L, const KitsuneVariable* v) {
	if (!v) {
		lua_pushnil(L);
		return;
	}
	switch (v->type) {
	case LUA_TNUMBER:
		lua_pushnumber(L, v->number);
		break;
	case KITSUNE_TINTEGER:
		lua_pushinteger(L, (lua_Integer)v->integer);
		break;
	case LUA_TBOOLEAN:
		lua_pushboolean(L, v->boolean ? 1 : 0);
		break;
	case LUA_TSTRING:
		if (v->data && v->length > 0)
			lua_pushlstring(L, (const char*)v->data, v->length);
		else
			lua_pushstring(L, "");
		break;
	case KITSUNE_TCHAR16:
		// On Windows wchar_t == char16_t (both 2 bytes): reinterpret cast is safe.
		// On Linux wchar_t is 4 bytes (UTF-32): must decode UTF-16 surrogate pairs.
		if (v->char16data) {
#ifdef _WIN32
			lua_pushwchar(L, Char16AsWchar(v->char16data), v->length);
#else
			{
				size_t wlen = 0;
				wchar_t* wbuf = char16_alloc_as_wchar(v->char16data, v->length, &wlen);
				lua_pushwchar(L, wbuf ? wbuf : L"", wlen);
				gff_free(wbuf);
			}
#endif
		}
		else {
			lua_pushwchar(L, L"", 0);
		}
		break;
	case KITSUNE_TJSON: {
		// Decode JSON using the shared bridge instance (avoids GC churn per call).
		if (v->data && v->length > 0) {
			lua_pushcfunction(L, lua_json_decode);
			lua_rawgetp(L, LUA_REGISTRYINDEX, lua_json_bridge_registry_key());
			lua_pushlstring(L, (const char*)v->data, v->length);
			if (lua_pcall_nohook(L, 2, 1, 0) != LUA_OK) {
				lua_pop(L, 1);
				lua_pushnil(L);
			}
		}
		else {
			lua_newtable(L);  // empty JSON object → empty table
		}
		break;
	}
	case LUA_TTABLE:
		lua_newtable(L);
		if (v->table) {
			// Populate the Lua table from the linked list; keys and values are pushed recursively.
			const KeyValuePairKitsuneVariableNode* node = v->table;
			while (node) {
				PushKitsuneVariable(L, &node->key);
				PushKitsuneVariable(L, &node->value);
				lua_settable(L, -3);
				node = node->next;
			}
		}
		break;
	case KITSUNE_TSTREAM:
		if (v->stream && (v->stream->flags & KITSUNE_SHARED_MEMORY_FLAG_KITSUNE_OWNED))
			lua_push_sharedmemory_stream(L, v->stream);
		else
			lua_pushnil(L);  // NULL or not created via KitsuneCreateMemoryBlock — push nil
		break;
	default:
		lua_pushnil(L);
		break;
	}
}

static void SetSlotResult(KitsuneCoroutine* slot, lua_State* T, int idx) {
	FreeVariableData(&slot->result);
	memset(&slot->result, 0, sizeof(slot->result));
	slot->result.type = LUA_TNONE;
	int t = lua_type(T, idx);
	switch (t) {
	case LUA_TUSERDATA: {
		// Wchar is bridged as KITSUNE_TCHAR16: the internal wchar_t* is converted to char16_t*
		// at the boundary via AllocChar16FromWchar so the native object can be reconstructed on push.
		if (lua_iswchar(T, idx)) {
			LuaWChar* wch = (LuaWChar*)lua_touserdata(T, idx);
			if (wch && wch->str && wch->len > 0) {
				slot->result.char16data = AllocChar16FromWchar(wch->str, wch->len, &slot->result.length);
			}
			slot->result.type = KITSUNE_TCHAR16;
			break;
		}
		// Streams are bridged as KITSUNE_TSTREAM, but only outbound shared-memory streams
		// (created with Stream.OpenSharedMemory) may cross the boundary.  Any other stream
		// type is an error: the host cannot meaningfully own a file or in-memory stream.
		if (lua_isstream(T, idx)) {
			LuaStream* s = (LuaStream*)lua_touserdata(T, idx);
			if (lua_is_outbound_sharedmemory_stream(s)) {
				SharedMemoryBlock* block = lua_get_outbound_sharedmemory_block(s);
				slot->result.type   = KITSUNE_TSTREAM;
				slot->result.stream = block;
				// Clear ACCESSOR_DISPOSED: the result slot holds the accessor reference for C#.
				block->flags &= ~KITSUNE_SHARED_MEMORY_FLAG_ACCESSOR_DISPOSED;
			}
			else if ((s->Caps & STREAM_CAP_READ) && (s->Caps & STREAM_CAP_SEEK)) {
				// Snapshot the full contents into a new outbound shared-memory block.
				LuaStream* outStream = lua_try_push_sharedmemory_stream_outbound_from_stream(T, s);
				if (!outStream) {
					SetSlotError(slot, "failed to snapshot stream for result");
					break;
				}
				SharedMemoryBlock* block = lua_get_outbound_sharedmemory_block(outStream);
				slot->result.type   = KITSUNE_TSTREAM;
				slot->result.stream = block;
				block->flags &= ~KITSUNE_SHARED_MEMORY_FLAG_ACCESSOR_DISPOSED;
				lua_pop(T, 1);  // pop the outbound userdata
			}
			else {
				SetSlotError(slot, "stream result must be readable and seekable");
			}
			break;
		}
		// All other userdata types: read __name from the metatable and store it as a string.
		// __tostring is intentionally not called (may execute arbitrary Lua code).
		if (lua_getmetatable(T, idx)) {
			lua_getfield(T, -1, "__name");
			if (lua_type(T, -1) == LUA_TSTRING) {
				size_t typeNameLen;
				const char* typeName = lua_tolstring(T, -1, &typeNameLen);
				if (typeName && typeNameLen > 0) {
					slot->result.data = (unsigned char*)gff_malloc(typeNameLen + 1);
					if (slot->result.data) {
						memcpy(slot->result.data, typeName, typeNameLen + 1);
						slot->result.length = typeNameLen;
					}
				}
			}
			lua_pop(T, 2);  // pop __name and metatable
		}
		slot->result.type = LUA_TUSERDATA;
		break;
	}
	case LUA_TTABLE: {
		slot->result.type = LUA_TTABLE;
		slot->result.table = TableToLinkedList(T, idx, KITSUNE_MAX_TABLE_DEPTH);
		slot->result.length = 0;
		break;
	}
	default:
		// Handles numbers, booleans, strings, and any non-bridgeable type uniformly.
		FillKitsuneVariableFromStack(T, idx, &slot->result);
		if (slot->result.type == LUA_TNONE)
			SetSlotError(slot, "out of memory");  // string result lost; surface as error rather than silent TNONE
		break;
	}
}

// Caller must hold slotsLock, or be the scheduler thread.
static KitsuneCoroutine* FindSlot(KitsuneState* state, int id) {
	for (int i = 0; i < state->slotCount; i++) {
		if (state->slots[i]->id == id)
			return state->slots[i];
	}
	return NULL;
}

// Acquire exclusive access to the Lua state.
// If running, requests a pause and waits for the ticker to park.
// accessLock is held on return; caller MUST call ReleaseLuaAccess.
static void AcquireLuaAccess(KitsuneState* state) {
	state->accessLock.lock();
	// Set pauseFlag and wake the scheduler, then unconditionally wait for it to
	// acknowledge. The scheduler signals pausedEvent at the top of its loop
	// (step 1) and the Ticker does the same mid-resume; always waiting prevents
	// a phantom signal (produced when runningCount==0 but the scheduler still
	// reaches step 1) from being consumed by a later call where runningCount>0,
	// which would let this thread race with the scheduler on state->L.
	state->pauseFlag.store(1);
	state->workEvent.Set();  // wake the scheduler if it is sleeping in step 5
	state->pausedEvent.Wait();
}

static void ReleaseLuaAccess(KitsuneState* state) {
	if (state->pauseFlag.load()) {
		state->pauseFlag.store(0);
		state->resumeEvent.Set();
	}
	state->accessLock.unlock();
}

static void Ticker(lua_State* L, lua_Debug* ar) {
	void* ud;
	lua_getallocf(L, &ud);
	KitsuneState* state = (KitsuneState*)ud;

	// Do not auto-clear interrupt here; the scheduler clears it once all coroutines are done.
	if (state->interrupt.load()) {
		luaL_error(L, "interrupted");
		return;
	}

	// Per-coroutine cancel: check whether this specific coroutine has been cancelled.
	// currentCoroutineId is set by the scheduler immediately before lua_resume.
	int cancelId = (int)state->currentCoroutineId.load();
	if (cancelId) {
		KitsuneCoroutine* curSlot = FindSlot(state, cancelId);
		if (curSlot && curSlot->interrupted.load()) {
			luaL_error(L, "cancelled");
			return;
		}
	}

	if (state->pauseFlag.load()) {
		state->pausedEvent.Set();
		state->resumeEvent.Wait();
		if (state->interrupt.load()) {
			luaL_error(L, "interrupted");
			return;
		}
	}

	// Yield to let the scheduler run other coroutines before returning here.
	// Only yield when the scheduler initiated this resume and there are other coroutines waiting.
	// lua_isyieldable guards against metamethods triggered by C functions: luaT_callTM uses
	// luaD_callnoyield (non-yieldable) when L->ci is a C frame, so lua_yield would raise
	// "attempt to yield across a C-call boundary".  Skipping the yield here is safe — the
	// coroutine will be preempted at the next hook firing that lands in a yieldable Lua frame.
	if (state->runningCount.load() > 1 && state->currentCoroutineId.load() && lua_isyieldable(L))
		lua_yield(L, 0);
}

// Retrieve the coroutine's cached lua_State*.
// Only call while the scheduler owns Lua access (i.e. not from external threads).
static lua_State* GetCoroutineThread(KitsuneState* state, KitsuneCoroutine* slot) {
	(void)state;
	return slot->thread;
}

// Create a new coroutine thread, anchor it in the registry, and install the scheduler hook.
// Caller must hold LuaAccess. Stores the registry ref in slot->threadRef.
static lua_State* CreateCoroutineThread(KitsuneState* state, KitsuneCoroutine* slot) {
	lua_State* T = lua_newthread(state->L);
	slot->thread = T;
	slot->threadRef = luaL_ref(state->L, LUA_REGISTRYINDEX);
	// The hook is per-thread: lua_newthread does not inherit the parent's hook,
	// so this call cannot be moved to KitsuneInit.
	lua_sethook(T, Ticker, LUA_MASKCOUNT, 1000);
	return T;
}

static void FinishCoroutine(KitsuneState* state, KitsuneCoroutine* slot, lua_State* T, int rc, int nresults) {
	if (rc == LUA_OK) {
		if (nresults > 0)
			SetSlotResult(slot, T, 1);
		else
			slot->result.type = LUA_TNONE;
	}
	else {
		slot->result.type = LUA_TNONE;
		const char* err = lua_tolstring(T, -1, NULL);
		SetSlotError(slot, err ? err : "unknown error");
	}
	lua_settop(T, 0);
	slot->done.store(1);
	--state->runningCount;
	if (slot->fireAndForget.load())
		slot->released.store(1);
}

static void SchedulerProc(KitsuneState* state) {
#ifdef _WIN32
	state->schedulerThreadId.store((uint32_t)GetCurrentThreadId());
#endif
	g_isSchedulerThread = true;

	bool prevAnyActive = false;

	while (!state->schedulerStop.load()) {
		// ── Step 1: Service pause requests BEFORE touching state->L ──────────────
		// AcquireLuaAccess (SetVariable, GetVariable, StartCoroutine) sets pauseFlag
		// regardless of runningCount, so this is the single serialisation point.
		while (state->pauseFlag.load()) {
			state->pausedEvent.Set();
			state->resumeEvent.Wait();
		}
		if (state->schedulerStop.load())
			break;  // KitsuneCleanup called while paused

		// Sweep the global shared-memory block registry: free any block where both
		// OWNER_DISPOSED and ACCESSOR_DISPOSED flags are set.
		lua_shmem_sweep_disposed_blocks();

		bool anyActive = false;

		// ── Step 2: Interrupt all non-done coroutines if requested ────────────
		if (state->interrupt.load()) {
			for (int i = 0; i < state->slotCount; i++) {
				KitsuneCoroutine* slot = state->slots[i];
				if (slot->id != 0 && !slot->done.load()) {
					SetSlotError(slot, "interrupted");
					slot->result.type = LUA_TNONE;
					lua_State* T = GetCoroutineThread(state, slot);
					if (T)
						lua_settop(T, 0);
					slot->done.store(1);
					--state->runningCount;
					if (slot->fireAndForget.load())
						slot->released.store(1);
				}
			}
		}
		else {
			// ── Step 2: Resume each active coroutine once ─────────────────────
			for (int i = 0; i < state->slotCount; i++) {
				// Service any pause request between coroutine resumes.
				// Without this, an external caller (AcquireLuaAccess — variable bridge,
				// StartCoroutine) must wait for every remaining coroutine in the batch to
				// complete its current time-slice before the pause is acknowledged.
				// With this check the worst case is a single 1000-instruction time-slice.
				while (state->pauseFlag.load()) {
					state->pausedEvent.Set();
					state->resumeEvent.Wait();
				}
				if (state->schedulerStop.load())
					break;

				KitsuneCoroutine* slot = state->slots[i];
				if (slot->id == 0 || slot->done.load())
					continue;
				// Per-coroutine cancel: terminate before the next resume (or wake from sleep).
				if (slot->interrupted.load()) {
					SetSlotError(slot, "cancelled");
					slot->result.type = LUA_TNONE;
					lua_State* Tc = GetCoroutineThread(state, slot);
					if (Tc)
						lua_settop(Tc, 0);
					slot->done.store(1);
					--state->runningCount;
					slot->released.store(1);
					continue;
				}
				// Skip coroutines that are waiting out a Sleep() call.
				if (slot->sleepUntil > 0.0) {
					if (GetCounter(state) < slot->sleepUntil)
						continue;
					slot->sleepUntil = 0.0;
				}
				anyActive = true;

				lua_State* T = GetCoroutineThread(state, slot);
				if (!T) {
					SetSlotError(slot, "internal: coroutine thread unavailable");
					slot->result.type = LUA_TNONE;
					slot->done.store(1);
					--state->runningCount;
					if (slot->fireAndForget.load())
						slot->released.store(1);
					continue;
				}

				// Refresh ARGS for this coroutine before resuming so the global
				// always reflects the currently running coroutine's arguments.
				if (slot->argsRef != LUA_NOREF) {
					lua_rawgeti(T, LUA_REGISTRYINDEX, slot->argsRef);
					lua_setglobal(T, "ARGS");
				}

				lua_pushinteger(T, slot->id);
				lua_setglobal(T, "ID");

				int nresults = 0;
				int nstart = (lua_status(T) == LUA_OK) ? slot->initialNArgs : 0;
				state->currentCoroutineId.store((long)slot->id);
				int rc = lua_resume(T, state->L, nstart, &nresults);
				state->currentCoroutineId.store(0);
				if (rc == LUA_YIELD)
					lua_pop(T, nresults);  // discard yielded values only; lua_settop(T,0) would corrupt locals
				else
					FinishCoroutine(state, slot, T, rc, nresults);
			}
		}

		// ── Step 3: Clear interrupt once no coroutines remain active ──────────
		if (state->runningCount.load() == 0)
			state->interrupt.store(0);

		// ── Step 4: Release done + released slots – zero the struct for reuse ─
		{
			// Phase 1 (under slotsLock): collect registry refs and zero each slot.
			// luaL_unref is kept outside the lock so its internal Lua allocations
			// do not block concurrent KitsuneCancel / KitsuneGetActiveIds callers.
			int pendingArgs[KITSUNE_MAX_COROUTINES];
			int pendingThreads[KITSUNE_MAX_COROUTINES];
			int pendingCount = 0;

			state->slotsLock.lock();
			for (int i = 0; i < state->slotCount; i++) {
				KitsuneCoroutine* slot = state->slots[i];
				if (slot->id != 0 && slot->done.load() && slot->released.load()) {
					pendingArgs[pendingCount] = slot->argsRef;
					pendingThreads[pendingCount] = slot->threadRef;
					pendingCount++;
					slot->thread = NULL;  // invariant: null before memset so the pointer is never stale
					gff_free(slot->error);
					FreeVariableData(&slot->result);
					memset(slot, 0, sizeof(KitsuneCoroutine));  // id = 0 marks the slot as reusable
				}
			}
			state->slotsLock.unlock();

			// Phase 2 (outside slotsLock): release Lua registry references.
			for (int i = 0; i < pendingCount; i++) {
				if (pendingArgs[i] != LUA_NOREF)
					luaL_unref(state->L, LUA_REGISTRYINDEX, pendingArgs[i]);
				if (pendingThreads[i] != LUA_NOREF)
					luaL_unref(state->L, LUA_REGISTRYINDEX, pendingThreads[i]);
			}
		}

		// ── Step 5: GC on active→idle transition, then sleep ─────────────────
		// Run a full collection cycle when the last coroutine finishes so that all
		// memory allocated during execution is reclaimed before the scheduler idles.
		// prevAnyActive ensures this fires exactly once per work batch, not every
		// idle iteration, and only after Step 4 has released thread registry refs.
		if (prevAnyActive && !anyActive && state->runningCount.load() == 0)
			lua_gc(state->L, LUA_GCCOLLECT, 0);
		prevAnyActive = anyActive;

		if (!anyActive) {
			// Use a short wait when coroutines are mid-Sleep() so their deadlines
			// are checked promptly; fall back to 10ms when the engine is truly idle.
			bool hasSleeping = false;
			for (int i = 0; i < state->slotCount; i++) {
				KitsuneCoroutine* slot = state->slots[i];
				if (slot->id != 0 && !slot->done.load() && slot->sleepUntil > 0.0) {
					hasSleeping = true;
					break;
				}
			}
			state->workEvent.WaitFor(hasSleeping ? 1 : 10);
		}
	}
}

static int L_GetRuntime(lua_State* L) {
	void* ud;
	lua_getallocf(L, &ud);
	KitsuneState* state = (KitsuneState*)ud;

	// If called from within a scheduler-managed coroutine, return that coroutine's runtime.
	int id = (int)state->currentCoroutineId.load();
	if (id) {
		KitsuneCoroutine* slot = FindSlot(state, id);
		if (slot) {
			lua_pushnumber(L, GetCounter(state) - slot->startTime);
			return 1;
		}
	}

	// Otherwise return the engine's total runtime since KitsuneInit.
	lua_pushnumber(L, GetCounter(state));
	return 1;
}

static int L_SleepContinuation(lua_State* L, int status, lua_KContext ctx) {
	(void)status; (void)ctx;
	return 0;
}

// Sleep(ms) — yields the calling coroutine for at least ms milliseconds without blocking any OS thread.
// The scheduler uses the GetCounter clock to skip this coroutine until its deadline has passed.
// If called outside a scheduler-managed coroutine, falls back to a blocking Win32 Sleep.
static int L_Sleep(lua_State* L) {
	lua_Number ms = luaL_optnumber(L, 1, 0);

	void* ud;
	lua_getallocf(L, &ud);
	KitsuneState* state = (KitsuneState*)ud;
	int id = (int)state->currentCoroutineId.load();
	KitsuneCoroutine* slot = FindSlot(state, id);  // NULL when id==0 (not inside scheduler's lua_resume)

	if (slot && lua_isyieldable(L)) {
		// Scheduler-managed coroutine with a yieldable call stack: yield cooperatively.
		if (ms > 0.0)
			slot->sleepUntil = GetCounter(state) + (double)ms;
		return lua_yieldk(L, 0, 0, L_SleepContinuation);
	}

	// Fall back to a blocking OS sleep when: (a) not a scheduler-managed coroutine,
	// or (b) lua_isyieldable(L) is false — we are inside a luaD_callnoyield boundary
	// (lua_pcall_nohook, lua_call_nohook, or a metamethod triggered from C code)
	// and lua_yieldk would raise "attempt to yield across a C-call boundary".
	if (ms > 0.0)
		::Sleep((unsigned long)(ms > (lua_Number)MAXDWORD ? MAXDWORD : (unsigned long)ms));
	return 0;
}

static void* l_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
	if (nsize == 0) {
		gff_free(ptr);
		return NULL;
	}
	else {
		return gff_realloc(ptr, nsize);
	}
}

// True only on the scheduler thread; set once in SchedulerProc and never cleared.
// Used by all KitsuneExecute* guards on every platform without OS-specific thread IDs.
static thread_local bool g_isSchedulerThread = false;

// Allocates a heap KitsuneVariable with KITSUNE_TERROR and an optional message.
// The caller must free the returned pointer with KitsuneVariableFree.
static KitsuneVariable* MakeErrorVariable(const char* msg) {
	KitsuneVariable* var = (KitsuneVariable*)gff_malloc(sizeof(KitsuneVariable));
	if (!var) return NULL;
	memset(var, 0, sizeof(KitsuneVariable));
	var->type = KITSUNE_TERROR;
	if (msg) {
		size_t len = strlen(msg);
		var->data = (unsigned char*)gff_malloc(len + 1);
		if (var->data) {
			memcpy(var->data, msg, len + 1);
			var->length = len;
		}
	}
	return var;
}

// ============================================================
// Exported API
// ============================================================

static KitsuneState* g_state = nullptr;
#ifdef _WIN32
static bool g_coOwned = false;
#endif

extern "C" {

	KITSUNE_API bool KitsuneInit(kitsune_Init initFunc) {
		if (g_state)
			return true;

		#ifdef _WIN32
		// RPC_E_CHANGED_MODE means COM was already initialised by the host (e.g. .NET's
		// MTA thread pool).  We can still use COM; we just must not call CoUninitialize.
		HRESULT cohr = CoInitialize(NULL);
		if (FAILED(cohr) && cohr != RPC_E_CHANGED_MODE)
			return false;
		g_coOwned = SUCCEEDED(cohr);
#endif

		InitMemoryManager();

#ifndef _WIN32
		// Prevent SIGPIPE from terminating the process when a child exits while we
		// are still writing to its stdin pipe; write() will return -1/EPIPE instead.
		signal(SIGPIPE, SIG_IGN);
#endif

		#ifdef _WIN32
		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
			EndMemoryManager();
			if (g_coOwned) CoUninitialize();
			return false;
		}
#endif
#ifdef KITSUNE_HTTP
		curl_global_init(CURL_GLOBAL_DEFAULT);
#endif

		KitsuneState* state = new KitsuneState{};
		// PlatformEvent default ctor initialises all three events; no Create() call needed.
		StartCounter(state);

		state->L = lua_newstate(l_alloc, state);
		if (!state->L) {
			delete state;
#ifdef _WIN32
			WSACleanup();
			if (g_coOwned) CoUninitialize();
#endif
			EndMemoryManager();
			return false;
		}

		lua_State* L = state->L;
		lua_gc(L, LUA_GCGEN, 20, 100);
		luaL_openlibs(L);

#ifdef _DEBUG
		lua_pushboolean(L, TRUE);
		lua_setglobal(L, "DEBUG");
#endif

		lua_pushstring(L, KITSUNE_VERSION);
		lua_setglobal(L, "VERSION");



		#ifdef KITSUNE_MYSQL
		luaopen_mysql(L);        lua_setglobal(L, "MySQL");
#endif
#ifdef KITSUNE_POSTGRES
		luaopen_postgres(L);     lua_setglobal(L, "Postgres");
#endif

#ifdef KITSUNE_KAFKA
		luaopen_kafka(L);        lua_setglobal(L, "Kafka");
#endif
#ifdef KITSUNE_ARCHIVE
		luaopen_archive(L);      lua_setglobal(L, "Archive");
#endif
#ifdef KITSUNE_REDIS
		luaopen_redis(L);        lua_setglobal(L, "Redis");
#endif
		luaopen_process(L);      lua_setglobal(L, "Process");
		luaopen_luaaes(L);       lua_setglobal(L, "Aes");
		luaopen_sqlite(L);       lua_setglobal(L, "SQLite");
		luaopen_timer(L);        lua_setglobal(L, "Timer");
		luaopen_filesystem(L);   lua_setglobal(L, "FileSystem");
		luaopen_md5(L);          lua_setglobal(L, "MD5");
		luaopen_stream(L);       lua_setglobal(L, "Stream");
#ifdef KITSUNE_HTTP
		luaopen_http(L);         lua_setglobal(L, "Http");
#endif
		luaopen_sha256(L);       lua_setglobal(L, "SHA256");
		luaopen_mutex(L);        lua_setglobal(L, "Mutex");
		luaopen_json(L);         lua_setglobal(L, "Json");
		// Store a single LuaJson instance in the registry for the C bridge to reuse
		// when decoding KITSUNE_TJSON values — avoids one GC allocation per bridge call.
		lua_json_push(L);
		lua_rawsetp(L, LUA_REGISTRYINDEX, lua_json_bridge_registry_key());
		luaopen_base64(L);       lua_setglobal(L, "Base64");
		luaopen_wchar(L);        lua_setglobal(L, "Wchar");
		luaopen_csv(L);          lua_setglobal(L, "CSV");
		luaopen_sha1(L);         lua_setglobal(L, "SHA1");

		lua_pushcfunction(L, L_GetRuntime);    lua_setglobal(L, "Runtime");
#ifdef _WIN32
		lua_pushcfunction(L, L_ShellExecute);  lua_setglobal(L, "ShellExecute");
#endif
		lua_pushcfunction(L, L_GetMemory);     lua_setglobal(L, "GetMemory");

		luaopen_misc(L);
		lua_pushcfunction(L, L_Sleep);
		lua_setglobal(L, "Sleep");

		if (initFunc)
			initFunc(L);

		// Coroutine threads each receive their own hook; no hook is set on the main state.
		state->schedulerThread = std::thread(SchedulerProc, state);
		if (!state->schedulerThread.joinable()) {
			lua_close(state->L);
			delete state;
#ifdef _WIN32
			WSACleanup();
			if (g_coOwned) CoUninitialize();
#endif
			EndMemoryManager();
			return false;
		}

		g_state = state;
		return true;
	}

	// ── Start a coroutine directly: acquire Lua access, create the thread, hand it to the scheduler ─
	static int StartCoroutine(KitsuneState* state, bool isFile,
		const char* source, int argc, const KitsuneVariable* argv,
		bool fireAndForget) {
		if (!state || !source) return -1;

		// Acquire Lua access: pauses any running coroutine at the next ticker boundary
		// and serialises concurrent calls (e.g. with SetVariable / GetVariable).
		AcquireLuaAccess(state);

		// Find a reusable zeroed slot, or allocate a new one.
		KitsuneCoroutine* slot = NULL;
		bool isNewSlot = false;
		for (int i = 0; i < state->slotCount; i++) {
			if (state->slots[i]->id == 0) {
				slot = state->slots[i];
				break;
			}
		}
		if (!slot) {
			if (state->slotCount >= KITSUNE_MAX_COROUTINES) {
				ReleaseLuaAccess(state);
				return -1;
			}
			slot = new (std::nothrow) KitsuneCoroutine{};
			if (!slot) {
				ReleaseLuaAccess(state);
				return -1;
			}
			isNewSlot = true;
		}

		slot->threadRef = LUA_NOREF;
		slot->argsRef = LUA_NOREF;
		slot->fireAndForget = fireAndForget ? 1 : 0;

		// Create the coroutine thread and anchor it so the GC cannot collect it.
		lua_State* T = CreateCoroutineThread(state, slot);

		// Build the ARGS table: ARGS[1]=path (file) or ARGS[1..]=argv[0..] (string).
		lua_newtable(state->L);
		if (isFile) {
			lua_pushstring(state->L, source);
			lua_rawseti(state->L, -2, 1);
			for (int n = 0; n < argc; n++) {
				PushKitsuneVariable(state->L, argv ? &argv[n] : nullptr);
				lua_rawseti(state->L, -2, n + 2);
			}
		}
		else {
			for (int n = 0; n < argc; n++) {
				PushKitsuneVariable(state->L, argv ? &argv[n] : nullptr);
				lua_rawseti(state->L, -2, n + 1);
			}
		}
		slot->argsRef = luaL_ref(state->L, LUA_REGISTRYINDEX);

		// Load the script onto the coroutine thread's stack.
		int loadrc = isFile
			? luaL_loadfile(T, source)
			: luaL_loadbuffer(T, source, strlen(source), "string");

		int id = (int)(++state->nextId);

		if (loadrc != 0) {
			const char* err = lua_tolstring(T, -1, NULL);
			SetSlotError(slot, err ? err : "load error");
			slot->result.type = LUA_TNONE;
			lua_settop(T, 0);
			luaL_unref(state->L, LUA_REGISTRYINDEX, slot->argsRef);   slot->argsRef = LUA_NOREF;
			luaL_unref(state->L, LUA_REGISTRYINDEX, slot->threadRef); slot->threadRef = LUA_NOREF;
			slot->thread = NULL;  // invariant: thread is only valid while threadRef != LUA_NOREF
			slot->done.store(1);
			if (slot->fireAndForget.load())
				slot->released.store(1);
		}
		else {
			++state->runningCount;
		}

		slot->startTime = GetCounter(state);

		// Expose the slot by assigning its ID; add it to the array if newly allocated.
		state->slotsLock.lock();
		slot->id = id;
		if (isNewSlot)
			state->slots[state->slotCount++] = slot;
		state->slotsLock.unlock();

		ReleaseLuaAccess(state);
		state->workEvent.Set();
		return id;
	}

	KITSUNE_API int KitsuneExecuteFileAsync(const char* path, int argc, const KitsuneVariable* argv, bool fireAndForget) {
		if (g_isSchedulerThread && g_state && g_state->DelegateState) return -1;
		return StartCoroutine(g_state, true, path, argc, argv, fireAndForget);
	}

	KITSUNE_API int KitsuneExecuteStringAsync(const char* script, int argc, const KitsuneVariable* argv, bool fireAndForget) {
		if (g_isSchedulerThread && g_state && g_state->DelegateState) return -1;
		return StartCoroutine(g_state, false, script, argc, argv, fireAndForget);
	}

	// Forward declaration: PushGlobalAtPath is defined after the coroutine-start helpers.
	static bool PushGlobalAtPath(lua_State* L, const char* path);

	static int StartCoroutineFunction(KitsuneState* state, const char* functionName,
		int argc, const KitsuneVariable* argv, bool fireAndForget) {
		if (!state || !functionName) return -1;

		AcquireLuaAccess(state);

		KitsuneCoroutine* slot = NULL;
		bool isNewSlot = false;
		for (int i = 0; i < state->slotCount; i++) {
			if (state->slots[i]->id == 0) {
				slot = state->slots[i];
				break;
			}
		}
		if (!slot) {
			if (state->slotCount >= KITSUNE_MAX_COROUTINES) {
				ReleaseLuaAccess(state);
				return -1;
			}
			slot = new (std::nothrow) KitsuneCoroutine{};
			if (!slot) {
				ReleaseLuaAccess(state);
				return -1;
			}
			isNewSlot = true;
		}

		slot->threadRef = LUA_NOREF;
		slot->argsRef = LUA_NOREF;  // no ARGS table – args are passed directly to the function
		slot->fireAndForget = fireAndForget ? 1 : 0;

		lua_State* T = CreateCoroutineThread(state, slot);

		// Resolve the function via dot-path navigation on the main state, then move it to T.
		// PushGlobalAtPath handles both "Foo" and "Ns.Foo" uniformly, matching SetVariable behaviour.
		if (PushGlobalAtPath(state->L, functionName))
			lua_xmove(state->L, T, 1);  // move the resolved value (function or nil) to T
		else
			lua_pushnil(T);  // invalid path; !lua_isfunction below triggers "function not found"

		int id = (int)(++state->nextId);

		if (!lua_isfunction(T, -1)) {
			lua_pop(T, 1);
			SetSlotError(slot, "function not found");
			slot->result.type = LUA_TNONE;
			luaL_unref(state->L, LUA_REGISTRYINDEX, slot->threadRef);
			slot->threadRef = LUA_NOREF;
			slot->thread = NULL;
			slot->done.store(1);
			if (slot->fireAndForget.load())
				slot->released.store(1);
		}
		else {
			for (int n = 0; n < argc; n++)
				PushKitsuneVariable(T, argv ? &argv[n] : nullptr);
			slot->initialNArgs = argc;
			++state->runningCount;
		}

		slot->startTime = GetCounter(state);

		state->slotsLock.lock();
		slot->id = id;
		if (isNewSlot)
			state->slots[state->slotCount++] = slot;
		state->slotsLock.unlock();

		ReleaseLuaAccess(state);
		state->workEvent.Set();
		return id;
	}

	KITSUNE_API int KitsuneExecuteFunctionAsync(const char* functionName, int argc, const KitsuneVariable* argv, bool fireAndForget) {
		if (g_isSchedulerThread && g_state && g_state->DelegateState) return -1;
		return StartCoroutineFunction(g_state, functionName, argc, argv, fireAndForget);
	}

	KITSUNE_API KitsuneVariable* KitsuneExecuteFile(const char* path, int argc, const KitsuneVariable* argv) {
		if (g_isSchedulerThread && g_state && g_state->DelegateState) return MakeErrorVariable("cannot be called from within a registered function");
		int id = KitsuneExecuteFileAsync(path, argc, argv, false);
		if (id < 0) return NULL;
		while (!KitsuneHasResult(id, NULL))
			Sleep(1);
		return KitsuneGetResult(id);
	}

	KITSUNE_API KitsuneVariable* KitsuneExecuteString(const char* script, int argc, const KitsuneVariable* argv) {
		if (g_isSchedulerThread && g_state && g_state->DelegateState) return MakeErrorVariable("cannot be called from within a registered function");
		int id = KitsuneExecuteStringAsync(script, argc, argv, false);
		if (id < 0) return NULL;
		while (!KitsuneHasResult(id, NULL))
			Sleep(1);
		return KitsuneGetResult(id);
	}

	KITSUNE_API KitsuneVariable* KitsuneExecuteFunction(const char* functionName, int argc, const KitsuneVariable* argv) {
		if (g_isSchedulerThread && g_state && g_state->DelegateState) return MakeErrorVariable("cannot be called from within a registered function");
		int id = KitsuneExecuteFunctionAsync(functionName, argc, argv, false);
		if (id < 0) return NULL;
		while (!KitsuneHasResult(id, NULL))
			Sleep(1);
		return KitsuneGetResult(id);
	}

	KITSUNE_API size_t KitsuneGetError(int id, char* buf, size_t bufSize) {
		KitsuneState* state = g_state;
		if (!state) return 0;
		state->slotsLock.lock();
		KitsuneCoroutine* slot = FindSlot(state, id);
		size_t len = 0;
		if (slot && slot->error) {
			len = strlen(slot->error);
			if (buf && bufSize > 0) {
				size_t copyLen = len < bufSize - 1 ? len : bufSize - 1;
				memcpy(buf, slot->error, copyLen);
				buf[copyLen] = '\0';
			}
		}
		state->slotsLock.unlock();
		return len;
	}

	KITSUNE_API bool KitsuneHasResult(int id, size_t* len) {
		KitsuneState* state = g_state;
		if (!state) {
			if (len) *len = 0;
			return false;
		}
		state->slotsLock.lock();
		KitsuneCoroutine* slot = FindSlot(state, id);
		bool done = slot ? (slot->done.load() != 0) : false;
		if (len) *len = (done && slot && slot->result.type == LUA_TSTRING) ? slot->result.length : 0;
		state->slotsLock.unlock();
		return done;
	}

	KITSUNE_API KitsuneVariable* KitsuneGetResult(int id) {
		KitsuneState* state = g_state;
		if (!state) return NULL;

		state->slotsLock.lock();
		KitsuneCoroutine* slot = FindSlot(state, id);
		if (!slot || !slot->done.load()) {
			state->slotsLock.unlock();
			return NULL;
		}

		KitsuneVariable* out = (KitsuneVariable*)gff_malloc(sizeof(KitsuneVariable));
		if (!out) {
			slot->released.store(1);
			state->slotsLock.unlock();
			return NULL;
		}
		memset(out, 0, sizeof(KitsuneVariable));

		// Transfer owned data (string or table linked list) atomically to prevent double-free.
		if ((slot->result.type == LUA_TSTRING || slot->result.type == KITSUNE_TCHAR16 || slot->result.type == LUA_TUSERDATA || slot->result.type == LUA_TTABLE) && slot->result.data) {
			out->type = slot->result.type;
			out->length = slot->result.length;
			out->data = slot->result.data;
			slot->result.data = nullptr;
			slot->result.length = 0;
		}
		else if (slot->result.type == KITSUNE_TSTREAM && slot->result.stream) {
			out->type = KITSUNE_TSTREAM;
			out->stream = slot->result.stream;
			slot->result.stream = nullptr;
		}
		else {
			*out = slot->result;  // inline copy for number / bool / none
		}
		slot->result.type = LUA_TNONE;  // mark consumed; prevents stale reads before scheduler compacts

		slot->released.store(1);
		state->slotsLock.unlock();
		return out;
	}

	KITSUNE_API void KitsuneCancel(int id) {
		KitsuneState* state = g_state;
		if (!state) return;
		state->slotsLock.lock();
		KitsuneCoroutine* slot = FindSlot(state, id);
		if (slot) {
			slot->fireAndForget.store(1);
			if (slot->done.load())
				slot->released.store(1);  // already finished, release directly
			else
				slot->interrupted.store(1);  // still running, signal per-coroutine cancel
		}
		state->slotsLock.unlock();
		state->workEvent.Set();  // wake the scheduler to process the cancel promptly
	}

	KITSUNE_API double KitsuneGetRuntime(int id) {
		KitsuneState* state = g_state;
		if (!state) return 0.0;
		state->slotsLock.lock();
		KitsuneCoroutine* slot = FindSlot(state, id);
		double runtime = slot ? GetCounter(state) - slot->startTime : 0.0;
		state->slotsLock.unlock();
		return runtime;
	}

	KITSUNE_API int KitsuneGetStatus(int id) {
		KitsuneState* state = g_state;
		if (!state) return KITSUNE_STATUS_NONE;
		state->slotsLock.lock();
		KitsuneCoroutine* slot = FindSlot(state, id);
		int status = KITSUNE_STATUS_NONE;
		if (slot) {
			if (slot->done.load()) {
				if (slot->interrupted.load())
					status = KITSUNE_STATUS_CANCELLED;
				else if (slot->error)
					status = KITSUNE_STATUS_FAULTED;
				else
					status = KITSUNE_STATUS_DONE;
			}
			else if (slot->interrupted.load()) {
				status = KITSUNE_STATUS_CANCELLED;
			}
			else if ((int)state->currentCoroutineId.load() == id) {
				status = KITSUNE_STATUS_RUNNING;
			}
			else if (slot->sleepUntil > 0.0 && GetCounter(state) < slot->sleepUntil) {
				status = KITSUNE_STATUS_SLEEPING;
			}
			else {
				status = KITSUNE_STATUS_IDLE;
			}
		}
		state->slotsLock.unlock();
		return status;
	}

	KITSUNE_API bool KitsuneIsRunning() {
		KitsuneState* state = g_state;
		if (!state) return false;
		if (state->currentCoroutineId.load()) return true;
		state->slotsLock.lock();
		bool running = false;
		for (int i = 0; i < state->slotCount; i++) {
			if (state->slots[i]->id != 0 && !state->slots[i]->done.load()) {
				running = true;
				break;
			}
		}
		state->slotsLock.unlock();
		return running;
	}

	KITSUNE_API int KitsuneGetRunningId() {
		KitsuneState* state = g_state;
		if (!state) return 0;
		int active = (int)state->currentCoroutineId.load();
		if (active) return active;
		state->slotsLock.lock();
		int id = 0;
		for (int i = 0; i < state->slotCount; i++) {
			if (state->slots[i]->id != 0 && !state->slots[i]->done.load()) {
				id = state->slots[i]->id;
				break;
			}
		}
		state->slotsLock.unlock();
		return id;
	}

	KITSUNE_API void KitsuneReleaseResult(int id) {
		KitsuneState* state = g_state;
		if (!state) return;
		state->slotsLock.lock();
		KitsuneCoroutine* slot = FindSlot(state, id);
		if (slot && slot->done.load())
			slot->released.store(1);
		state->slotsLock.unlock();
	}

	KITSUNE_API void KitsuneInterrupt() {
		KitsuneState* state = g_state;
		if (state)
			state->interrupt.store(1);
	}

	KITSUNE_API void KitsuneWait() {
		KitsuneState* state = g_state;
		if (!state) return;
		while (state->runningCount.load())
			Sleep(1);
	}

	KITSUNE_API int KitsuneGetActiveIds(int* buffer, int bufferSize) {
		KitsuneState* state = g_state;
		if (!state) return 0;

		state->slotsLock.lock();
		int count = 0;
		for (int i = 0; i < state->slotCount; i++) {
			KitsuneCoroutine* slot = state->slots[i];
			if (slot->id != 0 && !slot->released.load()) {
				if (buffer && count < bufferSize)
					buffer[count] = slot->id;
				count++;
			}
		}
		state->slotsLock.unlock();
		return count;
	}

	KITSUNE_API void KitsuneVariableFree(KitsuneVariable* var) {
		if (!var) return;
		// TSTREAM: the block was already transferred to the host by KitsuneGetResult and is owned
		// by the caller (e.g. a LuaStream in C#).  The host calls block->close when it is done.
		// Null the pointer before FreeVariableData so the new close-on-free path is not triggered —
		// that path is only correct for unconsumed slots (KitsuneReleaseResult / fireAndForget).
		if (var->type == KITSUNE_TSTREAM)
			var->stream = NULL;
		FreeVariableData(var);
		gff_free(var);
	}

	// Navigate from the table on top of L into the parent table of the final key in a dot-path.
	// Returns the final key component on success; the parent table remains on top.
	// Returns NULL on failure; the stack is fully restored (the initial push is undone).
	// createMissing=true: auto-create missing intermediate tables.
	// PRECONDITION: the root table must be on top of L when this function is called.
	//               startTop = gettop-1 saves the restore depth so any failure undoes that one push.
	static const char* NavigateToParent(lua_State* L, const char* path, bool createMissing) {
		assert(lua_gettop(L) >= 1 && lua_istable(L, -1));  // root table must be on top
		int startTop = lua_gettop(L) - 1;  // depth before the root table was pushed; restore target on failure
		const char* p = path;
		for (;;) {
			const char* dot = strchr(p, '.');
			if (!dot) return p;  // success: parent on top, final key is p
			size_t len = (size_t)(dot - p);
			lua_pushlstring(L, p, len);
			lua_gettable(L, -2);
			if (lua_isnil(L, -1)) {
				if (!createMissing) {
					lua_settop(L, startTop);
					return NULL;
				}
				lua_pop(L, 1);
				lua_newtable(L);
				lua_pushlstring(L, p, len);
				lua_pushvalue(L, -2);   // dup subtable
				lua_settable(L, -4);    // parent[key] = subtable, pops key + dup
			}
			else if (!lua_istable(L, -1)) {
				lua_settop(L, startTop);
				return NULL;
			}
			lua_remove(L, -2);  // remove parent, subtable is now on top
			p = dot + 1;
		}
	}

	// Navigate from the Lua global environment into the parent table of the final key in a dot-path.
	// Equivalent to pushing _G then calling NavigateToParent. See NavigateToParent for full semantics.
	static const char* NavigateGlobalParent(lua_State* L, const char* path, bool createMissing) {
		if (!path || !*path) return NULL;
		lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
		return NavigateToParent(L, path, createMissing);
	}

	// Push the value at the dot-separated path from the Lua global environment.
	// If path is NULL or "", pushes _G itself. Returns true on success (value or _G on top).
	// Returns false if navigation fails for a non-empty path (stack fully restored).
	static bool PushGlobalAtPath(lua_State* L, const char* path) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
		if (!path || !*path) return true;
		const char* finalKey = NavigateToParent(L, path, false);
		if (!finalKey) return false;
		lua_getfield(L, -1, finalKey);
		lua_remove(L, -2);  // remove parent, keep value
		return true;
	}

	KITSUNE_API bool KitsuneSetVariable(const char* path, const KitsuneVariable* var) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !path || !*path) return false;
#ifdef _WIN32
		if (GetCurrentThreadId() == state->schedulerThreadId.load() && state->DelegateState) return false;  // scheduler thread re-entering from within a registered function; would deadlock
#endif
		if (var && var->type == KITSUNE_TSTREAM) {
			if (!var->stream || !(var->stream->flags & KITSUNE_SHARED_MEMORY_FLAG_KITSUNE_OWNED))
				return false;  // stream block was not created by KitsuneCreateMemoryBlock
		}
		AcquireLuaAccess(state);
		bool ok = false;
		const char* finalKey = NavigateGlobalParent(state->L, path, true);
		if (finalKey) {
			if (!var || var->type == LUA_TNONE)
				lua_pushnil(state->L);
			else
				PushKitsuneVariable(state->L, var);
			lua_setfield(state->L, -2, finalKey);
			lua_pop(state->L, 1);  // pop parent table
				ok = true;
			}
			ReleaseLuaAccess(state);
		return ok;
	}

	KITSUNE_API KitsuneVariable* KitsuneGetVariable(const char* path) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !path || !*path) return NULL;
#ifdef _WIN32
		if (GetCurrentThreadId() == state->schedulerThreadId.load() && state->DelegateState) return NULL;  // scheduler thread re-entering from within a registered function; would deadlock
#endif

		AcquireLuaAccess(state);
		KitsuneVariable* out = NULL;
		const char* finalKey = NavigateGlobalParent(state->L, path, false);
		if (finalKey) {
			if (lua_istable(state->L, -1)) {
				lua_getfield(state->L, -1, finalKey);
				int t = lua_type(state->L, -1);
				if (t != LUA_TNIL && t != LUA_TNONE) {
					out = (KitsuneVariable*)gff_malloc(sizeof(KitsuneVariable));
					if (out) {
						// FillKitsuneVariableFromStack handles scalars and userdata (__tostring);
						// tables are left opaque (table = NULL) — the variable bridge is shallow.
						FillKitsuneVariableFromStack(state->L, -1, out);
						if (out->type == LUA_TNONE) {
							// String allocation failed; return NULL rather than a misleading LUA_TNIL.
							gff_free(out);
							out = NULL;
						}
					}
				}
				lua_pop(state->L, 1);  // pop value
			}
			lua_pop(state->L, 1);  // pop parent table
		}
		ReleaseLuaAccess(state);
		return out;
	}

	// Context passed as a light-userdata closure upvalue to GetAllIteratorBody.
	struct KitsuneGetAllCtx { kitsune_KeyValuePairCallback callback; void* userdata; };

	// Protected body for KitsuneGetAll: receives the table at index 1, iterates it via
	// lua_next, and invokes the callback for each key-value pair.
	// Running inside lua_pcall means any error from lua_next (e.g. invalid key, OOM)
	// is caught and returned to the caller — ReleaseLuaAccess is always reached.
	static int GetAllIteratorBody(lua_State* L) {
		KitsuneGetAllCtx* ctx = (KitsuneGetAllCtx*)lua_touserdata(L, lua_upvalueindex(1));
		lua_pushnil(L);  // first key
		while (lua_next(L, 1)) {
			// Stack: [table, key, value]
			KitsuneVariable k = {}, v = {};
			FillKitsuneVariableFromStack(L, -2, &k);
			FillKitsuneVariableFromStack(L, -1, &v);
			if (k.type == LUA_TNONE || v.type == LUA_TNONE) {
				// OOM filling key or value: release any allocated data and abort iteration.
				FreeVariableData(&k);
				FreeVariableData(&v);
				luaL_error(L, "out of memory");
			}
			ctx->callback(&k, &v, ctx->userdata);
			FreeVariableData(&k);
			FreeVariableData(&v);
			lua_pop(L, 1);  // pop value, keep key for next lua_next
		}
		return 0;
	}

	KITSUNE_API void KitsuneGetAll(const char* path, kitsune_KeyValuePairCallback callback, void* userdata) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !callback) return;
#ifdef _WIN32
		if (GetCurrentThreadId() == state->schedulerThreadId.load() && state->DelegateState) return;  // scheduler thread re-entering from within a registered function; would deadlock
#endif

		AcquireLuaAccess(state);

		if (!PushGlobalAtPath(state->L, path)) {
			ReleaseLuaAccess(state);
			return;
		}

		if (lua_istable(state->L, -1)) {
			// Wrap iteration in lua_pcall so any error from lua_next (bad key, OOM, etc.)
			// is caught here rather than longjmp-ing past ReleaseLuaAccess below.
			KitsuneGetAllCtx ctx = { callback, userdata };
			lua_pushlightuserdata(state->L, &ctx);        // upvalue 1 for the closure
			lua_pushcclosure(state->L, GetAllIteratorBody, 1);
			lua_pushvalue(state->L, -2);                  // dup the table as the sole argument
			if (lua_pcall(state->L, 1, 0, 0) != LUA_OK)
				lua_pop(state->L, 1);  // discard error message; stack is now [..., table]
		}
		lua_pop(state->L, 1);  // pop table
		ReleaseLuaAccess(state);
	}

	static int LuaResultSetter(const KitsuneVariable* result) {
		KitsuneState* state = g_state;
		lua_State* L = state->DelegateState;
		if (!L || !result) return 0;

		if (result->type == KITSUNE_TERROR) {
			// Defer the raise until LuaCFunctionWrapper has freed its args array.
			if (state->lastCallError) { gff_free(state->lastCallError); state->lastCallError = nullptr; }
			const char* msg = (result->data) ? (const char*)result->data : "error";
			size_t len = (result->data && result->length > 0) ? result->length : strlen(msg);
			state->lastCallError = (char*)gff_malloc(len + 1);
			if (state->lastCallError) { memcpy(state->lastCallError, msg, len); state->lastCallError[len] = '\0'; }
			return 0;
		}

		// Push the result directly onto the calling coroutine's stack.
		PushKitsuneVariable(L, result);
		return 1;
	}

	static int LuaCFunctionWrapper(lua_State* L) {
		KitsuneState* state = g_state;
		kitsune_CFunction func = (kitsune_CFunction)lua_touserdata(L, lua_upvalueindex(1));
		void* userdata = lua_touserdata(L, lua_upvalueindex(2));
		if (!state || !func) {
			lua_pushstring(L, "invalid function wrapper");
			lua_error(L);
			return 0;  // unreachable
		}

		int argc = lua_gettop(L);
		KitsuneVariable* args = nullptr;
		if (argc > 0) {
			args = (KitsuneVariable*)gff_calloc(argc, sizeof(KitsuneVariable));
			if (!args) {
				lua_pushstring(L, "out of memory");
				lua_error(L);
				return 0;  // unreachable
			}
		}

		// Set DelegateState before filling args: if a LUA_TUSERDATA argument's __tostring
		// metamethod calls a guarded Kitsune API function, the guard returns early instead
		// of deadlocking. Save/restore handles re-entrant registered-function calls correctly.
		lua_State* prevDelegateState = state->DelegateState;
		state->DelegateState = L;

		for (int i = 0; i < argc; i++)
			FillKitsuneVariableFromStack(L, i + 1, &args[i], /*shallow=*/false);

		// FillKitsuneVariableFromStack signals string allocation failure via LUA_TNONE.
		for (int i = 0; i < argc; i++) {
			if (args[i].type == LUA_TNONE) {
				for (int j = 0; j < argc; j++)
					FreeVariableData(&args[j]);
				gff_free(args);
				state->DelegateState = prevDelegateState;
				lua_pushstring(L, "out of memory");
				lua_error(L);
				return 0;  // unreachable
			}
		}

		// Clear the stack so LuaResultSetter pushes results onto a clean base.
		lua_settop(L, 0);

		int rc = func(argc, args, LuaResultSetter, userdata);
		state->DelegateState = prevDelegateState;  // restore; handles nesting correctly

		// Free args before any potential lua_error so we never leak them on the error path.
		for (int i = 0; i < argc; i++)
			FreeVariableData(&args[i]);
		gff_free(args);

		// Raise a deferred error that was stored by LuaResultSetter for KITSUNE_TERROR.
		if (state->lastCallError) {
			lua_pushstring(L, state->lastCallError);
			gff_free(state->lastCallError);
			state->lastCallError = nullptr;
			lua_error(L);
			return 0;  // unreachable
		}

		if (rc <= 0) {
			lua_pushstring(L, "delegate function error");
			lua_error(L);
			return 0;  // unreachable
		}

		return lua_gettop(L);  // number of values pushed by LuaResultSetter
	}

	KITSUNE_API void KitsuneRegisterFunction(const char* name, kitsune_CFunction func, void* userdata) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !name || !*name || !func) return;
#ifdef _WIN32
		if (GetCurrentThreadId() == state->schedulerThreadId.load() && state->DelegateState) return;  // scheduler thread re-entering from within a registered function; would deadlock
#endif
		AcquireLuaAccess(state);

		const char* finalKey = NavigateGlobalParent(state->L, name, true);
		if (finalKey) {
			lua_pushlightuserdata(state->L, (void*)func);
			lua_pushlightuserdata(state->L, userdata);
			lua_pushcclosure(state->L, LuaCFunctionWrapper, 2);
			lua_setfield(state->L, -2, finalKey);  // parent[finalKey] = closure
			lua_pop(state->L, 1);  // pop parent table
		}

		ReleaseLuaAccess(state);
	}

	KITSUNE_API size_t KitsuneCleanup() {
		KitsuneState* state = g_state;
		g_state = nullptr;

		if (state) {
			// Signal the scheduler to exit and wait for it to finish.
			if (state->schedulerThread.joinable()) {
				// Interrupt any running coroutines first so the scheduler is not stuck inside
				// lua_resume waiting for a script to yield; the Ticker will call luaL_error at
				// the next instruction boundary, unblocking the scheduler promptly.
				state->interrupt.store(1);
				state->schedulerStop.store(1);
				state->resumeEvent.Set();   // unblock scheduler if it is in the pause handler
				state->workEvent.Set();     // wake scheduler if it is sleeping
				state->schedulerThread.join();
			}

			// Free all slot pointers. Slots with id==0 are already zeroed; only active slots need resource cleanup.
			for (int i = 0; i < state->slotCount; i++) {
				KitsuneCoroutine* slot = state->slots[i];
				if (slot->id != 0) {
					if (state->L) {
						if (slot->argsRef != LUA_NOREF)
							luaL_unref(state->L, LUA_REGISTRYINDEX, slot->argsRef);
						if (slot->threadRef != LUA_NOREF)
							luaL_unref(state->L, LUA_REGISTRYINDEX, slot->threadRef);
					}
					gff_free(slot->error);
					FreeVariableData(&slot->result);
				}
				delete slot;
			}
			state->slotCount = 0;

			if (state->L) {
							if (state->lastCallError) {
									gff_free(state->lastCallError);
									state->lastCallError = nullptr;
								}
						lua_gc(state->L, LUA_GCCOLLECT, 0);
				lua_close(state->L);
				state->L = nullptr;
			}
			// After lua_close all Lua streams are GC'd (OWNER_DISPOSED set).
			// Sweep the block registry to free completed blocks.
			// Blocks still held by live C# LuaStream instances remain until C# Dispose.
			lua_shmem_sweep_disposed_blocks();

			delete state;
		}

		#ifdef KITSUNE_HTTP
		curl_global_cleanup();
#endif
		#ifdef _WIN32
		WSACleanup();
#endif
		size_t leaked = EndMemoryManager();
#ifdef _WIN32
		if (g_coOwned) {
			CoUninitialize();
			g_coOwned = false;
		}
#endif
		return leaked;
	}

	KITSUNE_API void KitsuneRegisterSession() {
		KitsuneState* state = g_state;
		if (!state || !state->L) return;
#ifdef _WIN32
		if (GetCurrentThreadId() == state->schedulerThreadId.load() && state->DelegateState) return;
#endif
		AcquireLuaAccess(state);
		luaopen_session(state->L);
		ReleaseLuaAccess(state);
	}

	KITSUNE_API SharedMemoryBlock* KitsuneCreateMemoryBlock(size_t size) {
		if (!g_state || size == 0) return NULL;

		SharedMemoryBlock* block = (SharedMemoryBlock*)gff_malloc(sizeof(SharedMemoryBlock) + size);
		if (!block) return NULL;
		memset(block, 0, sizeof(SharedMemoryBlock) + size);
		block->size  = size;
		// KITSUNE_OWNED: accepted by PushKitsuneVariable.
		// ACCESSOR_DISPOSED=1: cleared by LuaStream constructor when C# takes ownership.
		block->flags = KITSUNE_SHARED_MEMORY_FLAG_KITSUNE_OWNED
					 | KITSUNE_SHARED_MEMORY_FLAG_ACCESSOR_DISPOSED;
		lua_shmem_list_add(block);
		return block;
	}

} // extern "C"
