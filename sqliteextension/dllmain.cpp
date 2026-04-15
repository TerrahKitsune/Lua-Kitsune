#include <Windows.h>
#include "dllmain.h"
#include "luafunctions.h"
#include "sqlitefunctions.h"
SQLITE_EXTENSION_INIT1

#define KITSUNE_EXTENSION_VERSION "1.0.0.0"

// True only when this DLL called KitsuneInit and created the engine state.
// False when the engine was already initialised by the host before this DLL loaded,
// in which case the host owns the lifecycle and KitsuneCleanup must not be called here.
static bool g_kitsuneOwned = false;

// Module handle for this DLL, captured in DllMain for later path resolution.
static HMODULE g_hModule = NULL;

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
		g_hModule = hModule;
		// Initialise only the extension state (malloc only — no KitsuneEngine calls).
		// Engine init is deferred to sqlite3_sqlitekitsune_init so it runs outside
		// the loader lock.
		lua_init_kitsune_state();
		break;
	case DLL_PROCESS_DETACH:
		// lpReserved is non-NULL when the DLL is unloaded due to process exit: all
		// threads may already be terminated, so waiting on schedulerDoneEvent inside
		// KitsuneCleanup would hang forever. Skip cleanup; the OS reclaims resources.
		if (lpReserved == NULL) {
			// Free all registered Lua function refs before KitsuneCleanup so the
			// deferred luaL_unref queue is drained before lua_close destroys the state.
			lua_cleanup_kitsune_state();
			if (g_kitsuneOwned)
				KitsuneCleanup();
		}
		break;
	}
	return TRUE;
}

// -- SQLite extension functions -----------------------------------------------

static void kitsune_version_func(sqlite3_context* context, int argc, sqlite3_value** argv) {
	sqlite3_result_text(context, KITSUNE_EXTENSION_VERSION, -1, SQLITE_STATIC);
}

#ifdef __cplusplus
extern "C" {
#endif

	__declspec(dllexport) int sqlite3_sqlitekitsune_init(sqlite3* db, char** pzErrMsg, const sqlite3_api_routines* pApi) {
		SQLITE_EXTENSION_INIT2(pApi);

		// Insert this DLL's directory into the DLL search path so that
		// KitsuneEngine.dll and its dependencies are found via the delay-load.
		// Called here (not DllMain) so we are outside the loader lock.
		static bool s_pathSet = false;
		if (!s_pathSet) {
			s_pathSet = true;
			wchar_t szDir[MAX_PATH] = {};
			if (g_hModule && GetModuleFileNameW(g_hModule, szDir, MAX_PATH)) {
				wchar_t* lastSlash = wcsrchr(szDir, L'\\');
				if (lastSlash)
					*lastSlash = L'\0';
				if (szDir[0])
					SetDllDirectoryW(szDir);
			}
		}

		// KitsuneInit returns true only if it created a new state (we are the owner).
		// It returns false if the engine was already initialised by another caller.
		if (!g_kitsuneOwned)
			g_kitsuneOwned = KitsuneInit();

		// Special, this isnt related to the kitsune engine.
		sqlite3_create_function(db, "KitsuneVersion", 0, SQLITE_UTF8 | SQLITE_DIRECTONLY, NULL, kitsune_version_func, NULL, NULL);

		if (lua_register_kitsune_functions(db, pzErrMsg) != SQLITE_OK) {
			return SQLITE_ERROR;
		}

		if (sqlite_register_kitsune_functions(db, pzErrMsg) != SQLITE_OK) {
			return SQLITE_ERROR;
		}

		return SQLITE_OK;
	}

	// Generic fallback name used by older SQLite builds when the derived name is not found.
	__declspec(dllexport) int sqlite3_extension_init(sqlite3* db, char** pzErrMsg, const sqlite3_api_routines* pApi) {
		return sqlite3_sqlitekitsune_init(db, pzErrMsg, pApi);
	}

#ifdef __cplusplus
}
#endif
