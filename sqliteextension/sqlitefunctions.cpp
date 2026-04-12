#include "sqlitefunctions.h"
#include <stdio.h>
SQLITE_EXTENSION_INIT3
#include "kitsuneext.h"

// LuaString(script, args...) — compiles and runs a Lua string; extra args available as ARGS[1..n].
static void lua_string_func(sqlite3_context* context, int argc, sqlite3_value** argv) {
	if (argc < 1) {
		sqlite3_result_error(context, "LuaString requires at least one argument (the script)", -1);
		return;
	}
	const char* script = (const char*)sqlite3_value_text(argv[0]);
	if (!script) {
		sqlite3_result_error(context, "LuaString: script argument is NULL", -1);
		return;
	}
	int luaArgc = 0;
	KitsuneVariable* args = sqlite_build_args(context, argc, argv, 1, &luaArgc);
	if (luaArgc < 0) return;
	KitsuneVariable* result = KitsuneExecuteString(script, luaArgc, args);
	if (args) sqlite3_free(args);
	kitsune_result_to_sqlite(context, result);
}

// DoFile(path, args...) — loads and runs a Lua file; extra args available as ARGS[2..n]
// (ARGS[1] is the file path, matching the standard KitsuneExecuteFile convention).
static void lua_file_func(sqlite3_context* context, int argc, sqlite3_value** argv) {
	if (argc < 1) {
		sqlite3_result_error(context, "DoFile requires at least one argument (the file path)", -1);
		return;
	}
	const char* path = (const char*)sqlite3_value_text(argv[0]);
	if (!path) {
		sqlite3_result_error(context, "DoFile: path argument is NULL", -1);
		return;
	}
	int luaArgc = 0;
	KitsuneVariable* args = sqlite_build_args(context, argc, argv, 1, &luaArgc);
	if (luaArgc < 0) return;
	KitsuneVariable* result = KitsuneExecuteFile(path, luaArgc, args);
	if (args) sqlite3_free(args);
	kitsune_result_to_sqlite(context, result);
}

// DoFunction(name, args...) — calls a named Lua global function; args passed as direct parameters.
static void lua_function_func(sqlite3_context* context, int argc, sqlite3_value** argv) {
	if (argc < 1) {
		sqlite3_result_error(context, "DoFunction requires at least one argument (the function name)", -1);
		return;
	}
	const char* name = (const char*)sqlite3_value_text(argv[0]);
	if (!name) {
		sqlite3_result_error(context, "DoFunction: function name argument is NULL", -1);
		return;
	}
	int luaArgc = 0;
	KitsuneVariable* args = sqlite_build_args(context, argc, argv, 1, &luaArgc);
	if (luaArgc < 0) return;
	KitsuneVariable* result = KitsuneExecuteFunction(name, luaArgc, args);
	if (args) sqlite3_free(args);
	kitsune_result_to_sqlite(context, result);
}

int sqlite_register_kitsune_functions(sqlite3* db, char** pzErrMsg) {
	sqlite3_create_function(db, "LuaString", -1, SQLITE_UTF8 | SQLITE_DIRECTONLY, NULL, lua_string_func, NULL, NULL);
	sqlite3_create_function(db, "LuaFile", -1, SQLITE_UTF8 | SQLITE_DIRECTONLY, NULL, lua_file_func, NULL, NULL);
	sqlite3_create_function(db, "LuaFunction", -1, SQLITE_UTF8 | SQLITE_DIRECTONLY, NULL, lua_function_func, NULL, NULL);

	const char* dbPath = sqlite3_db_filename(db, "main");
	if (dbPath && *dbPath) {
		const char* lastSep = NULL;
		for (const char* p = dbPath; *p; p++) {
			if (*p == '/' || *p == '\\')
				lastSep = p;
		}

		char* extPath = lastSep
			? sqlite3_mprintf("%.*sextension.lua", (int)(lastSep + 1 - dbPath), dbPath)
			: sqlite3_mprintf("extension.lua");

		if (extPath) {
			FILE* f = fopen(extPath, "r");
			if (f) {
				fclose(f);
				KitsuneVariable* result = KitsuneExecuteFile(extPath, 0, NULL);
				if (result) {
					if (result->type == KITSUNE_TERROR) {
						if (pzErrMsg) {
							*pzErrMsg = (result->data && result->length > 0)
								? sqlite3_mprintf("%.*s", (int)result->length, (const char*)result->data)
								: sqlite3_mprintf("extension.lua: Lua error");
						}
						KitsuneVariableFree(result);
						sqlite3_free(extPath);
						return SQLITE_ERROR;
					}
					KitsuneVariableFree(result);
				}
			}
			sqlite3_free(extPath);
		}
	}

	return SQLITE_OK;
}