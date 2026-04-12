#include "luafunctions.h"

int lua_register_kitsune_functions(sqlite3* db, char** pzErrMsg) {
	// Register all the Lua functions in this extension.
	// This is called by the main entry point in dllmain.cpp, and can be called by other code if needed.
	return SQLITE_OK;
}