#pragma once
#include "dllmain.h"

// Allocates the process-wide extension state. Call from DLL_PROCESS_ATTACH.
void lua_init_kitsune_state();

// Frees all registered Lua function refs and the extension state.
// Must be called before KitsuneCleanup so the deferred luaL_unref queue drains before lua_close.
// Call from DLL_PROCESS_DETACH (only when lpReserved == NULL).
void lua_cleanup_kitsune_state();

int lua_register_kitsune_functions(sqlite3* db, char** pzErrMsg);
