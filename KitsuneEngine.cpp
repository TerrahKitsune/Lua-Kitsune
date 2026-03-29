#ifdef _DEBUG
#define _CRTDBG_MAP_ALLOC
#endif

// WinSock2 must be included before windows.h or any headers that include it
#include <WinSock2.h>
#include <Windows.h>

// OpenSSL headers
#include "openssl/err.h"
#include "openssl/evp.h"
#include "openssl/ssl.h"

#include "mem.h"
#include "lua_main_incl.h"
#include "GFFMain.h"
#include "TimerMain.h"
#include "MySQLMain.h"
#include "PostgresMain.h"
#include "lua_misc.h"
#include "LuaFileSystemMain.h"
#include "LuaSQLiteMain.h"
#include "ERFMain.h"
#include "MD5Main.h"
#include "HttpMain.h"
#include "ProcessMain.h"
#include "LuaClientMain.h"
#include "LuaServerMain.h"
#include "TlkMain.h"
#include "2DAMain.h"
#include "NamedPipeMain.h"
#include "LuaImageMain.h"
#include "StreamMain.h"
#include "ODBCMain.h"
#include "WinServicesMain.h"
#include "luakafkamain.h"
#include "Sha256Main.h"
#include "LuaFTPMain.h"
#include "FileAsyncMain.h"
#include "LuaMutexMain.h"
#include "LuaAesMain.h"
#include "luajsonmain.h"
#include "base64.h"
#include "MacroMain.h"
#include "wcharmain.h"
#include "LuaCsvMain.h"
#include "LuaArchiveMain.h"
#include "LuaImguiMain.h"
#include "RedisMain.h"
#include "LuaTTSMain.h"
#include "SHA1Main.h"
#include "LuaServer.h"

#include "KitsuneEngine.h"
#include "LuaEngineBuiltins.h"

// ── Per-coroutine slot ────────────────────────────────────────────────────────
struct KitsuneCoroutine {
	int           id;
	int           threadRef;    // LUA_REGISTRYINDEX anchor; keeps the thread alive for GC
	lua_State* thread;       // cached lua_State*; valid iff threadRef != LUA_NOREF.
	// Must be NULLed whenever threadRef is unref'd.
	// Written only under AcquireLuaAccess; read only by the scheduler.
	int           argsRef;      // LUA_REGISTRYINDEX anchor for the ARGS table; retrieved by GetArgs()
	volatile LONG fireAndForget;
	volatile LONG done;         // 0 = still running / yielded, 1 = finished
	volatile LONG released;     // 1 = slot should be freed; scheduler zeros it on next compaction
	volatile LONG interrupted;   // set to 1 by KitsuneCancel; observed by the scheduler before resuming this coroutine
	char* error;
	KitsuneVariable  result;
	double        sleepUntil;   // GetCounter deadline (ms) before which the coroutine must not be resumed; 0 = not sleeping
	double        startTime;    // GetCounter value recorded when the coroutine was created
	int           initialNArgs; // number of args already on the thread stack for the first lua_resume; 0 for file/string coroutines
};

#define KITSUNE_MAX_COROUTINES 256

// ── Engine state ──────────────────────────────────────────────────────────────
struct KitsuneState {
	// ── Lua ──────────────────────────────────────────────────────────────────
	lua_State* L;
	double           PCFreq;
	__int64          CounterStart;
	lua_State* DelegateState; // calling coroutine's state during a RegisterFunction call
	char* lastCallError;  // deferred KITSUNE_TERROR message; freed after args cleanup

	// ── Interrupt / pause ────────────────────────────────────────────────────
	volatile LONG interrupt;     // set by KitsuneInterrupt; cleared by scheduler when all done
	volatile LONG pauseFlag;     // set by AcquireLuaAccess; serviced by hook + scheduler
	HANDLE        pausedEvent;   // hook signals this when it parks
	HANDLE        resumeEvent;   // AcquireLuaAccess signals this to let hook continue

	// ── SetVariable/GetVariable serialisation ────────────────────────────────
	CRITICAL_SECTION accessLock; // serialises concurrent external callers
	int              varsRef;    // registry ref for the current Vars target; updated by KitsuneSetTable

	// ── Scheduler thread ─────────────────────────────────────────────────────
	HANDLE        schedulerThread;
	volatile LONG schedulerStop; // set to 1 by KitsuneCleanup
	HANDLE        workEvent;     // signaled when a new coroutine is ready to run

	// ── Active coroutine slots (written only by scheduler; read by callers) ──
	KitsuneCoroutine* slots[KITSUNE_MAX_COROUTINES];
	int               slotCount;
	CRITICAL_SECTION  slotsLock; // guards add/remove of slots[] entries

	// ── Counters ─────────────────────────────────────────────────────────────
	volatile LONG nextId;             // monotonically increasing coroutine ID
	volatile LONG runningCount;       // number of slots where done == 0
	volatile LONG currentCoroutineId; // ID of the coroutine currently inside lua_resume, or 0
};

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	return TRUE;
}

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

static void PushKitsuneVariable(lua_State* L, const KitsuneVariable* v) {
	if (!v) { lua_pushnil(L); return; }
	switch (v->type) {
	case LUA_TNUMBER: {
		lua_Integer iv = (lua_Integer)v->number;
		if ((double)iv == v->number && v->number >= (double)LUA_MININTEGER && v->number <= (double)LUA_MAXINTEGER)
			lua_pushinteger(L, iv);
		else
			lua_pushnumber(L, v->number);
		break;
	}
	case LUA_TBOOLEAN:
		lua_pushboolean(L, v->boolean ? 1 : 0);
		break;
	case LUA_TSTRING:
		if (v->data && v->length > 0)
			lua_pushlstring(L, (const char*)v->data, v->length);
		else
			lua_pushstring(L, "");
		break;
	case LUA_TTABLE:  // KITSUNE_TTABLE on Set: create an empty table at the given path
		lua_newtable(L);
		break;
	default:
		lua_pushnil(L);
		break;
	}
}

static void SetSlotResult(KitsuneCoroutine* slot, lua_State* T, int idx) {
	if (slot->result.type == LUA_TSTRING && slot->result.data) {
		gff_free(slot->result.data);
		slot->result.data = NULL;
	}
	memset(&slot->result, 0, sizeof(slot->result));
	slot->result.type = LUA_TNONE;
	int t = lua_type(T, idx);
	switch (t) {
	case LUA_TNUMBER:
		slot->result.type = LUA_TNUMBER;
		slot->result.number = lua_tonumber(T, idx);
		break;
	case LUA_TBOOLEAN:
		slot->result.type = LUA_TBOOLEAN;
		slot->result.boolean = lua_toboolean(T, idx) != 0;
		break;
	case LUA_TSTRING: {
		size_t len;
		const char* s = lua_tolstring(T, idx, &len);
		if (s) {
			slot->result.data = (unsigned char*)gff_malloc(len + 1);
			if (slot->result.data) {
				memcpy(slot->result.data, s, len + 1);
				slot->result.length = len;
				slot->result.type = LUA_TSTRING;
			}
		}
		break;
	}
	case LUA_TUSERDATA: {
		// Attempt to stringify via __tostring metamethod using a protected call on the main state.
		// Running on the main state ensures any error in __tostring is caught by lua_pcall
		// rather than propagating through the coroutine thread's unguarded execution context.
		void* ud; lua_getallocf(T, &ud);
		lua_State* mainL = ((KitsuneState*)ud)->L;
		lua_pushvalue(T, idx);      // copy the userdata onto T's top
		lua_xmove(T, mainL, 1);    // move that copy to the main state
		if (luaL_getmetafield(mainL, -1, "__tostring") != LUA_TNIL) {
			// mainL: [..., userdata_copy, __tostring]
			lua_pushvalue(mainL, -2);  // push another copy as the argument
			if (lua_pcall(mainL, 1, 1, 0) == LUA_OK && lua_type(mainL, -1) == LUA_TSTRING) {
				size_t len;
				const char* s = lua_tolstring(mainL, -1, &len);
				if (s) {
					slot->result.data = (unsigned char*)gff_malloc(len + 1);
					if (slot->result.data) {
						memcpy(slot->result.data, s, len + 1);
						slot->result.length = len;
						slot->result.type = LUA_TSTRING;
					}
				}
			}
			lua_pop(mainL, 1);  // pop pcall result or error
		}
		lua_pop(mainL, 1);  // pop userdata_copy
		if (slot->result.type != LUA_TSTRING)
			slot->result.type = LUA_TUSERDATA;
		break;
	}
	default:
		slot->result.type = t;  // preserve actual type; data remains null for non-bridgeable values
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
	EnterCriticalSection(&state->accessLock);
	// Set pauseFlag and wake the scheduler, then unconditionally wait for it to
	// acknowledge. The scheduler signals pausedEvent at the top of its loop
	// (step 1) and the Ticker does the same mid-resume; always waiting prevents
	// a phantom signal (produced when runningCount==0 but the scheduler still
	// reaches step 1) from being consumed by a later call where runningCount>0,
	// which would let this thread race with the scheduler on state->L.
	InterlockedExchange(&state->pauseFlag, 1);
	SetEvent(state->workEvent);  // wake the scheduler if it is sleeping in step 5
	WaitForSingleObject(state->pausedEvent, INFINITE);
}

static void ReleaseLuaAccess(KitsuneState* state) {
	if (InterlockedAdd(&state->pauseFlag, 0)) {
		InterlockedExchange(&state->pauseFlag, 0);
		SetEvent(state->resumeEvent);
	}
	LeaveCriticalSection(&state->accessLock);
}

static void Ticker(lua_State* L, lua_Debug* ar) {
	void* ud;
	lua_getallocf(L, &ud);
	KitsuneState* state = (KitsuneState*)ud;

	// Do not auto-clear interrupt here; the scheduler clears it once all coroutines are done.
	if (InterlockedAdd(&state->interrupt, 0)) {
		luaL_error(L, "interrupted");
		return;
	}

	// Per-coroutine cancel: check whether this specific coroutine has been cancelled.
	// currentCoroutineId is set by the scheduler immediately before lua_resume.
	int cancelId = (int)InterlockedAdd(&state->currentCoroutineId, 0);
	if (cancelId) {
		KitsuneCoroutine* curSlot = FindSlot(state, cancelId);
		if (curSlot && InterlockedAdd(&curSlot->interrupted, 0)) {
			luaL_error(L, "cancelled");
			return;
		}
	}

	if (InterlockedAdd(&state->pauseFlag, 0)) {
		SetEvent(state->pausedEvent);
		WaitForSingleObject(state->resumeEvent, INFINITE);
		if (InterlockedAdd(&state->interrupt, 0)) {
			luaL_error(L, "interrupted");
			return;
		}
	}

	// Yield to let the scheduler run other coroutines before returning here.
	// Only yield when the scheduler initiated this resume and there are other coroutines waiting.
	if (InterlockedAdd(&state->runningCount, 0) > 1 && InterlockedAdd(&state->currentCoroutineId, 0))
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
	InterlockedExchange(&slot->done, 1);
	InterlockedDecrement(&state->runningCount);
	if (InterlockedAdd(&slot->fireAndForget, 0))
		InterlockedExchange(&slot->released, 1);
}

static DWORD WINAPI SchedulerProc(LPVOID param) {
	KitsuneState* state = (KitsuneState*)param;

	while (!InterlockedAdd(&state->schedulerStop, 0)) {
		// ── Step 1: Service pause requests BEFORE touching state->L ──────────────
		// AcquireLuaAccess (SetVariable, GetVariable, StartCoroutine) sets pauseFlag
		// regardless of runningCount, so this is the single serialisation point.
		while (InterlockedAdd(&state->pauseFlag, 0)) {
			SetEvent(state->pausedEvent);
			WaitForSingleObject(state->resumeEvent, INFINITE);
		}
		if (InterlockedAdd(&state->schedulerStop, 0)) break;  // KitsuneCleanup called while paused

		bool anyActive = false;

		// ── Step 2: Interrupt all non-done coroutines if requested ────────────
		if (InterlockedAdd(&state->interrupt, 0)) {
			for (int i = 0; i < state->slotCount; i++) {
				KitsuneCoroutine* slot = state->slots[i];
				if (slot->id != 0 && !InterlockedAdd(&slot->done, 0)) {
					SetSlotError(slot, "interrupted");
					lua_State* T = GetCoroutineThread(state, slot);
					if (T) lua_settop(T, 0);
					InterlockedExchange(&slot->done, 1);
					InterlockedDecrement(&state->runningCount);
					if (InterlockedAdd(&slot->fireAndForget, 0))
						InterlockedExchange(&slot->released, 1);
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
				while (InterlockedAdd(&state->pauseFlag, 0)) {
					SetEvent(state->pausedEvent);
					WaitForSingleObject(state->resumeEvent, INFINITE);
				}
				if (InterlockedAdd(&state->schedulerStop, 0)) break;

				KitsuneCoroutine* slot = state->slots[i];
				if (slot->id == 0 || InterlockedAdd(&slot->done, 0)) continue;
				// Per-coroutine cancel: terminate before the next resume (or wake from sleep).
				if (InterlockedAdd(&slot->interrupted, 0)) {
					SetSlotError(slot, "cancelled");
					lua_State* Tc = GetCoroutineThread(state, slot);
					if (Tc) lua_settop(Tc, 0);
					InterlockedExchange(&slot->done, 1);
					InterlockedDecrement(&state->runningCount);
					InterlockedExchange(&slot->released, 1);
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
					InterlockedExchange(&slot->done, 1);
					InterlockedDecrement(&state->runningCount);
					if (InterlockedAdd(&slot->fireAndForget, 0))
						InterlockedExchange(&slot->released, 1);
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
				InterlockedExchange(&state->currentCoroutineId, (LONG)slot->id);
				int rc = lua_resume(T, state->L, nstart, &nresults);
				InterlockedExchange(&state->currentCoroutineId, 0);
				if (rc == LUA_YIELD)
					lua_pop(T, nresults);  // discard yielded values only; lua_settop(T,0) would corrupt locals
				else
					FinishCoroutine(state, slot, T, rc, nresults);
			}
		}

		// ── Step 3: Clear interrupt once no coroutines remain active ──────────
		if (InterlockedAdd(&state->runningCount, 0) == 0)
			InterlockedExchange(&state->interrupt, 0);

		// ── Step 4: Release done + released slots – zero the struct for reuse ─
		{
			// Phase 1 (under slotsLock): collect registry refs and zero each slot.
			// luaL_unref is kept outside the lock so its internal Lua allocations
			// do not block concurrent KitsuneCancel / KitsuneGetActiveIds callers.
			int pendingArgs[KITSUNE_MAX_COROUTINES];
			int pendingThreads[KITSUNE_MAX_COROUTINES];
			int pendingCount = 0;

			EnterCriticalSection(&state->slotsLock);
			for (int i = 0; i < state->slotCount; i++) {
				KitsuneCoroutine* slot = state->slots[i];
				if (slot->id != 0 && InterlockedAdd(&slot->done, 0) && InterlockedAdd(&slot->released, 0)) {
					pendingArgs[pendingCount]    = slot->argsRef;
					pendingThreads[pendingCount] = slot->threadRef;
					pendingCount++;
					slot->thread = NULL;  // invariant: null before memset so the pointer is never stale
					gff_free(slot->error);
					if (slot->result.type == LUA_TSTRING && slot->result.data)
						gff_free(slot->result.data);
					memset(slot, 0, sizeof(KitsuneCoroutine));  // id = 0 marks the slot as reusable
				}
			}
			LeaveCriticalSection(&state->slotsLock);

			// Phase 2 (outside slotsLock): release Lua registry references.
			for (int i = 0; i < pendingCount; i++) {
				if (pendingArgs[i]    != LUA_NOREF) luaL_unref(state->L, LUA_REGISTRYINDEX, pendingArgs[i]);
				if (pendingThreads[i] != LUA_NOREF) luaL_unref(state->L, LUA_REGISTRYINDEX, pendingThreads[i]);
			}
		}

		// ── Step 5: Sleep when there is nothing active ────────────────────────
		if (!anyActive) {
			// Use a short wait when coroutines are mid-Sleep() so their deadlines
			// are checked promptly; fall back to 10ms when the engine is truly idle.
			bool hasSleeping = false;
			for (int i = 0; i < state->slotCount; i++) {
				KitsuneCoroutine* slot = state->slots[i];
				if (slot->id != 0 && !InterlockedAdd(&slot->done, 0) && slot->sleepUntil > 0.0) {
					hasSleeping = true;
					break;
				}
			}
			WaitForSingleObject(state->workEvent, hasSleeping ? 1 : 10);
		}
	}

	return 0;
}

static int L_GetRuntime(lua_State* L) {
	void* ud;
	lua_getallocf(L, &ud);
	KitsuneState* state = (KitsuneState*)ud;

	// If called from within a scheduler-managed coroutine, return that coroutine's runtime.
	int id = (int)InterlockedAdd(&state->currentCoroutineId, 0);
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
	int id = (int)InterlockedAdd(&state->currentCoroutineId, 0);
	KitsuneCoroutine* slot = FindSlot(state, id);  // NULL when id==0 (not inside scheduler's lua_resume)

	if (slot) {
		// Scheduler-managed coroutine: record the wake-up deadline and yield cooperatively.
		if (ms > 0.0)
			slot->sleepUntil = GetCounter(state) + (double)ms;
		return lua_yieldk(L, 0, 0, L_SleepContinuation);
	}

	// Not called from a scheduler-managed coroutine — fall back to a blocking OS sleep.
	if (ms > 0.0)
		::Sleep((DWORD)(ms < (lua_Number)MAXDWORD ? (DWORD)ms : MAXDWORD));
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

// ============================================================
// Exported API
// ============================================================

static KitsuneState* g_state = nullptr;
static bool          g_coOwned = false;

extern "C" {

	KITSUNE_API bool KitsuneInit() {
		if (g_state)
			return true;

		// RPC_E_CHANGED_MODE means COM was already initialised by the host (e.g. .NET's
		// MTA thread pool).  We can still use COM; we just must not call CoUninitialize.
		HRESULT cohr = CoInitialize(NULL);
		if (FAILED(cohr) && cohr != RPC_E_CHANGED_MODE)
			return false;
		g_coOwned = SUCCEEDED(cohr);

		InitMemoryManager();

		WSADATA wsa;
		if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
			ERR_free_strings();
			EVP_cleanup();
			EndMemoryManager();
			if (g_coOwned) CoUninitialize();
			return false;
		}

		SSL_load_error_strings();
		SSL_library_init();
		OpenSSL_add_all_algorithms();

		KitsuneState* state = (KitsuneState*)gff_malloc(sizeof(KitsuneState));
		memset(state, 0, sizeof(KitsuneState));
		state->varsRef = LUA_NOREF;
		state->pausedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		state->resumeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		state->workEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		if (!state->pausedEvent || !state->resumeEvent || !state->workEvent) {
			if (state->pausedEvent) CloseHandle(state->pausedEvent);
			if (state->resumeEvent) CloseHandle(state->resumeEvent);
			if (state->workEvent)   CloseHandle(state->workEvent);
			gff_free(state);
			ERR_free_strings();
			EVP_cleanup();
			WSACleanup();
			EndMemoryManager();
			if (g_coOwned) CoUninitialize();
			return false;
		}
		InitializeCriticalSection(&state->accessLock);
		InitializeCriticalSection(&state->slotsLock);
		StartCounter(state);

		state->L = lua_newstate(l_alloc, state);
		if (!state->L) {
			CloseHandle(state->pausedEvent);
			CloseHandle(state->resumeEvent);
			CloseHandle(state->workEvent);
			DeleteCriticalSection(&state->accessLock);
			DeleteCriticalSection(&state->slotsLock);
			gff_free(state);
			ERR_free_strings();
			EVP_cleanup();
			WSACleanup();
			EndMemoryManager();
			if (g_coOwned) CoUninitialize();
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

		luaopen_gff(L);          lua_setglobal(L, "GFF");
		luaopen_timer(L);        lua_setglobal(L, "Timer");
		luaopen_mysql(L);        lua_setglobal(L, "MySQL");
		luaopen_postgres(L);     lua_setglobal(L, "Postgres");
		luaopen_filesystem(L);   lua_setglobal(L, "FileSystem");
		luaopen_sqlite(L);       lua_setglobal(L, "SQLite");
		luaopen_md5(L);          lua_setglobal(L, "MD5");
		luaopen_erf(L);          lua_setglobal(L, "ERF");
		luaopen_http(L);         lua_setglobal(L, "Http");
		luaopen_process(L);      lua_setglobal(L, "Process");
		luaopen_luaserver(L);    lua_setglobal(L, "Server");
		luaopen_luaclient(L);    lua_setglobal(L, "Client");
		luaopen_tlk(L);          lua_setglobal(L, "TLK");
		luaopen_twoda(L);        lua_setglobal(L, "TWODA");
		luaopen_namedpipe(L);    lua_setglobal(L, "Pipe");
		luaopen_image(L);        lua_setglobal(L, "Image");
		luaopen_stream(L);       lua_setglobal(L, "Stream");
		luaopen_odbc(L);         lua_setglobal(L, "ODBC");
		luaopen_winservice(L);   lua_setglobal(L, "Services");
		luaopen_kafka(L);        lua_setglobal(L, "Kafka");
		luaopen_sha256(L);       lua_setglobal(L, "SHA256");
		luaopen_ftp(L);          lua_setglobal(L, "FTP");
		luaopen_fileasync(L);    lua_setglobal(L, "FileAsync");
		luaopen_mutex(L);        lua_setglobal(L, "Mutex");
		luaopen_luaaes(L);       lua_setglobal(L, "Aes");
		luaopen_json(L);         lua_setglobal(L, "Json");
		luaopen_base64(L);       lua_setglobal(L, "Base64");
		luaopen_macro(L);        lua_setglobal(L, "Macro");
		luaopen_wchar(L);        lua_setglobal(L, "Wchar");
		luaopen_csv(L);          lua_setglobal(L, "CSV");
		luaopen_archive(L);      lua_setglobal(L, "Archive");
		luaopen_imgui(L);        lua_setglobal(L, "Imgui");
		luaopen_redis(L);        lua_setglobal(L, "Redis");
		luaopen_tts(L);          lua_setglobal(L, "TTS");
		luaopen_sha1(L);         lua_setglobal(L, "SHA1");

		lua_pushcfunction(L, L_GetRuntime);    lua_setglobal(L, "Runtime");
		lua_pushcfunction(L, L_SetTitle);      lua_setglobal(L, "SetTitle");
		lua_pushcfunction(L, L_ToggleConsole); lua_setglobal(L, "ToggleConsole");
		lua_pushcfunction(L, L_GetReg);        lua_setglobal(L, "GetRegistryValue");
		lua_pushcfunction(L, L_ShellExecute);  lua_setglobal(L, "ShellExecute");
		lua_pushcfunction(L, L_GetMemory);     lua_setglobal(L, "GetMemory");
		lua_pushcfunction(L, L_GetTextColor);  lua_setglobal(L, "GetTextColor");
		lua_pushcfunction(L, L_SetTextColor);  lua_setglobal(L, "SetTextColor");
		lua_pushcfunction(L, L_getch);         lua_setglobal(L, "GetKey");
		lua_pushcfunction(L, L_kbhit);         lua_setglobal(L, "HasKeyDown");
		lua_pushcfunction(L, L_put);           lua_setglobal(L, "Put");
		lua_pushcfunction(L, L_cls);           lua_setglobal(L, "CLS");

		luaopen_misc(L);
		lua_pushcfunction(L, L_Sleep);
		lua_setglobal(L, "Sleep");

		lua_newtable(L);
		lua_pushvalue(L, -1);  // duplicate: one for the registry anchor, one for the global
		state->varsRef = luaL_ref(L, LUA_REGISTRYINDEX);
		lua_setglobal(L, "Vars");

		// Coroutine threads each receive their own hook; no hook is set on the main state.
		state->schedulerThread = CreateThread(NULL, 0, SchedulerProc, state, 0, NULL);
		if (!state->schedulerThread) {
			GetHttpBuffer(0);
			luaserver_KillAll(state->L);
			lua_close(state->L);
			CloseHandle(state->pausedEvent);
			CloseHandle(state->resumeEvent);
			CloseHandle(state->workEvent);
			DeleteCriticalSection(&state->accessLock);
			DeleteCriticalSection(&state->slotsLock);
			gff_free(state);
			ERR_free_strings();
			EVP_cleanup();
			WSACleanup();
			EndMemoryManager();
			if (g_coOwned) CoUninitialize();
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
			if (state->slots[i]->id == 0) { slot = state->slots[i]; break; }
		}
		if (!slot) {
			if (state->slotCount >= KITSUNE_MAX_COROUTINES) {
				ReleaseLuaAccess(state);
				return -1;
			}
			slot = (KitsuneCoroutine*)gff_malloc(sizeof(KitsuneCoroutine));
			if (!slot) { ReleaseLuaAccess(state); return -1; }
			memset(slot, 0, sizeof(KitsuneCoroutine));
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

		int id = (int)InterlockedIncrement(&state->nextId);

		if (loadrc != 0) {
			const char* err = lua_tolstring(T, -1, NULL);
			SetSlotError(slot, err ? err : "load error");
			slot->result.type = LUA_TNONE;
			lua_settop(T, 0);
			luaL_unref(state->L, LUA_REGISTRYINDEX, slot->argsRef);   slot->argsRef = LUA_NOREF;
			luaL_unref(state->L, LUA_REGISTRYINDEX, slot->threadRef); slot->threadRef = LUA_NOREF;
			slot->thread = NULL;  // invariant: thread is only valid while threadRef != LUA_NOREF
			InterlockedExchange(&slot->done, 1);
			if (InterlockedAdd(&slot->fireAndForget, 0))
				InterlockedExchange(&slot->released, 1);
		}
		else {
			InterlockedIncrement(&state->runningCount);
		}

		slot->startTime = GetCounter(state);

		// Expose the slot by assigning its ID; add it to the array if newly allocated.
		EnterCriticalSection(&state->slotsLock);
		slot->id = id;
		if (isNewSlot)
			state->slots[state->slotCount++] = slot;
		LeaveCriticalSection(&state->slotsLock);

		ReleaseLuaAccess(state);
		SetEvent(state->workEvent);
		return id;
	}

	KITSUNE_API int KitsuneExecuteFile(const char* path, int argc, const KitsuneVariable* argv, bool fireAndForget) {
		if (g_state && g_state->DelegateState) return -1;
		return StartCoroutine(g_state, true, path, argc, argv, fireAndForget);
	}

	KITSUNE_API int KitsuneExecuteString(const char* script, int argc, const KitsuneVariable* argv, bool fireAndForget) {
		if (g_state && g_state->DelegateState) return -1;
		return StartCoroutine(g_state, false, script, argc, argv, fireAndForget);
	}

	static int StartCoroutineFunction(KitsuneState* state, const char* functionName,
		int argc, const KitsuneVariable* argv, bool fireAndForget) {
		if (!state || !functionName) return -1;

		AcquireLuaAccess(state);

		KitsuneCoroutine* slot = NULL;
		bool isNewSlot = false;
		for (int i = 0; i < state->slotCount; i++) {
			if (state->slots[i]->id == 0) { slot = state->slots[i]; break; }
		}
		if (!slot) {
			if (state->slotCount >= KITSUNE_MAX_COROUTINES) {
				ReleaseLuaAccess(state);
				return -1;
			}
			slot = (KitsuneCoroutine*)gff_malloc(sizeof(KitsuneCoroutine));
			if (!slot) { ReleaseLuaAccess(state); return -1; }
			memset(slot, 0, sizeof(KitsuneCoroutine));
			isNewSlot = true;
		}

		slot->threadRef = LUA_NOREF;
		slot->argsRef = LUA_NOREF;  // no ARGS table – args are passed directly to the function
		slot->fireAndForget = fireAndForget ? 1 : 0;

		lua_State* T = CreateCoroutineThread(state, slot);

		// Push the global function onto the coroutine thread's stack.
		lua_getglobal(T, functionName);

		int id = (int)InterlockedIncrement(&state->nextId);

		if (!lua_isfunction(T, -1)) {
			lua_pop(T, 1);
			SetSlotError(slot, "function not found");
			slot->result.type = LUA_TNONE;
			luaL_unref(state->L, LUA_REGISTRYINDEX, slot->threadRef);
			slot->threadRef = LUA_NOREF;
			slot->thread = NULL;
			InterlockedExchange(&slot->done, 1);
			if (InterlockedAdd(&slot->fireAndForget, 0))
				InterlockedExchange(&slot->released, 1);
		}
		else {
			for (int n = 0; n < argc; n++)
				PushKitsuneVariable(T, argv ? &argv[n] : nullptr);
			slot->initialNArgs = argc;
			InterlockedIncrement(&state->runningCount);
		}

		slot->startTime = GetCounter(state);

		EnterCriticalSection(&state->slotsLock);
		slot->id = id;
		if (isNewSlot)
			state->slots[state->slotCount++] = slot;
		LeaveCriticalSection(&state->slotsLock);

		ReleaseLuaAccess(state);
		SetEvent(state->workEvent);
		return id;
	}

	KITSUNE_API int KitsuneExecuteFunction(const char* functionName, int argc, const KitsuneVariable* argv, bool fireAndForget) {
		if (g_state && g_state->DelegateState) return -1;
		return StartCoroutineFunction(g_state, functionName, argc, argv, fireAndForget);
	}

	KITSUNE_API size_t KitsuneGetError(int id, char* buf, size_t bufSize) {
		KitsuneState* state = g_state;
		if (!state) return 0;
		EnterCriticalSection(&state->slotsLock);
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
		LeaveCriticalSection(&state->slotsLock);
		return len;
	}

	KITSUNE_API bool KitsuneHasResult(int id, size_t* len) {
		KitsuneState* state = g_state;
		if (!state) {
			if (len) *len = 0;
			return false;
		}
		EnterCriticalSection(&state->slotsLock);
		KitsuneCoroutine* slot = FindSlot(state, id);
		bool done = slot ? (InterlockedAdd(&slot->done, 0) != 0) : false;
		if (len) *len = (done && slot && slot->result.type == LUA_TSTRING) ? slot->result.length : 0;
		LeaveCriticalSection(&state->slotsLock);
		return done;
	}

	KITSUNE_API KitsuneVariable* KitsuneGetResult(int id) {
		KitsuneState* state = g_state;
		if (!state) return NULL;

		EnterCriticalSection(&state->slotsLock);
		KitsuneCoroutine* slot = FindSlot(state, id);
		if (!slot || !InterlockedAdd(&slot->done, 0)) {
			LeaveCriticalSection(&state->slotsLock);
			return NULL;
		}

		KitsuneVariable* out = (KitsuneVariable*)gff_malloc(sizeof(KitsuneVariable));
		if (!out) {
			InterlockedExchange(&slot->released, 1);
			LeaveCriticalSection(&state->slotsLock);
			return NULL;
		}
		memset(out, 0, sizeof(KitsuneVariable));

		// Transfer string data ownership directly; zero the slot's pointer to prevent double-free.
		if (slot->result.type == LUA_TSTRING && slot->result.data) {
			out->type = LUA_TSTRING;
			out->length = slot->result.length;
			out->data = (unsigned char*)InterlockedExchangePointer((PVOID*)&slot->result.data, NULL);
			slot->result.length = 0;
		}
		else {
			*out = slot->result;  // inline copy for number / bool / none
		}
		slot->result.type = LUA_TNONE;  // mark consumed; prevents stale reads before scheduler compacts

		InterlockedExchange(&slot->released, 1);
		LeaveCriticalSection(&state->slotsLock);
		return out;
	}

	KITSUNE_API void KitsuneCancel(int id) {
		KitsuneState* state = g_state;
		if (!state) return;
		EnterCriticalSection(&state->slotsLock);
		KitsuneCoroutine* slot = FindSlot(state, id);
		if (slot) {
			InterlockedExchange(&slot->fireAndForget, 1);
			if (InterlockedAdd(&slot->done, 0))
				InterlockedExchange(&slot->released, 1);  // already finished, release directly
			else
				InterlockedExchange(&slot->interrupted, 1);  // still running, signal per-coroutine cancel
		}
		LeaveCriticalSection(&state->slotsLock);
		SetEvent(state->workEvent);  // wake the scheduler to process the cancel promptly
	}

	KITSUNE_API double KitsuneGetRuntime(int id) {
		KitsuneState* state = g_state;
		if (!state) return 0.0;
		EnterCriticalSection(&state->slotsLock);
		KitsuneCoroutine* slot = FindSlot(state, id);
		double runtime = slot ? GetCounter(state) - slot->startTime : 0.0;
		LeaveCriticalSection(&state->slotsLock);
		return runtime;
	}

	KITSUNE_API bool KitsuneIsRunning() {
		KitsuneState* state = g_state;
		if (!state) return false;
		if (InterlockedAdd(&state->currentCoroutineId, 0)) return true;
		EnterCriticalSection(&state->slotsLock);
		bool running = false;
		for (int i = 0; i < state->slotCount; i++) {
			if (state->slots[i]->id != 0 && !InterlockedAdd(&state->slots[i]->done, 0)) {
				running = true;
				break;
			}
		}
		LeaveCriticalSection(&state->slotsLock);
		return running;
	}

	KITSUNE_API int KitsuneGetRunningId() {
		KitsuneState* state = g_state;
		if (!state) return 0;
		int active = (int)InterlockedAdd(&state->currentCoroutineId, 0);
		if (active) return active;
		EnterCriticalSection(&state->slotsLock);
		int id = 0;
		for (int i = 0; i < state->slotCount; i++) {
			if (state->slots[i]->id != 0 && !InterlockedAdd(&state->slots[i]->done, 0)) {
				id = state->slots[i]->id;
				break;
			}
		}
		LeaveCriticalSection(&state->slotsLock);
		return id;
	}

	KITSUNE_API void KitsuneReleaseResult(int id) {
		KitsuneState* state = g_state;
		if (!state) return;
		EnterCriticalSection(&state->slotsLock);
		KitsuneCoroutine* slot = FindSlot(state, id);
		if (slot && InterlockedAdd(&slot->done, 0))
			InterlockedExchange(&slot->released, 1);
		LeaveCriticalSection(&state->slotsLock);
	}

	KITSUNE_API void KitsuneInterrupt() {
		KitsuneState* state = g_state;
		if (state)
			InterlockedExchange(&state->interrupt, 1);
	}

	KITSUNE_API void KitsuneWait() {
		KitsuneState* state = g_state;
		if (!state) return;
		while (InterlockedAdd(&state->runningCount, 0))
			Sleep(1);
	}

	KITSUNE_API int KitsuneGetActiveIds(int* buffer, int bufferSize) {
		KitsuneState* state = g_state;
		if (!state) return 0;

		EnterCriticalSection(&state->slotsLock);
		int count = 0;
		for (int i = 0; i < state->slotCount; i++) {
			KitsuneCoroutine* slot = state->slots[i];
			if (slot->id != 0 && !InterlockedAdd(&slot->released, 0)) {
				if (buffer && count < bufferSize)
					buffer[count] = slot->id;
				count++;
			}
		}
		LeaveCriticalSection(&state->slotsLock);
		return count;
	}

	// Fills a KitsuneVariable from the Lua stack at the given index.
	// Handles string, number, boolean, and userdata (__tostring). Caller owns any allocated data.
	static void FillKitsuneVariableFromStack(lua_State* L, int idx, KitsuneVariable* out) {
		memset(out, 0, sizeof(KitsuneVariable));
		int abs_idx = lua_absindex(L, idx);
		int t = lua_type(L, abs_idx);
		switch (t) {
		case LUA_TNUMBER:
			out->type = LUA_TNUMBER;
			out->number = lua_tonumber(L, abs_idx);
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
			}
			break;
		}
		case LUA_TUSERDATA: {
			// Attempt __tostring via protected call; stack balance is preserved by lua_absindex.
			if (luaL_getmetafield(L, abs_idx, "__tostring") != LUA_TNIL) {
				lua_pushvalue(L, abs_idx);  // copy of userdata as the argument
				if (lua_pcall(L, 1, 1, 0) == LUA_OK && lua_type(L, -1) == LUA_TSTRING) {
					size_t len;
					const char* s = lua_tolstring(L, -1, &len);
					if (s) {
						out->data = (unsigned char*)gff_malloc(len + 1);
						if (out->data) {
							memcpy(out->data, s, len + 1);
							out->length = len;
							out->type = LUA_TSTRING;
						}
					}
				}
				lua_pop(L, 1);  // pop pcall result or error
			}
			if (out->type != LUA_TSTRING)
				out->type = LUA_TUSERDATA;
			break;
		}
		default:
			out->type = t;  // preserve actual type; data remains null for non-bridgeable values
			break;
		}
	}

	KITSUNE_API void KitsuneVariableFree(KitsuneVariable* var) {
		if (!var) return;
		if ((var->type == LUA_TSTRING || var->type == KITSUNE_TERROR) && var->data)
			gff_free(var->data);
		gff_free(var);
	}

	// Navigate from the table on top of L into the parent table of the final key in a dot-path.
	// Returns the final key component on success; the parent table remains on top.
	// Returns NULL on failure; the stack is fully restored (the initial push is undone).
	// createMissing=true: auto-create missing intermediate tables.
	static const char* NavigateToParent(lua_State* L, const char* path, bool createMissing) {
		int startTop = lua_gettop(L) - 1;  // depth before the initial table was pushed
		const char* p = path;
		for (;;) {
			const char* dot = strchr(p, '.');
			if (!dot) return p;  // success: parent on top, final key is p
			size_t len = (size_t)(dot - p);
			lua_pushlstring(L, p, len);
			lua_gettable(L, -2);
			if (lua_isnil(L, -1)) {
				if (!createMissing) { lua_settop(L, startTop); return NULL; }
				lua_pop(L, 1);
				lua_newtable(L);
				lua_pushlstring(L, p, len);
				lua_pushvalue(L, -2);   // dup subtable
				lua_settable(L, -4);    // parent[key] = subtable, pops key + dup
			} else if (!lua_istable(L, -1)) {
				lua_settop(L, startTop);
				return NULL;
			}
			lua_remove(L, -2);  // remove parent, subtable is now on top
			p = dot + 1;
		}
	}

	KITSUNE_API bool KitsuneSetVariable(const char* path, const KitsuneVariable* var) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !path || !*path) return false;
		if (state->DelegateState) return false;  // called from within a registered function; would deadlock
		AcquireLuaAccess(state);
		bool ok = false;
		lua_rawgeti(state->L, LUA_REGISTRYINDEX, state->varsRef);
		const char* finalKey = NavigateToParent(state->L, path, true);
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
		if (state->DelegateState) return NULL;  // called from within a registered function; would deadlock

		AcquireLuaAccess(state);
		KitsuneVariable* out = NULL;
		lua_rawgeti(state->L, LUA_REGISTRYINDEX, state->varsRef);
		const char* finalKey = NavigateToParent(state->L, path, false);
		if (finalKey) {
			if (lua_istable(state->L, -1)) {
				lua_getfield(state->L, -1, finalKey);
				int t = lua_type(state->L, -1);
				if (t != LUA_TNIL && t != LUA_TNONE) {
					out = (KitsuneVariable*)gff_malloc(sizeof(KitsuneVariable));
					if (out) {
						memset(out, 0, sizeof(KitsuneVariable));
						switch (t) {
						case LUA_TNUMBER:
							out->type = LUA_TNUMBER;
							out->number = lua_tonumber(state->L, -1);
							break;
						case LUA_TBOOLEAN:
							out->type = LUA_TBOOLEAN;
							out->boolean = lua_toboolean(state->L, -1) != 0;
							break;
						case LUA_TSTRING: {
							size_t len;
							const char* s = lua_tolstring(state->L, -1, &len);
							if (s) {
								out->data = (unsigned char*)gff_malloc(len + 1);
								if (out->data) {
									memcpy(out->data, s, len + 1);
									out->length = len;
									out->type = LUA_TSTRING;
								}
							}
							break;
						}
						case LUA_TUSERDATA: {
							// Attempt to stringify via __tostring metamethod using a protected call.
							// lua_pushvalue copies the userdata so the original at -1 is preserved for
							// the outer lua_pop after the switch regardless of pcall outcome.
							if (luaL_getmetafield(state->L, -1, "__tostring") != LUA_TNIL) {
								lua_pushvalue(state->L, -2);  // copy of userdata as argument
								if (lua_pcall(state->L, 1, 1, 0) == LUA_OK && lua_type(state->L, -1) == LUA_TSTRING) {
									size_t len;
									const char* s = lua_tolstring(state->L, -1, &len);
									if (s) {
										out->data = (unsigned char*)gff_malloc(len + 1);
										if (out->data) {
											memcpy(out->data, s, len + 1);
											out->length = len;
											out->type = LUA_TSTRING;
										}
									}
								}
								lua_pop(state->L, 1);  // pop pcall result or error
							}
							if (out->type != LUA_TSTRING)
								out->type = LUA_TUSERDATA;
							break;
						}
						default:
							out->type = t;
							break;
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

	KITSUNE_API void KitsuneGetAll(const char* path, kitsune_KeyValuePairCallback callback, void* userdata) {
		KitsuneState* state = g_state;
		if (!state || !state->L || !callback) return;
		if (state->DelegateState) return;  // called from within a registered function; would deadlock

		AcquireLuaAccess(state);

		lua_rawgeti(state->L, LUA_REGISTRYINDEX, state->varsRef);

		// If a path is given, navigate into the specified subtable.
		if (path && *path) {
			const char* finalKey = NavigateToParent(state->L, path, false);
			if (finalKey) {
				lua_getfield(state->L, -1, finalKey);
				lua_remove(state->L, -2);  // remove parent, keep target
				if (!lua_istable(state->L, -1)) {
					lua_pop(state->L, 1);
					ReleaseLuaAccess(state);
					return;
				}
			} else {
				ReleaseLuaAccess(state);
				return;
			}
		}

		if (lua_istable(state->L, -1)) {
			lua_pushnil(state->L);  // initial key for lua_next
			while (lua_next(state->L, -2)) {
				// Stack: [..., table, key, value]
				KitsuneVariable k = {}, v = {};
				FillKitsuneVariableFromStack(state->L, -2, &k);
				FillKitsuneVariableFromStack(state->L, -1, &v);
				callback(&k, &v, userdata);
				if (k.type == LUA_TSTRING && k.data) gff_free(k.data);
				if (v.type == LUA_TSTRING && v.data) gff_free(v.data);
				lua_pop(state->L, 1);  // pop value, keep key for next iteration
			}
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
			FillKitsuneVariableFromStack(L, i + 1, &args[i]);

		// Clear the stack so LuaResultSetter pushes results onto a clean base.
		lua_settop(L, 0);

		int rc = func(argc, args, LuaResultSetter, userdata);
		state->DelegateState = prevDelegateState;  // restore; handles nesting correctly

		// Free args before any potential lua_error so we never leak them on the error path.
		for (int i = 0; i < argc; i++) {
			if (args[i].type == LUA_TSTRING && args[i].data)
				gff_free(args[i].data);
		}
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
		if (!state || !state->L || !name || !func) return;
		if (state->DelegateState) return;  // called from within a registered function; would deadlock
		AcquireLuaAccess(state);

		// Get or create the Kitsune table so functions are called as Kitsune.Name().
		lua_getglobal(state->L, "Kitsune");
		if (!lua_istable(state->L, -1)) {
			lua_pop(state->L, 1);
			lua_newtable(state->L);
			lua_pushvalue(state->L, -1);  // dup: one for setglobal, one stays on stack
			lua_setglobal(state->L, "Kitsune");
		}
		// Stack: [Kitsune table]
		lua_pushlightuserdata(state->L, (void*)func);
		lua_pushlightuserdata(state->L, userdata);
		lua_pushcclosure(state->L, LuaCFunctionWrapper, 2);
		lua_setfield(state->L, -2, name);  // Kitsune[name] = closure
		lua_pop(state->L, 1);  // pop the Kitsune table

		ReleaseLuaAccess(state);
	}

	KITSUNE_API void KitsuneCleanup() {
		KitsuneState* state = g_state;
		g_state = nullptr;

		if (state) {
			// Signal the scheduler to exit and wait for it to finish.
			if (state->schedulerThread) {
				// Interrupt any running coroutines first so the scheduler is not stuck inside
				// lua_resume waiting for a script to yield; the Ticker will call luaL_error at
				// the next instruction boundary, unblocking the scheduler promptly.
				InterlockedExchange(&state->interrupt, 1);
				InterlockedExchange(&state->schedulerStop, 1);
				SetEvent(state->resumeEvent);   // unblock scheduler if it is in the pause handler
				SetEvent(state->workEvent);     // wake scheduler if it is sleeping
				WaitForSingleObject(state->schedulerThread, INFINITE);
				CloseHandle(state->schedulerThread);
			}

			// Free all slot pointers. Slots with id==0 are already zeroed; only active slots need resource cleanup.
			for (int i = 0; i < state->slotCount; i++) {
				KitsuneCoroutine* slot = state->slots[i];
				if (slot->id != 0) {
					if (state->L) {
						if (slot->argsRef != LUA_NOREF) luaL_unref(state->L, LUA_REGISTRYINDEX, slot->argsRef);
						if (slot->threadRef != LUA_NOREF) luaL_unref(state->L, LUA_REGISTRYINDEX, slot->threadRef);
					}
					gff_free(slot->error);
					if (slot->result.type == LUA_TSTRING && slot->result.data)
						gff_free(slot->result.data);
				}
				gff_free(slot);
			}
			state->slotCount = 0;

			if (state->L) {
				if (state->lastCallError) {
					gff_free(state->lastCallError);
					state->lastCallError = nullptr;
				}
				if (state->varsRef != LUA_NOREF) {
					luaL_unref(state->L, LUA_REGISTRYINDEX, state->varsRef);
					state->varsRef = LUA_NOREF;
				}
				GetHttpBuffer(0);
				luaserver_KillAll(state->L);
				lua_gc(state->L, LUA_GCCOLLECT, 0);
				lua_close(state->L);
				state->L = nullptr;
			}

			if (state->pausedEvent) CloseHandle(state->pausedEvent);
			if (state->resumeEvent) CloseHandle(state->resumeEvent);
			if (state->workEvent)   CloseHandle(state->workEvent);
			DeleteCriticalSection(&state->accessLock);
			DeleteCriticalSection(&state->slotsLock);
			gff_free(state);
		}

		ERR_free_strings();
		EVP_cleanup();
		WSACleanup();
		EndMemoryManager();
		if (g_coOwned) { CoUninitialize(); g_coOwned = false; }
	}

} // extern "C"
