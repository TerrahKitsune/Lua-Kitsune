#pragma once
#include "dllmain.h"

// Allocates the process-wide extension state. Call from DLL_PROCESS_ATTACH.
void lua_init_kitsune_state();

// Frees all registered Lua function refs and the extension state.
// Must be called before KitsuneCleanup so the deferred luaL_unref queue drains before lua_close.
// Call from DLL_PROCESS_DETACH (only when lpReserved == NULL).
void lua_cleanup_kitsune_state();

// Anchors var in the Lua registry and registers it with g_extState for cleanup on DLL unload.
// Returns the anchored KitsuneVariable* on success; NULL on failure.
// The caller must NOT free the returned pointer — it is owned by g_extState.
KitsuneVariable* lua_add_kitsune_state(const KitsuneVariable* var);

int lua_register_kitsune_functions(sqlite3* db, char** pzErrMsg);
