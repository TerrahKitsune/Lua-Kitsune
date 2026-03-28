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
	lua_State*    thread;       // cached lua_State*; valid iff threadRef != LUA_NOREF.
	                            // Must be NULLed whenever threadRef is unref'd.
	                            // Written only under AcquireLuaAccess; read only by the scheduler.
	int           argsRef;      // LUA_REGISTRYINDEX anchor for the ARGS table; retrieved by GetArgs()
	volatile LONG fireAndForget;
	volatile LONG done;         // 0 = still running / yielded, 1 = finished
	volatile LONG released;     // 1 = slot should be freed; scheduler zeros it on next compaction
	char*         error;
	char*         result;
	size_t        resultLen;
	double        sleepUntil;   // GetCounter deadline (ms) before which the coroutine must not be resumed; 0 = not sleeping
};

#define KITSUNE_MAX_COROUTINES 256

// ── Engine state ──────────────────────────────────────────────────────────────
struct KitsuneState {
	// ── Lua ──────────────────────────────────────────────────────────────────
	lua_State*   L;
	double       PCFreq;
	__int64      CounterStart;

	// ── Interrupt / pause ────────────────────────────────────────────────────
	volatile LONG interrupt;     // set by KitsuneInterrupt; cleared by scheduler when all done
	volatile LONG pauseFlag;     // set by AcquireLuaAccess; serviced by hook + scheduler
	HANDLE        pausedEvent;   // hook signals this when it parks
	HANDLE        resumeEvent;   // AcquireLuaAccess signals this to let hook continue

	// ── SetVariable/GetVariable serialisation ────────────────────────────────
	CRITICAL_SECTION accessLock; // serialises concurrent external callers

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

static void SetSlotResult(KitsuneCoroutine* slot, const char* data, size_t len) {
	gff_free(slot->result);
	slot->result    = NULL;
	slot->resultLen = 0;
	if (data && len > 0) {
		slot->result = (char*)gff_malloc(len);
		if (slot->result) {
			memcpy(slot->result, data, len);
			slot->resultLen = len;
		}
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

static void Ticker(lua_State *L, lua_Debug *ar) {
	void* ud;
	lua_getallocf(L, &ud);
	KitsuneState* state = (KitsuneState*)ud;

	// Do not auto-clear interrupt here; the scheduler clears it once all coroutines are done.
	if (InterlockedAdd(&state->interrupt, 0)) {
		luaL_error(L, "interrupted");
		return;
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

static void FinishCoroutine(KitsuneState* state, KitsuneCoroutine* slot, lua_State* T, int rc, int nresults) {
	if (rc == LUA_OK) {
		if (nresults > 0 && !lua_isnil(T, 1)) {
			size_t len;
			const char* s = luaL_tolstring(T, 1, &len);
			SetSlotResult(slot, s, len);
			lua_pop(T, 1);  // pop luaL_tolstring's pushed string rep
		}
	} else {
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
		} else {
			// ── Step 2: Resume each active coroutine once ─────────────────────
			for (int i = 0; i < state->slotCount; i++) {
				KitsuneCoroutine* slot = state->slots[i];
				if (slot->id == 0 || InterlockedAdd(&slot->done, 0)) continue;
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

				int nresults = 0;
				InterlockedExchange(&state->currentCoroutineId, (LONG)slot->id);
				int rc = lua_resume(T, state->L, 0, &nresults);
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
			EnterCriticalSection(&state->slotsLock);
			for (int i = 0; i < state->slotCount; i++) {
				KitsuneCoroutine* slot = state->slots[i];
				if (slot->id != 0 && InterlockedAdd(&slot->done, 0) && InterlockedAdd(&slot->released, 0)) {
					if (slot->argsRef   != LUA_NOREF) luaL_unref(state->L, LUA_REGISTRYINDEX, slot->argsRef);
					if (slot->threadRef != LUA_NOREF) luaL_unref(state->L, LUA_REGISTRYINDEX, slot->threadRef);
					slot->thread = NULL;  // invariant: null before memset so the pointer is never stale
					gff_free(slot->error);
					gff_free(slot->result);
					memset(slot, 0, sizeof(KitsuneCoroutine));  // id = 0 marks the slot as reusable
				}
			}
			LeaveCriticalSection(&state->slotsLock);
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

static int L_GetRuntime(lua_State *L) {
	void* ud;
	lua_getallocf(L, &ud);
	KitsuneState* state = (KitsuneState*)ud;
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
	} else {
		return gff_realloc(ptr, nsize);
	}
}

// ============================================================
// Exported API
// ============================================================

static KitsuneState* g_state    = nullptr;
static bool          g_coOwned  = false;

extern "C" {

KITSUNE_API lua_State* KitsuneInit() {
	if (g_state)
		return g_state->L;

	// RPC_E_CHANGED_MODE means COM was already initialised by the host (e.g. .NET's
	// MTA thread pool).  We can still use COM; we just must not call CoUninitialize.
	HRESULT cohr = CoInitialize(NULL);
	if (FAILED(cohr) && cohr != RPC_E_CHANGED_MODE)
		return NULL;
	g_coOwned = SUCCEEDED(cohr);

	InitMemoryManager();

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		if (g_coOwned) CoUninitialize();
		return NULL;
	}

	SSL_load_error_strings();
	SSL_library_init();
	OpenSSL_add_all_algorithms();

	KitsuneState* state = (KitsuneState*)gff_malloc(sizeof(KitsuneState));
	memset(state, 0, sizeof(KitsuneState));
	state->pausedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	state->resumeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	state->workEvent   = CreateEvent(NULL, FALSE, FALSE, NULL);
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
		return NULL;
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
		return NULL;
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
		return NULL;
	}

	g_state = state;
	return state->L;
}

// ── Start a coroutine directly: acquire Lua access, create the thread, hand it to the scheduler ─
static int StartCoroutine(KitsuneState* state, bool isFile,
						  const char* source, int argc, const char** argv,
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

	slot->threadRef    = LUA_NOREF;
	slot->argsRef      = LUA_NOREF;
	slot->fireAndForget = fireAndForget ? 1 : 0;

	// Create the coroutine thread and anchor it so the GC cannot collect it.
	lua_State* T    = lua_newthread(state->L);
	slot->thread    = T;
	slot->threadRef = luaL_ref(state->L, LUA_REGISTRYINDEX);
	lua_sethook(T, Ticker, LUA_MASKCOUNT, 1000);

	// Build the ARGS table and anchor it in the registry.
	// GetArgs() retrieves it via the current coroutine ID and slot->argsRef.
	// file:   ARGS[1]=path, ARGS[2..]=argv[2..]
	// string: ARGS[1..]=argv[1..]
	lua_newtable(state->L);
	if (isFile) {
		lua_pushstring(state->L, source);
		lua_rawseti(state->L, -2, 1);
		for (int n = 2; argv && n < argc; n++) {
			lua_pushstring(state->L, argv[n]);
			lua_rawseti(state->L, -2, n);
		}
	} else {
		for (int n = 1; argv && n < argc; n++) {
			lua_pushstring(state->L, argv[n]);
			lua_rawseti(state->L, -2, n);
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
		lua_settop(T, 0);
		luaL_unref(state->L, LUA_REGISTRYINDEX, slot->argsRef);   slot->argsRef   = LUA_NOREF;
		luaL_unref(state->L, LUA_REGISTRYINDEX, slot->threadRef); slot->threadRef = LUA_NOREF;
		slot->thread = NULL;  // invariant: thread is only valid while threadRef != LUA_NOREF
		InterlockedExchange(&slot->done, 1);
		if (InterlockedAdd(&slot->fireAndForget, 0))
			InterlockedExchange(&slot->released, 1);
	} else {
		InterlockedIncrement(&state->runningCount);
	}

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

KITSUNE_API int KitsuneExecuteFile(const char* path, int argc, const char** argv, bool fireAndForget) {
	return StartCoroutine(g_state, true, path, argc, argv, fireAndForget);
}

KITSUNE_API int KitsuneExecuteString(const char* script, int argc, const char** argv, bool fireAndForget) {
	return StartCoroutine(g_state, false, script, argc, argv, fireAndForget);
}

KITSUNE_API const char* KitsuneGetError(int id) {
	KitsuneState* state = g_state;
	if (!state) return NULL;
	EnterCriticalSection(&state->slotsLock);
	KitsuneCoroutine* slot = FindSlot(state, id);
	const char* err = slot ? slot->error : NULL;
	LeaveCriticalSection(&state->slotsLock);
	return err;
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
	if (len) *len = (done && slot) ? slot->resultLen : 0;
	LeaveCriticalSection(&state->slotsLock);
	return done;
}

KITSUNE_API size_t KitsuneGetResult(int id, char* buffer, size_t bufferSize) {
	KitsuneState* state = g_state;
	if (!state) return 0;

	EnterCriticalSection(&state->slotsLock);
	KitsuneCoroutine* slot = FindSlot(state, id);
	if (!slot || !InterlockedAdd(&slot->done, 0)) {
		LeaveCriticalSection(&state->slotsLock);
		return 0;
	}

	// Claim the result and mark the slot for release in one step under the lock.
	char* result    = (char*)InterlockedExchangePointer((PVOID*)&slot->result, NULL);
	size_t len      = slot->resultLen;
	slot->resultLen = 0;
	InterlockedExchange(&slot->released, 1);  // scheduler frees the slot on next compaction
	LeaveCriticalSection(&state->slotsLock);

	if (!result) return 0;

	if (buffer && bufferSize > 0) {
		size_t copy = len < bufferSize ? len : bufferSize;
		memcpy(buffer, result, copy);
		if (copy < bufferSize)
			buffer[copy] = '\0';
	}

	gff_free(result);
	return len;
}

KITSUNE_API int KitsuneIsRunning() {
	KitsuneState* state = g_state;
	if (!state) return 0;

	// Fast path: a coroutine is actively inside lua_resume right now.
	int active = (int)InterlockedAdd(&state->currentCoroutineId, 0);
	if (active) return active;

	// Slow path: scan for any non-done slot (coroutines that are yielded but not yet finished).
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

KITSUNE_API bool KitsuneSetString(const char* name, const char* value, size_t length) {
	KitsuneState* state = g_state;
	if (!state || !state->L || !name) return false;
	AcquireLuaAccess(state);
	lua_getglobal(state->L, "Vars");
	if (!lua_istable(state->L, -1)) {
		lua_pop(state->L, 1);
		lua_newtable(state->L);
		lua_pushvalue(state->L, -1);
		lua_setglobal(state->L, "Vars");
	}
	if (value)
		lua_pushlstring(state->L, value, length);
	else
		lua_pushnil(state->L);
	lua_setfield(state->L, -2, name);
	lua_pop(state->L, 1);
	ReleaseLuaAccess(state);
	return true;
}

KITSUNE_API bool KitsuneSetBool(const char* name, bool value) {
	KitsuneState* state = g_state;
	if (!state || !state->L || !name) return false;
	AcquireLuaAccess(state);
	lua_getglobal(state->L, "Vars");
	if (!lua_istable(state->L, -1)) {
		lua_pop(state->L, 1);
		lua_newtable(state->L);
		lua_pushvalue(state->L, -1);
		lua_setglobal(state->L, "Vars");
	}
	lua_pushboolean(state->L, value ? 1 : 0);
	lua_setfield(state->L, -2, name);
	lua_pop(state->L, 1);
	ReleaseLuaAccess(state);
	return true;
}

KITSUNE_API bool KitsuneSetNumber(const char* name, double value) {
	KitsuneState* state = g_state;
	if (!state || !state->L || !name) return false;
	AcquireLuaAccess(state);
	lua_getglobal(state->L, "Vars");
	if (!lua_istable(state->L, -1)) {
		lua_pop(state->L, 1);
		lua_newtable(state->L);
		lua_pushvalue(state->L, -1);
		lua_setglobal(state->L, "Vars");
	}
	lua_Integer intVal = (lua_Integer)value;
	if (value >= (double)LUA_MININTEGER && value <= (double)LUA_MAXINTEGER && (double)intVal == value)
		lua_pushinteger(state->L, intVal);
	else
		lua_pushnumber(state->L, value);
	lua_setfield(state->L, -2, name);
	lua_pop(state->L, 1);
	ReleaseLuaAccess(state);
	return true;
}

KITSUNE_API size_t KitsuneGetVariable(const char* name, char* buffer, size_t bufferSize) {
	KitsuneState* state = g_state;
	if (!state || !state->L || !name) return 0;
	AcquireLuaAccess(state);

	size_t result = 0;
	lua_getglobal(state->L, "Vars");    // +1: Vars (or nil)
	if (lua_istable(state->L, -1)) {
		lua_getfield(state->L, -1, name);   // +1: Vars[name]
		if (!lua_isnil(state->L, -1)) {
			size_t len;
			const char* s = luaL_tolstring(state->L, -1, &len);  // +1: string rep
			result = len;
			if (buffer && bufferSize > 0) {
				size_t copy = len < bufferSize - 1 ? len : bufferSize - 1;
				memcpy(buffer, s, copy);
				buffer[copy] = '\0';
			}
			lua_pop(state->L, 1);               // pop string rep
		}
		lua_pop(state->L, 1);               // pop Vars[name]
	}
	lua_pop(state->L, 1);               // pop Vars
	ReleaseLuaAccess(state);
	return result;
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
					if (slot->argsRef   != LUA_NOREF) luaL_unref(state->L, LUA_REGISTRYINDEX, slot->argsRef);
					if (slot->threadRef != LUA_NOREF) luaL_unref(state->L, LUA_REGISTRYINDEX, slot->threadRef);
				}
				gff_free(slot->error);
				gff_free(slot->result);
			}
			gff_free(slot);
		}
		state->slotCount = 0;

		if (state->L) {
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
