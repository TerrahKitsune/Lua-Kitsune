#include "../networking.h"
#include <Windows.h>
#include "objbase.h"
#include "luasqlite.h"
#include "dlllua.h"
#include "../stream.h"
#include "../luawchar.h"
#include "../HttpMain.h"
SQLITE_EXTENSION_INIT1
int JsonObjectRef = LUA_NOREF;
int SqliteDbRef = LUA_NOREF;
lua_State* GlobalState = NULL;

static void* l_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
	return sqlite3_realloc(ptr, nsize);
}

int query(lua_State* L) {

	if (SqliteDbRef == LUA_NOREF) {
		luaL_error(L, "Internal context is not set");
		return 0;
	}

	lua_rawgeti(L, LUA_REGISTRYINDEX, SqliteDbRef);
	sqlite3* db = (sqlite3*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (db == NULL) {
		luaL_error(L, "SQLite instance has been closed");
		return 0;
	}
	sqlite3_stmt* stmt;

	const char* query = luaL_checkstring(L, 1);
	int cnt = 0;
	size_t len;
	const char* data;
	const char* name;
	LuaWChar* wchar;
	LuaStream* stream;

	int err = sqlite3_prepare_v2(db, query, -1, &stmt, 0);
	if (err) {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, sqlite3_errmsg(db));
		return 2;
	}
	else if (lua_istable(L, 2)) {

		for (int n = 0; n < sqlite3_bind_parameter_count(stmt); n++) {
			name = sqlite3_bind_parameter_name(stmt, n + 1);
			if (name == NULL || strlen(name) < 2) {
				lua_pop(L, lua_gettop(L));
				lua_pushboolean(L, false);
				lua_pushstring(L, "Parameters contain a nameless parameter!");
				return 2;
			}

			lua_pushstring(L, &name[1]);
			lua_gettable(L, -2);

			switch (lua_type(L, -1))
			{
			case LUA_TNIL:
				sqlite3_bind_null(stmt, ++cnt);
				break;
			case LUA_TNUMBER:
				if (lua_isinteger(L, -1)) {
					sqlite3_bind_int64(stmt, ++cnt, lua_tointeger(L, -1));
				}
				else {
					sqlite3_bind_double(stmt, ++cnt, lua_tonumber(L, -1));
				}
				break;
			case LUA_TBOOLEAN:
				sqlite3_bind_int(stmt, ++cnt, lua_toboolean(L, -1));
				break;
			case LUA_TSTRING:
				data = lua_tolstring(L, -1, &len);
				sqlite3_bind_text(stmt, ++cnt, data, len, SQLITE_STATIC);
				break;
			case LUA_TUSERDATA:

				if (luaL_testudata(L, -1, LUAWCHAR)) {
					wchar = lua_towchar(L, -1);
					if (wchar->str) {
						sqlite3_bind_text16(stmt, ++cnt, wchar->str, wchar->len * sizeof(wchar_t), SQLITE_STATIC);
						break;
					}
				}
				else if (luaL_testudata(L, -1, STREAM)) {
					stream = lua_toluastream(L, -1);
					if (stream->data) {
						sqlite3_bind_blob64(stmt, ++cnt, stream->data, stream->len, SQLITE_STATIC);
						break;
					}
				}

				sqlite3_bind_null(stmt, ++cnt);
				break;
			}

			lua_pop(L, 1);
		}
	}
	else if (lua_isfunction(L, 2)) {

		for (int n = 0; n < sqlite3_bind_parameter_count(stmt); n++) {
			name = sqlite3_bind_parameter_name(stmt, n + 1);
			if (name == NULL || strlen(name) < 2) {
				lua_pop(L, lua_gettop(L));
				lua_pushboolean(L, false);
				lua_pushstring(L, "Parameters contain a nameless parameter!");
				return 2;
			}

			lua_pushvalue(L, 2);
			lua_pushstring(L, &name[1]);

			if (lua_pcall(L, 1, 1, 0) != 0) {
				lua_error(L);
				return 0;
			}

			switch (lua_type(L, -1))
			{
			case LUA_TNIL:
				sqlite3_bind_null(stmt, ++cnt);
				break;
			case LUA_TNUMBER:

				if (lua_isinteger(L, -1)) {
					sqlite3_bind_int64(stmt, ++cnt, lua_tointeger(L, -1));
				}
				else {
					sqlite3_bind_double(stmt, ++cnt, lua_tonumber(L, -1));
				}
				break;
			case LUA_TBOOLEAN:
				sqlite3_bind_int(stmt, ++cnt, lua_toboolean(L, -1));
				break;
			case LUA_TSTRING:
				data = lua_tolstring(L, -1, &len);
				sqlite3_bind_text(stmt, ++cnt, data, len, SQLITE_STATIC);
				break;
			case LUA_TUSERDATA:

				if (luaL_testudata(L, -1, LUAWCHAR)) {
					wchar = lua_towchar(L, -1);
					if (wchar->str) {
						sqlite3_bind_text16(stmt, ++cnt, wchar->str, wchar->len * sizeof(wchar_t), SQLITE_STATIC);
						break;
					}
				}
				else if (luaL_testudata(L, -1, STREAM)) {
					stream = lua_toluastream(L, -1);
					if (stream->data) {
						sqlite3_bind_blob64(stmt, ++cnt, stream->data, stream->len, SQLITE_STATIC);
						break;
					}
				}

				sqlite3_bind_null(stmt, ++cnt);
				break;
			}

			lua_pop(L, 1);
		}
	}

	int status = sqlite3_step(stmt);

	lua_pop(L, lua_gettop(L));

	if (status == SQLITE_OK) {
		lua_pushstring(L, "OK");
	}
	else if (status == SQLITE_ROW) {
		lua_newtable(L);
		int fields = sqlite3_column_count(stmt);
		int cnt = 0;
		while (status == SQLITE_ROW) {
			lua_createtable(L, 0, cnt);
			for (int i = 0; i < fields; i++)
			{
				lua_pushstring(L, sqlite3_column_name(stmt, i));
				switch (sqlite3_column_type(stmt, i)) {
				case SQLITE_INTEGER:
					lua_pushinteger(L, sqlite3_column_int64(stmt, i));
					break;
				case SQLITE_FLOAT:
					lua_pushnumber(L, sqlite3_column_double(stmt, i));
					break;
				case SQLITE_BLOB:
					lua_pushluastream(L, (const BYTE*)sqlite3_column_blob(stmt, i), sqlite3_column_bytes(stmt, i));
					break;
				case SQLITE_NULL:
					lua_pushnil(L);
					break;
				default:
					lua_pushlstring(L, (const char*)sqlite3_column_text(stmt, i), sqlite3_column_bytes(stmt, i));
					break;
				}
				lua_settable(L, -3);			
			}

			lua_rawseti(L, -2, ++cnt);
			status = sqlite3_step(stmt);
		}
	}
	else if (status == SQLITE_DONE) {
		lua_pushstring(L, "DONE");
	}
	else {
		lua_pushboolean(L, false);
		lua_pushstring(L, sqlite3_errmsg(db));
		sqlite3_finalize(stmt);
		return 2;
	}

	sqlite3_finalize(stmt);

	return 1;
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
	case LUA_TFUNCTION:
		lua_pushvalue(L, idx);
		lua_pcall(L, 0, 1, NULL);
		lua_tosqlite3value(L, -1, context);
		lua_pop(L, 1);
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

static void runluafunction(sqlite3_context* context, int argc, sqlite3_value** argv) {
	lua_State* L = (lua_State*)sqlite3_user_data(context);

	if (argc < 1) {
		sqlite3_result_error(context, "LuaFunction requires at least one argument", -1);
		return;
	}

	const char* function = (const char*)sqlite3_value_text(argv[0]);
	if (strstr(function, ".")) {

		char* buf = (char*)sqlite3_malloc(strlen(function) + 1);
		if (!buf) {
			sqlite3_result_error(context, "Out of memory", -1);
			return;
		}

		const char* start = function;
		bool first = true;
		int len;
		int idx = lua_gettop(L);
		for (size_t i = 0; function[i]; i++)
		{
			if (function[i] == '.') {
				len = &function[i] - start;
				strncpy(buf, start, len);
				buf[len] = '\0';
				start = &function[i + 1];

				if (first) {
					lua_getglobal(L, buf);
					first = false;
				}
				else {
					lua_pushstring(L, buf);
					lua_gettable(L, -2);
				}

				if (!lua_istable(L, -1)) {
					lua_pop(L, lua_gettop(L) - idx);
					sqlite3_result_error(context, "Function not found", -1);
					return;
				}
			}
		}

		sqlite3_free(buf);

		lua_pushstring(L, start);
		lua_gettable(L, -2);
		lua_copy(L, -1, idx + 1);
		lua_pop(L, lua_gettop(L) - idx - 1);
	}
	else {
		lua_getglobal(L, function);
	}

	if (lua_type(L, -1) != LUA_TFUNCTION) {
		lua_pop(L, 1);
		sqlite3_result_error(context, "Function not found", -1);
		return;
	}

	for (int i = 1; i < argc; i++)
	{
		lua_pushsqlite3value(L, argv[i]);
	}

	if (lua_pcall(L, argc - 1, 1, NULL)) {
		sqlite3_result_error(context, lua_tostring(L, -1), -1);
		lua_pop(L, 1);
		return;
	}

	lua_tosqlite3value(L, -1, context);
	lua_pop(L, 1);
}

int lua_registerfunction(lua_State* L) {

	lua_rawgeti(L, LUA_REGISTRYINDEX, SqliteDbRef);
	sqlite3* db = (sqlite3*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	return sqlite3_createfunction(L, db);
}

int lua_registertable(lua_State* L) {

	lua_rawgeti(L, LUA_REGISTRYINDEX, SqliteDbRef);
	sqlite3* db = (sqlite3*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	return sqlite3_registertable(L, db);
}

extern "C" __declspec(dllexport)
int sqlite3_sqlitekitsune_init(sqlite3* db, char** pzErrMsg, const sqlite3_api_routines* pApi) {
	SQLITE_EXTENSION_INIT2(pApi);
	lua_State* L = OpenLuaState(l_alloc);
	GlobalState = L;

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

	lua_pushlightuserdata(L, db);
	SqliteDbRef = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_pushcfunction(L, lua_registerfunction);
	lua_setglobal(L, "RegisterFunction");

	lua_pushcfunction(L, lua_registertable);
	lua_setglobal(L, "RegisterTable");

	sqlite3_create_function(db, "LuaFunction", -1, SQLITE_UTF8, L, runluafunction, NULL, NULL);

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
		JsonObjectRef = LUA_NOREF;
		SqliteDbRef = LUA_NOREF;
		lua_close(GlobalState);
		GlobalState = NULL;
		CoUninitialize();
		break;
	}
	return TRUE;
}