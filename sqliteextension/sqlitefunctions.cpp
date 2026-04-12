#include "sqlitefunctions.h"
#include <stdio.h>
SQLITE_EXTENSION_INIT3

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

	int luaArgc = argc - 1;
	KitsuneVariable* luaArgs = NULL;

	if (luaArgc > 0) {
		luaArgs = (KitsuneVariable*)sqlite3_malloc((int)(sizeof(KitsuneVariable) * luaArgc));
		if (!luaArgs) {
			sqlite3_result_error_nomem(context);
			return;
		}

		for (int i = 0; i < luaArgc; i++) {
			sqlite3_value* val = argv[i + 1];
			KitsuneVariable* kv = &luaArgs[i];
			kv->length = 0;

			switch (sqlite3_value_type(val)) {
			case SQLITE_INTEGER:
				kv->type = KITSUNE_TINTEGER;
				kv->integer = sqlite3_value_int64(val);
				break;
			case SQLITE_FLOAT:
				kv->type = KITSUNE_TNUMBER;
				kv->number = sqlite3_value_double(val);
				break;
			case SQLITE_TEXT:
				kv->type = KITSUNE_TSTRING;
				kv->data = (unsigned char*)sqlite3_value_text(val);
				kv->length = (size_t)sqlite3_value_bytes(val);
				break;
			case SQLITE_BLOB:
				kv->type = KITSUNE_TSTRING;
				kv->data = (unsigned char*)sqlite3_value_blob(val);
				kv->length = (size_t)sqlite3_value_bytes(val);
				break;
			case SQLITE_NULL:
			default:
				kv->type = KITSUNE_TNIL;
				kv->data = NULL;
				break;
			}
		}
	}

	KitsuneVariable* result = KitsuneExecuteString(script, luaArgc, luaArgs);

	if (luaArgs)
		sqlite3_free(luaArgs);

	if (!result) {
		sqlite3_result_error(context, "LuaString: execution failed", -1);
		return;
	}

	if (result->type == KITSUNE_TERROR) {
		if (result->data && result->length > 0)
			sqlite3_result_error(context, (const char*)result->data, (int)result->length);
		else
			sqlite3_result_error(context, "LuaString: Lua error", -1);
		KitsuneVariableFree(result);
		return;
	}

	switch (result->type) {
		case KITSUNE_TINTEGER:
			sqlite3_result_int64(context, result->integer);
			break;
		case KITSUNE_TNUMBER:
			sqlite3_result_double(context, result->number);
			break;
		case KITSUNE_TSTRING:
			sqlite3_result_text(context, (const char*)result->data, (int)result->length, SQLITE_TRANSIENT);
			break;
		case KITSUNE_TBOOLEAN:
			sqlite3_result_int(context, result->boolean ? 1 : 0);
			break;
		case KITSUNE_TNIL:
		case KITSUNE_TNONE:
		default:
			sqlite3_result_null(context);
		break;
	}

	KitsuneVariableFree(result);
}

int sqlite_register_kitsune_functions(sqlite3* db, char** pzErrMsg) {
	sqlite3_create_function(db, "LuaString", -1, SQLITE_UTF8 | SQLITE_DIRECTONLY, NULL, lua_string_func, NULL, NULL);

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