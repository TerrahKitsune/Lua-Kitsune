#include "Windows.h"
#include "SQLite/sqlite3ext.h"
#include "lua_main_incl.h"
#include "dlllua.h"
SQLITE_EXTENSION_INIT1

static void* l_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    return sqlite3_realloc(ptr, nsize);
}

static void executeluastring(sqlite3_context* context, int argc, sqlite3_value** argv) {
	lua_State* L = (lua_State*)sqlite3_user_data(context);

	if (argc == 1 && sqlite3_value_type(argv[0]) == SQLITE_TEXT) {

		const char* script = (const char*)sqlite3_value_text(argv[0]);
		if (luaL_loadstring(L, script) != LUA_OK) {
			script = lua_tostring(L, -1);
			lua_pop(L, 1);
			sqlite3_result_error(context, script, -1);
			return;
		}

		if (lua_pcall(L, 0, 1, 0) != LUA_OK) {
			script = lua_tostring(L, -1);
			lua_pop(L, 1);
			sqlite3_result_error(context, script, -1);
			return;
		}

		if (lua_type(L, -1) == LUA_TNIL) {
			sqlite3_result_null(context);
			lua_pop(L, 1);
			return;
		}

		size_t len;
		script = luaL_tolstring(L, -1, &len);

		lua_pop(L, 2);

		sqlite3_result_text(context, script, len, SQLITE_TRANSIENT);
	}
	else {
		sqlite3_result_null(context);
	}
}

extern "C" __declspec(dllexport)
int sqlite3_sqlitekitsune_init(sqlite3* db, char** pzErrMsg, const sqlite3_api_routines* pApi) {
    SQLITE_EXTENSION_INIT2(pApi);
    lua_State*L = OpenLuaState(l_alloc);

    sqlite3_create_function(db, "Lua", 1, SQLITE_UTF8, L, executeluastring, NULL, NULL);

    return SQLITE_OK;
}

extern "C" __declspec(dllexport)
int sqlite3_extension_init(sqlite3* db, char** pzErrMsg, const sqlite3_api_routines* pApi) {
	return sqlite3_sqlitekitsune_init(db, pzErrMsg, pApi);
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

