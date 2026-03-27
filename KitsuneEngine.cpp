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

struct KitsuneState {
	lua_State*       L;
	int              hook;
	double           ticktime;
	double           PCFreq;
	__int64          CounterStart;
	double           TickPCFreq;
	__int64          TickCounterStart;
	char*            lastError;
	char*            lastResult;
	size_t           lastResultLen;
	volatile LONG    running;
	volatile LONG    interrupt;
	volatile LONG    pauseFlag;
	HANDLE           pausedEvent;
	HANDLE           resumeEvent;
	CRITICAL_SECTION accessLock;
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

static void TickStartCounter(KitsuneState* state) {
	LARGE_INTEGER li;
	QueryPerformanceFrequency(&li);
	state->TickPCFreq = double(li.QuadPart) / 1000.0;
	QueryPerformanceCounter(&li);
	state->TickCounterStart = li.QuadPart;
}

static double TickGetCounter(KitsuneState* state) {
	LARGE_INTEGER li;
	QueryPerformanceCounter(&li);
	return double(li.QuadPart - state->TickCounterStart) / state->TickPCFreq;
}

// Frees any existing error and optionally stores a new one.
// Pass NULL to clear without setting a new message.
static void SetError(KitsuneState* state, const char* msg) {
	gff_free(state->lastError);
	state->lastError = NULL;
	if (msg) {
		size_t len = strlen(msg);
		state->lastError = (char*)gff_malloc(len + 1);
		if (state->lastError)
			memcpy(state->lastError, msg, len + 1);
	}
}

static void SetResult(KitsuneState* state, const char* data, size_t len) {
	gff_free(state->lastResult);
	state->lastResult    = NULL;
	state->lastResultLen = 0;
	if (data && len > 0) {
		state->lastResult = (char*)gff_malloc(len);
		if (state->lastResult) {
			memcpy(state->lastResult, data, len);
			state->lastResultLen = len;
		}
	}
}

// Acquire exclusive access to the Lua state.
// If running, requests a pause and waits for the ticker to park.
// accessLock is held on return; caller MUST call ReleaseLuaAccess.
static void AcquireLuaAccess(KitsuneState* state) {
	EnterCriticalSection(&state->accessLock);
	if (InterlockedAdd(&state->running, 0)) {
		InterlockedExchange(&state->pauseFlag, 1);
		WaitForSingleObject(state->pausedEvent, INFINITE);
	}
}

static void ReleaseLuaAccess(KitsuneState* state) {
	if (InterlockedAdd(&state->pauseFlag, 0)) {
		InterlockedExchange(&state->pauseFlag, 0);
		SetEvent(state->resumeEvent);
	}
	LeaveCriticalSection(&state->accessLock);
}

// Service any pending pause requests before clearing running.
// Loops until no more accessors are waiting.
static void ServicePauseRequests(KitsuneState* state) {
	while (InterlockedAdd(&state->pauseFlag, 0)) {
		SetEvent(state->pausedEvent);
		WaitForSingleObject(state->resumeEvent, INFINITE);
	}
}

static void L_Ticker(lua_State *L, lua_Debug *ar) {
	void* ud;
	lua_getallocf(L, &ud);
	KitsuneState* state = (KitsuneState*)ud;

	if (InterlockedCompareExchange(&state->interrupt, 0, 1) != 0) {
		luaL_error(L, "interrupted");
		return;
	}

	if (InterlockedAdd(&state->pauseFlag, 0)) {
		SetEvent(state->pausedEvent);
		WaitForSingleObject(state->resumeEvent, INFINITE);
		if (InterlockedCompareExchange(&state->interrupt, 0, 1) != 0) {
			luaL_error(L, "interrupted");
			return;
		}
	}

	if (state->hook == -1)
		return;
	else if (TickGetCounter(state) < state->ticktime)
		return;
	else
		TickStartCounter(state);

	lua_rawgeti(L, LUA_REGISTRYINDEX, state->hook);
	if (lua_isfunction(L, 1)) {
		lua_pcall(L, 0, 1, NULL);
	}
	lua_pop(L, lua_gettop(L));
}

static int L_SetTick(lua_State *L) {
	void* ud;
	lua_getallocf(L, &ud);
	KitsuneState* state = (KitsuneState*)ud;

	if (!lua_isfunction(L, 1)) {
		lua_sethook(L, L_Ticker, LUA_MASKCOUNT, 1000);
		if (state->hook != -1)
			luaL_unref(L, LUA_REGISTRYINDEX, state->hook);
		state->hook = -1;
		return 0;
	}

	lua_pushvalue(L, 1);
	state->hook = luaL_ref(L, LUA_REGISTRYINDEX);
	state->ticktime = luaL_optnumber(L, 2, 1000.0);
	if (state->ticktime < 0.0)
		state->ticktime = 0.0;
	lua_pop(L, lua_gettop(L));
	TickStartCounter(state);

	int maskcnt = (int)state->ticktime;
	if (maskcnt <= 0) maskcnt = 1;
	else if (maskcnt > 1000) maskcnt = 1000;

	lua_sethook(L, L_Ticker, 0xFFFFFFFF, maskcnt);
	return 0;
}

static int L_GetRuntime(lua_State *L) {
	void* ud;
	lua_getallocf(L, &ud);
	KitsuneState* state = (KitsuneState*)ud;
	lua_pushnumber(L, GetCounter(state));
	return 1;
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
	state->hook = -1;
	state->pausedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	state->resumeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (!state->pausedEvent || !state->resumeEvent) {
		if (state->pausedEvent) CloseHandle(state->pausedEvent);
		if (state->resumeEvent) CloseHandle(state->resumeEvent);
		gff_free(state);
		ERR_free_strings();
		EVP_cleanup();
		WSACleanup();
		EndMemoryManager();
		if (g_coOwned) CoUninitialize();
		return NULL;
	}
	InitializeCriticalSection(&state->accessLock);
	StartCounter(state);

	state->L = lua_newstate(l_alloc, state);
	if (!state->L) {
		if (state->pausedEvent)
			CloseHandle(state->pausedEvent);
		if (state->resumeEvent)
			CloseHandle(state->resumeEvent);
		DeleteCriticalSection(&state->accessLock);
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
	lua_pushcfunction(L, L_SetTick);       lua_setglobal(L, "SetTicker");
	lua_pushcfunction(L, L_ShellExecute);  lua_setglobal(L, "ShellExecute");
	lua_pushcfunction(L, L_GetMemory);     lua_setglobal(L, "GetMemory");
	lua_pushcfunction(L, L_GetTextColor);  lua_setglobal(L, "GetTextColor");
	lua_pushcfunction(L, L_SetTextColor);  lua_setglobal(L, "SetTextColor");
	lua_pushcfunction(L, L_getch);         lua_setglobal(L, "GetKey");
	lua_pushcfunction(L, L_kbhit);         lua_setglobal(L, "HasKeyDown");
	lua_pushcfunction(L, L_put);           lua_setglobal(L, "Put");
	lua_pushcfunction(L, L_cls);           lua_setglobal(L, "CLS");

	luaopen_misc(L);

	lua_newtable(L);
	lua_setglobal(L, "Vars");

	lua_sethook(L, L_Ticker, LUA_MASKCOUNT, 1000);
	g_state = state;
	return state->L;
}

KITSUNE_API int KitsuneExecuteFile(const char* path, int argc, const char** argv) {

	KitsuneState* state = g_state;
	if (!state || !state->L) return -1;
	lua_State* L = state->L;

	if (InterlockedCompareExchange(&state->running, 1, 0) != 0)
		return -1;
	InterlockedExchange(&state->interrupt, 0);
	lua_settop(L, 0);
	lua_newtable(L);
	lua_pushstring(L, path);
	lua_rawseti(L, -2, 1);
	for (int n = 2; argv && n < argc; n++) {
		lua_pushstring(L, argv[n]);
		lua_rawseti(L, -2, n);
	}
	lua_setglobal(L, "ARGS");
	SetConsoleTitle(path);
	int ret = 0;

	SetError(state, NULL);
	SetResult(state, NULL, 0);
	if (luaL_loadfile(L, path) != 0 || lua_pcall(L, 0, 1, NULL) != 0) {
		SetError(state, lua_tolstring(L, -1, NULL));
		lua_pop(L, 1);
	}

	ServicePauseRequests(state);

	if (lua_isnumber(L, -1))
		ret = (int)lua_tointeger(L, -1);

	if (lua_gettop(L) > 0 && !lua_isnil(L, -1)) {
		size_t len;
		const char* s = luaL_tolstring(L, -1, &len);
		SetResult(state, s, len);
		lua_pop(L, 1);
	}

	lua_settop(L, 0);

	ServicePauseRequests(state);
	InterlockedExchange(&state->running, 0);
	ServicePauseRequests(state);
	return ret;
}

KITSUNE_API int KitsuneExecuteString(const char* script, int argc, const char** argv) {

	KitsuneState* state = g_state;
	if (!state || !state->L) return -1;
	lua_State* L = state->L;

	if (InterlockedCompareExchange(&state->running, 1, 0) != 0)
		return -1;
	InterlockedExchange(&state->interrupt, 0);
	lua_settop(L, 0);
	lua_newtable(L);
	for (int n = 1; argv && n < argc; n++) {
		lua_pushstring(L, argv[n]);
		lua_rawseti(L, -2, n);
	}
	lua_setglobal(L, "ARGS");
	SetError(state, NULL);
	SetResult(state, NULL, 0);
	int error = luaL_loadbuffer(L, script, strlen(script), "string") ||
				lua_pcall(L, 0, 1, 0);
	if (error) {
		SetError(state, lua_tolstring(L, -1, NULL));
		lua_pop(L, 1);
		ServicePauseRequests(state);
		InterlockedExchange(&state->running, 0);
		ServicePauseRequests(state);
		return -1;
	}
	if (lua_gettop(L) > 0 && !lua_isnil(L, -1)) {
		size_t len;
		const char* s = luaL_tolstring(L, -1, &len);
		SetResult(state, s, len);
		lua_pop(L, 1);
	}
	lua_settop(L, 0);
	ServicePauseRequests(state);
	InterlockedExchange(&state->running, 0);
	ServicePauseRequests(state);
	return 0;
}

KITSUNE_API const char* KitsuneGetError() {
	if (!g_state) return NULL;
	return g_state->lastError;
}

KITSUNE_API size_t KitsuneHasResult() {
	KitsuneState* state = g_state;
	if (!state) return 0;
	return state->lastResult ? state->lastResultLen : 0;
}

KITSUNE_API size_t KitsuneGetResult(char* buffer, size_t bufferSize) {
	KitsuneState* state = g_state;
	if (!state) return 0;

	while (InterlockedAdd(&state->running, 0))
		Sleep(1);

	// Atomically claim the result so concurrent callers can't double-read it
	char* result = (char*)InterlockedExchangePointer((PVOID*)&state->lastResult, NULL);
	if (!result) return 0;

	size_t len = state->lastResultLen;
	state->lastResultLen = 0;

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
	return InterlockedAdd(&state->running, 0);
}

KITSUNE_API void KitsuneInterrupt() {
	KitsuneState* state = g_state;
	if (state && InterlockedAdd(&state->running, 0))
		InterlockedExchange(&state->interrupt, 1);
}

KITSUNE_API void KitsuneWait() {
	KitsuneState* state = g_state;
	if (!state) return;
	while (InterlockedAdd(&state->running, 0))
		Sleep(1);
}

KITSUNE_API bool KitsuneSetVariable(const char* name, const char* value, size_t length) {
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
		if (state->L) {
			GetHttpBuffer(0);
			luaserver_KillAll(state->L);
			lua_gc(state->L, LUA_GCCOLLECT, 0);
			lua_close(state->L);
			state->L = nullptr;
		}
		SetError(state, NULL);
		SetResult(state, NULL, 0);
		if (state->pausedEvent)
			CloseHandle(state->pausedEvent);
		if (state->resumeEvent)
			CloseHandle(state->resumeEvent);
		DeleteCriticalSection(&state->accessLock);
		gff_free(state);
	}

	ERR_free_strings();
	EVP_cleanup();
	WSACleanup();
	EndMemoryManager();
	if (g_coOwned) { CoUninitialize(); g_coOwned = false; }
}

} // extern "C"
