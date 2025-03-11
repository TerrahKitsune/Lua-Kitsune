#include "luasqlite.h"
#include <string.h>
#include <ctype.h>

int crc64func_ref = LUA_NOREF;

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
	sqlite3_int64 row;
	RegisteredTableContext* context;
} RegisteredTableCursor;

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

	*ppCursor = (sqlite3_vtab_cursor*)cursor;
	return SQLITE_OK;
}

static int registeredtable_close(sqlite3_vtab_cursor* cursor) {
	RegisteredTableCursor* context = (RegisteredTableCursor*)cursor;

	lua_gc(context->context->table->L, LUA_GCCOLLECT, 0);
	sqlite3_free(cursor);
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
	lua_State* L = luacursor->context->table->L;
	lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->context->table->table_ref);
	lua_len(L, -1);
	int len = lua_tointeger(L, -1);
	lua_pop(L, 2);
	return len <= luacursor->row;
}

static int registeredtable_next(sqlite3_vtab_cursor* cursor) {
	RegisteredTableCursor* luacursor = (RegisteredTableCursor*)cursor;
	luacursor->row++;
	return SQLITE_OK;
}

static int registeredtable_column(sqlite3_vtab_cursor* cursor, sqlite3_context* context, int N) {
	RegisteredTableCursor* luacursor = (RegisteredTableCursor*)cursor;
	lua_State* L = luacursor->context->table->L;
	lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->context->table->table_ref);
	lua_geti(L, -1, luacursor->row + 1);

	if (lua_isnoneornil(L, -1)) {
		sqlite3_result_null(context);
	}
	else {
		lua_geti(L, -1, N + 1);
		lua_tosqlite3value(L, -1, context);
		lua_pop(L, 1);
	}

	lua_pop(L, 2);

	return SQLITE_OK;
}

static int registeredtable_rowid(sqlite3_vtab_cursor* cursor, sqlite_int64* pRowid) {

	RegisteredTableCursor* luacursor = (RegisteredTableCursor*)cursor;
	*pRowid = luacursor->row;

	return SQLITE_OK;
}

static int registeredtable_pushluavaluebykey(lua_State* L, int len) {

	for (int i = 0; i < len; i++)
	{
		lua_geti(L, -1, i + 1);
		if (!lua_isnoneornil(L, -1)) {
			lua_geti(L, -1, 1);
			
			if (lua_rawequal(L, -1, -4)) {
				lua_pop(L, 1);
				return i + 1;
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}

	return 0;
}

static int registeredtable_update(sqlite3_vtab* vtab, int argc, sqlite3_value** argv, sqlite3_int64* pRowid) {
	RegisteredTableContext* context = (RegisteredTableContext*)vtab;
	lua_State* L = context->table->L;
	lua_rawgeti(L, LUA_REGISTRYINDEX, context->table->table_ref);

	if (lua_isnoneornil(L, -1)) {
		lua_pop(L, 1);
		return SQLITE_ERROR;
	}

	for (size_t i = 0; i < argc; i++)
	{
		lua_pushsqlite3value(L, argv[i]);
	}

	// Delete
	if (argc == 1) {
		lua_pushvalue(L, -2);
		lua_len(L, -1);
		int len = lua_tointeger(L, -1);
		lua_pop(L, 1);
		int index = registeredtable_pushluavaluebykey(L, len);
		if (index > 0) {
			lua_pop(L, 1);
			if (index >= len) {
				lua_pushnil(L);
				lua_rawseti(L, -2, index);
			}
			else {
				lua_rawgeti(L, -1, len);
				lua_rawseti(L, -2, index);
				lua_pushnil(L);
				lua_rawseti(L, -2, len);
			}
		}

		lua_pop(L, 3);	
	}
	// Insert 
	else if (lua_isnil(L, argc * -1)) {

		lua_len(L, (argc + 1) * -1);
		int len = lua_tointeger(L, -1);
		lua_pop(L, 1);
		lua_createtable(L, context->table->numbfields, 0);

		for (size_t i = 0; i < argc - 2; i++)
		{
			lua_pushvalue(L, ((argc - 1) - i) * -1);
			lua_rawseti(L, -2, i + 1);
		}

		lua_rawseti(L, (argc + 2) * -1, len + 1);
		lua_pop(L, argc + 1);
	}
	else if (argc >= 3) {
		lua_len(L, (argc + 1) * -1);
		int len = lua_tointeger(L, -1);
		lua_pop(L, 1);
		lua_pushvalue(L, argc * -1);
		lua_pushvalue(L, (argc + 2) * -1);
		if (registeredtable_pushluavaluebykey(L, len)) {
			lua_copy(L, -1, -3);
			lua_pop(L, 2);
			for (size_t i = 0; i < argc - 2; i++)
			{
				lua_pushvalue(L, ((argc - 1) - i) * -1);
				lua_rawseti(L, -2, i + 1);
			}
		}
		else {
			lua_pop(L, 1);
		}
		lua_pop(L, argc + 2);
	}
	else {
		lua_pop(L, 1);
		return SQLITE_ERROR;
	}

	return SQLITE_OK;
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
		luaL_unref(data->L, LUA_REGISTRYINDEX, data->table_ref);
		data->table_ref = -1;

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

	if (lua_type(L, -1) != LUA_TTABLE) {
		luaL_error(L, "Third argument is not a table");
		return 0;
	}
	else if (lua_type(L, -2) != LUA_TTABLE) {
		luaL_error(L, "Second argument is not a table");
		return 0;
	}

	if (crc64func_ref == LUA_NOREF) {
		lua_getglobal(L, "CRC64");
		crc64func_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	const char* name = luaL_checkstring(L, -3);

	RegisteredTable* data = (RegisteredTable*)sqlite3_malloc(sizeof(RegisteredTable));
	if (!data) {
		luaL_error(L, "Out of memory");
		return 0;
	}
	else {
		memset(data, 0, sizeof(RegisteredTable));
		data->table_ref = LUA_NOREF;
	}

	lua_pushvalue(L, -2);
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

			if (!data->fields[i]) {
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

	return sqlite3_create_module_v2(db, name, &registertableModule, data, destroytable);
}
