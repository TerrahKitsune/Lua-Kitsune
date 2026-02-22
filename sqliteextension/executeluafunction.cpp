#include "luasqlite.h"
#include <string.h>
#include <ctype.h>

typedef struct {
	int function_read;
	int function_update;
	int numbfields;
	char** fields;
	lua_State* L;
} ExecuteLuaFunction;

typedef struct {
	sqlite3_vtab vtab;
	ExecuteLuaFunction* state;
} ExecuteLuaFunctionContext;

typedef struct {
	sqlite3_vtab_cursor cursor;
	ExecuteLuaFunctionContext* context;
	int context_ref;
	int current_ref;

} ExecuteLuaFunctionCursor;

int executeluastring_next(sqlite3_vtab_cursor* cursor);

static int executeluastring_connect(sqlite3* db, void* pAux, int argc, const char* const* argv, sqlite3_vtab** ppVtab, char** pzErr) {

	ExecuteLuaFunctionContext* context = (ExecuteLuaFunctionContext*)sqlite3_malloc(sizeof(ExecuteLuaFunctionContext));

	if (!context) {
		return SQLITE_NOMEM;
	}

	char* createtable = (char*)sqlite3_malloc(1000000 * sizeof(char));
	if (!createtable) {
		sqlite3_free(context);
		return SQLITE_NOMEM;
	}

	context->state = (ExecuteLuaFunction*)pAux;
	*ppVtab = &context->vtab;
	(*ppVtab)->nRef = 0;

	strcpy(createtable, "CREATE TABLE x(");
	for (size_t i = 0; i < context->state->numbfields; i++)
	{
		strcat(createtable, context->state->fields[i]);
		if (i == 0) {
			strcat(createtable, " PRIMARY KEY");
		}
		strcat(createtable, ",");
	}

	createtable[strlen(createtable) - 1] = ')';
	strcat(createtable, "WITHOUT ROWID;");

	int result = sqlite3_declare_vtab(db, createtable);
	sqlite3_free(createtable);

	return result;
}

static int executeluastring_disconnect(sqlite3_vtab* pVtab) {
	sqlite3_free(pVtab);
	return SQLITE_OK;
}

static int executeluastring_open(sqlite3_vtab* pVtab, sqlite3_vtab_cursor** ppCursor) {

	ExecuteLuaFunctionContext* context = (ExecuteLuaFunctionContext*)pVtab;
	ExecuteLuaFunctionCursor* cursor = (ExecuteLuaFunctionCursor*)sqlite3_malloc(sizeof(ExecuteLuaFunctionCursor));

	if (!cursor) {
		return SQLITE_NOMEM;
	}
	else {
		memset(cursor, 0, sizeof(ExecuteLuaFunctionCursor));
	}
	cursor->context = context;
	lua_State* L = context->state->L;
	lua_newtable(L);
	cursor->context_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	*ppCursor = (sqlite3_vtab_cursor*)cursor;
	return executeluastring_next(*ppCursor);
}

static int executeluastring_close(sqlite3_vtab_cursor* cursor) {

	ExecuteLuaFunctionCursor* luacursor = (ExecuteLuaFunctionCursor*)cursor;

	if (luacursor->context_ref != LUA_NOREF) {
		luaL_unref(luacursor->context->state->L, LUA_REGISTRYINDEX, luacursor->context_ref);
		luacursor->context_ref = LUA_NOREF;
	}

	if (luacursor->current_ref != LUA_NOREF) {
		luaL_unref(luacursor->context->state->L, LUA_REGISTRYINDEX, luacursor->current_ref);
		luacursor->current_ref = LUA_NOREF;
	}

	lua_gc(luacursor->context->state->L, LUA_GCCOLLECT, 0);
	sqlite3_free(cursor);
	return SQLITE_OK;
}

int executeluastring_bestindex(sqlite3_vtab* pVTab, sqlite3_index_info* info) {
	return SQLITE_OK;
}

int executeluastring_filter(sqlite3_vtab_cursor*, int idxNum, const char* idxStr, int argc, sqlite3_value** argv) {
	return SQLITE_OK;
}

int executeluastring_eof(sqlite3_vtab_cursor* cursor) {

	ExecuteLuaFunctionCursor* luacursor = (ExecuteLuaFunctionCursor*)cursor;
	return luacursor->current_ref == LUA_NOREF;
}

int executeluastring_column(sqlite3_vtab_cursor* cursor, sqlite3_context* context, int N) {

	ExecuteLuaFunctionCursor* luacursor = (ExecuteLuaFunctionCursor*)cursor;
	lua_State* L = luacursor->context->state->L;

	lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->current_ref);
	lua_rawgeti(L, -1, N + 1);
	lua_tosqlite3value(L, -1, context);
	lua_pop(L, 2);

	return SQLITE_OK;
}

int executeluastring_next(sqlite3_vtab_cursor* cursor) {
	ExecuteLuaFunctionCursor* luacursor = (ExecuteLuaFunctionCursor*)cursor;
	lua_State* L = luacursor->context->state->L;

	lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->context->state->function_read);
	lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->context_ref);

	if (lua_pcall(L, 1, 1, NULL)) {
		cursor->pVtab->zErrMsg = sqlite3_mprintf("Error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return SQLITE_ERROR;
	}

	if (lua_istable(L, -1)) {

		if (luacursor->current_ref == LUA_NOREF) {
			luacursor->current_ref = luaL_ref(L, LUA_REGISTRYINDEX);
		}
		else {
			lua_rawseti(L, LUA_REGISTRYINDEX, luacursor->current_ref);
		}
	}
	else if (luacursor->current_ref != LUA_NOREF) {

		luaL_unref(L, LUA_REGISTRYINDEX, luacursor->current_ref);
		luacursor->current_ref = LUA_NOREF;
		lua_pop(L, 1);
	}

	return SQLITE_OK;
}

int executeluastring_rowid(sqlite3_vtab_cursor* cursor, sqlite_int64* pRowid) {
	ExecuteLuaFunctionCursor* luacursor = (ExecuteLuaFunctionCursor*)cursor;
	return SQLITE_OK;
}

int executeluastring_findfunction(sqlite3_vtab* pVtab, int nArg, const char* zName, void (**pxFunc)(sqlite3_context*, int, sqlite3_value**), void** ppArg) {

	return SQLITE_OK;
}

static int executeluastring_update(sqlite3_vtab* vtab, int argc, sqlite3_value** argv, sqlite3_int64* pRowid) {

	ExecuteLuaFunctionContext* context = (ExecuteLuaFunctionContext*)vtab;
	if (context->state->function_update == LUA_NOREF) {
		vtab->zErrMsg = sqlite3_mprintf("Readonly");
		return SQLITE_READONLY;
	}
	lua_State* L = context->state->L;
	int top = lua_gettop(L);
	lua_rawgeti(L, LUA_REGISTRYINDEX, context->state->function_update);

	if (argc == 1) {
		lua_pushsqlite3value(L, argv[0]);
		lua_pushnil(L);
	}
	else {
		lua_pushsqlite3value(L, argv[0]);
		lua_createtable(L, argc - 2, 0);
		for (size_t i = 2; i < argc; i++)
		{
			lua_pushsqlite3value(L, argv[i]);
			lua_rawseti(L, -2, i - 1);
		}
	}

	if (lua_pcall(L, 2, 0, NULL)) {
		vtab->zErrMsg = sqlite3_mprintf(lua_tostring(L, -1));
		lua_pop(L, 1);
		return SQLITE_ERROR;
	}

	return SQLITE_OK;
}

static sqlite3_module executeluastringModule = {
	0,
	executeluastring_connect,
	executeluastring_connect,
	executeluastring_bestindex,
	executeluastring_disconnect,
	executeluastring_disconnect,
	executeluastring_open,
	executeluastring_close,
	executeluastring_filter,
	executeluastring_next,
	executeluastring_eof,
	executeluastring_column,
	executeluastring_rowid,
	executeluastring_update,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0,
	0
};

static void destroyfunction(void* pClientData) {
	ExecuteLuaFunction* data = (ExecuteLuaFunction*)pClientData;
	if (data) {
		if (data->function_read != LUA_NOREF) {
			luaL_unref(data->L, LUA_REGISTRYINDEX, data->function_read);
			data->function_read = LUA_NOREF;
		}
		if (data->function_update = LUA_NOREF) {
			luaL_unref(data->L, LUA_REGISTRYINDEX, data->function_update);
			data->function_update = LUA_NOREF;
		}

		if (data->fields)
		{
			for (size_t i = 0; i < data->numbfields; i++)
			{
				if (data->fields[i]) {
					sqlite3_free(data->fields[i]);
				}
			}

			sqlite3_free(data->fields);
			data->fields = NULL;
			data->numbfields = 0;
		}

		sqlite3_free(data);
	}
}

int sqlite3_createfunction(lua_State* L, ResState* state) {

	if (lua_type(L, 2) != LUA_TTABLE) {
		luaL_error(L, "Second argument is not a table");
		return 0;
	}
	else if (lua_type(L, 3) != LUA_TFUNCTION) {
		luaL_error(L, "Third argument is not a function");
		return 0;
	}

	const char* name = luaL_checkstring(L, 1);

	if (GetRegistration(state, name)) {
		luaL_error(L, "%s is already a registered sqlite resource", name);
		return 0;
	}
	else if(!AddRegistration(state, name, RES_TYPE_VTABLE)){
		luaL_error(L, "Out of memory");
		return 0;
	}

	ExecuteLuaFunction* data = (ExecuteLuaFunction*)sqlite3_malloc(sizeof(ExecuteLuaFunction));
	if (!data) {
		luaL_error(L, "Out of memory");
		return 0;
	}
	else {
		memset(data, 0, sizeof(ExecuteLuaFunction));
		data->function_read = LUA_NOREF;
		data->function_update = LUA_NOREF;
	}

	lua_pushvalue(L, 2);
	lua_len(L, -1);
	data->numbfields = (int)lua_tointeger(L, -1);
	lua_pop(L, 1);

	if (data->numbfields <= 0) {
		lua_pop(L, 1);
		destroyfunction(data);
		luaL_error(L, "Definition table contains no entries");
		return 0;
	}
	else {

		data->fields = (char**)sqlite3_malloc(sizeof(char*) * data->numbfields);
		memset(data->fields, 0, sizeof(char*) * data->numbfields);

		if (!data->fields) {
			lua_pop(L, 1);
			destroyfunction(data);
			luaL_error(L, "No memory");
			return 0;
		}

		size_t len;
		const char* field;

		for (int i = 0; i < data->numbfields; i++)
		{
			lua_geti(L, -1, i + 1);
			field = luaL_tolstring(L, -1, &len);
			if (!field || len == 0) {
				lua_pop(L, 2);
				destroyfunction(data);
				luaL_error(L, "Field cannot be empty string");
				return 0;
			}
			else {
				for (size_t n = 0; n < len; n++)
				{
					if (!isalpha(field[n])) {
						lua_pop(L, 2);
						destroyfunction(data);
						luaL_error(L, "Field names may only contain letters");
						return 0;
					}
				}
			}

			data->fields[i] = (char*)sqlite3_malloc((int)(sizeof(char) * (len + 1)));

			if (data->fields[i] == NULL) {
				lua_pop(L, 2);
				destroyfunction(data);
				luaL_error(L, "No memory");
				return 0;
			}

			data->fields[i][len] = '\0';
			memcpy(data->fields[i], field, len * sizeof(char));

			lua_pop(L, 2);
		}
		lua_pop(L, 1);
	}

	data->L = L;
	lua_pushvalue(L, 4);

	if (lua_type(L, -1) == LUA_TFUNCTION) {
		data->function_update = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	lua_pushvalue(L, 3);

	if (lua_type(L, -1) == LUA_TFUNCTION) {
		data->function_read = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	else {
		destroyfunction(data);
		luaL_error(L, "No read function found");
		return 0;
	}

	lua_pop(L, 1);
	sqlite3_create_module_v2(state->db, name, &executeluastringModule, data, destroyfunction);
	return 0;
}
