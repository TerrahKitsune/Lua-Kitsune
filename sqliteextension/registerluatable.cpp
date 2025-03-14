#include "luasqlite.h"
#include <string.h>
#include <ctype.h>

typedef struct {
	int table_ref;
	int numbfields;
	char** fields;
	lua_State* L;
} RegisteredTable;

typedef struct {
	sqlite3_vtab vtab;
	RegisteredTable* table;
} RegisteredTableContext;

typedef struct {
	sqlite3_vtab_cursor cursor;
	RegisteredTableContext* context;
	int key_ref;
	int value_ref;
} RegisteredTableCursor;

static int registeredtable_next(sqlite3_vtab_cursor* cursor);

static int registeredtable_connect(sqlite3* db, void* pAux, int argc, const char* const* argv, sqlite3_vtab** ppVtab, char** pzErr) {

	RegisteredTableContext* context = (RegisteredTableContext*)sqlite3_malloc(sizeof(RegisteredTableContext));

	if (!context) {
		return SQLITE_NOMEM;
	}

	char* createtable = (char*)sqlite3_malloc(1000000 * sizeof(char));
	if (!createtable) {
		sqlite3_free(context);
		return SQLITE_NOMEM;
	}

	context->table = (RegisteredTable*)pAux;
	*ppVtab = &context->vtab;
	(*ppVtab)->nRef = 0;

	strcpy(createtable, "CREATE TABLE x(");
	for (size_t i = 0; i < context->table->numbfields; i++)
	{
		strcat(createtable, context->table->fields[i]);
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

static int registeredtable_disconnect(sqlite3_vtab* pVtab) {
	sqlite3_free(pVtab);
	return SQLITE_OK;
}

static int registeredtable_open(sqlite3_vtab* pVtab, sqlite3_vtab_cursor** ppCursor) {
	RegisteredTableContext* context = (RegisteredTableContext*)pVtab;
	RegisteredTableCursor* cursor = (RegisteredTableCursor*)sqlite3_malloc(sizeof(RegisteredTableCursor));

	if (!cursor) {
		return SQLITE_NOMEM;
	}
	else {
		memset(cursor, 0, sizeof(RegisteredTableCursor));
	}

	cursor->context = context;
	cursor->key_ref = LUA_NOREF;
	cursor->value_ref = LUA_NOREF;

	*ppCursor = (sqlite3_vtab_cursor*)cursor;

	return registeredtable_next(*ppCursor);
}

static int registeredtable_close(sqlite3_vtab_cursor* pCursor) {
	RegisteredTableCursor* cursor = (RegisteredTableCursor*)pCursor;
	lua_State* L = cursor->context->table->L;

	if (cursor->key_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, cursor->key_ref);
		cursor->key_ref = LUA_NOREF;
	}

	if (cursor->value_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, cursor->value_ref);
		cursor->value_ref = LUA_NOREF;
	}

	lua_gc(L, LUA_GCCOLLECT, 0);
	sqlite3_free(pCursor);
	return SQLITE_OK;
}

static int registeredtable_bestindex(sqlite3_vtab* pVTab, sqlite3_index_info* info) {
	return SQLITE_OK;
}

static int registeredtable_filter(sqlite3_vtab_cursor*, int idxNum, const char* idxStr, int argc, sqlite3_value** argv) {
	return SQLITE_OK;
}

static int registeredtable_eof(sqlite3_vtab_cursor* cursor) {
	RegisteredTableCursor* luacursor = (RegisteredTableCursor*)cursor;
	return luacursor->key_ref == LUA_NOREF;
}

static int registeredtable_next(sqlite3_vtab_cursor* cursor) {
	RegisteredTableCursor* luacursor = (RegisteredTableCursor*)cursor;
	lua_State* L = luacursor->context->table->L;
	lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->context->table->table_ref);

	if (luacursor->key_ref == LUA_NOREF) {
		lua_pushnil(L);
	}
	else {
		lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->key_ref);
	}

	if (lua_next(L, -2)) {

		if (luacursor->value_ref == LUA_NOREF) {
			luacursor->value_ref = luaL_ref(L, LUA_REGISTRYINDEX);
		}
		else {
			lua_rawseti(L, LUA_REGISTRYINDEX, luacursor->value_ref);
		}
		if (luacursor->key_ref == LUA_NOREF) {
			luacursor->key_ref = luaL_ref(L, LUA_REGISTRYINDEX);
		}
		else {
			lua_rawseti(L, LUA_REGISTRYINDEX, luacursor->key_ref);
		}
	}
	else {

		if (luacursor->key_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, luacursor->key_ref);
			luacursor->key_ref = LUA_NOREF;
		}

		if (luacursor->value_ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, luacursor->value_ref);
			luacursor->value_ref = LUA_NOREF;
		}
	}

	lua_pop(L, 1);

	return SQLITE_OK;
}

static int registeredtable_column(sqlite3_vtab_cursor* cursor, sqlite3_context* context, int N) {
	RegisteredTableCursor* luacursor = (RegisteredTableCursor*)cursor;
	lua_State* L = luacursor->context->table->L;

	if (N == 0) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->key_ref);
		lua_tosqlite3value(L, -1, context);
	}
	else {
		lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->value_ref);
		if (lua_istable(L, -1)) {
			lua_geti(L, -1, N);
			lua_tosqlite3value(L, -1, context);
			lua_pop(L, 1);
		}
		else {
			lua_tosqlite3value(L, -1, context);
		}
	}
	
	lua_pop(L, 1);

	return SQLITE_OK;
}

static int registeredtable_rowid(sqlite3_vtab_cursor* cursor, sqlite_int64* pRowid) {
	return SQLITE_OK;
}

static int registeredtable_update(sqlite3_vtab* vtab, int argc, sqlite3_value** argv, sqlite3_int64* pRowid) {
	RegisteredTableContext* context = (RegisteredTableContext*)vtab;
	lua_State* L = context->table->L;
	int top = lua_gettop(L);
	lua_rawgeti(L, LUA_REGISTRYINDEX, context->table->table_ref);

	if (lua_isnoneornil(L, -1)) {
		lua_settop(L, top);
		return SQLITE_ERROR;
	}

	for (size_t i = 0; i < argc; i++)
	{
		lua_pushsqlite3value(L, argv[i]);
	}

	// Delete
	if (argc == 1) {
		lua_pushnil(L);
		lua_settable(L, -3);
		lua_settop(L, top);
		return SQLITE_OK;
	}
	// Insert 
	else if (lua_isnil(L, argc * -1)) {

		lua_pushvalue(L, (argc + 1) * -1);
		lua_pushvalue(L, (argc - 1) * -1);
		lua_gettable(L, -2);

		if (lua_isnil(L, -1)) {
			lua_pop(L, 2);

			int realargs = argc - 3;
			if (context->table->numbfields > 2)
			{
				lua_createtable(L, realargs, 0);

				for (size_t i = 0; i < realargs; i++)
				{
					lua_pushvalue(L, (realargs - i + 1) * -1);
					lua_rawseti(L, -2, i + 1);
				}
			}
			else if (context->table->numbfields == 2) {
				lua_pushvalue(L, -1);
			}
			else if (context->table->numbfields == 1) {
				lua_pushboolean(L, 1);
			}

			lua_pushvalue(L, (argc - 1) * -1);
			lua_pushvalue(L, -2);
			lua_settable(L, (argc + 4) * -1);
			lua_settop(L, top);
			return SQLITE_OK;
		}
		else {
			lua_pop(L, 2);
			size_t len;
			const char* errorkey = luaL_tolstring(L, (argc - 2) * -1, &len);
			lua_settop(L, top);
			vtab->zErrMsg = sqlite3_mprintf("Duplicate key: %s", errorkey);
			return SQLITE_ERROR;
		}
	}
	else if (argc >= 3) {
		lua_pushvalue(L, argc * -1);
		lua_gettable(L, (argc + 2) * -1);

		if (lua_isnil(L, -1)) {
			lua_pushvalue(L, (argc+1) * -1);
			lua_pop(L, 2);
			size_t len;
			const char* errorkey = luaL_tolstring(L, (argc - 2) * -1, &len);
			lua_settop(L, top);
			vtab->zErrMsg = sqlite3_mprintf("Update key not found: %s", errorkey);
			return SQLITE_ERROR;
		}
		else if (context->table->numbfields <= 2) {
			lua_pop(L, 1);
			if (context->table->numbfields == 1) {
				lua_pushboolean(L, 1);
			}
			lua_settable(L, (argc + 1) * -1);

			if (lua_rawequal(L, (argc - 2) * -1, (argc - 3) * -1)) {
				lua_settop(L, top);
				return SQLITE_OK;
			}

			lua_pushvalue(L, (argc - 2) * -1);
			lua_pushnil(L);
			lua_settable(L, (argc + 1) * -1);
			lua_settop(L, top);
			return SQLITE_OK;
		}
		else 
		{
			int realargs = argc - 3;
			for (size_t i = 0; i < realargs; i++)
			{
				lua_pushvalue(L, (realargs - i + 1) * -1);
				lua_rawseti(L, -2, i + 1);
			}

			if (lua_rawequal(L, (argc + 1) * -1, argc * -1)) {
				lua_settop(L, top);
				return SQLITE_OK;
			}

			lua_pushvalue(L, (argc + 1) * -1);
			lua_pushnil(L);
			lua_settable(L, (argc + 4) * -1);
			lua_pushvalue(L, argc * -1);
			lua_pushvalue(L, -2);
			lua_settable(L, (argc + 4) * -1);
			lua_settop(L, top);
		}

		return SQLITE_OK;
	}
	else {
		lua_settop(L, top);
	}

	return SQLITE_ERROR;
}

static sqlite3_module registertableModule = {
	0,
	registeredtable_connect,
	registeredtable_connect,
	registeredtable_bestindex,
	registeredtable_disconnect,
	registeredtable_disconnect,
	registeredtable_open,
	registeredtable_close,
	registeredtable_filter,
	registeredtable_next,
	registeredtable_eof,
	registeredtable_column,
	registeredtable_rowid,
	registeredtable_update,
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

static void destroytable(void* pClientData) {
	RegisteredTable* data = (RegisteredTable*)pClientData;
	if (data) {
		if (data->table_ref != LUA_NOREF) {
			luaL_unref(data->L, LUA_REGISTRYINDEX, data->table_ref);
			data->table_ref = LUA_NOREF;
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

int sqlite3_registertable(lua_State* L, sqlite3* db) {

	if (lua_type(L, 2) != LUA_TTABLE) {
		luaL_error(L, "Second argument is not a table");
		return 0;
	}
	else if (lua_type(L, 3) != LUA_TTABLE) {
		luaL_error(L, "Third argument is not a table");
		return 0;
	}

	const char* name = luaL_checkstring(L, 1);

	RegisteredTable* data = (RegisteredTable*)sqlite3_malloc(sizeof(RegisteredTable));
	if (!data) {
		luaL_error(L, "Out of memory");
		return 0;
	}
	else {
		memset(data, 0, sizeof(RegisteredTable));
		data->table_ref = LUA_NOREF;
	}

	lua_pushvalue(L, 2);
	lua_len(L, -1);
	data->numbfields = lua_tointeger(L, -1);
	lua_pop(L, 1);

	if (data->numbfields <= 0) {
		lua_pop(L, 1);
		destroytable(data);
		luaL_error(L, "Definition table contains no entries");
		return 0;
	}
	else {

		data->fields = (char**)sqlite3_malloc(sizeof(char*) * data->numbfields);
		memset(data->fields, 0, sizeof(char*) * data->numbfields);

		if (!data->fields) {
			lua_pop(L, 1);
			destroytable(data);
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
				destroytable(data);
				luaL_error(L, "Field cannot be empty string");
				return 0;
			}
			else {
				for (size_t n = 0; n < len; n++)
				{
					if (!isalpha(field[n])) {
						lua_pop(L, 2);
						destroytable(data);
						luaL_error(L, "Field names may only contain letters");
						return 0;
					}
				}
			}

			data->fields[i] = (char*)sqlite3_malloc(sizeof(char) * (len + 1));

			if (data->fields[i] == NULL) {
				lua_pop(L, 2);
				destroytable(data);
				luaL_error(L, "No memory");
				return 0;
			}

			data->fields[i][len] = '\0';
			memcpy(data->fields[i], field, len * sizeof(char));

			lua_pop(L, 2);
		}
		lua_pop(L, 1);
	}

	data->table_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	data->L = L;
	lua_pop(L, 1);
	sqlite3_create_module_v2(db, name, &registertableModule, data, destroytable);
	return 0;
}
