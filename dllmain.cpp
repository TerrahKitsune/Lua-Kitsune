#include "networking.h"
#include <Windows.h>
#include "objbase.h"
#include "SQLite/sqlite3ext.h"
#include "lua_main_incl.h"
#include "dlllua.h"
#include "stream.h"
#include "luawchar.h"
#include "HttpMain.h"
SQLITE_EXTENSION_INIT1

int JsonObjectRef = -1;
lua_State* GlobalState = NULL;

static void* l_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
	return sqlite3_realloc(ptr, nsize);
}

void lua_pushsqlite3value(lua_State* L, sqlite3_value* value) {

	switch (sqlite3_value_type(value)) {
	case SQLITE_INTEGER:
		lua_pushinteger(L, sqlite3_value_int64(value));
		break;
	case SQLITE_FLOAT:
		lua_pushnumber(L, sqlite3_value_double(value));
		break;
	case SQLITE_BLOB:
		lua_pushluastream(L, (const BYTE*)sqlite3_value_blob(value), sqlite3_value_bytes(value));
		break;
	case SQLITE_TEXT:
		lua_pushlstring(L, (const char*)sqlite3_value_text(value), sqlite3_value_bytes(value));
		break;
	default:
		lua_pushnil(L);
		break;
	}
}

void lua_tosqlite3value(lua_State* L, int idx, sqlite3_context* context) {

	size_t len;
	const char* str;

	switch (lua_type(L, idx)) {

	case LUA_TNIL:
		sqlite3_result_null(context);
		break;
	case LUA_TBOOLEAN:
		sqlite3_result_int(context, lua_toboolean(L, idx));
		break;
	case LUA_TNUMBER:
		if (lua_isinteger(L, idx)) {
			sqlite3_result_int64(context, lua_tointeger(L, idx));
		}
		else {
			sqlite3_result_double(context, lua_tonumber(L, idx));
		}
		break;
	case LUA_TSTRING:
		str = lua_tolstring(L, idx, &len);
		if (str) {
			sqlite3_result_text64(context, str, len, SQLITE_TRANSIENT, SQLITE_UTF8);
		}
		else {
			sqlite3_result_null(context);
		}
		break;
	case LUA_TTABLE:
		lua_pushvalue(L, idx);
		lua_rawgeti(L, LUA_REGISTRYINDEX, JsonObjectRef);
		lua_pushstring(L, "Encode");
		lua_gettable(L, -2);
		lua_pushvalue(L, -2);
		lua_pushvalue(L, -4);
		if (lua_pcall(L, 2, 1, NULL)) {
			sqlite3_result_null(context);
		}
		else {
			str = lua_tolstring(L, -1, &len);
			if (str) {
				sqlite3_result_text64(context, str, len, SQLITE_TRANSIENT, SQLITE_UTF8);
			}
			else {
				sqlite3_result_null(context);
			}
		}
		lua_pop(L, 3);
		break;
	case LUA_TUSERDATA:

		if (lua_isstream(L, idx)) {
			LuaStream* stream = lua_toluastream(L, idx);
			if (stream && stream->data) {
				sqlite3_result_blob64(context, stream->data, stream->len, SQLITE_TRANSIENT);
			}
			else {
				sqlite3_result_null(context);
			}
			break;
		}
		else if (lua_iswchar(L, idx)) {
			lua_pushvalue(L, idx);
			ToUtf8(L);
			str = lua_tolstring(L, -1, &len);
			if (str) {
				sqlite3_result_text64(context, str, len, SQLITE_TRANSIENT, SQLITE_UTF8);
			}
			else {
				sqlite3_result_null(context);
			}
			lua_pop(L, 2);
			break;
		}
	// Intentionally fallthrough here.
	default:
		str = luaL_tolstring(L, idx, &len);
		if (str) {
			sqlite3_result_text64(context, str, len, SQLITE_TRANSIENT, SQLITE_UTF8);
		}
		else {
			sqlite3_result_null(context);
		}
		lua_pop(L, 1);
		break;
	}
}

static void executeluastring(sqlite3_context* context, int argc, sqlite3_value** argv) {
	lua_State* L = (lua_State*)sqlite3_user_data(context);
	GlobalState = L;

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

		lua_tosqlite3value(L, -1, context);
		lua_pop(L, 1);
	}
	else {
		sqlite3_result_null(context);
	}
}

extern "C" __declspec(dllexport)
int sqlite3_sqlitekitsune_init(sqlite3* db, char** pzErrMsg, const sqlite3_api_routines* pApi) {
	SQLITE_EXTENSION_INIT2(pApi);
	lua_State* L = OpenLuaState(l_alloc);

	lua_getglobal(L, "Json");
	lua_pushstring(L, "Create");
	lua_gettable(L, -2);

	if (lua_pcall(L, 0, 1, NULL)) {

		*pzErrMsg = sqlite3_mprintf("Failed to load extension: %s", lua_tostring(L, -1));
		lua_pop(L, lua_gettop(L));
		return SQLITE_FAIL;
	}

	JsonObjectRef = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pop(L, 1);

	sqlite3_create_function(db, "ExecuteLuaString", 1, SQLITE_UTF8, L, executeluastring, NULL, NULL);

	return SQLITE_OK;
}

extern "C" __declspec(dllexport)
int sqlite3_extension_init(sqlite3* db, char** pzErrMsg, const sqlite3_api_routines* pApi) {
	return sqlite3_sqlitekitsune_init(db, pzErrMsg, pApi);
}

BOOL APIENTRY DllMain(HMODULE hModule,
	DWORD  ul_reason_for_call,
	LPVOID lpReserved
)
{
	switch (ul_reason_for_call)
	{
	case DLL_PROCESS_ATTACH:
		if (FAILED(CoInitialize(NULL))) {
			puts("CoInitialize failed");
		}
		break;
	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
		break;
	case DLL_PROCESS_DETACH:
		GetHttpBuffer(0);
		JsonObjectRef = -1;
		lua_close(GlobalState);
		CoUninitialize();
		break;
	}
	return TRUE;
}