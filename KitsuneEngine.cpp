#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#endif

#include <cassert>  // assert() is a no-op in release builds (NDEBUG defined by MSVC /MD /MT)
#include <clocale>  // setlocale — force LC_NUMERIC to "C" so Lua number formatting uses '.' not ','
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
#include "identifiermain.h"
#include "luaidentifier.h"
#include "datetimemain.h"
#include "luadatetime.h"
#include "decimalmain.h"
#include "luadecimal.h"
#include "luawchar.h"
#include "LuaCsvMain.h"
#include "SHA1Main.h"

#include "KitsuneEngine.h"
#include "LuaEngineBuiltins.h"
#include "kitsuneuserdata.h"

// Unique address used as the Lua registry key for the shared bridge LuaJson instance.
// Defined in luajson.cpp; accessed via lua_json_bridge_registry_key().

// -- Portable auto-reset event (replaces Win32 HANDLE-based WinEvent) ---------
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

// -- Per-coroutine slot --------------------------------------------------------
struct KitsuneCoroutine {
	int           id;
	int           threadRef;    // LUA_REGISTRYINDEX anchor; keeps the thread alive for GC
	lua_State* thread;       // cached lua_State*; valid iff threadRef != LUA_NOREF.
	// Must be NULLed whenever threadRef is unref'd.
	// Written only under AcquireLuaAccess; read only by the scheduler.
	int               argsRef;        // LUA_REGISTRYINDEX anchor for the ARGS table; retrieved by GetArgs()
	std::atomic<long> fireAndForget{ 0 };
	std::atomic<long> done{ 0 };        // 0 = still running / yielded, 1 = finished
	std::atomic<long> released{ 0 };    // 1 = slot should be freed; scheduler zeros it on next compaction
	std::atomic<long> interrupted{ 0 }; // set to 1 by KitsuneCancel; observed by the scheduler before resuming this coroutine
	char* error;
	KitsuneVariable  result;
	double        sleepUntil;   // GetCounter deadline (ms) before which the coroutine must not be resumed; 0 = not sleeping
	double        startTime;    // GetCounter value recorded when the coroutine was created
	int           initialNArgs; // number of args already on the thread stack for the first lua_resume; 0 for file/string coroutines
	std::atomic<long> isInline{ 0 }; // 1 = inline sync call; scheduler skips Step 2 resume
};

#define KITSUNE_MAX_COROUTINES 256

// -- Engine state --------------------------------------------------------------
struct KitsuneState {
	// -- Lua ------------------------------------------------------------------
	lua_State* L;
	double           PCFreq;
	int64_t          CounterStart;
	lua_State* DelegateState; // calling coroutine's state during a RegisterFunction call
	char* lastCallError;  // deferred KITSUNE_TERROR message; freed after args cleanup

	// -- Interrupt / pause ----------------------------------------------------
	std::atomic<long> interrupt{ 0 };   // set by KitsuneInterrupt; cleared by scheduler when all done
	std::atomic<long> pauseFlag{ 0 };   // set by AcquireLuaAccess; serviced by hook + scheduler
	PlatformEvent pausedEvent;   // hook signals this when it parks
	PlatformEvent resumeEvent;   // AcquireLuaAccess signals this to let hook continue

	// -- SetVariable/GetVariable serialisation --------------------------------
	std::mutex       accessLock; // serialises concurrent external callers

	// -- Scheduler thread -----------------------------------------------------
	std::thread           schedulerThread;
	std::atomic<long>     schedulerStop{ 0 }; // set to 1 by KitsuneCleanup
	PlatformEvent         workEvent;     // signaled when a new coroutine is ready to run
	// Signalled by SchedulerProc just before it returns (all work done, state no longer
	// accessed). KitsuneCleanup waits on this instead of join() so that the thread can
	// acquire the loader lock for DLL_THREAD_DETACH independently — avoiding the DllMain
	// loader-lock deadlock that join() causes when called from FreeLibrary/DLL_PROCESS_DETACH.
	PlatformEvent         schedulerDoneEvent;

	// -- Active coroutine slots (written only by scheduler; read by callers) --
	KitsuneCoroutine* slots[KITSUNE_MAX_COROUTINES];
	int               slotCount;
	std::mutex        slotsLock; // guards add/remove of slots[] entries

	// -- Done notification ----------------------------------------------------
	// Signalled (notify_all) whenever any slot transitions to done=1 or runningCount reaches 0.
	// Allows sync Execute* callers and KitsuneWait to block without Sleep(1) polling.
	std::mutex              doneMtx;
	std::condition_variable doneCV;

	// -- Counters -------------------------------------------------------------
	std::atomic<long> nextId{ 0 };             // monotonically increasing coroutine ID
	std::atomic<long> runningCount{ 0 };       // number of slots where done == 0
	std::atomic<long> currentCoroutineId{ 0 }; // ID of the coroutine currently inside lua_resume, or 0
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
	kitsune_free(slot->error);
	slot->error = NULL;
	if (msg) {
		size_t len = strlen(msg);
		slot->error = (char*)kitsune_malloc(len + 1);
		if (slot->error)
			memcpy(slot->error, msg, len + 1);
	}
}

// Forward declaration — defined after FillKitsuneVariableFromStack.
static KeyValuePairKitsuneVariableNode* TableToLinkedList(lua_State* L, int idx);

// -- Deferred variable-free queue ---------------------------------------------
// KitsuneVariableFree enqueues TFUNCTION / TTABLE (with function nodes) here instead of
// blocking on AcquireLuaAccess.  The scheduler drains the queue each cycle so luaL_unref
// is always called from a context that already owns the Lua state.
struct KitsuneVariableChain {
	KitsuneVariable* variable;
	KitsuneVariableChain* next;
};
static std::atomic<KitsuneVariableChain*> g_pendingVariableChainHead{ nullptr };
// True on a calling thread while it is inside RunInline (holds accessLock, running lua_resume).
// Suppresses the Ticker's pause-park so AcquireLuaAccess on the same thread doesn't deadlock.
static thread_local bool g_inlineExecution = false;

// Forward declaration — FreeVariableData is defined after FreeKVNode below.
static void FreeVariableData(KitsuneVariable* var, lua_State* L);


// Must be called from a context that holds Lua access (scheduler thread or KitsuneCleanup).
static void DrainPendingVariableChain(lua_State* L) {
	KitsuneVariableChain* chain = g_pendingVariableChainHead.exchange(nullptr, std::memory_order_acquire);
	while (chain) {
		KitsuneVariableChain* next = chain->next;
		FreeVariableData(chain->variable, L);
		kitsune_free(chain->variable);
		kitsune_free(chain);
		chain = next;
	}
}

// Recursively frees a KeyValuePairKitsuneVariableNode linked list produced by TableToLinkedList.
// L must be non-NULL if any node key or value may be LUA_TFUNCTION or LUA_TTHREAD (to release registry refs).
static void FreeKVNode(KeyValuePairKitsuneVariableNode* node, lua_State* L) {
	while (node) {
		if ((node->key.type == LUA_TSTRING || node->key.type == KITSUNE_TJSON || node->key.type == KITSUNE_TCHAR16 || node->key.type == KITSUNE_TERROR) && node->key.data)
			kitsune_free(node->key.data);
		else if (node->key.type == LUA_TUSERDATA && node->key.userdata) {
			kitsune_free(node->key.userdata->name);
			if (L && node->key.userdata->ref > 0)
				luaL_unref(L, LUA_REGISTRYINDEX, node->key.userdata->ref);
			kitsune_free(node->key.userdata);
		}
		else if (node->key.type == KITSUNE_TTABLECONTENTS && node->key.table)
			FreeKVNode(node->key.table, L);
		else if ((node->key.type == LUA_TTABLE || node->key.type == LUA_TFUNCTION || node->key.type == LUA_TTHREAD) && L && node->key.ref > 0)
			luaL_unref(L, LUA_REGISTRYINDEX, node->key.ref);
		if ((node->value.type == LUA_TSTRING || node->value.type == KITSUNE_TJSON || node->value.type == KITSUNE_TCHAR16 || node->value.type == KITSUNE_TERROR) && node->value.data)
			kitsune_free(node->value.data);
		else if (node->value.type == LUA_TUSERDATA && node->value.userdata) {
			kitsune_free(node->value.userdata->name);
			if (L && node->value.userdata->ref > 0)
				luaL_unref(L, LUA_REGISTRYINDEX, node->value.userdata->ref);
			kitsune_free(node->value.userdata);
		}
		else if (node->value.type == KITSUNE_TTABLECONTENTS && node->value.table)
			FreeKVNode(node->value.table, L);
		else if ((node->value.type == LUA_TTABLE || node->value.type == LUA_TFUNCTION || node->value.type == LUA_TTHREAD) && L && node->value.ref > 0)
			luaL_unref(L, LUA_REGISTRYINDEX, node->value.ref);
		KeyValuePairKitsuneVariableNode* next = node->next;
		kitsune_free(node);
		node = next;
	}
}

// Frees the heap data owned by a KitsuneVariable (string bytes, table linked list, or Lua function/thread ref).
// Nulls the data pointer after freeing to prevent double-free. Does NOT free var itself.
// L must be non-NULL when var may be LUA_TFUNCTION, LUA_TTHREAD, or LUA_TTABLE containing functions.
static void FreeVariableData(KitsuneVariable* var, lua_State* L) {
	if (!var) return;
	if ((var->type == LUA_TSTRING || var->type == KITSUNE_TJSON || var->type == KITSUNE_TERROR) && var->data) {
		kitsune_free(var->data);
		var->data = NULL;
	}
	else if (var->type == LUA_TUSERDATA && var->userdata) {
		kitsune_free(var->userdata->name);
		if (L && var->userdata->ref > 0)
			luaL_unref(L, LUA_REGISTRYINDEX, var->userdata->ref);
		kitsune_free(var->userdata);
		var->userdata = NULL;
	}
	else if (var->type == KITSUNE_TCHAR16 && var->char16data) {
		kitsune_free(var->char16data);
		var->char16data = NULL;
	}
	else if (var->type == KITSUNE_TTABLECONTENTS && var->table) {
		FreeKVNode(var->table, L);
		var->table = NULL;
	}
	else if (var->type == KITSUNE_TSTREAM) {
		// Signal the global-list sweeper that this slot's accessor reference is released.
		if (var->stream)
			var->stream->flags |= KITSUNE_SHARED_MEMORY_FLAG_ACCESSOR_DISPOSED;
		var->stream = NULL;
	}
	else if (var->type == KITSUNE_TITERATOR) {
		// KitsuneIterator* is caller-owned; the engine only nulls the pointer.
		var->iterator = nullptr;
	}
	else if ((var->type == LUA_TTABLE || var->type == LUA_TFUNCTION || var->type == LUA_TTHREAD) && L && var->ref > 0) {
		luaL_unref(L, LUA_REGISTRYINDEX, var->ref);
		var->ref = LUA_NOREF;
	}
}

// -- char16_t / wchar_t boundary helpers -----------------------------------------------------
// All casting between the public ABI type (char16_t, stored in KitsuneVariable) and the
// internal Lua representation (wchar_t, used by LuaWChar) is confined here.
// On Windows, wchar_t is 2 bytes (UTF-16 LE), so both helpers are zero-cost operations.
// A future non-Windows port replaces these two functions with real UTF-32 <-> UTF-16
// converters and adds the appropriate #ifdef guard — nothing outside these helpers changes.

// Allocates a char16_t* copy of a wchar_t* src (len code units, excluding null terminator).
// The caller owns the result; free with kitsune_free.
static char16_t* AllocChar16FromWchar(const wchar_t* src, size_t len, size_t* outChar16Len) {
	return wchar_alloc_as_char16(src, len, outChar16Len);
}

// Returns a wchar_t* view of a char16_t* for passing to Lua APIs.
// On Windows this is a no-op reinterpret cast. A future non-Windows port that stores UTF-32
// internally must allocate and convert here (and update callers to free the result).
static inline const wchar_t* Char16AsWchar(const char16_t* p) {
	return reinterpret_cast<const wchar_t*>(p);
}

// Forward declaration — LuaCFunctionWrapper is defined inside the extern "C" block below;
// wrapping in extern "C" here matches the definition's C language linkage and avoids C2732.
extern "C" { static int LuaCFunctionWrapper(lua_State* L); }
// Native single-step function for LUA_TTHREAD execution (replaces THREAD_STEP_SCRIPT).
// Upvalue 1: target thread (Lua thread value). Upvalue 2: argc (integer).
// argc args must be pre-pushed onto the target thread's stack before the wrapper is started.
// Returns: first yielded/returned value; nil if alive but yielded nothing (KITSUNE_TNIL);
// nothing (KITSUNE_TNONE) if dead with no return; raises a Lua error on failure.
extern "C" { static int ThreadStepNative(lua_State* L); }

// KitsuneIteratorUD — Lua-owned (lua_newuserdata) memory; GC'd via "KitsuneIterator" metatable.
struct KitsuneIteratorUD {
	kitsune_CFunctionData first;
	kitsune_CFunctionData next;
	kitsune_CFunctionData finalized;
	void* iteratorUserdata; // copy of KitsuneIterator.userdata; passed as userdata to each callback
	int state;              // 0=uncalled, 1=first called, 2=next, 3=finalized/dead
};

// Forward declarations — defined inside extern "C" below alongside LuaCFunctionWrapper.
extern "C" {
	static int KitsuneIteratorUD_gc(lua_State* L);
	static int KitsuneIteratorWrapper(lua_State* L);
}

// Fills a KitsuneVariable from the Lua stack at the given index.
// Tables are always returned as a live LUA_TTABLE registry ref; use KitsuneGetTableContents
// to snapshot contents, or KitsuneGetAll to iterate them. Caller owns any allocated data.
static void FillKitsuneVariableFromStack(lua_State* L, int idx, KitsuneVariable* out) {
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
			out->data = (unsigned char*)kitsune_malloc(len + 1);
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
		// Identifier is bridged as LUA_TSTRING (KITSUNE_TSTRING): canonical string representation.
		if (lua_isidentifier(L, abs_idx)) {
			lua_identifier_push_string(L, abs_idx);
			size_t slen;
			const char* s = lua_tolstring(L, -1, &slen);
			if (s) {
				out->data = (unsigned char*)kitsune_malloc(slen + 1);
				if (out->data) {
					memcpy(out->data, s, slen + 1);
					out->length = slen;
					out->type = LUA_TSTRING;
				}
			}
			lua_pop(L, 1);
			break;
		}
		// DateTime is bridged as LUA_TSTRING (KITSUNE_TSTRING): ISO 8601 string.
		if (lua_isdatetime(L, abs_idx)) {
			lua_datetime_push_string(L, abs_idx);
			size_t slen;
			const char* s = lua_tolstring(L, -1, &slen);
			if (s) {
				out->data = (unsigned char*)kitsune_malloc(slen + 1);
				if (out->data) {
					memcpy(out->data, s, slen + 1);
					out->length = slen;
					out->type = LUA_TSTRING;
				}
			}
			lua_pop(L, 1);
			break;
		}
		// Decimal is bridged as LUA_TSTRING (KITSUNE_TSTRING): canonical decimal string.
		if (lua_isdecimal(L, abs_idx)) {
			lua_decimal_push_string(L, abs_idx);
			size_t slen;
			const char* s = lua_tolstring(L, -1, &slen);
			if (s) {
				out->data = (unsigned char*)kitsune_malloc(slen + 1);
				if (out->data) {
					memcpy(out->data, s, slen + 1);
					out->length = slen;
					out->type = LUA_TSTRING;
				}
			}
			lua_pop(L, 1);
			break;
		}
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
		// All other userdata types: bridge as KITSUNE_TUSERDATA with a KitsuneUserData*.
		// Kitsune-registered userdatas (sentinel present) also surface their instance pointer.
		// __tostring is intentionally not called (may execute arbitrary Lua code).
		if (lua_getmetatable(L, abs_idx)) {
			lua_pushliteral(L, "__kitsune_userdata");
			lua_rawget(L, -2);
			bool isKitsuneRegistered = lua_touserdata(L, -1) == (void*)lua_registerkitsuneuserdata;
			lua_pop(L, 1);  // pop sentinel value

			lua_getfield(L, -1, "__name");
			if (lua_type(L, -1) == LUA_TSTRING) {
				size_t typeNameLen;
				const char* typeName = lua_tolstring(L, -1, &typeNameLen);
				if (typeName && typeNameLen > 0) {
					KitsuneUserData* kud = (KitsuneUserData*)kitsune_malloc(sizeof(KitsuneUserData));
					if (kud) {
						kud->name = (char*)kitsune_malloc(typeNameLen + 1);
						if (kud->name) {
							memcpy(kud->name, typeName, typeNameLen + 1);
							kud->userdata = isKitsuneRegistered
								? ((LuaKitsuneUserdata*)lua_touserdata(L, abs_idx))->userdata
								: NULL;
							kud->ref = LUA_NOREF;
							lua_pushvalue(L, abs_idx);
							kud->ref = luaL_ref(L, LUA_REGISTRYINDEX);
							out->userdata = kud;
							out->length = typeNameLen;
						}
						else {
							kitsune_free(kud);
						}
					}
				}
			}
			lua_pop(L, 2);  // pop __name and metatable
		}
		out->type = LUA_TUSERDATA;
		break;
	}
	case LUA_TTABLE:
		lua_pushvalue(L, abs_idx);
		out->ref = luaL_ref(L, LUA_REGISTRYINDEX);
		out->type = LUA_TTABLE;
		break;
	case LUA_TFUNCTION:
		// Anchor the function in the Lua registry so it survives beyond this stack frame.
		// The ref is stored in out->ref; release with luaL_unref via FreeVariableData.
		lua_pushvalue(L, abs_idx);
		out->ref = luaL_ref(L, LUA_REGISTRYINDEX);
		out->type = LUA_TFUNCTION;
		break;
	case LUA_TTHREAD:
		// Anchor the coroutine thread in the Lua registry so it can be iterated from C#.
		// The ref is stored in out->ref; release with luaL_unref via FreeVariableData.
		lua_pushvalue(L, abs_idx);
		out->ref = luaL_ref(L, LUA_REGISTRYINDEX);
		out->type = LUA_TTHREAD;
		break;
	case LUA_TLIGHTUSERDATA:
		out->type = LUA_TLIGHTUSERDATA;
		out->lightuserdata = lua_touserdata(L, abs_idx);
		break;
	default:
		out->type = t;
		break;
	}
}

// Converts a Lua table at stack index idx into a heap-allocated KeyValuePairKitsuneVariableNode
// linked list. Nested tables are returned as live LUA_TTABLE registry refs via FillKitsuneVariableFromStack.
static KeyValuePairKitsuneVariableNode* TableToLinkedList(lua_State* L, int idx) {
	KeyValuePairKitsuneVariableNode* head = NULL;
	KeyValuePairKitsuneVariableNode** tail = &head;
	int abs_idx = lua_absindex(L, idx);
	lua_pushnil(L);  // first key
	while (lua_next(L, abs_idx)) {
		KeyValuePairKitsuneVariableNode* node = (KeyValuePairKitsuneVariableNode*)kitsune_malloc(sizeof(KeyValuePairKitsuneVariableNode));
		if (!node) {
			lua_pop(L, 2);
			break;  // OOM: abort iteration with partial list
		}
		memset(node, 0, sizeof(KeyValuePairKitsuneVariableNode));
		FillKitsuneVariableFromStack(L, -2, &node->key);
		FillKitsuneVariableFromStack(L, -1, &node->value);
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
				kitsune_free(wbuf);
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
			lua_newtable(L);  // empty JSON object ? empty table
		}
		break;
	}
	case LUA_TTABLE:
		// Live ref — push the actual Lua table from the registry.
		if (v->ref > 0) {  // valid luaL_ref is always positive; 0 and LUA_NOREF(-2) mean no ref
			lua_rawgeti(L, LUA_REGISTRYINDEX, v->ref);
			if (lua_type(L, -1) != LUA_TTABLE) {
				lua_pop(L, 1);
				lua_pushnil(L);
			}
		}
		else
			lua_newtable(L);  // no ref: push a fresh empty table
		break;
	case KITSUNE_TTABLECONTENTS:
		// Snapshot — create a new Lua table and populate it from the linked list.
		lua_newtable(L);
		if (v->table) {
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
	case LUA_TFUNCTION:
		// Push the function from the Lua registry using the stored ref.
		// Pushing via rawgeti does not consume the ref; the caller retains ownership.
		if (v->ref > 0) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, v->ref);
			if (lua_type(L, -1) != LUA_TFUNCTION) {
				lua_pop(L, 1);
				lua_pushnil(L);
			}
		}
		else
			lua_pushnil(L);
		break;
	case LUA_TTHREAD:
		// Push the coroutine thread from the Lua registry using the stored ref.
		if (v->ref > 0) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, v->ref);
			if (lua_type(L, -1) != LUA_TTHREAD) {
				lua_pop(L, 1);
				lua_pushnil(L);
			}
		}
		else
			lua_pushnil(L);
		break;
	case KITSUNE_TCFUNCTION: {
		// Create an anonymous Lua closure from the kitsune_CFunctionData pointed to by data.
		// func and userdata are stored by value as light-userdata upvalues so the struct
		// is not referenced after this case returns.
		const kitsune_CFunctionData* cfd = (const kitsune_CFunctionData*)v->data;
		if (cfd && cfd->func) {
			lua_pushlightuserdata(L, (void*)cfd->func);
			lua_pushlightuserdata(L, cfd->userdata);
			lua_pushcclosure(L, LuaCFunctionWrapper, 2);
		}
		else {
			lua_pushnil(L);
		}
		break;
	}
	case KITSUNE_TITERATOR: {
		// Allocate a KitsuneIteratorUD full userdata (Lua-owned), copy the three callback
		// slots from the host's KitsuneIterator, set the __gc metatable, then wrap it in a
		// closure. Lua sees only the closure; the userdata is upvalue 1.
		const KitsuneIterator* it = v->iterator;
		if (!it) {
			lua_pushnil(L);
			break;
		}
		KitsuneIteratorUD* ud = (KitsuneIteratorUD*)lua_newuserdata(L, sizeof(KitsuneIteratorUD));
		memset(ud, 0, sizeof(KitsuneIteratorUD));
		if (it->first)     ud->first = *it->first;
		if (it->next)      ud->next = *it->next;
		if (it->finalized) ud->finalized = *it->finalized;
		ud->iteratorUserdata = it->userdata;
		ud->state = 0;
		luaL_setmetatable(L, "KitsuneIterator");
		lua_pushcclosure(L, KitsuneIteratorWrapper, 1);
		break;
	}
	case LUA_TUSERDATA: {
		// Push the original Lua userdata from the registry when available (preserves Lua identity).
		// Fall back to constructing a new wrapper for manually built variables (ref == LUA_NOREF).
		const KitsuneUserData* ud = v->userdata;
		if (!ud || !ud->name) {
			lua_pushnil(L);
			break;
		}
		if (ud->ref > 0) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, ud->ref);
			if (lua_type(L, -1) != LUA_TUSERDATA) {
				lua_pop(L, 1);
				lua_pushnil(L);
			}
			break;
		}
		LuaKitsuneUserdata* lkud = (LuaKitsuneUserdata*)lua_newuserdata(L, sizeof(LuaKitsuneUserdata));
		lkud->name = ud->name;
		lkud->userdata = ud->userdata;
		if (luaL_getmetatable(L, ud->name) != LUA_TTABLE) {
			lua_pop(L, 2);  // pop failed getmetatable result and the new userdata
			lua_pushnil(L);
			break;
		}
		lua_setmetatable(L, -2);
		break;
	}
	case LUA_TLIGHTUSERDATA:
		lua_pushlightuserdata(L, v->lightuserdata);
		break;
	default:
		lua_pushnil(L);
		break;
	}
}

static void SetSlotResult(KitsuneCoroutine* slot, lua_State* T, int idx) {
	FreeVariableData(&slot->result, T);
	memset(&slot->result, 0, sizeof(slot->result));
	slot->result.type = LUA_TNONE;
	int t = lua_type(T, idx);
	switch (t) {
	case LUA_TUSERDATA: {
		// Identifier is bridged as LUA_TSTRING (KITSUNE_TSTRING): canonical string representation.
		if (lua_isidentifier(T, idx)) {
			lua_identifier_push_string(T, idx);
			size_t slen;
			const char* s = lua_tolstring(T, -1, &slen);
			if (s) {
				slot->result.data = (unsigned char*)kitsune_malloc(slen + 1);
				if (slot->result.data) {
					memcpy(slot->result.data, s, slen + 1);
					slot->result.length = slen;
					slot->result.type = LUA_TSTRING;
				}
			}
			lua_pop(T, 1);
			break;
		}
		// DateTime is bridged as LUA_TSTRING (KITSUNE_TSTRING): ISO 8601 string.
		if (lua_isdatetime(T, idx)) {
			lua_datetime_push_string(T, idx);
			size_t slen;
			const char* s = lua_tolstring(T, -1, &slen);
			if (s) {
				slot->result.data = (unsigned char*)kitsune_malloc(slen + 1);
				if (slot->result.data) {
					memcpy(slot->result.data, s, slen + 1);
					slot->result.length = slen;
					slot->result.type = LUA_TSTRING;
				}
			}
			lua_pop(T, 1);
			break;
		}
		// Decimal is bridged as LUA_TSTRING (KITSUNE_TSTRING): canonical decimal string.
		if (lua_isdecimal(T, idx)) {
			lua_decimal_push_string(T, idx);
			size_t slen;
			const char* s = lua_tolstring(T, -1, &slen);
			if (s) {
				slot->result.data = (unsigned char*)kitsune_malloc(slen + 1);
				if (slot->result.data) {
					memcpy(slot->result.data, s, slen + 1);
					slot->result.length = slen;
					slot->result.type = LUA_TSTRING;
				}
			}
			lua_pop(T, 1);
			break;
		}
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
				slot->result.type = KITSUNE_TSTREAM;
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
				slot->result.type = KITSUNE_TSTREAM;
				slot->result.stream = block;
				block->flags &= ~KITSUNE_SHARED_MEMORY_FLAG_ACCESSOR_DISPOSED;
				lua_pop(T, 1);  // pop the outbound userdata
			}
			else {
				SetSlotError(slot, "stream result must be readable and seekable");
			}
			break;
		}
		// All other userdata types: bridge as KITSUNE_TUSERDATA with a KitsuneUserData*.
		// Kitsune-registered userdatas (sentinel present) also surface their instance pointer.
		if (lua_getmetatable(T, idx)) {
			lua_pushliteral(T, "__kitsune_userdata");
			lua_rawget(T, -2);
			bool isKitsuneRegistered = lua_touserdata(T, -1) == (void*)lua_registerkitsuneuserdata;
			lua_pop(T, 1);  // pop sentinel value

			lua_getfield(T, -1, "__name");
			if (lua_type(T, -1) == LUA_TSTRING) {
				size_t typeNameLen;
				const char* typeName = lua_tolstring(T, -1, &typeNameLen);
				if (typeName && typeNameLen > 0) {
					KitsuneUserData* kud = (KitsuneUserData*)kitsune_malloc(sizeof(KitsuneUserData));
					if (kud) {
						kud->name = (char*)kitsune_malloc(typeNameLen + 1);
						if (kud->name) {
							memcpy(kud->name, typeName, typeNameLen + 1);
							kud->userdata = isKitsuneRegistered
								? ((LuaKitsuneUserdata*)lua_touserdata(T, idx))->userdata
								: NULL;
							kud->ref = LUA_NOREF;
							lua_pushvalue(T, idx);
							kud->ref = luaL_ref(T, LUA_REGISTRYINDEX);
							slot->result.userdata = kud;
							slot->result.length = typeNameLen;
						}
						else {
							kitsune_free(kud);
						}
					}
				}
			}
			lua_pop(T, 2);  // pop __name and metatable
		}
		slot->result.type = LUA_TUSERDATA;
		break;
	}
	case LUA_TTABLE: {
		lua_pushvalue(T, idx);
		slot->result.ref = luaL_ref(T, LUA_REGISTRYINDEX);
		slot->result.type = LUA_TTABLE;
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
	// If the scheduler has already stopped (KitsuneCleanup detached it before this
	// call), no one will ever signal pausedEvent.  Self-signal here so the Wait
	// below returns immediately rather than blocking forever.
	if (state->schedulerStop.load())
		state->pausedEvent.Set();
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

	if (state->pauseFlag.load() && !g_inlineExecution) {
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
	state->doneCV.notify_all();
	if (slot->fireAndForget.load())
		slot->released.store(1);
}

// True only on the scheduler thread; set once in SchedulerProc and never cleared.
// Used by all KitsuneExecute* guards on every platform without OS-specific thread IDs.
static thread_local bool g_isSchedulerThread = false;

// Sets the ARGS and ID globals on a coroutine's lua_State before each lua_resume.
static void SetCoroutineGlobals(lua_State* T, int id, int argsRef) {
	if (argsRef != LUA_NOREF) {
		lua_rawgeti(T, LUA_REGISTRYINDEX, argsRef);
		lua_setglobal(T, "ARGS");
	}
	lua_pushinteger(T, id);
	lua_setglobal(T, "ID");
}

static void SchedulerProc(KitsuneState* state) {
	g_isSchedulerThread = true;

	while (!state->schedulerStop.load()) {
		// -- Step 1: Service pause requests BEFORE touching state->L --------------
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
		// Drain variables queued for deferred release by KitsuneVariableFree.
		DrainPendingVariableChain(state->L);

		bool anyActive = false;

		// -- Step 2: Interrupt all non-done coroutines if requested ------------
		if (state->interrupt.load()) {
			for (int i = 0; i < state->slotCount; i++) {
				KitsuneCoroutine* slot = state->slots[i];
				if (slot->id != 0 && !slot->done.load() && !slot->isInline.load()) {
					SetSlotError(slot, "interrupted");
					slot->result.type = LUA_TNONE;
					lua_State* T = GetCoroutineThread(state, slot);
					if (T)
						lua_settop(T, 0);
					slot->done.store(1);
					--state->runningCount;
					state->doneCV.notify_all();
					if (slot->fireAndForget.load())
						slot->released.store(1);
				}
			}
		}
		else {
			// -- Step 2: Resume each active coroutine once ---------------------
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
				if (slot->isInline.load())
					continue;  // inline slot — managed by calling thread, not the scheduler
				// Per-coroutine cancel: terminate before the next resume (or wake from sleep).
				if (slot->interrupted.load()) {
					SetSlotError(slot, "cancelled");
					slot->result.type = LUA_TNONE;
					lua_State* Tc = GetCoroutineThread(state, slot);
					if (Tc)
						lua_settop(Tc, 0);
					slot->done.store(1);
					--state->runningCount;
					state->doneCV.notify_all();
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
					state->doneCV.notify_all();
					if (slot->fireAndForget.load())
						slot->released.store(1);
					continue;
				}

				// Refresh ARGS and ID globals for this coroutine before resuming.
				SetCoroutineGlobals(T, slot->id, slot->argsRef);

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

		// -- Step 3: Clear interrupt once no coroutines remain active ----------
		if (state->runningCount.load() == 0)
			state->interrupt.store(0);

		// -- Step 4: Release done + released slots – zero the struct for reuse -
		{
			// Phase 1 (under slotsLock): collect registry refs and slot results, then zero each slot.
			// All luaL_unref calls (pendingArgs, pendingThreads, and any TFUNCTION/TTABLE result)
			// are deferred to phase 2 so they do not block concurrent KitsuneCancel / KitsuneGetActiveIds
			// callers while Lua's internal allocator is running.
			int pendingArgs[KITSUNE_MAX_COROUTINES];
			int pendingThreads[KITSUNE_MAX_COROUTINES];
			KitsuneVariable pendingResults[KITSUNE_MAX_COROUTINES];
			int pendingCount = 0;

			state->slotsLock.lock();
			for (int i = 0; i < state->slotCount; i++) {
				KitsuneCoroutine* slot = state->slots[i];
				if (slot->id != 0 && slot->done.load() && slot->released.load()) {
					assert(pendingCount < KITSUNE_MAX_COROUTINES);
					pendingArgs[pendingCount] = slot->argsRef;
					pendingThreads[pendingCount] = slot->threadRef;
					pendingResults[pendingCount] = slot->result;  // shallow copy; ownership transferred
					pendingCount++;
					slot->thread = NULL;  // invariant: null before memset so the pointer is never stale
					kitsune_free(slot->error);
					memset(slot, 0, sizeof(KitsuneCoroutine));  // id = 0 marks the slot as reusable
				}
			}
			state->slotsLock.unlock();

			// Phase 2 (outside slotsLock): release all Lua registry references.
			for (int i = 0; i < pendingCount; i++) {
				if (pendingArgs[i] != LUA_NOREF)
					luaL_unref(state->L, LUA_REGISTRYINDEX, pendingArgs[i]);
				if (pendingThreads[i] != LUA_NOREF)
					luaL_unref(state->L, LUA_REGISTRYINDEX, pendingThreads[i]);
				FreeVariableData(&pendingResults[i], state->L);
			}
			if (pendingCount > 0)
				state->doneCV.notify_all(); // wake KitsuneWait: slots were compacted
		}

		// -- Step 5: Sleep until new work arrives -----------------------------
		// Lua's generational GC (LUA_GCGEN, minor=20%, major=100%) handles collection
		// incrementally under allocation pressure.  Forced full cycles belong only in
		// KitsuneCleanup (before lua_close) and in the explicit KitsuneGC() API.
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
	// All work is done; signal KitsuneCleanup that it is safe to proceed.
	// This must be the last access to state in this function.
	state->schedulerDoneEvent.Set();
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

// Yield() — cooperatively yields the calling coroutine back to the scheduler.
// For inline sync calls this triggers the yield loop: access is released briefly so the
// scheduler and variable bridge can service their queues before the call is resumed.
static int L_Yield(lua_State* L) {
	return lua_yield(L, 0);
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
		kitsune_free(ptr);
		return NULL;
	}
	else {
		return kitsune_realloc(ptr, nsize);
	}
}

// Allocates a heap KitsuneVariable with the given type and copies len bytes from data into
// var->data (plus a null terminator). Pass data=NULL or len=0 for type-only variables.
// Returns NULL on OOM. The caller must free the returned pointer with KitsuneVariableFree.
static KitsuneVariable* MakeStringVariable(int type, const char* data, size_t len) {
	KitsuneVariable* var = (KitsuneVariable*)kitsune_malloc(sizeof(KitsuneVariable));
	if (!var)
		return NULL;
	memset(var, 0, sizeof(KitsuneVariable));
	var->type = type;
	if (data && len > 0) {
		var->data = (unsigned char*)kitsune_malloc(len + 1);
		if (!var->data) {
			kitsune_free(var);
			return NULL;
		}
		memcpy(var->data, data, len + 1);
		var->length = len;
	}
	return var;
}

// Allocates a heap KitsuneVariable with KITSUNE_TERROR and an optional message.
// The caller must free the returned pointer with KitsuneVariableFree.
static KitsuneVariable* MakeErrorVariable(const char* msg) {
	return MakeStringVariable(KITSUNE_TERROR, msg, msg ? strlen(msg) : 0);
}

// Allocates a heap KitsuneVariable with KITSUNE_TNONE (no result, method/metamethod absent).
static KitsuneVariable* MakeNoneVariable() {
	return MakeStringVariable(LUA_TNONE, NULL, 0);
}

// Allocates a heap KitsuneVariable with KITSUNE_TNIL (method ran, returned nothing or explicit nil).
static KitsuneVariable* MakeNilVariable() {
	return MakeStringVariable(LUA_TNIL, NULL, 0);
}

// ============================================================
// Exported API
// ============================================================

static KitsuneState* g_state = nullptr;
#ifdef _WIN32
static bool g_coOwned = false;
#endif

extern "C" {

	KITSUNE_API bool KitsuneInit(MemoryAllocator* memoryAllocator) {
		if (g_state)
			return false;

		// Apply custom allocators before InitMemoryManager so every subsequent
		// allocation — including the KitsuneState itself — uses the caller's heap.
		if (memoryAllocator)
			kitsune_set_allocators(memoryAllocator->malloc, memoryAllocator->realloc, memoryAllocator->free);

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

		// Force LC_NUMERIC to "C" so Lua's tostring / string.format always use '.' as the
		// decimal separator, regardless of the OS locale.  Without this, machines with a
		// locale that uses ',' (e.g. German) produce "7,0" instead of "7.0".
		setlocale(LC_NUMERIC, "C");

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
		luaopen_identifier(L);   lua_setglobal(L, "Identifier");
		luaopen_datetime(L);     lua_setglobal(L, "DateTime");
		luaopen_decimal(L);      lua_setglobal(L, "Decimal");
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
		lua_pushcfunction(L, L_Yield);
		lua_setglobal(L, "Yield");

		// Register the KitsuneIterator metatable used by KITSUNE_TITERATOR closures.
		luaL_newmetatable(L, "KitsuneIterator");
		lua_pushcfunction(L, KitsuneIteratorUD_gc);
		lua_setfield(L, -2, "__gc");
		lua_pop(L, 1);

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

	// -- Async coroutine helpers ------------------------------------------------

	// Initialises a slot's refs to LUA_NOREF and creates the coroutine thread.
	// Caller must hold AcquireLuaAccess.  Returns the new lua_State*.
	static lua_State* PrepareSlotThread(KitsuneState* state, KitsuneCoroutine* slot) {
		slot->threadRef = LUA_NOREF;
		slot->argsRef = LUA_NOREF;
		return CreateCoroutineThread(state, slot);
	}

	// Builds the ARGS table on state->L and stores a registry reference in slot->argsRef.
	// File mode (isFile=true): ARGS[1]=path, ARGS[2..n+1]=argv[0..n-1].
	// String mode (isFile=false): ARGS[1..n]=argv[0..n-1].
	static void BuildArgsRef(KitsuneState* state, KitsuneCoroutine* slot,
		bool isFile, const char* path, int argc, const KitsuneVariable* argv) {
		lua_newtable(state->L);
		int base = 1;
		if (isFile) {
			lua_pushstring(state->L, path);
			lua_rawseti(state->L, -2, 1);
			base = 2;
		}
		for (int n = 0; n < argc; n++) {
			PushKitsuneVariable(state->L, argv ? &argv[n] : nullptr);
			lua_rawseti(state->L, -2, base + n);
		}
		slot->argsRef = luaL_ref(state->L, LUA_REGISTRYINDEX);
	}

	// Finds or allocates a reusable async (non-inline) slot and sets fireAndForget.
	// Returns NULL at capacity or on OOM; caller must ReleaseLuaAccess and return -1.
	static KitsuneCoroutine* AcquireAsyncSlot(KitsuneState* state,
		bool fireAndForget, bool& isNewSlot) {
		isNewSlot = false;
		for (int i = 0; i < state->slotCount; i++) {
			if (state->slots[i]->id == 0) {
				KitsuneCoroutine* slot = state->slots[i];
				slot->fireAndForget.store(fireAndForget ? 1 : 0);
				return slot;
			}
		}
		if (state->slotCount >= KITSUNE_MAX_COROUTINES)
			return NULL;
		KitsuneCoroutine* slot = new (std::nothrow) KitsuneCoroutine{};
		if (!slot)
			return NULL;
		slot->fireAndForget.store(fireAndForget ? 1 : 0);
		isNewSlot = true;
		return slot;
	}

	// Marks a pre-running async slot as done/failed and releases any held registry refs.
	// err=NULL means the error was already set via SetSlotError (or there is no error).
	static void FailAsyncSlot(KitsuneState* state, KitsuneCoroutine* slot, const char* err) {
		if (err)
			SetSlotError(slot, err);
		slot->result.type = LUA_TNONE;
		if (slot->argsRef != LUA_NOREF) {
			luaL_unref(state->L, LUA_REGISTRYINDEX, slot->argsRef);
			slot->argsRef = LUA_NOREF;
		}
		if (slot->threadRef != LUA_NOREF) {
			luaL_unref(state->L, LUA_REGISTRYINDEX, slot->threadRef);
			slot->threadRef = LUA_NOREF;
			slot->thread = NULL;
		}
		slot->done.store(1);
		state->doneCV.notify_all();
		if (slot->fireAndForget.load())
			slot->released.store(1);
	}

	// Assigns an id, adds the slot to slots[] if new, releases Lua access, and wakes the scheduler.
	// Always the final step of every StartCoroutine* path (success or failure).
	static int CommitAndLaunchAsync(KitsuneState* state, KitsuneCoroutine* slot,
		int id, bool isNewSlot) {
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

	// Allocates and fills callback args from the Lua call frame (1..argc), then clears the frame.
	// Returns the args array on success (NULL for argc==0 is not an error).
	// Returns NULL on OOM; all partial allocations are freed before returning.
	// DelegateState must be set by the caller before this call.
	static KitsuneVariable* AllocAndFillArgs(lua_State* L, int argc) {
		if (argc == 0) {
			lua_settop(L, 0);
			return nullptr;
		}
		KitsuneVariable* args = (KitsuneVariable*)kitsune_calloc(argc, sizeof(KitsuneVariable));
		if (!args)
			return nullptr;
		for (int i = 0; i < argc; i++)
			FillKitsuneVariableFromStack(L, i + 1, &args[i]);
		lua_settop(L, 0);
		for (int i = 0; i < argc; i++) {
			if (args[i].type == LUA_TNONE) {
				for (int j = 0; j < argc; j++) FreeVariableData(&args[j], L);
				kitsune_free(args);
				return nullptr;
			}
		}
		return args;
	}

	// Releases the args array returned by AllocAndFillArgs.
	static void FreeCallbackArgs(lua_State* L, KitsuneVariable* args, int argc) {
		for (int i = 0; i < argc; i++) FreeVariableData(&args[i], L);
		kitsune_free(args);
	}

	// -- Start a coroutine directly: acquire Lua access, create the thread, hand it to the scheduler -
	static int StartCoroutine(KitsuneState* state, bool isFile,
		const char* source, int argc, const KitsuneVariable* argv,
		bool fireAndForget) {
		if (!state || !source) return -1;

		// Acquire Lua access: pauses any running coroutine at the next ticker boundary
		// and serialises concurrent calls (e.g. with SetVariable / GetVariable).
		AcquireLuaAccess(state);

		bool isNewSlot = false;
		KitsuneCoroutine* slot = AcquireAsyncSlot(state, fireAndForget, isNewSlot);
		if (!slot) {
			ReleaseLuaAccess(state);
			return -1;
		}

		lua_State* T = PrepareSlotThread(state, slot);

		// Build the ARGS table: ARGS[1]=path (file) or ARGS[1..]=argv[0..] (string).
		BuildArgsRef(state, slot, isFile, source, argc, argv);

		// Load the script onto the coroutine thread's stack.
		int loadrc = isFile
			? luaL_loadfile(T, source)
			: luaL_loadbuffer(T, source, strlen(source), "string");

		int id = (int)(++state->nextId);

		if (loadrc != 0) {
			const char* err = lua_tolstring(T, -1, NULL);
			lua_settop(T, 0);
			FailAsyncSlot(state, slot, err ? err : "load error");
		}
		else {
			++state->runningCount;
		}

		return CommitAndLaunchAsync(state, slot, id, isNewSlot);
	}

	KITSUNE_API int KitsuneExecuteFileAsync(const char* path, int argc, const KitsuneVariable* argv, bool fireAndForget) {
		if (g_isSchedulerThread || g_inlineExecution) return -1;
		return StartCoroutine(g_state, true, path, argc, argv, fireAndForget);
	}

	KITSUNE_API int KitsuneExecuteStringAsync(const char* script, int argc, const KitsuneVariable* argv, bool fireAndForget) {
		if (g_isSchedulerThread || g_inlineExecution) return -1;
		return StartCoroutine(g_state, false, script, argc, argv, fireAndForget);
	}

	// Forward declaration: PushGlobalAtPath is defined after the coroutine-start helpers.
	static bool PushGlobalAtPath(lua_State* L, const char* path);

	static int StartCoroutineFunction(KitsuneState* state, const char* functionName,
		int argc, const KitsuneVariable* argv, bool fireAndForget) {
		if (!state || !functionName) return -1;

		AcquireLuaAccess(state);

		bool isNewSlot = false;
		KitsuneCoroutine* slot = AcquireAsyncSlot(state, fireAndForget, isNewSlot);
		if (!slot) {
			ReleaseLuaAccess(state);
			return -1;
		}

		lua_State* T = PrepareSlotThread(state, slot);

		// Resolve the function via dot-path navigation on the main state, then move it to T.
		// PushGlobalAtPath handles both "Foo" and "Ns.Foo" uniformly, matching SetVariable behaviour.
		if (PushGlobalAtPath(state->L, functionName))
			lua_xmove(state->L, T, 1);  // move the resolved value (function or nil) to T
		else
			lua_pushnil(T);  // invalid path; !lua_isfunction below triggers "function not found"

		int id = (int)(++state->nextId);

		if (!lua_isfunction(T, -1)) {
			lua_pop(T, 1);
			FailAsyncSlot(state, slot, "function not found");
		}
		else {
			for (int n = 0; n < argc; n++)
				PushKitsuneVariable(T, argv ? &argv[n] : nullptr);
			slot->initialNArgs = argc;
			++state->runningCount;
		}

		return CommitAndLaunchAsync(state, slot, id, isNewSlot);
	}

	KITSUNE_API int KitsuneExecuteFunctionAsync(const char* functionName, int argc, const KitsuneVariable* argv, bool fireAndForget) {
		if (g_isSchedulerThread || g_inlineExecution) return -1;
		return StartCoroutineFunction(g_state, functionName, argc, argv, fireAndForget);
	}

	// Executes a KitsuneVariable as a coroutine:
	//   LUA_TFUNCTION — pushes the function from the Lua registry and calls it with argc/argv as direct parameters.
	//   LUA_TSTRING   — loads the string as a Lua chunk and runs it; argv is exposed as ARGS[1..argc].
	//   Anything else — the slot is created in done/faulted state with a descriptive error.
	static int StartCoroutineVariable(KitsuneState* state, const KitsuneVariable* var,
		int argc, const KitsuneVariable* argv, bool fireAndForget) {
		if (!state || !var) return -1;

		AcquireLuaAccess(state);

		bool isNewSlot = false;
		KitsuneCoroutine* slot = AcquireAsyncSlot(state, fireAndForget, isNewSlot);
		if (!slot) {
			ReleaseLuaAccess(state);
			return -1;
		}

		lua_State* T = PrepareSlotThread(state, slot);

		int id = (int)(++state->nextId);
		bool loadOk = false;

		if (var->type == LUA_TFUNCTION) {
			// Lift the function from the Lua registry onto T; args are passed as direct parameters.
			if (var->ref > 0)
				lua_rawgeti(state->L, LUA_REGISTRYINDEX, var->ref);
			else
				lua_pushnil(state->L);
			lua_xmove(state->L, T, 1);

			if (!lua_isfunction(T, -1)) {
				lua_pop(T, 1);
				SetSlotError(slot, "invalid function reference");
			}
			else {
				for (int n = 0; n < argc; n++)
					PushKitsuneVariable(T, argv ? &argv[n] : nullptr);
				slot->initialNArgs = argc;
				loadOk = true;
			}
		}
		else if (var->type == LUA_TSTRING && var->data && var->length > 0) {
			// Build ARGS table: argv[0..argc-1] map to ARGS[1..argc].
			BuildArgsRef(state, slot, false, nullptr, argc, argv);

			int rc = luaL_loadbuffer(T, (const char*)var->data, var->length, "variable");
			if (rc != 0) {
				const char* err = lua_tolstring(T, -1, NULL);
				SetSlotError(slot, err ? err : "load error");
				lua_settop(T, 0);
				luaL_unref(state->L, LUA_REGISTRYINDEX, slot->argsRef);
				slot->argsRef = LUA_NOREF;
			}
			else {
				loadOk = true;
			}
		}
		else if (var->type == LUA_TTHREAD && var->ref != LUA_NOREF) {
			// Native single-step: resume the target thread exactly once with argc/argv as args.
			// The first yielded or returned value is surfaced as the result.
			lua_rawgeti(state->L, LUA_REGISTRYINDEX, var->ref);
			lua_State* targetT = lua_tothread(state->L, -1);
			lua_pop(state->L, 1);

			if (!targetT) {
				SetSlotError(slot, "invalid thread");
			}
			else if (lua_status(targetT) == LUA_OK && lua_gettop(targetT) == 0) {
				// Thread is dead; leave loadOk=false with no error so result is TNONE.
			}
			else {
				lua_sethook(targetT, Ticker, LUA_MASKCOUNT, 1000);
				// Pre-push argv onto targetT — these become resume args for this step.
				for (int n = 0; n < argc; n++)
					PushKitsuneVariable(targetT, argv ? &argv[n] : nullptr);
				// Push ThreadStepNative closure onto wrapper T.
				// Upvalue 1 = target thread (Lua value); upvalue 2 = argc.
				lua_rawgeti(state->L, LUA_REGISTRYINDEX, var->ref);
				lua_xmove(state->L, T, 1);
				lua_pushinteger(T, (lua_Integer)argc);
				lua_pushcclosure(T, ThreadStepNative, 2);
				loadOk = true;
			}
		}
		else {
			SetSlotError(slot, "variable is not executable");
		}

		if (!loadOk)
			FailAsyncSlot(state, slot, nullptr);  // error already set above (or none for dead thread)
		else
			++state->runningCount;

		return CommitAndLaunchAsync(state, slot, id, isNewSlot);
	}

	KITSUNE_API int KitsuneExecuteVariableAsync(const KitsuneVariable* var, int argc, const KitsuneVariable* argv, bool fireAndForget) {
		if (g_isSchedulerThread || g_inlineExecution) return -1;
		return StartCoroutineVariable(g_state, var, argc, argv, fireAndForget);
	}

	// Extracts the coroutine result after lua_resume completes.
	// Returns a heap-allocated KitsuneVariable*; caller must KitsuneVariableFree it.
	static KitsuneVariable* ExtractCoroutineResult(lua_State* T, int rc, int nresults) {
		if (rc == LUA_OK) {
			if (nresults == 0)
				return MakeNoneVariable();
			KitsuneVariable* out = (KitsuneVariable*)kitsune_malloc(sizeof(KitsuneVariable));
			if (!out)
				return MakeErrorVariable("out of memory");
			memset(out, 0, sizeof(KitsuneVariable));
			KitsuneCoroutine tmp{};
			SetSlotResult(&tmp, T, 1);
			if (tmp.error) {
				// SetSlotResult encountered an internal error (e.g. string OOM, stream snapshot failure).
				// Surface it as KITSUNE_TERROR rather than silently returning TNONE.
				kitsune_free(out);
				out = MakeErrorVariable(tmp.error);
				kitsune_free(tmp.error);
				return out;
			}
			*out = tmp.result;
			memset(&tmp.result, 0, sizeof(KitsuneVariable));
			return out;
		}
		const char* err = lua_tolstring(T, -1, NULL);
		return MakeErrorVariable(err ? err : "unknown error");
	}

	// Tears down an inline slot after its coroutine finishes: decrements runningCount,
	// releases registry refs, zeroes the slot for reuse, and notifies waiters.
	static void CleanupInlineSlot(KitsuneState* state, KitsuneCoroutine* slot) {
		--state->runningCount;
		if (slot->threadRef != LUA_NOREF) {
			luaL_unref(state->L, LUA_REGISTRYINDEX, slot->threadRef);
			slot->threadRef = LUA_NOREF;
			slot->thread = NULL;
		}
		if (slot->argsRef != LUA_NOREF) {
			luaL_unref(state->L, LUA_REGISTRYINDEX, slot->argsRef);
			slot->argsRef = LUA_NOREF;
		}
		state->slotsLock.lock();
		kitsune_free(slot->error);
		memset(slot, 0, sizeof(KitsuneCoroutine));
		state->slotsLock.unlock();
		state->doneCV.notify_all();
	}

	// -- RunInline: runs a pre-configured coroutine T on the calling thread ------
	// Precondition: AcquireLuaAccess is held; slot is in slots[] with isInline=1,
	// id assigned, runningCount incremented, T set up for the first lua_resume.
	// On each LUA_YIELD: reads and zeros slot->sleepUntil, releases access briefly
	// (so the scheduler and variable bridge can run), then re-acquires and resumes.
	// Refreshes ARGS and ID globals after each re-acquire in case async coroutines
	// overwrote them during the yield window.
	// After completion: zeroes the slot for reuse, calls doneCV.notify_all.
	// Returns a heap-allocated KitsuneVariable*; caller must call ReleaseLuaAccess
	// and then KitsuneVariableFree the result when done.
	static KitsuneVariable* RunInline(KitsuneState* state, KitsuneCoroutine* slot,
		lua_State* T, int initialNArgs) {
		int id = slot->id;

		SetCoroutineGlobals(T, id, slot->argsRef);

		state->currentCoroutineId.store((long)id);
		g_inlineExecution = true;

		int nresults = 0;
		int rc = lua_resume(T, state->L, initialNArgs, &nresults);

		g_inlineExecution = false;

		bool cancelledInYield = false;
		while (rc == LUA_YIELD) {
			lua_pop(T, nresults);
			nresults = 0;

			double sleepMs = 0.0;
			if (slot->sleepUntil > 0.0) {
				sleepMs = slot->sleepUntil - GetCounter(state);
				slot->sleepUntil = 0.0;
			}

			state->currentCoroutineId.store(0);

			ReleaseLuaAccess(state);

			unsigned long sleepDur = (sleepMs > 1.0)
				? (unsigned long)(sleepMs > (double)MAXDWORD ? MAXDWORD : sleepMs)
				: 1;
			Sleep(sleepDur);

			AcquireLuaAccess(state);

			// Check for a per-coroutine cancel that arrived during the yield/sleep window.
			// KitsuneCancel sets interrupted without holding accessLock, so it may fire while the
			// inline call is OS-sleeping.  A short script (e.g. bare Sleep()) might finish in
			// fewer than 1000 instructions after the resume, so the Ticker never fires; we must
			// also check here to guarantee cancellation is always honoured promptly.
			if (slot->interrupted.load()) {
				lua_settop(T, 0);
				cancelledInYield = true;
				break;
			}

			SetCoroutineGlobals(T, id, slot->argsRef);

			state->currentCoroutineId.store((long)id);
			g_inlineExecution = true;

			rc = lua_resume(T, state->L, 0, &nresults);

			g_inlineExecution = false;
		}

		KitsuneVariable* out = cancelledInYield
			? MakeErrorVariable("cancelled")
			: ExtractCoroutineResult(T, rc, nresults);

		lua_settop(T, 0);
		DrainPendingVariableChain(state->L);
		state->currentCoroutineId.store(0);
		g_inlineExecution = false;
		CleanupInlineSlot(state, slot);
		return out;
	}

	// -- RunInlineTight: re-entrant variant for nested KitsuneExecute* calls ------------------
	// Used from three contexts:
	//   (a) scheduler thread inside LuaCFunctionWrapper (DelegateState set)
	//   (b) scheduler thread inside any raw Lua C callback (g_isSchedulerThread && currentCoroutineId != 0)
	//   (c) inline calling thread inside RunInline's lua_resume (g_inlineExecution && currentCoroutineId != 0)
	// In all cases Lua access is already owned; Sleep/Yield in the called function are no-ops
	// (the coroutine is immediately re-resumed on each LUA_YIELD without releasing Lua access).
	// Saves and restores currentCoroutineId and g_inlineExecution so the outer context is undisturbed.
	// Does NOT call ReleaseLuaAccess — the caller never acquired it.
	static KitsuneVariable* RunInlineTight(KitsuneState* state, KitsuneCoroutine* slot,
		lua_State* T, int initialNArgs) {
		int id = slot->id;

		SetCoroutineGlobals(T, id, slot->argsRef);

		long prevCoroutineId = state->currentCoroutineId.load();
		bool prevInlineExecution = g_inlineExecution;
		state->currentCoroutineId.store((long)id);
		g_inlineExecution = true;

		int nresults = 0;
		int rc = lua_resume(T, state->L, initialNArgs, &nresults);

		while (rc == LUA_YIELD) {
			lua_pop(T, nresults);
			nresults = 0;
			slot->sleepUntil = 0.0;
			rc = lua_resume(T, state->L, 0, &nresults);
		}

		g_inlineExecution = prevInlineExecution;
		state->currentCoroutineId.store(prevCoroutineId);

		KitsuneVariable* out = ExtractCoroutineResult(T, rc, nresults);

		lua_settop(T, 0);
		DrainPendingVariableChain(state->L);
		CleanupInlineSlot(state, slot);
		return out;
	}

	// -- Shared slot-acquisition helper for inline sync execute functions --------
	// Finds a zeroed (reusable) slot or allocates a new one, marks it isInline=1,
	// and adds it to slots[] if newly allocated.
	// Caller must hold AcquireLuaAccess. Returns NULL if at capacity or OOM.
	static KitsuneCoroutine* AcquireInlineSlot(KitsuneState* state, bool& isNewSlot) {
		isNewSlot = false;
		for (int i = 0; i < state->slotCount; i++) {
			if (state->slots[i]->id == 0) {
				state->slots[i]->isInline.store(1);
				return state->slots[i];
			}
		}
		if (state->slotCount >= KITSUNE_MAX_COROUTINES)
			return NULL;

		KitsuneCoroutine* slot = new (std::nothrow) KitsuneCoroutine{};

		if (!slot)
			return NULL;

		slot->isInline.store(1);
		isNewSlot = true;
		return slot;
	}

	// Commits a slot into slots[] with the given id.
	static void CommitInlineSlot(KitsuneState* state, KitsuneCoroutine* slot,
		int id, bool isNewSlot) {
		state->slotsLock.lock();
		slot->id = id;
		if (isNewSlot)
			state->slots[state->slotCount++] = slot;
		state->slotsLock.unlock();
	}

	// Releases a slot that failed before RunInline was reached (load error, bad args, etc.).
	// Clears isInline, adds to slots[] as zeroed/reusable if newly allocated.
	static void ReleaseFailedInlineSlot(KitsuneState* state, KitsuneCoroutine* slot,
		bool isNewSlot) {
		slot->isInline.store(0);
		if (slot->threadRef != LUA_NOREF) {
			luaL_unref(state->L, LUA_REGISTRYINDEX, slot->threadRef);
			slot->threadRef = LUA_NOREF;
			slot->thread = NULL;
		}
		if (slot->argsRef != LUA_NOREF) {
			luaL_unref(state->L, LUA_REGISTRYINDEX, slot->argsRef);
			slot->argsRef = LUA_NOREF;
		}
		kitsune_free(slot->error);
		slot->error = NULL;
		if (isNewSlot) {
			state->slotsLock.lock();
			state->slots[state->slotCount++] = slot;
			state->slotsLock.unlock();
		}
	}

	KITSUNE_API KitsuneVariable* KitsuneExecuteFile(const char* path, int argc, const KitsuneVariable* argv) {
		KitsuneState* state = g_state;
		if (!state) return NULL;

		// Re-entrant path: DelegateState is set (LuaCFunctionWrapper), or we are on the
		// scheduler thread inside lua_resume (g_isSchedulerThread, e.g. async coroutine),
		// or on the inline calling thread inside lua_resume (g_inlineExecution, e.g. RunString
		// calling SQLite which calls LuaString). Lua access is already owned. Run in a tight
		// loop; Sleep/Yield are no-ops.
		if (state->DelegateState || ((g_isSchedulerThread || g_inlineExecution) && state->currentCoroutineId.load() != 0)) {
			if (!path) return NULL;
			bool isNewSlot = false;
			KitsuneCoroutine* slot = AcquireInlineSlot(state, isNewSlot);
			if (!slot)
				return NULL;
			lua_State* T = PrepareSlotThread(state, slot);
			BuildArgsRef(state, slot, true, path, argc, argv);
			int loadrc = luaL_loadfile(T, path);
			int id = (int)(++state->nextId);
			if (loadrc != 0) {
				const char* err = lua_tolstring(T, -1, NULL);
				KitsuneVariable* out = MakeErrorVariable(err ? err : "load error");
				lua_settop(T, 0);
				ReleaseFailedInlineSlot(state, slot, isNewSlot);
				return out;
			}
			++state->runningCount;
			slot->startTime = GetCounter(state);
			CommitInlineSlot(state, slot, id, isNewSlot);
			return RunInlineTight(state, slot, T, 0);
		}

		if (g_isSchedulerThread || state->DelegateState || g_inlineExecution)
			return MakeErrorVariable("cannot call Execute from this context");
		if (!path) return NULL;

		AcquireLuaAccess(state);

		bool isNewSlot = false;
		KitsuneCoroutine* slot = AcquireInlineSlot(state, isNewSlot);
		if (!slot) {
			ReleaseLuaAccess(state);
			return NULL;
		}

		lua_State* T = PrepareSlotThread(state, slot);

		BuildArgsRef(state, slot, true, path, argc, argv);

		int loadrc = luaL_loadfile(T, path);
		int id = (int)(++state->nextId);

		if (loadrc != 0) {
			const char* err = lua_tolstring(T, -1, NULL);
			KitsuneVariable* out = MakeErrorVariable(err ? err : "load error");
			lua_settop(T, 0);
			ReleaseFailedInlineSlot(state, slot, isNewSlot);
			ReleaseLuaAccess(state);
			return out;
		}

		++state->runningCount;
		slot->startTime = GetCounter(state);
		CommitInlineSlot(state, slot, id, isNewSlot);

		KitsuneVariable* out = RunInline(state, slot, T, 0);
		ReleaseLuaAccess(state);
		return out;
	}

	KITSUNE_API KitsuneVariable* KitsuneExecuteString(const char* script, int argc, const KitsuneVariable* argv) {
		KitsuneState* state = g_state;
		if (!state) return NULL;

		// Re-entrant path: DelegateState is set (LuaCFunctionWrapper), or we are on the
		// scheduler thread inside lua_resume (g_isSchedulerThread, e.g. async coroutine),
		// or on the inline calling thread inside lua_resume (g_inlineExecution, e.g. RunString
		// calling SQLite which calls LuaString). Lua access is already owned. Run in a tight
		// loop; Sleep/Yield are no-ops.
		if (state->DelegateState || ((g_isSchedulerThread || g_inlineExecution) && state->currentCoroutineId.load() != 0)) {
			if (!script) return NULL;
			bool isNewSlot = false;
			KitsuneCoroutine* slot = AcquireInlineSlot(state, isNewSlot);
			if (!slot)
				return NULL;
			lua_State* T = PrepareSlotThread(state, slot);
			BuildArgsRef(state, slot, false, nullptr, argc, argv);
			int loadrc = luaL_loadbuffer(T, script, strlen(script), "string");
			int id = (int)(++state->nextId);
			if (loadrc != 0) {
				const char* err = lua_tolstring(T, -1, NULL);
				KitsuneVariable* out = MakeErrorVariable(err ? err : "load error");
				lua_settop(T, 0);
				ReleaseFailedInlineSlot(state, slot, isNewSlot);
				return out;
			}
			++state->runningCount;
			slot->startTime = GetCounter(state);
			CommitInlineSlot(state, slot, id, isNewSlot);
			return RunInlineTight(state, slot, T, 0);
		}

		if (g_isSchedulerThread || state->DelegateState || g_inlineExecution)
			return MakeErrorVariable("cannot call Execute from this context");
		if (!script) return NULL;

		AcquireLuaAccess(state);

		bool isNewSlot = false;
		KitsuneCoroutine* slot = AcquireInlineSlot(state, isNewSlot);
		if (!slot) {
			ReleaseLuaAccess(state);
			return NULL;
		}

		lua_State* T = PrepareSlotThread(state, slot);

		BuildArgsRef(state, slot, false, nullptr, argc, argv);

		int loadrc = luaL_loadbuffer(T, script, strlen(script), "string");
		int id = (int)(++state->nextId);

		if (loadrc != 0) {
			const char* err = lua_tolstring(T, -1, NULL);
			KitsuneVariable* out = MakeErrorVariable(err ? err : "load error");
			lua_settop(T, 0);
			ReleaseFailedInlineSlot(state, slot, isNewSlot);
			ReleaseLuaAccess(state);
			return out;
		}

		++state->runningCount;
		slot->startTime = GetCounter(state);
		CommitInlineSlot(state, slot, id, isNewSlot);

		KitsuneVariable* out = RunInline(state, slot, T, 0);
		ReleaseLuaAccess(state);
		return out;
	}

	KITSUNE_API KitsuneVariable* KitsuneExecuteFunction(const char* functionName, int argc, const KitsuneVariable* argv) {
		KitsuneState* state = g_state;
		if (!state) return NULL;

		// Re-entrant path: DelegateState is set (LuaCFunctionWrapper), or we are on the
		// scheduler thread inside lua_resume (g_isSchedulerThread, e.g. async coroutine),
		// or on the inline calling thread inside lua_resume (g_inlineExecution, e.g. RunString
		// calling SQLite which calls LuaString). Lua access is already owned. Run in a tight
		// loop; Sleep/Yield are no-ops.
		if (state->DelegateState || ((g_isSchedulerThread || g_inlineExecution) && state->currentCoroutineId.load() != 0)) {
			if (!functionName) return NULL;
			bool isNewSlot = false;
			KitsuneCoroutine* slot = AcquireInlineSlot(state, isNewSlot);
			if (!slot)
				return NULL;
			lua_State* T = PrepareSlotThread(state, slot);
			if (PushGlobalAtPath(state->L, functionName))
				lua_xmove(state->L, T, 1);
			else
				lua_pushnil(T);
			int id = (int)(++state->nextId);
			if (!lua_isfunction(T, -1)) {
				lua_pop(T, 1);
				KitsuneVariable* out = MakeErrorVariable("function not found");
				ReleaseFailedInlineSlot(state, slot, isNewSlot);
				return out;
			}
			for (int n = 0; n < argc; n++)
				PushKitsuneVariable(T, argv ? &argv[n] : nullptr);
			slot->initialNArgs = argc;
			++state->runningCount;
			slot->startTime = GetCounter(state);
			CommitInlineSlot(state, slot, id, isNewSlot);
			return RunInlineTight(state, slot, T, argc);
		}

		if (g_isSchedulerThread || state->DelegateState || g_inlineExecution)
			return MakeErrorVariable("cannot call Execute from this context");
		if (!functionName) return NULL;

		AcquireLuaAccess(state);

		bool isNewSlot = false;
		KitsuneCoroutine* slot = AcquireInlineSlot(state, isNewSlot);
		if (!slot) {
			ReleaseLuaAccess(state);
			return NULL;
		}

		lua_State* T = PrepareSlotThread(state, slot);

		if (PushGlobalAtPath(state->L, functionName))
			lua_xmove(state->L, T, 1);
		else
			lua_pushnil(T);

		int id = (int)(++state->nextId);

		if (!lua_isfunction(T, -1)) {
			lua_pop(T, 1);
			KitsuneVariable* out = MakeErrorVariable("function not found");
			ReleaseFailedInlineSlot(state, slot, isNewSlot);
			ReleaseLuaAccess(state);
			return out;
		}

		for (int n = 0; n < argc; n++)
			PushKitsuneVariable(T, argv ? &argv[n] : nullptr);
		slot->initialNArgs = argc;

		++state->runningCount;
		slot->startTime = GetCounter(state);
		CommitInlineSlot(state, slot, id, isNewSlot);

		KitsuneVariable* out = RunInline(state, slot, T, argc);
		ReleaseLuaAccess(state);
		return out;
	}

	KITSUNE_API KitsuneVariable* KitsuneExecuteVariable(const KitsuneVariable* var, int argc, const KitsuneVariable* argv) {
		KitsuneState* state = g_state;
		if (!state) return NULL;

		// Re-entrant path: DelegateState is set (LuaCFunctionWrapper), or we are on the
		// scheduler thread inside lua_resume (g_isSchedulerThread, e.g. async coroutine),
		// or on the inline calling thread inside lua_resume (g_inlineExecution, e.g. RunString
		// calling SQLite which calls LuaString). Lua access is already owned. Run in a tight
		// loop; Sleep/Yield are no-ops.
		if (state->DelegateState || ((g_isSchedulerThread || g_inlineExecution) && state->currentCoroutineId.load() != 0)) {
			if (!var) return NULL;
			bool isNewSlot = false;
			KitsuneCoroutine* slot = AcquireInlineSlot(state, isNewSlot);
			if (!slot)
				return NULL;
			lua_State* T = PrepareSlotThread(state, slot);
			int id = (int)(++state->nextId);
			bool loadOk = false;
			int initialNArgs = 0;
			if (var->type == LUA_TFUNCTION) {
				if (var->ref > 0)
					lua_rawgeti(state->L, LUA_REGISTRYINDEX, var->ref);
				else
					lua_pushnil(state->L);
				lua_xmove(state->L, T, 1);
				if (lua_isfunction(T, -1)) {
					for (int n = 0; n < argc; n++)
						PushKitsuneVariable(T, argv ? &argv[n] : nullptr);
					slot->initialNArgs = argc;
					initialNArgs = argc;
					loadOk = true;
				}
				else {
					lua_pop(T, 1);
				}
			}
			else if (var->type == LUA_TSTRING && var->data && var->length > 0) {
				BuildArgsRef(state, slot, false, nullptr, argc, argv);
				int rc = luaL_loadbuffer(T, (const char*)var->data, var->length, "variable");
				if (rc != 0) {
					const char* err = lua_tolstring(T, -1, NULL);
					KitsuneVariable* out = MakeErrorVariable(err ? err : "load error");
					lua_settop(T, 0);
					ReleaseFailedInlineSlot(state, slot, isNewSlot);
					return out;
				}
				loadOk = true;
			}
			else if (var->type == LUA_TTHREAD && var->ref > 0) {
				lua_rawgeti(state->L, LUA_REGISTRYINDEX, var->ref);
				lua_State* targetT = lua_tothread(state->L, -1);
				lua_pop(state->L, 1);
				if (!targetT) {
					KitsuneVariable* out = MakeErrorVariable("invalid thread");
					ReleaseFailedInlineSlot(state, slot, isNewSlot);
					return out;
				}
				if (lua_status(targetT) == LUA_OK && lua_gettop(targetT) == 0) {
					ReleaseFailedInlineSlot(state, slot, isNewSlot);
					return MakeNoneVariable();
				}
				lua_sethook(targetT, Ticker, LUA_MASKCOUNT, 1000);
				for (int n = 0; n < argc; n++)
					PushKitsuneVariable(targetT, argv ? &argv[n] : nullptr);
				lua_rawgeti(state->L, LUA_REGISTRYINDEX, var->ref);
				lua_xmove(state->L, T, 1);
				lua_pushinteger(T, (lua_Integer)argc);
				lua_pushcclosure(T, ThreadStepNative, 2);
				loadOk = true;
			}
			if (!loadOk) {
				KitsuneVariable* out = MakeErrorVariable("variable is not executable");
				ReleaseFailedInlineSlot(state, slot, isNewSlot);
				return out;
			}
			++state->runningCount;
			slot->startTime = GetCounter(state);
			CommitInlineSlot(state, slot, id, isNewSlot);
			return RunInlineTight(state, slot, T, initialNArgs);
		}

		if (g_isSchedulerThread || state->DelegateState || g_inlineExecution)
			return MakeErrorVariable("cannot call Execute from this context");
		if (!var) return NULL;

		AcquireLuaAccess(state);

		bool isNewSlot = false;
		KitsuneCoroutine* slot = AcquireInlineSlot(state, isNewSlot);
		if (!slot) {
			ReleaseLuaAccess(state);
			return NULL;
		}

		lua_State* T = PrepareSlotThread(state, slot);

		int id = (int)(++state->nextId);
		bool loadOk = false;
		int initialNArgs = 0;

		if (var->type == LUA_TFUNCTION) {
			if (var->ref > 0)
				lua_rawgeti(state->L, LUA_REGISTRYINDEX, var->ref);
			else
				lua_pushnil(state->L);
			lua_xmove(state->L, T, 1);

			if (!lua_isfunction(T, -1)) {
				lua_pop(T, 1);
			}
			else {
				for (int n = 0; n < argc; n++)
					PushKitsuneVariable(T, argv ? &argv[n] : nullptr);
				slot->initialNArgs = argc;
				initialNArgs = argc;
				loadOk = true;
			}
		}
		else if (var->type == LUA_TSTRING && var->data && var->length > 0) {
			BuildArgsRef(state, slot, false, nullptr, argc, argv);

			int rc = luaL_loadbuffer(T, (const char*)var->data, var->length, "variable");
			if (rc != 0) {
				const char* err = lua_tolstring(T, -1, NULL);
				KitsuneVariable* out = MakeErrorVariable(err ? err : "load error");
				lua_settop(T, 0);
				ReleaseFailedInlineSlot(state, slot, isNewSlot);
				ReleaseLuaAccess(state);
				return out;
			}
			loadOk = true;
		}
		else if (var->type == LUA_TTHREAD && var->ref > 0) {
			// Native single-step: resume the target thread exactly once with argc/argv as args.
			lua_rawgeti(state->L, LUA_REGISTRYINDEX, var->ref);
			lua_State* targetT = lua_tothread(state->L, -1);
			lua_pop(state->L, 1);

			if (!targetT) {
				KitsuneVariable* out = MakeErrorVariable("invalid thread");
				ReleaseFailedInlineSlot(state, slot, isNewSlot);
				ReleaseLuaAccess(state);
				return out;
			}

			if (lua_status(targetT) == LUA_OK && lua_gettop(targetT) == 0) {
				// Thread is dead: return TNONE.
				ReleaseFailedInlineSlot(state, slot, isNewSlot);
				ReleaseLuaAccess(state);
				return MakeNoneVariable();
			}

			lua_sethook(targetT, Ticker, LUA_MASKCOUNT, 1000);
			// Pre-push argv onto targetT — these become resume args for this step.
			for (int n = 0; n < argc; n++)
				PushKitsuneVariable(targetT, argv ? &argv[n] : nullptr);
			// Push ThreadStepNative closure onto wrapper T.
			// Upvalue 1 = target thread (Lua value); upvalue 2 = argc.
			lua_rawgeti(state->L, LUA_REGISTRYINDEX, var->ref);
			lua_xmove(state->L, T, 1);
			lua_pushinteger(T, (lua_Integer)argc);
			lua_pushcclosure(T, ThreadStepNative, 2);
			initialNArgs = 0;
			loadOk = true;
		}

		if (!loadOk) {
			KitsuneVariable* out = MakeErrorVariable("variable is not executable");
			ReleaseFailedInlineSlot(state, slot, isNewSlot);
			ReleaseLuaAccess(state);
			return out;
		}

		++state->runningCount;
		slot->startTime = GetCounter(state);
		CommitInlineSlot(state, slot, id, isNewSlot);

		KitsuneVariable* out = RunInline(state, slot, T, initialNArgs);
		ReleaseLuaAccess(state);
		return out;
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

		KitsuneVariable* out = (KitsuneVariable*)kitsune_malloc(sizeof(KitsuneVariable));
		if (!out) {
			slot->released.store(1);
			state->slotsLock.unlock();
			return NULL;
		}
		memset(out, 0, sizeof(KitsuneVariable));

		// Transfer owned data (string or table linked list) atomically to prevent double-free.
		if ((slot->result.type == LUA_TSTRING || slot->result.type == KITSUNE_TCHAR16 || slot->result.type == LUA_TUSERDATA) && slot->result.data) {
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
		else if (slot->result.type == LUA_TFUNCTION || slot->result.type == LUA_TTHREAD || slot->result.type == LUA_TTABLE) {
			// Transfer the registry ref; zero the slot field so no stale ref remains.
			out->type = slot->result.type;
			out->ref = slot->result.ref;
			slot->result.ref = LUA_NOREF;
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
				status = slot->isInline.load() ? KITSUNE_STATUS_INLINE : KITSUNE_STATUS_IDLE;
			}
		}
		state->slotsLock.unlock();
		return status;
	}

	KITSUNE_API bool KitsuneIsRunning() {
		KitsuneState* state = g_state;
		if (!state) return false;
		state->slotsLock.lock();
		bool any = false;
		for (int i = 0; i < state->slotCount; i++) {
			if (state->slots[i]->id != 0 && !state->slots[i]->released.load()) {
				any = true;
				break;
			}
		}
		state->slotsLock.unlock();
		return any;
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
		std::unique_lock<std::mutex> lk(state->doneMtx);
		state->doneCV.wait(lk, [state] {
			if (state->schedulerStop.load()) return true;
			std::lock_guard<std::mutex> slk(state->slotsLock);
			for (int i = 0; i < state->slotCount; i++) {
				if (state->slots[i]->id != 0 && !state->slots[i]->released.load())
					return false;
			}
			return true;
			});
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
		// TSTREAM: null the pointer before FreeVariableData so the accessor-dispose path is not
		// triggered — that path is only correct for unconsumed slots, not host-owned blocks.
		if (var->type == KITSUNE_TSTREAM)
			var->stream = NULL;
		// TFUNCTION, TTHREAD, and TTABLE (with nodes): need the Lua state to luaL_unref registry
		// entries.  On the scheduler thread Lua access is already owned so call directly.
		// On any other thread, enqueue the variable for the scheduler to drain — this avoids
		// blocking the caller while a coroutine is running (same pattern as stream sweep).
		if (var->type == LUA_TFUNCTION || var->type == LUA_TTHREAD
			|| (var->type == LUA_TTABLE && var->ref > 0)
			|| (var->type == KITSUNE_TTABLECONTENTS && var->table)
			|| (var->type == LUA_TUSERDATA && var->userdata && var->userdata->ref > 0)) {
			KitsuneState* state = g_state;
			if (state && state->L) {
				if (g_isSchedulerThread || g_inlineExecution) {
					// Scheduler thread or inline calling thread: Lua access already owned.
					FreeVariableData(var, state->L);
				}
				else {
					// Non-blocking path: push onto the deferred queue.
					KitsuneVariableChain* node = (KitsuneVariableChain*)kitsune_malloc(sizeof(KitsuneVariableChain));
					if (node) {
						node->variable = var;
						KitsuneVariableChain* head = g_pendingVariableChainHead.load(std::memory_order_relaxed);
						do {
							node->next = head;
						} while (!g_pendingVariableChainHead.compare_exchange_weak(
							head, node, std::memory_order_release, std::memory_order_relaxed));
						return;  // scheduler owns var now; do not kitsune_free here
					}
					// OOM fallback: block until we can call luaL_unref directly.
					AcquireLuaAccess(state);
					FreeVariableData(var, state->L);
					ReleaseLuaAccess(state);
				}
			}
			else {
				FreeVariableData(var, nullptr);
			}
		}
		else {
			FreeVariableData(var, nullptr);
		}
		kitsune_free(var);
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

	// RAII guard that acquires the Lua access lock when the calling thread does not already
	// own it (i.e. is not the scheduler or an inline-execution thread), and releases it in
	// the destructor.  All exit paths — including early returns — release automatically,
	// eliminating the manual `bool hasAccess` pattern in every variable-bridge API function.
	struct LuaAccessGuard {
		KitsuneState* const state;
		const bool owned;
		explicit LuaAccessGuard(KitsuneState* s)
			: state(s), owned(!(g_isSchedulerThread || g_inlineExecution)) {
			if (owned) AcquireLuaAccess(state);
		}
		~LuaAccessGuard() {
			if (owned) ReleaseLuaAccess(state);
		}
		LuaAccessGuard(const LuaAccessGuard&) = delete;
		LuaAccessGuard& operator=(const LuaAccessGuard&) = delete;
	};

	KITSUNE_API bool KitsuneSetVariable(const char* path, const KitsuneVariable* var) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !path || !*path) return false;
		if (var && var->type == KITSUNE_TSTREAM) {
			if (!var->stream || !(var->stream->flags & KITSUNE_SHARED_MEMORY_FLAG_KITSUNE_OWNED))
				return false;  // stream block was not created by KitsuneCreateMemoryBlock
		}
		LuaAccessGuard lock(state);
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
		return ok;
	}

	KITSUNE_API KitsuneVariable* KitsuneGetVariable(const char* path) {
		KitsuneState* state = g_state;
		if (!state || !state->L) return NULL;
		LuaAccessGuard lock(state);
		KitsuneVariable* out = NULL;
		if (!path || !*path) {
			lua_rawgeti(state->L, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
			out = (KitsuneVariable*)kitsune_malloc(sizeof(KitsuneVariable));
			if (out)
				FillKitsuneVariableFromStack(state->L, -1, out);
			lua_pop(state->L, 1);
			return out;
		}
		const char* finalKey = NavigateGlobalParent(state->L, path, false);
		if (finalKey) {
			if (lua_istable(state->L, -1)) {
				lua_getfield(state->L, -1, finalKey);
				int t = lua_type(state->L, -1);
				if (t != LUA_TNIL && t != LUA_TNONE) {
					out = (KitsuneVariable*)kitsune_malloc(sizeof(KitsuneVariable));
					if (out) {
						FillKitsuneVariableFromStack(state->L, -1, out);
						if (out->type == LUA_TNONE) {
							// String allocation failed; return NULL rather than a misleading LUA_TNIL.
							kitsune_free(out);
							out = NULL;
						}
					}
				}
				lua_pop(state->L, 1);  // pop value
			}
			lua_pop(state->L, 1);  // pop parent table
		}
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
				FreeVariableData(&k, L);
				FreeVariableData(&v, L);
				luaL_error(L, "out of memory");
			}
			ctx->callback(&k, &v, ctx->userdata);
			FreeVariableData(&k, L);
			FreeVariableData(&v, L);
			lua_pop(L, 1);  // pop value, keep key for next lua_next
		}
		return 0;
	}

	KITSUNE_API void KitsuneGetAll(const char* path, kitsune_KeyValuePairCallback callback, void* userdata) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !callback) return;
		LuaAccessGuard lock(state);

		if (!PushGlobalAtPath(state->L, path))
			return;

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
	}

	static int LuaResultSetter(const KitsuneVariable* result) {
		KitsuneState* state = g_state;
		lua_State* L = state->DelegateState;
		if (!L || !result) return 0;

		if (result->type == KITSUNE_TERROR) {
			// Defer the raise until LuaCFunctionWrapper has freed its args array.
			if (state->lastCallError) { kitsune_free(state->lastCallError); state->lastCallError = nullptr; }
			const char* msg = (result->data) ? (const char*)result->data : "error";
			size_t len = (result->data && result->length > 0) ? result->length : strlen(msg);
			state->lastCallError = (char*)kitsune_malloc(len + 1);
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

		// Set DelegateState before filling args: if a LUA_TUSERDATA argument's __tostring
		// metamethod calls a guarded Kitsune API function, the guard returns early instead
		// of deadlocking. Save/restore handles re-entrant registered-function calls correctly.
		int argc = lua_gettop(L);
		lua_State* prevDelegateState = state->DelegateState;
		state->DelegateState = L;
		KitsuneVariable* args = AllocAndFillArgs(L, argc);
		if (argc > 0 && !args) {
			state->DelegateState = prevDelegateState;
			lua_pushstring(L, "out of memory");
			lua_error(L);
			return 0;  // unreachable
		}

		int rc = func(argc, args, LuaResultSetter, userdata);
		state->DelegateState = prevDelegateState;  // restore; handles nesting correctly

		// Free args before any potential lua_error so we never leak them on the error path.
		FreeCallbackArgs(L, args, argc);

		// Raise a deferred error that was stored by LuaResultSetter for KITSUNE_TERROR.
		if (state->lastCallError) {
			lua_pushstring(L, state->lastCallError);
			kitsune_free(state->lastCallError);
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

	// Native single-step implementation for LUA_TTHREAD execution.
	// Upvalue 1: target thread (Lua thread value). Upvalue 2: argc (integer).
	// The caller pre-pushes argc args onto the target thread before starting the wrapper.
	// Returns: first yielded/returned value; nil (KITSUNE_TNIL) if alive with no value;
	// nothing (KITSUNE_TNONE) if dead with no return; raises a Lua error on thread error.
	static int ThreadStepNative(lua_State* L) {
		lua_State* targetT = lua_tothread(L, lua_upvalueindex(1));
		int argc = (int)lua_tointeger(L, lua_upvalueindex(2));

		if (!targetT) {
			luaL_error(L, "invalid thread");
			return 0;
		}

		// Safety: if dead (status LUA_OK and empty stack), return nothing (TNONE).
		if (lua_status(targetT) == LUA_OK && lua_gettop(targetT) == 0) {
			return 0;
		}

		int nresults = 0;
		int rc = lua_resume(targetT, L, argc, &nresults);

		if (rc == LUA_YIELD) {
			if (nresults > 0) {
				// Yielded values are at the top of targetT's stack; locals may be below.
				// Copy the first yielded value to this wrapper thread, then discard all yielded values.
				int firstIdx = lua_gettop(targetT) - nresults + 1;
				lua_pushvalue(targetT, firstIdx);
				lua_xmove(targetT, L, 1);
				lua_pop(targetT, nresults);
				return 1;
			}
			lua_pushnil(L); // alive but yielded nothing -> KITSUNE_TNIL
			return 1;
		}

		if (rc == LUA_OK) {
			if (nresults > 0) {
				// After normal return: stack has exactly nresults values at indices 1..n.
				lua_pushvalue(targetT, 1);
				lua_xmove(targetT, L, 1);
				lua_settop(targetT, 0);
				return 1;
			}
			lua_settop(targetT, 0);
			return 0; // dead, returned nothing -> KITSUNE_TNONE
		}

		// Thread error: propagate to the wrapper coroutine.
		const char* err = lua_tolstring(targetT, -1, NULL);
		lua_settop(targetT, 0);
		if (err)
			lua_pushstring(L, err);
		else
			lua_pushliteral(L, "thread error");
		return lua_error(L);
	}

	// Called by Lua GC when the KitsuneIteratorUD upvalue is collected.
	// Sets state=3 before calling finalized so any reentrant call is a no-op.
	// Passes a no-op resultSetter — never nullptr — to avoid a null-pointer crash
	// in LuaFunctionTrampoline when finalized tries to return a value.
	static int KitsuneIteratorUD_gc(lua_State* L) {
		KitsuneIteratorUD* ud = (KitsuneIteratorUD*)lua_touserdata(L, 1);
		if (!ud || ud->state == 3)
			return 0;
		ud->state = 3;
		if (ud->finalized.func) {
			auto noop = [](const KitsuneVariable*) -> int { return 1; };
			ud->finalized.func(0, nullptr, noop, ud->finalized.userdata);
		}
		return 0;
	}

	// Lua closure pushed by PushKitsuneVariable for KITSUNE_TITERATOR values.
	// Upvalue 1 is the KitsuneIteratorUD full userdata.
	// On state==0 calls first; on state==1/2 calls next.
	// Returning KITSUNE_TNONE or rc<=0 signals end-of-iteration (pushes nil) — NOT a Lua error.
	static int KitsuneIteratorWrapper(lua_State* L) {
		KitsuneIteratorUD* ud = (KitsuneIteratorUD*)lua_touserdata(L, lua_upvalueindex(1));
		if (!ud || ud->state == 3) {
			lua_pushnil(L);
			return 1;
		}

		kitsune_CFunctionData* cfd = (ud->state == 0) ? &ud->first : &ud->next;
		if (ud->state == 0)
			ud->state = 1;
		else
			ud->state = 2;

		if (!cfd->func) {
			ud->state = 3;
			lua_pushnil(L);
			return 1;
		}

		KitsuneState* state = g_state;
		int argc = lua_gettop(L);
		lua_State* prevDelegateState = state->DelegateState;
		state->DelegateState = L;
		KitsuneVariable* args = AllocAndFillArgs(L, argc);
		if (argc > 0 && !args) {
			state->DelegateState = prevDelegateState;
			lua_pushstring(L, "out of memory");
			lua_error(L);
			return 0;  // unreachable
		}

		int rc = cfd->func(argc, args, LuaResultSetter, cfd->userdata);
		state->DelegateState = prevDelegateState;

		FreeCallbackArgs(L, args, argc);

		// A deferred TERROR from LuaResultSetter is still raised as a Lua error.
		if (state->lastCallError) {
			ud->state = 3;  // iterator is dead after an error; any further call returns nil
			lua_pushstring(L, state->lastCallError);
			kitsune_free(state->lastCallError);
			state->lastCallError = nullptr;
			lua_error(L);
			return 0;  // unreachable
		}

		if (rc <= 0 || lua_gettop(L) == 0) {
			// End of iteration: push nil to break the for loop; not an error.
			ud->state = 3;
			lua_pushnil(L);
			return 1;
		}

		return lua_gettop(L);
	}

	KITSUNE_API void KitsuneRegisterFunction(const char* name, kitsune_CFunction func, void* userdata) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !name || !*name || !func) return;

		// LuaAccessGuard is a no-op when Lua access is already owned (scheduler/inline thread).
		// This covers registration from any Lua C callback (e.g. SQLite extension init
		// fired by load_extension inside a running coroutine).
		LuaAccessGuard lock(state);

		const char* finalKey = NavigateGlobalParent(state->L, name, true);
		if (finalKey) {
			lua_pushlightuserdata(state->L, (void*)func);
			lua_pushlightuserdata(state->L, userdata);
			lua_pushcclosure(state->L, LuaCFunctionWrapper, 2);
			lua_setfield(state->L, -2, finalKey);  // parent[finalKey] = closure
			lua_pop(state->L, 1);  // pop parent table
		}
	}

	KITSUNE_API bool KitsuneRegisterUserdata(const char* name, const KitsuneUserDataRegistration* registration) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !name || !*name || !registration) return false;
		LuaAccessGuard lock(state);
		return lua_registerkitsuneuserdata(state->L, name, registration, LuaCFunctionWrapper);
	}

	KITSUNE_API int KitsuneRegister(const KitsuneVariable* var) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !var) return LUA_NOREF;
		LuaAccessGuard lock(state);
		PushKitsuneVariable(state->L, var);
		return luaL_ref(state->L, LUA_REGISTRYINDEX);
	}

	KITSUNE_API KitsuneVariable* KitsuneGetByReference(int ref) {
		KitsuneState* state = g_state;
		if (!state || !state->L || ref <= 0) return NULL;
		LuaAccessGuard lock(state);
		KitsuneVariable* out = NULL;
		lua_rawgeti(state->L, LUA_REGISTRYINDEX, ref);
		int t = lua_type(state->L, -1);
		if (t != LUA_TNIL && t != LUA_TNONE) {
			out = (KitsuneVariable*)kitsune_malloc(sizeof(KitsuneVariable));
			if (out) {
				FillKitsuneVariableFromStack(state->L, -1, out);
				if (out->type == LUA_TNONE) {
					kitsune_free(out);
					out = NULL;
				}
			}
		}
		lua_pop(state->L, 1);
		return out;
	}

	KITSUNE_API KitsuneVariable* KitsuneUnregister(int ref) {
		KitsuneState* state = g_state;
		if (!state || !state->L || ref <= 0) return NULL;
		LuaAccessGuard lock(state);
		KitsuneVariable* out = NULL;
		lua_rawgeti(state->L, LUA_REGISTRYINDEX, ref);
		int t = lua_type(state->L, -1);
		if (t != LUA_TNIL && t != LUA_TNONE) {
			out = (KitsuneVariable*)kitsune_malloc(sizeof(KitsuneVariable));
			if (out) {
				FillKitsuneVariableFromStack(state->L, -1, out);
				if (out->type == LUA_TNONE) {
					kitsune_free(out);
					out = NULL;
				}
			}
		}
		lua_pop(state->L, 1);
		luaL_unref(state->L, LUA_REGISTRYINDEX, ref);
		return out;
	}

	KITSUNE_API KitsuneVariable* KitsuneAnchorVariable(const KitsuneVariable* var) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !var) return NULL;
		LuaAccessGuard lock(state);
		PushKitsuneVariable(state->L, var);
		KitsuneVariable* out = (KitsuneVariable*)kitsune_malloc(sizeof(KitsuneVariable));
		if (!out) {
			lua_pop(state->L, 1);
			return NULL;
		}
		FillKitsuneVariableFromStack(state->L, -1, out);
		lua_pop(state->L, 1);
		if (out->type == LUA_TNONE) {
			kitsune_free(out);
			return NULL;
		}
		return out;
	}

	KITSUNE_API KitsuneVariable* KitsuneGetTableContents(const KitsuneVariable* tableVar) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !tableVar || tableVar->type != LUA_TTABLE || tableVar->ref <= 0)
			return NULL;

		LuaAccessGuard lock(state);
		KitsuneVariable* out = (KitsuneVariable*)kitsune_malloc(sizeof(KitsuneVariable));
		if (!out)
			return NULL;
		memset(out, 0, sizeof(KitsuneVariable));
		out->type = KITSUNE_TTABLECONTENTS;

		lua_rawgeti(state->L, LUA_REGISTRYINDEX, tableVar->ref);
		if (lua_istable(state->L, -1))
			out->table = TableToLinkedList(state->L, -1);
		lua_pop(state->L, 1);

		return out;
	}

	KITSUNE_API KitsuneVariable* KitsuneGetTableContentsAsJson(const KitsuneVariable* var) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !var || var->type != LUA_TTABLE || var->ref <= 0)
			return MakeNoneVariable();

		LuaAccessGuard lock(state);
		int stackBefore = lua_gettop(state->L);

		lua_pushcfunction(state->L, lua_json_encode);
		lua_rawgetp(state->L, LUA_REGISTRYINDEX, lua_json_bridge_registry_key());
		lua_rawgeti(state->L, LUA_REGISTRYINDEX, var->ref);

		if (lua_pcall_nohook(state->L, 2, 1, 0) != LUA_OK) {
			const char* err = lua_tolstring(state->L, -1, NULL);
			KitsuneVariable* out = MakeErrorVariable(err ? err : "json encode error");
			lua_settop(state->L, stackBefore);
			return out;
		}

		size_t jsonLen = 0;
		const char* jsonStr = lua_tolstring(state->L, -1, &jsonLen);
		KitsuneVariable* out;
		if (jsonStr && jsonLen > 0) {
			out = MakeStringVariable(KITSUNE_TJSON, jsonStr, jsonLen);
			if (!out)
				out = MakeErrorVariable("out of memory");
		}
		else {
			out = MakeNilVariable();
		}
		lua_settop(state->L, stackBefore);
		return out;
	}

	KITSUNE_API bool KitsuneSetTableContents(const KitsuneVariable* tableVar, const KitsuneVariable* contentsVar) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !tableVar || tableVar->type != LUA_TTABLE || tableVar->ref <= 0)
			return false;
		if (!contentsVar || contentsVar->type != KITSUNE_TTABLECONTENTS)
			return false;

		LuaAccessGuard lock(state);

		lua_rawgeti(state->L, LUA_REGISTRYINDEX, tableVar->ref);
		if (!lua_istable(state->L, -1)) {
			lua_pop(state->L, 1);
			return false;
		}

		int tableIdx = lua_absindex(state->L, -1);

		// Pass 1: collect all existing keys into a temporary table so we can nil them
		// without modifying the table during lua_next iteration (undefined behaviour).
		lua_newtable(state->L);
		int tempIdx = lua_absindex(state->L, -1);
		int keyCount = 1;
		lua_pushnil(state->L);
		while (lua_next(state->L, tableIdx)) {
			lua_pop(state->L, 1);              // pop value
			lua_pushvalue(state->L, -1);       // dup key
			lua_rawseti(state->L, tempIdx, keyCount++);
		}

		// Pass 2: nil each collected key (safe: not iterating the original table now).
		for (int i = 1; i < keyCount; i++) {
			lua_rawgeti(state->L, tempIdx, i);
			lua_pushnil(state->L);
			lua_rawset(state->L, tableIdx);
		}
		lua_pop(state->L, 1);  // pop temp table

		// Pass 3: populate from the snapshot.
		const KeyValuePairKitsuneVariableNode* node = contentsVar->table;
		while (node) {
			PushKitsuneVariable(state->L, &node->key);
			PushKitsuneVariable(state->L, &node->value);
			lua_rawset(state->L, tableIdx);
			node = node->next;
		}

		lua_pop(state->L, 1);  // pop the table
		return true;
	}

	// Protected body for KitsuneGetIndex. Stack on entry: [obj, key].
	static int DoGetIndex(lua_State* L) {
		lua_gettable(L, 1);  // obj[key] via __index; pops key, pushes result
		return 1;
	}

	KITSUNE_API KitsuneVariable* KitsuneGetIndex(const KitsuneVariable* obj, const KitsuneVariable* key) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !obj || !key) return NULL;

		bool validObj = (obj->type == LUA_TTABLE && obj->ref > 0)
			|| (obj->type == LUA_TUSERDATA && obj->userdata);
		if (!validObj) return NULL;

		LuaAccessGuard lock(state);
		int stackBefore = lua_gettop(state->L);

		lua_pushcfunction(state->L, DoGetIndex);
		PushKitsuneVariable(state->L, obj);
		PushKitsuneVariable(state->L, key);

		if (lua_pcall_nohook(state->L, 2, LUA_MULTRET, 0) != LUA_OK) {
			const char* err = lua_tolstring(state->L, -1, NULL);
			KitsuneVariable* out = MakeErrorVariable(err ? err : "__index error");
			lua_pop(state->L, 1);
			return out;
		}

		int nresults = lua_gettop(state->L) - stackBefore;
		if (nresults == 0) {
			return MakeNilVariable();
		}
		KitsuneVariable* out = (KitsuneVariable*)kitsune_malloc(sizeof(KitsuneVariable));
		if (!out) {
			lua_settop(state->L, stackBefore);
			return NULL;
		}
		FillKitsuneVariableFromStack(state->L, stackBefore + 1, out);
		lua_settop(state->L, stackBefore);
		if (out->type == LUA_TNONE) {
			kitsune_free(out);
			return MakeNilVariable();
		}
		return out;
	}

	// Protected body for KitsuneSetIndex. Stack on entry: [obj, key, value].
	static int DoSetIndex(lua_State* L) {
		lua_settable(L, 1);  // obj[key] = value via __newindex; pops key and value
		return 0;
	}

	KITSUNE_API bool KitsuneSetIndex(const KitsuneVariable* obj, const KitsuneVariable* key, const KitsuneVariable* value) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !obj || !key || !value) return false;

		bool validObj = (obj->type == LUA_TTABLE && obj->ref > 0)
			|| (obj->type == LUA_TUSERDATA && obj->userdata);
		if (!validObj) return false;

		LuaAccessGuard lock(state);

		lua_pushcfunction(state->L, DoSetIndex);
		PushKitsuneVariable(state->L, obj);
		PushKitsuneVariable(state->L, key);
		PushKitsuneVariable(state->L, value);

		if (lua_pcall_nohook(state->L, 3, 0, 0) != LUA_OK) {
			lua_pop(state->L, 1);  // pop error message
			return false;
		}
		return true;
	}

	// Protected body for KitsuneGetLength. Stack on entry: [obj].
	static int DoGetLength(lua_State* L) {
		lua_len(L, 1);  // pushes #obj, respects __len metamethod
		return 1;
	}

	KITSUNE_API KitsuneVariable* KitsuneGetLength(const KitsuneVariable* obj) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !obj) return NULL;

		bool validObj = (obj->type == LUA_TTABLE && obj->ref > 0)
			|| (obj->type == LUA_TUSERDATA && obj->userdata);
		if (!validObj) return NULL;

		LuaAccessGuard lock(state);
		int stackBefore = lua_gettop(state->L);

		lua_pushcfunction(state->L, DoGetLength);
		PushKitsuneVariable(state->L, obj);

		if (lua_pcall_nohook(state->L, 1, LUA_MULTRET, 0) != LUA_OK) {
			const char* err = lua_tolstring(state->L, -1, NULL);
			KitsuneVariable* out = MakeErrorVariable(err ? err : "__len error");
			lua_pop(state->L, 1);
			return out;
		}

		int nresults = lua_gettop(state->L) - stackBefore;
		if (nresults == 0) {
			return MakeNilVariable();
		}
		KitsuneVariable* out = (KitsuneVariable*)kitsune_malloc(sizeof(KitsuneVariable));
		if (!out) {
			lua_settop(state->L, stackBefore);
			return NULL;
		}
		FillKitsuneVariableFromStack(state->L, stackBefore + 1, out);
		lua_settop(state->L, stackBefore);
		if (out->type == LUA_TNONE) {
			kitsune_free(out);
			return MakeNilVariable();
		}
		return out;
	}

	// Protected body for KitsuneNext. Stack on entry: [table, key].
	// Returns 0 when the table is exhausted, 2 (next key + value) when an entry exists.
	static int DoNext(lua_State* L) {
		if (lua_next(L, 1) == 0)
			return 0;
		return 2;
	}

	KITSUNE_API KitsuneVariable* KitsuneNext(const KitsuneVariable* tableVar, KitsuneVariable* key) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !tableVar || tableVar->type != LUA_TTABLE || tableVar->ref <= 0)
			return NULL;

		LuaAccessGuard lock(state);
		int stackBefore = lua_gettop(state->L);

		lua_pushcfunction(state->L, DoNext);
		PushKitsuneVariable(state->L, tableVar);  // arg 1: table

		// arg 2: cursor key — either nil (start) or the embedded key from the previous result
		if (key && key->type == KITSUNE_TTABLECONTENTS && key->table) {
			PushKitsuneVariable(state->L, &key->table->key);
			// Ownership transfer: free the previous result now that its key is on the stack.
			// KitsuneVariableFree uses a lock-free deferred queue on non-scheduler threads, so
			// calling it while holding LuaAccessGuard is safe in all but a theoretical OOM path.
			KitsuneVariableFree(key);
		}
		else {
			lua_pushnil(state->L);
		}

		if (lua_pcall_nohook(state->L, 2, LUA_MULTRET, 0) != LUA_OK) {
			const char* err = lua_tolstring(state->L, -1, NULL);
			KitsuneVariable* out = MakeErrorVariable(err ? err : "next: key invalidated");
			lua_pop(state->L, 1);
			return out;
		}

		int nresults = lua_gettop(state->L) - stackBefore;
		if (nresults < 2) {
			lua_settop(state->L, stackBefore);
			return MakeNoneVariable();  // table exhausted
		}

		KitsuneVariable* out = (KitsuneVariable*)kitsune_malloc(sizeof(KitsuneVariable));
		if (!out) {
			lua_settop(state->L, stackBefore);
			return NULL;
		}
		KeyValuePairKitsuneVariableNode* node = (KeyValuePairKitsuneVariableNode*)kitsune_malloc(sizeof(KeyValuePairKitsuneVariableNode));
		if (!node) {
			lua_settop(state->L, stackBefore);
			kitsune_free(out);
			return NULL;
		}
		memset(node, 0, sizeof(KeyValuePairKitsuneVariableNode));
		FillKitsuneVariableFromStack(state->L, stackBefore + 1, &node->key);
		FillKitsuneVariableFromStack(state->L, stackBefore + 2, &node->value);
		lua_settop(state->L, stackBefore);
		node->next = NULL;
		out->type = KITSUNE_TTABLECONTENTS;
		out->length = 1;
		out->table = node;
		return out;
	}

	KITSUNE_API KitsuneVariable* KitsuneCallMetamethod(const KitsuneVariable* obj, const char* metamethod, int argc, const KitsuneVariable* argv) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !obj || !metamethod || !*metamethod) return NULL;

		bool validObj = (obj->type == LUA_TTABLE && obj->ref > 0)
			|| (obj->type == LUA_TUSERDATA && obj->userdata);
		if (!validObj) return NULL;

		LuaAccessGuard lock(state);

		PushKitsuneVariable(state->L, obj);
		int objIdx = lua_absindex(state->L, -1);
		int stackBefore = objIdx - 1;  // stack depth before any of our pushes

		// luaL_getmetafield uses lua_rawget internally — safe without a pcall.
		int mtype = luaL_getmetafield(state->L, -1, metamethod);
		if (mtype == LUA_TNIL) {
			lua_pop(state->L, 1);  // pop obj
			return MakeNoneVariable();  // TNONE: metamethod absent
		}
		// Stack: [obj, metamethod_fn]
		// Slide metamethod_fn before obj so the call frame is metamethod_fn(obj, args...).
		lua_insert(state->L, objIdx);

		for (int n = 0; n < argc; n++)
			PushKitsuneVariable(state->L, &argv[n]);
		// Stack: [metamethod_fn, obj, arg1, ..., argN]

		if (lua_pcall_nohook(state->L, argc + 1, LUA_MULTRET, 0) != LUA_OK) {
			const char* err = lua_tolstring(state->L, -1, NULL);
			KitsuneVariable* out = MakeErrorVariable(err ? err : "metamethod error");
			lua_pop(state->L, 1);
			return out;
		}

		int nresults = lua_gettop(state->L) - stackBefore;
		if (nresults == 0) {
			return MakeNilVariable();  // TNIL: ran but returned nothing
		}
		KitsuneVariable* out = (KitsuneVariable*)kitsune_malloc(sizeof(KitsuneVariable));
		if (!out) {
			lua_settop(state->L, stackBefore);
			return NULL;
		}
		FillKitsuneVariableFromStack(state->L, stackBefore + 1, out);
		lua_settop(state->L, stackBefore);
		if (out->type == LUA_TNONE) {
			kitsune_free(out);
			return MakeErrorVariable("out of memory");
		}
		return out;
	}

	// Protected body for KitsuneCallMethod — step 1: field lookup via __index.
	// Upvalue 1: method name (Lua string). Stack on entry: [obj].
	// Returns the value found at obj[method]; never errors here — errors in __index become
	// pcall failures surfaced as KITSUNE_TERROR by the caller.
	static int DoLookupMethod(lua_State* L) {
		const char* method = lua_tostring(L, lua_upvalueindex(1));
		lua_getfield(L, 1, method);  // respects __index; can raise
		return 1;
	}

	KITSUNE_API KitsuneVariable* KitsuneCallMethod(const KitsuneVariable* obj, const char* method, int argc, const KitsuneVariable* argv) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !obj || !method || !*method) return NULL;

		bool validObj = (obj->type == LUA_TTABLE && obj->ref > 0)
			|| (obj->type == LUA_TUSERDATA && obj->userdata);
		if (!validObj) return NULL;

		LuaAccessGuard lock(state);
		lua_State* L = state->L;
		int stackBefore = lua_gettop(L);

		// Step 1: look up the method via __index in a protected call.
		// Separating lookup from the call means __index errors surface as TERROR, not crashes.
		lua_pushstring(L, method);
		lua_pushcclosure(L, DoLookupMethod, 1);
		PushKitsuneVariable(L, obj);

		if (lua_pcall_nohook(L, 1, 1, 0) != LUA_OK) {
			const char* err = lua_tolstring(L, -1, NULL);
			KitsuneVariable* out = MakeErrorVariable(err ? err : "index error");
			lua_pop(L, 1);
			return out;
		}

		// nil or non-function: method not found → TNONE
		if (lua_type(L, -1) != LUA_TFUNCTION) {
			lua_pop(L, 1);
			return MakeNoneVariable();
		}
		// Stack: [..., method_fn]

		// Step 2: call method_fn(obj, arg1, ..., argN) in a protected call.
		PushKitsuneVariable(L, obj);  // self
		for (int n = 0; n < argc; n++)
			PushKitsuneVariable(L, &argv[n]);
		// Stack: [..., method_fn, obj, arg1, ..., argN]

		if (lua_pcall_nohook(L, argc + 1, LUA_MULTRET, 0) != LUA_OK) {
			const char* err = lua_tolstring(L, -1, NULL);
			KitsuneVariable* out = MakeErrorVariable(err ? err : "call error");
			lua_pop(L, 1);
			return out;
		}

		int nresults = lua_gettop(L) - stackBefore;
		if (nresults == 0) {
			return MakeNilVariable();  // TNIL: ran but returned nothing
		}
		KitsuneVariable* out = (KitsuneVariable*)kitsune_malloc(sizeof(KitsuneVariable));
		if (!out) {
			lua_settop(L, stackBefore);
			return NULL;
		}
		FillKitsuneVariableFromStack(L, stackBefore + 1, out);
		lua_settop(L, stackBefore);
		if (out->type == LUA_TNONE) {
			kitsune_free(out);
			return MakeErrorVariable("out of memory");
		}
		return out;
	}

	KITSUNE_API long KitsuneGC(int mode) {
		KitsuneState* state = g_state;
		if (!state || !state->L)
			return -1;
		AcquireLuaAccess(state);
		// Drain deferred luaL_unref calls before any collection so those objects become candidates.
		DrainPendingVariableChain(state->L);
		switch (mode) {
		case 1: lua_gc(state->L, LUA_GCCOLLECT, 0); break;
		case 2: lua_gc(state->L, LUA_GCSTEP, 0); break;
		case 3: lua_gc(state->L, LUA_GCSTOP, 0); break;
		case 4: lua_gc(state->L, LUA_GCRESTART, 0); break;
		default: break; // mode 0: query only
		}
		long usage = (long)lua_gc(state->L, LUA_GCCOUNT, 0) * 1024L
			+ (long)lua_gc(state->L, LUA_GCCOUNTB, 0);
		ReleaseLuaAccess(state);
		return usage;
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
				// Wait for the scheduler to finish all work and signal it is done.
				// Using schedulerDoneEvent instead of join() avoids the loader-lock deadlock
				// that occurs when KitsuneCleanup is called from DLL_PROCESS_DETACH: the OS
				// holds the loader lock while delivering DllMain, and the scheduler thread
				// also needs the loader lock for its own DLL_THREAD_DETACH notifications.
				// schedulerDoneEvent fires before the thread exits, so state is safe to use
				// immediately after. The thread then exits asynchronously.
				state->schedulerDoneEvent.Wait();
				state->schedulerThread.detach();
				state->doneCV.notify_all(); // wake any threads blocked in WaitForResult or KitsuneWait

				// If an inline sync call was running when Dispose was called, it still holds
				// accessLock inside RunInline.  The interrupt above will cause the Ticker to raise
				// luaL_error at the next instruction boundary, after which the calling thread calls
				// ReleaseLuaAccess and unlocks accessLock.  Acquiring the lock here blocks until
				// that happens, guaranteeing lua_resume has returned and the Lua state is safe to close.
				state->accessLock.lock();
				state->accessLock.unlock();
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
					kitsune_free(slot->error);
					FreeVariableData(&slot->result, state->L);
				}
				delete slot;
			}
			state->slotCount = 0;

			// Drain any variables queued for deferred release by KitsuneVariableFree;
			// must run before lua_close so all luaL_unref calls complete while the state is live.
			if (state->L)
				DrainPendingVariableChain(state->L);

			if (state->L) {
				if (state->lastCallError) {
					kitsune_free(state->lastCallError);
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

#ifdef _DEBUG
		if (leaked != 0) {
			// Interactive debug test
			//DebugBreak();
		}
#endif

#ifdef _WIN32
		if (g_coOwned) {
			CoUninitialize();
			g_coOwned = false;
		}
#endif
		return leaked;
	}

	KITSUNE_API SharedMemoryBlock* KitsuneCreateMemoryBlock(size_t size) {
		if (!g_state || size == 0) return NULL;

		SharedMemoryBlock* block = (SharedMemoryBlock*)kitsune_malloc(sizeof(SharedMemoryBlock) + size);
		if (!block) return NULL;
		memset(block, 0, sizeof(SharedMemoryBlock) + size);
		block->size = size;
		// KITSUNE_OWNED: accepted by PushKitsuneVariable.
		// ACCESSOR_DISPOSED=1: cleared by LuaStream constructor when C# takes ownership.
		block->flags = KITSUNE_SHARED_MEMORY_FLAG_KITSUNE_OWNED
			| KITSUNE_SHARED_MEMORY_FLAG_ACCESSOR_DISPOSED;
		lua_shmem_list_add(block);
		return block;
	}

} // extern "C"
