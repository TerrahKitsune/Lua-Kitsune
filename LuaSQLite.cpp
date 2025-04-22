#include "LuaSQLite.h"
#include <string.h>
#include <stdlib.h>
#include "luawchar.h"
#include "stream.h"

void FinalizeStmt(LuaSQLite*state) {
	if (state->stmt) {
		sqlite3_finalize(state->stmt);
		state->stmt = NULL;
	}
}

LuaSQLite* luaL_checksqlite(lua_State* L, int index) {

	LuaSQLite* luasqlite = (LuaSQLite*)luaL_checkudata(L, index, LUASQLITE);
	if (luasqlite == NULL)
		luaL_error(L, "parameter is not a %s", LUASQLITE);
	return luasqlite;
}

LuaSQLite* lua_pushsqlite(lua_State* L) {

	LuaSQLite* luasqlite = (LuaSQLite*)lua_newuserdata(L, sizeof(LuaSQLite));
	if (luasqlite == NULL) {
		luaL_error(L, "Unable to create sqlite connection");
		return NULL;
	}
	luaL_getmetatable(L, LUASQLITE);
	lua_setmetatable(L, -2);
	memset(luasqlite, 0, sizeof(LuaSQLite));
	return luasqlite;
}

void push_sqlitevalue(lua_State* L, sqlite3_stmt* pStmt, int idx, bool usewchar) {
	switch (sqlite3_column_type(pStmt, idx)) {
	case SQLITE_INTEGER:
		lua_pushinteger(L, sqlite3_column_int64(pStmt, idx));
		break;
	case SQLITE_FLOAT:
		lua_pushnumber(L, sqlite3_column_double(pStmt, idx));
		break;
	case SQLITE_TEXT:
		if (usewchar) {
			lua_pushwchar(L, (wchar_t*)sqlite3_column_text16(pStmt, idx), sqlite3_column_bytes16(pStmt, idx) / sizeof(wchar_t));
		}
		else {
			lua_pushlstring(L, (const char*)sqlite3_column_text(pStmt, idx), sqlite3_column_bytes(pStmt, idx));
		}
		break;

	case SQLITE_BLOB:

		lua_pushluastream(L, (const BYTE*)sqlite3_column_blob(pStmt, idx), sqlite3_column_bytes(pStmt, idx));
		break;
	case SQLITE_NULL:
	default:
		lua_pushnil(L);
		break;
	}
}

int SQLiteSetUseWidechar(lua_State* L) {

	LuaSQLite* luasqlite = (LuaSQLite*)luaL_checksqlite(L, 1);

	luasqlite->useWidechar = lua_toboolean(L, 2) != 0;

	return 0;
}

int SQLiteGetRow(lua_State* L) {

	LuaSQLite* luasqlite = (LuaSQLite*)luaL_checksqlite(L, 1);
	if (luasqlite->db == NULL) {
		luaL_error(L, "SQLite instance has been closed");
		return 1;
	}

	if (luasqlite->stmt == NULL) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}
	else if (luasqlite->status != SQLITE_OK && luasqlite->status != SQLITE_ROW) {
		sqlite3_finalize(luasqlite->stmt);
		luasqlite->stmt = NULL;
		luasqlite->status = SQLITE_OK;
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
	}

	int idx = (int)luaL_optinteger(L, 2, -1);
	if (idx > 0) {
		idx--;
		if (idx >= sqlite3_column_count(luasqlite->stmt)) {
			lua_pop(L, lua_gettop(L));
			lua_pushnil(L);
		}
		else {
			lua_pop(L, lua_gettop(L));
			push_sqlitevalue(L, luasqlite->stmt, idx, luasqlite->useWidechar);
		}
		return 1;
	}

	int cnt = sqlite3_column_count(luasqlite->stmt);
	lua_pop(L, lua_gettop(L));
	lua_createtable(L, 0, cnt);
	for (int n = 0; n < cnt; n++) {

		lua_pushstring(L, sqlite3_column_name(luasqlite->stmt, n));
		push_sqlitevalue(L, luasqlite->stmt, n, luasqlite->useWidechar);
		lua_settable(L, -3);
	}

	return 1;
}

int SQLiteFetch(lua_State* L) {

	LuaSQLite* luasqlite = (LuaSQLite*)luaL_checksqlite(L, 1);
	if (luasqlite->db == NULL)
		luaL_error(L, "SQLite instance has been closed");

	if (luasqlite->stmt == NULL) {
		luasqlite->status = SQLITE_OK;
		lua_pop(L, 1);
		lua_pushboolean(L, false);
	}
	else if (luasqlite->status == SQLITE_ROW) {
		luasqlite->status = SQLITE_OK;
		lua_pop(L, 1);
		lua_pushboolean(L, true);
	}
	else if (luasqlite->status == SQLITE_OK) {

		if (sqlite3_step(luasqlite->stmt) == SQLITE_ROW) {
			lua_pop(L, 1);
			lua_pushboolean(L, true);
		}
		else {
			FinalizeStmt(luasqlite);
			luasqlite->status = SQLITE_OK;
			lua_pop(L, 1);
			lua_pushboolean(L, false);
		}
	}
	else {
		FinalizeStmt(luasqlite);
		luasqlite->status = SQLITE_OK;
		lua_pop(L, 1);
		lua_pushboolean(L, false);
	}

	return 1;
}

int SQLiteFinish(lua_State* L) {
	LuaSQLite* luasqlite = (LuaSQLite*)luaL_checksqlite(L, 1);
	FinalizeStmt(luasqlite);
	lua_gc(L, LUA_GCCOLLECT);
	return 0;
}

int SQLiteExecute(lua_State* L) {

	LuaSQLite* luasqlite = (LuaSQLite*)luaL_checksqlite(L, 1);
	if (luasqlite->db == NULL) {
		luaL_error(L, "SQLite instance has been closed");
		return 0;
	}
	const char* query = luaL_checkstring(L, 2);
	int cnt = 0;
	size_t len;
	const char* data;
	const char* name;
	LuaWChar* wchar;
	LuaStream* stream;

	FinalizeStmt(luasqlite);

	int err = sqlite3_prepare_v2(luasqlite->db, query, -1, &luasqlite->stmt, 0);
	if (err) {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, sqlite3_errmsg(luasqlite->db));
		FinalizeStmt(luasqlite);
		return 2;
	}
	else if (lua_istable(L, 3)) {

		for (int n = 0; n < sqlite3_bind_parameter_count(luasqlite->stmt); n++) {
			name = sqlite3_bind_parameter_name(luasqlite->stmt, n + 1);
			if (name == NULL || strlen(name) < 2) {
				lua_pop(L, lua_gettop(L));
				lua_pushboolean(L, false);
				lua_pushstring(L, "Parameters contain a nameless parameter!");
				FinalizeStmt(luasqlite);
				return 2;
			}

			lua_pushstring(L, &name[1]);
			lua_gettable(L, -2);

			switch (lua_type(L, -1))
			{
			case LUA_TNIL:
				sqlite3_bind_null(luasqlite->stmt, ++cnt);
				break;
			case LUA_TNUMBER:
				if (lua_isinteger(L, -1)) {
					sqlite3_bind_int64(luasqlite->stmt, ++cnt, lua_tointeger(L, -1));
				}
				else {
					sqlite3_bind_double(luasqlite->stmt, ++cnt, lua_tonumber(L, -1));
				}
				break;
			case LUA_TBOOLEAN:
				sqlite3_bind_int(luasqlite->stmt, ++cnt, lua_toboolean(L, -1));
				break;
			case LUA_TSTRING:
				data = lua_tolstring(L, -1, &len);
				sqlite3_bind_text(luasqlite->stmt, ++cnt, data, len, SQLITE_STATIC);
				break;
			case LUA_TUSERDATA:

				if (luaL_testudata(L, -1, LUAWCHAR)) {
					wchar = lua_towchar(L, -1);
					if (wchar->str) {
						sqlite3_bind_text16(luasqlite->stmt, ++cnt, wchar->str, wchar->len * sizeof(wchar_t), SQLITE_STATIC);
						break;
					}
				}
				else if (luaL_testudata(L, -1, STREAM)) {
					stream = lua_toluastream(L, -1);
					if (stream->data) {
						sqlite3_bind_blob64(luasqlite->stmt, ++cnt, stream->data, stream->len, SQLITE_STATIC);
						break;
					}
				}

				sqlite3_bind_null(luasqlite->stmt, ++cnt);
				break;
			}

			lua_pop(L, 1);
		}
	}
	else if (lua_isfunction(L, 3)) {

		for (int n = 0; n < sqlite3_bind_parameter_count(luasqlite->stmt); n++) {
			name = sqlite3_bind_parameter_name(luasqlite->stmt, n + 1);
			if (name == NULL || strlen(name) < 2) {
				lua_pop(L, lua_gettop(L));
				lua_pushboolean(L, false);
				lua_pushstring(L, "Parameters contain a nameless parameter!");
				FinalizeStmt(luasqlite);
				return 2;
			}

			lua_pushvalue(L, 3);
			lua_pushstring(L, &name[1]);

			if (lua_pcall(L, 1, 1, 0) != 0) {
				lua_error(L);
				return 0;
			}

			switch (lua_type(L, -1))
			{
			case LUA_TNIL:
				sqlite3_bind_null(luasqlite->stmt, ++cnt);
				break;
			case LUA_TNUMBER:

				if (lua_isinteger(L, -1)) {
					sqlite3_bind_int64(luasqlite->stmt, ++cnt, lua_tointeger(L, -1));
				}
				else {
					sqlite3_bind_double(luasqlite->stmt, ++cnt, lua_tonumber(L, -1));
				}
				break;
			case LUA_TBOOLEAN:
				sqlite3_bind_int(luasqlite->stmt, ++cnt, lua_toboolean(L, -1));
				break;
			case LUA_TSTRING:
				data = lua_tolstring(L, -1, &len);
				sqlite3_bind_text(luasqlite->stmt, ++cnt, data, len, SQLITE_STATIC);
				break;
			case LUA_TUSERDATA:

				if (luaL_testudata(L, -1, LUAWCHAR)) {
					wchar = lua_towchar(L, -1);
					if (wchar->str) {
						sqlite3_bind_text16(luasqlite->stmt, ++cnt, wchar->str, wchar->len * sizeof(wchar_t), SQLITE_STATIC);
						break;
					}
				}
				else if (luaL_testudata(L, -1, STREAM)) {
					stream = lua_toluastream(L, -1);
					if (stream->data) {
						sqlite3_bind_blob64(luasqlite->stmt, ++cnt, stream->data, stream->len, SQLITE_STATIC);
						break;
					}
				}

				sqlite3_bind_null(luasqlite->stmt, ++cnt);
				break;
			}

			lua_pop(L, 1);
		}
	}

	luasqlite->status = sqlite3_step(luasqlite->stmt);

	lua_pop(L, lua_gettop(L));

	if (luasqlite->status == SQLITE_OK) {
		lua_pushboolean(L, true);
		lua_pushstring(L, "OK");
		FinalizeStmt(luasqlite);
	}
	else if (luasqlite->status == SQLITE_ROW) {
		lua_pushboolean(L, true);
		lua_pushstring(L, "ROW");
	}
	else if (luasqlite->status == SQLITE_DONE) {
		lua_pushboolean(L, true);
		lua_pushstring(L, "DONE");
		FinalizeStmt(luasqlite);
	}
	else {
		lua_pushboolean(L, false);
		lua_pushstring(L, sqlite3_errmsg(luasqlite->db));
		FinalizeStmt(luasqlite);
	}

	return 2;
}

static void RemoveBusyHandler(lua_State* L, LuaSQLite* luasqlite) {
	if (luasqlite->busyhandler != -1) {
		luaL_unref(L, LUA_REGISTRYINDEX, luasqlite->busyhandler);
		luasqlite->busyhandler = -1;
		sqlite3_busy_handler(luasqlite->db, NULL, luasqlite);
	}
}

static int BusyHandler(void* d, int retries) {
	lua_State* L = (lua_State*)d;

	LuaSQLite* luasqlite = (LuaSQLite*)luaL_checksqlite(L, 1);
	lua_rawgeti(L, LUA_REGISTRYINDEX, luasqlite->busyhandler);
	if (!lua_isfunction(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	else {
		lua_pushvalue(L, 1);
		lua_pushinteger(L, retries);
		if (lua_pcall(L, 2, 1, NULL)) {
			return 0;
		}
		bool ok = lua_toboolean(L, -1) > 0;
		lua_pop(L, 1);
		return 1;
	}
	return 0;
}

int SQLiteSetBusyHandler(lua_State* L) {

	LuaSQLite* luasqlite = (LuaSQLite*)luaL_checksqlite(L, 1);
	if (luasqlite->db == NULL)
		luaL_error(L, "SQLite instance has been closed");

	RemoveBusyHandler(L, luasqlite);

	if (lua_type(L, 2) == LUA_TFUNCTION)
	{
		lua_pushvalue(L, 2);
		luasqlite->busyhandler = luaL_ref(L, LUA_REGISTRYINDEX);
		sqlite3_busy_handler(luasqlite->db, BusyHandler, L);
	}

	return 0;
}

void SqliteLuaFunction(sqlite3_context* context, int argc, sqlite3_value** argv) {

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

void SqlitePCallFunction(bool isFinish, LuaSQLiteFunction* function, sqlite3_context* context, int argc, sqlite3_value** argv) {

	lua_State* L = function->L;

	lua_rawgeti(L, LUA_REGISTRYINDEX, function->index);

	if (function->isaggregate) {
		lua_pushboolean(L, isFinish);
	}

	for (int n = 0; n < argc; n++) {
		switch (sqlite3_value_type(argv[n])) {
		case SQLITE_INTEGER:
			lua_pushinteger(L, sqlite3_value_int64(argv[n]));
			break;
		case SQLITE_FLOAT:
			lua_pushnumber(L, sqlite3_value_double(argv[n]));
			break;
		case SQLITE_TEXT:
			lua_pushlstring(L, (const char*)sqlite3_value_text(argv[n]), sqlite3_value_bytes(argv[n]));
			break;
		case SQLITE_BLOB:
			lua_pushluastream(L, (const BYTE*)sqlite3_value_blob(argv[n]), sqlite3_value_bytes(argv[n]));
			break;
		default:
			lua_pushnil(L);
			break;
		}
	}

	const char* result;
	size_t len;

	int returns = 1;

	if (function->isaggregate) {
		argc++;
		if (!isFinish) {
			returns = 0;
		}
	}

	if (lua_pcall(L, argc, returns, 0) != LUA_OK) {
		result = lua_tostring(L, -1);
		lua_pop(L, 1);
		sqlite3_result_error(context, result, -1);
		return;
	}

	if (returns == 0) {
		return;
	}

	int type = lua_type(L, -1);

	if (type == LUA_TNIL) {
		sqlite3_result_null(context);
		lua_pop(L, 1);
		return;
	}
	else if (type == LUA_TNUMBER) {
		sqlite3_result_double(context, lua_tonumber(L, -1));
		lua_pop(L, 1);
		return;
	}
	else if (type == LUA_TSTRING) {
		result = lua_tolstring(L, -1, &len);
		sqlite3_result_text(context, result, len, SQLITE_TRANSIENT);
		lua_pop(L, 1);
		return;
	}
	else if (type == LUA_TBOOLEAN) {
		sqlite3_result_int(context, lua_toboolean(L, -1));
		lua_pop(L, 1);
		return;
	}
	else if (type == LUA_TUSERDATA) {
		if (lua_iswchar(L, -1)) {
			LuaWChar* wchar = lua_towchar(L, -1);
			sqlite3_result_text16(context, wchar->str, wchar->len * sizeof(wchar_t), SQLITE_TRANSIENT);
			lua_pop(L, 1);
			return;
		}
		else if (lua_isstream(L, -1)) {
			LuaStream* stream = lua_toluastream(L, -1);
			if (stream->data) {
				sqlite3_result_blob(context, stream->data, stream->len, SQLITE_TRANSIENT);
			}
			else {
				sqlite3_result_null(context);
			}
			lua_pop(L, 1);
			return;
		}
	}

	result = luaL_tolstring(L, -1, &len);

	lua_pop(L, 2);

	sqlite3_result_text(context, result, len, SQLITE_TRANSIENT);
}

void DoPcallFunction(sqlite3_context* context, int argc, sqlite3_value** argv) {
	LuaSQLiteFunction* function = (LuaSQLiteFunction*)sqlite3_user_data(context);
	SqlitePCallFunction(false, function, context, argc, argv);
}

void DoPcallFunctionFinish(sqlite3_context* context) {
	LuaSQLiteFunction* function = (LuaSQLiteFunction*)sqlite3_user_data(context);
	SqlitePCallFunction(true, function, context, 0, NULL);
}

int RegisterFunction(lua_State* L, bool isAggregate) {

	LuaSQLite* luasqlite = (LuaSQLite*)luaL_checksqlite(L, 1);
	if (!luasqlite || luasqlite->db == NULL) {
		luaL_error(L, "SQLite instance has been closed");
		return 0;
	}

	luaL_checktype(L, -3, LUA_TFUNCTION);
	luaL_checktype(L, -2, LUA_TSTRING);
	luaL_checktype(L, -1, LUA_TNUMBER);

	const char* name = lua_tostring(L, -2);
	int args = lua_tointeger(L, -1);

	if (args < 0) {
		luaL_error(L, "SQLite function args can't be negative");
		return 0;
	}

	if (luasqlite->functions) {
		for (int n = 0; n < luasqlite->funcs; n++) {
			if (strcmp(luasqlite->functions[n]->name, name) == 0) {
				luaL_error(L, "SQLite function with name %s already exists", name);
				return 0;
			}
		}
	}

	LuaSQLiteFunction** newArray = (LuaSQLiteFunction**)gff_calloc(luasqlite->funcs + 1, sizeof(LuaSQLiteFunction*));

	if (!newArray) {
		luaL_error(L, "Out of memory", name);
		return 0;
	}
	else if (luasqlite->funcs > 0 && luasqlite->functions) {
		memcpy(newArray, luasqlite->functions, sizeof(LuaSQLiteFunction) * luasqlite->funcs);
		gff_free(luasqlite->functions);
	}

	luasqlite->functions = newArray;
	luasqlite->functions[luasqlite->funcs] = (LuaSQLiteFunction*)gff_calloc(1, sizeof(LuaSQLiteFunction));
	if (!luasqlite->functions[luasqlite->funcs]) {
		luaL_error(L, "Out of memory", name);
		return 0;
	}

	luasqlite->functions[luasqlite->funcs]->name = (char*)gff_malloc(strlen(name) + 1);
	if (!luasqlite->functions[luasqlite->funcs]->name) {
		luaL_error(L, "Out of memory", name);
		return 0;
	}
	else {
		strcpy(luasqlite->functions[luasqlite->funcs]->name, name);
	}

	lua_pushvalue(L, -3);
	luasqlite->functions[luasqlite->funcs]->isaggregate = isAggregate;
	luasqlite->functions[luasqlite->funcs]->L = L;
	luasqlite->functions[luasqlite->funcs]->index = luaL_ref(L, LUA_REGISTRYINDEX);
	luasqlite->functions[luasqlite->funcs]->args = args;

	if (!isAggregate)
	{
		sqlite3_create_function(
			luasqlite->db,
			luasqlite->functions[luasqlite->funcs]->name,
			luasqlite->functions[luasqlite->funcs]->args,
			SQLITE_UTF8,
			luasqlite->functions[luasqlite->funcs],
			DoPcallFunction,
			NULL,
			NULL);
	}
	else {
		sqlite3_create_function(
			luasqlite->db,
			luasqlite->functions[luasqlite->funcs]->name,
			luasqlite->functions[luasqlite->funcs]->args,
			SQLITE_UTF8,
			luasqlite->functions[luasqlite->funcs],
			NULL,
			DoPcallFunction,
			DoPcallFunctionFinish);
	}

	luasqlite->funcs++;
	lua_pop(L, 3);

	return 0;
}

int SQLiteRegisterFunction(lua_State* L) {
	return RegisterFunction(L, false);
}

int SQLiteRegisterAggregateFunction(lua_State* L) {
	return RegisterFunction(L, true);
}

int SQLiteConnect(lua_State* L) {

	size_t len;
	const char* db = luaL_optlstring(L, 1, ":memory:", &len);
	char* file = (char*)gff_malloc(len + 1);
	if (!file) {
		luaL_error(L, "Unable to allocate memory for sqlite");
		return 0;
	}

	file[len] = '\0';
	memcpy(file, db, len);

	int mode = (int)luaL_optinteger(L, 2, 0);

	lua_pop(L, lua_gettop(L));
	LuaSQLite* luasqlite = lua_pushsqlite(L);
	if (!luasqlite) {
		gff_free(file);
		luaL_error(L, "Unable to allocate memory for sqlite");
		return 0;
	}

	luasqlite->busyhandler = -1;

	int ok;

	switch (mode) {
	case 1:
		ok = sqlite3_config(SQLITE_CONFIG_MULTITHREAD);
		break;
	case 2:
		ok = sqlite3_config(SQLITE_CONFIG_SERIALIZED);
		break;
	default:
		ok = sqlite3_config(SQLITE_CONFIG_SINGLETHREAD);
		break;
	}

	//Ignore missuse
	if (ok != SQLITE_OK && ok != SQLITE_MISUSE) {
		gff_free(file);
		luaL_error(L, "SQLite error %s", sqlite3_errmsg(luasqlite->db));
	}

	int err = sqlite3_open(file, &luasqlite->db);
	if (err) {
		gff_free(file);
		luaL_error(L, "SQLite error %s", sqlite3_errmsg(luasqlite->db));
	}

	if (mode != 0) {
		sqlite3_exec(luasqlite->db, "PRAGMA journal_mode=WAL;", 0, 0, 0);
	}
	else {
		sqlite3_exec(luasqlite->db, "PRAGMA journal_mode=DELETE;", 0, 0, 0);
	}

	sqlite3_exec(luasqlite->db, "PRAGMA synchronous=NORMAL;", 0, 0, 0);

	luasqlite->file = file;
	luasqlite->useWidechar = true;

	sqlite3_enable_load_extension(luasqlite->db, 1);
	sqlite3_create_function(luasqlite->db, "Lua", 1, SQLITE_UTF8, L, SqliteLuaFunction, NULL, NULL);

	return 1;
}

int SQLite_GC(lua_State* L) {

	LuaSQLite* luasqlite = (LuaSQLite*)luaL_checksqlite(L, 1);

	RemoveBusyHandler(L, luasqlite);

	if (luasqlite->stmt) {
		sqlite3_finalize(luasqlite->stmt);
		luasqlite->stmt = NULL;
	}

	if (luasqlite->db) {		
		sqlite3_close(luasqlite->db);
		luasqlite->db = NULL;
	}

	if (luasqlite->file) {
		gff_free(luasqlite->file);
		luasqlite->file = NULL;
	}

	if (luasqlite->functions) {

		for (int n = 0; n < luasqlite->funcs; n++) {

			if (luasqlite->functions[n]) {

				if (luasqlite->functions[n]->index >= 0) {
					luaL_unref(L, LUA_REGISTRYINDEX, luasqlite->functions[n]->index);
					luasqlite->functions[n]->index = -1;
				}
				if (luasqlite->functions[n]->name) {
					gff_free(luasqlite->functions[n]->name);
					luasqlite->functions[n]->name = NULL;
				}
				gff_free(luasqlite->functions[n]);
			}
		}

		gff_free(luasqlite->functions);
		luasqlite->functions = NULL;
	}

	return 0;
}

int SQLite_ToString(lua_State* L) {

	LuaSQLite* sq = luaL_checksqlite(L, 1);
	char sqlite[_MAX_PATH + 20];
	sprintf(sqlite, "SQLite: 0x%08X File: %s", (int)sq, sq->file);

	lua_pushstring(L, sqlite);
	return 1;
}