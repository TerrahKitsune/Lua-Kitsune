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
int StateRef = LUA_NOREF;

ResState* GlobalState = NULL;

typedef struct {
	lua_State* L;
	int function_ref;
	int context_ref;
	bool isAggregate;
}AggregateData;

ResRegistration* GetRegistration(ResState* state, const char* name) {

	for (size_t i = 0; i < state->numbRegistrations; i++)
	{
		if (strcmp(state->Registrations[i].name, name) == 0) {
			return state->Registrations;
		}
	}

	return NULL;
}

ResRegistration* AddRegistration(ResState* state, const char* name, int type) {

	ResRegistration* temp = (ResRegistration*)sqlite3_realloc(state->Registrations, (int)(sizeof(ResRegistration) * (state->numbRegistrations + 1)));
	if (!temp) {
		return NULL;
	}
	else {
		state->Registrations = temp;
	}

	ResRegistration* current = &state->Registrations[state->numbRegistrations];
	state->numbRegistrations++;

	current->ptr = NULL;
	current->type = type;
	current->name = (char*)sqlite3_malloc((int)(sizeof(char) * (strlen(name) + 1)));

	if (!current->name) {
		return NULL;
	}
	else {
		current->name[strlen(name)] = '\0';
		strcpy(current->name, name);
	}

	return current;
}

static void* l_alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
	return sqlite3_realloc(ptr, (int)nsize);
}

int querysqlite(lua_State* L, bool isScalar) {

	if (StateRef == LUA_NOREF) {
		luaL_error(L, "Internal context is not set");
		return 0;
	}

	lua_rawgeti(L, LUA_REGISTRYINDEX, StateRef);
	ResState* state = (ResState*)lua_touserdata(L, -1);
	sqlite3* db = state->db;
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

	int err = sqlite3_prepare_v2(db, query, -1, &stmt, 0);
	if (err) {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, sqlite3_errmsg(db));
		sqlite3_finalize(stmt);

		if (isScalar) {
			lua_error(L);
		}

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
				sqlite3_bind_text(stmt, ++cnt, data, (int)len, SQLITE_STATIC);
				break;
			case LUA_TUSERDATA:

				if (luaL_testudata(L, -1, LUAWCHAR)) {
					wchar = lua_towchar(L, -1);
					if (wchar->str) {
						sqlite3_bind_text16(stmt, ++cnt, wchar->str, (int)(wchar->len * sizeof(wchar_t)), SQLITE_STATIC);
						break;
					}
				}
				else if (luaL_testudata(L, -1, STREAM)) {
					sqlite3_bind_null(stmt, ++cnt);
					break;
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
				sqlite3_finalize(stmt);

				if (isScalar) {
					lua_error(L);
				}
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
				sqlite3_bind_text(stmt, ++cnt, data, (int)len, SQLITE_STATIC);
				break;
			case LUA_TUSERDATA:

				if (luaL_testudata(L, -1, LUAWCHAR)) {
					wchar = lua_towchar(L, -1);
					if (wchar->str) {
						sqlite3_bind_text16(stmt, ++cnt, wchar->str, (int)(wchar->len * sizeof(wchar_t)), SQLITE_STATIC);
						break;
					}
				}
				else if (luaL_testudata(L, -1, STREAM)) {
					sqlite3_bind_null(stmt, ++cnt);
					break;
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

		if (isScalar) {
			lua_pushnil(L);
		}
		else {
			lua_pushstring(L, "OK");
		}
	}
	else if (status == SQLITE_ROW) {

		if (isScalar) {
			if (sqlite3_column_count(stmt) <= 0) {
				lua_pushnil(L);
			}
			else {
				switch (sqlite3_column_type(stmt, 0)) {
				case SQLITE_INTEGER:
					lua_pushinteger(L, sqlite3_column_int64(stmt, 0));
					break;
				case SQLITE_FLOAT:
					lua_pushnumber(L, sqlite3_column_double(stmt, 0));
					break;
				case SQLITE_BLOB:
					lua_pushluastream(L, (const BYTE*)sqlite3_column_blob(stmt, 0), sqlite3_column_bytes(stmt, 0));
					break;
				case SQLITE_NULL:
					lua_pushnil(L);
					break;
				default:
					lua_pushlstring(L, (const char*)sqlite3_column_text(stmt, 0), sqlite3_column_bytes(stmt, 0));
					break;
				}
			}
		}
		else {
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
	}
	else if (status == SQLITE_DONE) {
		if (isScalar) {
			lua_pushnil(L);
		}
		else {
			lua_pushstring(L, "DONE");
		}
	}
	else {
		lua_pushboolean(L, false);
		lua_pushstring(L, sqlite3_errmsg(db));
		sqlite3_finalize(stmt);

		if (isScalar) {
			lua_error(L);
		}

		return 2;
	}

	sqlite3_finalize(stmt);

	return 1;
}

int query(lua_State* L) {
	return querysqlite(L, false);
}

int scalar(lua_State* L) {
	return querysqlite(L, true);
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
			sqlite3_result_null(context);
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

static void executeluafunction(sqlite3_context* context, int argc, sqlite3_value** argv) {

	lua_State* L = (lua_State*)sqlite3_user_data(context);

	if (argc < 1) {
		sqlite3_result_error(context, "LuaFunction requires at least one argument", -1);
		return;
	}

	const char* function = (const char*)sqlite3_value_text(argv[0]);
	if (strstr(function, ".")) {

		char* buf = (char*)sqlite3_malloc((int)(strlen(function) + 1));
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
				len = (int)(&function[i] - start);
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

	lua_createtable(L, argc, 0);

	for (int i = 1; i < argc; i++)
	{
		lua_pushvalue(L, (argc - i + 1) * -1);
		lua_rawseti(L, -2, i);
	}

	lua_setglobal(L, "ARGS");

	if (lua_pcall(L, argc - 1, 1, NULL)) {
		sqlite3_result_error(context, lua_tostring(L, -1), -1);
		lua_pop(L, 1);
		return;
	}

	lua_pushnil(L);
	lua_setglobal(L, "ARGS");

	lua_tosqlite3value(L, -1, context);
	lua_pop(L, 1);
}

int lua_registerfunction(lua_State* L) {

	lua_rawgeti(L, LUA_REGISTRYINDEX, StateRef);
	ResState* state = (ResState*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	return sqlite3_createfunction(L, state);
}

int lua_registertable(lua_State* L) {

	lua_rawgeti(L, LUA_REGISTRYINDEX, StateRef);
	ResState* state = (ResState*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	return sqlite3_registertable(L, state);
}

void runluaaggregate(sqlite3_context* context, int argc, sqlite3_value** argv, AggregateData* aggregate, bool isFinished) {
	lua_State* L = aggregate->L;

	int top = lua_gettop(L);
	lua_rawgeti(L, LUA_REGISTRYINDEX, aggregate->function_ref);
	if (aggregate->isAggregate) {
		lua_pushboolean(L, isFinished);
	}
	else {
		isFinished = true;
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, aggregate->context_ref);

	for (size_t i = 0; i < argc; i++)
	{
		lua_pushsqlite3value(L, argv[i]);
	}

	if (lua_pcall(L, argc + (aggregate->isAggregate ? 2 : 1), isFinished ? 1 : 0, NULL)) {
		sqlite3_result_error(context, lua_tostring(L, -1), -1);
		lua_settop(L, top);
		return;
	}

	if (isFinished) {
		lua_tosqlite3value(L, -1, context);
	}

	lua_settop(L, top);
}

void runluaaggregatestep(sqlite3_context* context, int argc, sqlite3_value** argv) {
	AggregateData* aggregate = (AggregateData*)sqlite3_user_data(context);

	if (aggregate->context_ref == LUA_NOREF) {
		lua_newtable(aggregate->L);
		aggregate->context_ref = luaL_ref(aggregate->L, LUA_REGISTRYINDEX);
	}

	runluaaggregate(context, argc, argv, aggregate, false);
}

void runluaaggregatefinish(sqlite3_context* context) {
	AggregateData* aggregate = (AggregateData*)sqlite3_user_data(context);

	runluaaggregate(context, 0, NULL, aggregate, true);

	if (aggregate->context_ref != LUA_NOREF) {
		luaL_unref(aggregate->L, LUA_REGISTRYINDEX, aggregate->context_ref);
		aggregate->context_ref = LUA_NOREF;
	}

	lua_gc(aggregate->L, LUA_GCCOLLECT, 0);
}

void destroyaggregate(void* data) {
	AggregateData* aggData = (AggregateData*)data;

	if (aggData->context_ref != LUA_NOREF) {
		luaL_unref(aggData->L, LUA_REGISTRYINDEX, aggData->context_ref);
	}

	if (aggData->function_ref != LUA_NOREF) {
		luaL_unref(aggData->L, LUA_REGISTRYINDEX, aggData->function_ref);
	}

	sqlite3_free(aggData);
}

int lua_registerext(lua_State* L, bool isAggregate) {

	luaL_checktype(L, -2, LUA_TSTRING);
	luaL_checktype(L, -1, LUA_TFUNCTION);

	lua_rawgeti(L, LUA_REGISTRYINDEX, StateRef);
	ResState* state = (ResState*)lua_touserdata(L, -1);
	sqlite3* db = state->db;
	lua_pop(L, 1);

	const char* name = lua_tostring(L, -2);

	int type = isAggregate ? RES_TYPE_AGGREGATE_FUNCTION : RES_TYPE_FUNCTION;
	ResRegistration* res = GetRegistration(state, name);

	if (res && res->type != type) {
		luaL_error(L, "%s is already a registered sqlite resource", name);
		return 0;
	}
	else if (res && res->type == type) {
		AggregateData* existing = (AggregateData*)res->ptr;

		if (existing->context_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, existing->context_ref);
			existing->context_ref = LUA_NOREF;
		}

		if (existing->function_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, existing->function_ref);
			existing->function_ref = LUA_NOREF;
		}

		existing->function_ref = luaL_ref(L, LUA_REGISTRYINDEX);

		return 0;
	}

	AggregateData* aggData = (AggregateData*)sqlite3_malloc(sizeof(AggregateData));
	if (!aggData) {
		luaL_error(L, "Out of memory");
		return 0;
	}
	aggData->L = L;
	aggData->context_ref = LUA_NOREF;
	aggData->function_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	aggData->isAggregate = isAggregate;

	if (aggData->isAggregate) {
		sqlite3_create_function_v2(db, name, -1, SQLITE_UTF8, aggData, NULL, runluaaggregatestep, runluaaggregatefinish, destroyaggregate);
	}
	else {
		sqlite3_create_function_v2(db, name, -1, SQLITE_UTF8, aggData, runluaaggregatestep, NULL, NULL, destroyaggregate);
	}

	res = AddRegistration(state, name, type);

	if (res) {
		res->ptr = aggData;
	}

	return 0;
}

int lua_registersqliteaggregate(lua_State* L) {
	return lua_registerext(L, true);
}

int lua_registersqlitefunction(lua_State* L) {
	return lua_registerext(L, false);
}

__declspec(dllexport)
int sqlite3_sqlitekitsune_init(sqlite3* db, char** pzErrMsg, const sqlite3_api_routines* pApi) {
	SQLITE_EXTENSION_INIT2(pApi);

	GlobalState = (ResState*)sqlite3_malloc(sizeof(ResState));
	if (!GlobalState) {
		return SQLITE_NOMEM;
	}
	memset(GlobalState, 0, sizeof(ResState));

	lua_State* L = OpenLuaState(l_alloc);
	GlobalState->L = L;
	GlobalState->db = db;

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

	lua_pushlightuserdata(L, GlobalState);
	StateRef = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_pushcfunction(L, lua_registerfunction);
	lua_setglobal(L, "RegisterVirtualTable");

	lua_pushcfunction(L, lua_registertable);
	lua_setglobal(L, "RegisterTable");

	lua_pushcfunction(L, lua_registersqliteaggregate);
	lua_setglobal(L, "RegisterAggregate");

	lua_pushcfunction(L, lua_registersqlitefunction);
	lua_setglobal(L, "RegisterFunction");

	lua_pushcfunction(L, query);
	lua_setglobal(L, "query");

	lua_pushcfunction(L, scalar);
	lua_setglobal(L, "scalar");

	sqlite3_create_function(db, "LuaFunction", -1, SQLITE_UTF8, L, executeluafunction, NULL, NULL);

	const char* file = sqlite3_db_filename(db, NULL);

	if (file) {

		lua_pushstring(L, file);
		lua_setglobal(L, "FILE");

		char* path = (char*)sqlite3_malloc(MAX_PATH + 1);
		if (!path) {
			return SQLITE_NOMEM;
		}

		int idxLast = -1;

		for (size_t i = 0; i < strlen(file); i++)
		{
			if (file[i] == '/') {
				path[i] = '\\';
			}
			else {
				path[i] = file[i];
			}

			if (path[i] == '\\') {
				idxLast = (int)i;
			}
		}

		const char* luafile = "extension.lua";

		if (idxLast == -1) {
			strcpy(path, luafile);
		}
		else {
			path[idxLast + 1] = '\0';
			if (strlen(path) > MAX_PATH - strlen(luafile)) {
				sqlite3_free(path);
				return SQLITE_OK;
			}
			else {
				strcat(path, luafile);
			}
		}

		FILE* f = fopen(path, "rb");

		if (f) {

			fseek(f, 0, SEEK_END);
			long len = ftell(f);
			fclose(f);

			if (len > 0) {
				lua_getglobal(L, "dofile");
				lua_pushstring(L, path);
				if (lua_pcall(L, 1, 0, NULL)) {
					const char* err = lua_tostring(L, -1);
					*pzErrMsg = (char*)sqlite3_malloc((int)(strlen(err) + 1));
					strcpy(*pzErrMsg, err);
					sqlite3_free(path);
					lua_pop(L, lua_gettop(L));
					return SQLITE_ERROR;
				}
			}
		}

		sqlite3_free(path);
	}
	else {
		lua_pushliteral(L, "");
		lua_setglobal(L, "FILE");
	}

	lua_pop(L, lua_gettop(L));
	return SQLITE_OK;
}

extern "C"
{
	__declspec(dllexport)
		int sqlite3_extension_init(sqlite3* db, char** pzErrMsg, const sqlite3_api_routines* pApi) {
		return sqlite3_sqlitekitsune_init(db, pzErrMsg, pApi);
	}
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
		StateRef = LUA_NOREF;
		lua_close(GlobalState->L);

		if (GlobalState->Registrations) {
			for (size_t i = 0; i < GlobalState->numbRegistrations; i++)
			{
				if (GlobalState->Registrations[i].name) {
					sqlite3_free(GlobalState->Registrations[i].name);
					GlobalState->Registrations[i].name = NULL;
				}
			}
			sqlite3_free(GlobalState->Registrations);
		}

		sqlite3_free(GlobalState);
		GlobalState = NULL;
		CoUninitialize();
		break;
	}
	return TRUE;
}