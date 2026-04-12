#include <Windows.h>
#include "dllmain.h"
SQLITE_EXTENSION_INIT1

#define KITSUNE_EXTENSION_VERSION "1.0.0.0"

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
	case DLL_PROCESS_ATTACH:
		DisableThreadLibraryCalls(hModule);
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
		// Special, this isnt related to the kitsune engine.
		sqlite3_create_function(db, "KitsuneVersion", 0, SQLITE_UTF8, NULL, kitsune_version_func, NULL, NULL);
		return SQLITE_OK;
	}

	// Generic fallback name used by older SQLite builds when the derived name is not found.
	__declspec(dllexport) int sqlite3_extension_init(sqlite3* db, char** pzErrMsg, const sqlite3_api_routines* pApi) {
		return sqlite3_sqlitekitsune_init(db, pzErrMsg, pApi);
	}

#ifdef __cplusplus
}
#endif
