#include "luasqlite.h"
#include <string.h>

typedef struct {
	sqlite3_vtab_cursor cursor;
	lua_State* L;
	sqlite_int64 row;
	int result_ref;
	int iterator_ref;
	int value_ref;
	int elresult;
} ExecuteLuaFunctionCursor;

typedef struct {
	sqlite3_vtab vtab;
	lua_State* L;
	int function_ref;
} ExecuteLuaFunctionContext;

typedef struct {
	int function_ref;
	lua_State* L;
} ExecuteLuaFunction;

static int executeluastring_connect(sqlite3* db, void* pAux, int argc, const char* const* argv, sqlite3_vtab** ppVtab, char** pzErr) {

	ExecuteLuaFunctionContext* context = (ExecuteLuaFunctionContext*)sqlite3_malloc(sizeof(ExecuteLuaFunctionContext));

	if (!context) {
		return SQLITE_NOMEM;
	}

	ExecuteLuaFunction* func = (ExecuteLuaFunction*)pAux;

	context->L = func->L;
	context->function_ref = func->function_ref;
	*ppVtab = &context->vtab;
	(*ppVtab)->nRef = 0;

	return sqlite3_declare_vtab(db, "CREATE TABLE x(key, value)");
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

	cursor->iterator_ref = LUA_NOREF;
	cursor->value_ref = LUA_NOREF;
	lua_State* L = context->L;
	lua_State* thread;
	lua_rawgeti(L, LUA_REGISTRYINDEX, context->function_ref);

	if (lua_pcall(L, 0, 1, NULL)) {
		pVtab->zErrMsg = sqlite3_mprintf("Error: %s", lua_tostring(L, -1));
		lua_pop(L, 1);
		return SQLITE_ERROR;
	}

	switch (lua_type(L, -1)) {

	case LUA_TBOOLEAN:
	case LUA_TSTRING:
	case LUA_TNUMBER:
		cursor->elresult = EL_RESULT_SIMPLE;
		cursor->result_ref = luaL_ref(L, LUA_REGISTRYINDEX);
		break;
	case LUA_TFUNCTION:
		cursor->elresult = EL_RESULT_FUNC;
		cursor->result_ref = luaL_ref(L, LUA_REGISTRYINDEX);
		break;
	case LUA_TTHREAD:
		cursor->elresult = EL_RESULT_THREAD;
		cursor->result_ref = luaL_ref(L, LUA_REGISTRYINDEX);
		break;
	case LUA_TTABLE:
		lua_rawgeti(L, -1, 1);
		if (lua_isnoneornil(L, -1)) {
			if (lua_next(L, -2)) {
				lua_pop(L, 2);
				cursor->elresult = EL_RESULT_TABLE;
				cursor->result_ref = luaL_ref(L, LUA_REGISTRYINDEX);
			}
			else {
				lua_pop(L, 2);
				cursor->elresult = EL_RESULT_NONE;
				cursor->result_ref = LUA_NOREF;
			}
		}
		else {
			lua_pop(L, 1);
			cursor->elresult = EL_RESULT_ARRAY;
			cursor->result_ref = luaL_ref(L, LUA_REGISTRYINDEX);
		}
		break;
	default:
		lua_pop(L, 1);
		cursor->result_ref = LUA_NOREF;
		break;
	}

	cursor->L = L;
	*ppCursor = (sqlite3_vtab_cursor*)cursor;
	return SQLITE_OK;
}

static int executeluastring_close(sqlite3_vtab_cursor* cursor) {

	ExecuteLuaFunctionCursor* luacursor = (ExecuteLuaFunctionCursor*)cursor;
	if (luacursor->result_ref != LUA_NOREF) {
		luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->result_ref);
		luacursor->result_ref = LUA_NOREF;
	}

	if (luacursor->iterator_ref != LUA_NOREF) {
		luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->iterator_ref);
		luacursor->iterator_ref = LUA_NOREF;
	}

	if (luacursor->value_ref != LUA_NOREF) {
		luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->value_ref);
		luacursor->value_ref = LUA_NOREF;
	}

	lua_gc(luacursor->L, LUA_GCCOLLECT, 0);
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
	lua_State* L = luacursor->L;
	lua_State* thread;
	int status;
	lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->result_ref);

	switch (luacursor->elresult) {
	case EL_RESULT_ARRAY:
		lua_len(L, -1);
		if (lua_tointeger(L, -1) <= luacursor->row) {
			luaL_unref(L, LUA_REGISTRYINDEX, luacursor->result_ref);
			luacursor->result_ref = LUA_NOREF;
		}
		lua_pop(L, 1);
		break;
	case EL_RESULT_TABLE:
		if (luacursor->iterator_ref == LUA_NOREF) {
			lua_pushnil(L);
		}
		else {
			lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->iterator_ref);
		}

		if (lua_next(L, -2)) {

			if (luacursor->value_ref == LUA_NOREF) {
				luacursor->value_ref = luaL_ref(L, LUA_REGISTRYINDEX);
			}
			else {
				lua_rawseti(L, LUA_REGISTRYINDEX, luacursor->value_ref);
			}
			if (luacursor->iterator_ref == LUA_NOREF) {
				luacursor->iterator_ref = luaL_ref(L, LUA_REGISTRYINDEX);
			}
			else {
				lua_rawseti(L, LUA_REGISTRYINDEX, luacursor->iterator_ref);
			}
		}
		else if (luacursor->iterator_ref != LUA_NOREF) {
			if (luacursor->result_ref != LUA_NOREF) {
				luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->result_ref);
				luacursor->result_ref = LUA_NOREF;
			}
			if (luacursor->iterator_ref != LUA_NOREF) {
				luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->iterator_ref);
				luacursor->iterator_ref = LUA_NOREF;
			}
			if (luacursor->value_ref != LUA_NOREF) {
				luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->value_ref);
				luacursor->value_ref = LUA_NOREF;
			}
		}

		break;
	case EL_RESULT_THREAD:

		thread = lua_tothread(L, -1);
		status = lua_status(thread);

		if (status == LUA_YIELD || (status == LUA_OK && luacursor->iterator_ref == LUA_NOREF)) {
			lua_resume(thread, L, 0);
			status = lua_gettop(thread);

			if (status == 0) {
				lua_pushnil(L);
				lua_pushnil(L);
			}
			else if (status == 1) {
				lua_xmove(thread, L, 1);
				lua_pushnil(L);
			}
			else {
				lua_xmove(thread, L, 2);
				if (status > 2) {
					lua_settop(thread, 0);
				}
			}

			if (lua_type(L, -1) == LUA_TNIL && lua_type(L, -2) == LUA_TNIL) {

				if (luacursor->result_ref != LUA_NOREF) {
					luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->result_ref);
					luacursor->result_ref = LUA_NOREF;
				}
				if (luacursor->iterator_ref != LUA_NOREF) {
					luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->iterator_ref);
					luacursor->iterator_ref = LUA_NOREF;
				}
				if (luacursor->value_ref != LUA_NOREF) {
					luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->value_ref);
					luacursor->value_ref = LUA_NOREF;
				}

				lua_pop(L, 2);
			}
			else 
			{
				if (luacursor->value_ref == LUA_NOREF) {
					luacursor->value_ref = luaL_ref(L, LUA_REGISTRYINDEX);
				}
				else {
					lua_rawseti(L, LUA_REGISTRYINDEX, luacursor->value_ref);
				}

				if (luacursor->iterator_ref == LUA_NOREF) {
					luacursor->iterator_ref = luaL_ref(L, LUA_REGISTRYINDEX);
				}
				else {
					lua_rawseti(L, LUA_REGISTRYINDEX, luacursor->iterator_ref);
				}
			}
		}
		else {

			if (luacursor->result_ref != LUA_NOREF) {
				luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->result_ref);
				luacursor->result_ref = LUA_NOREF;
			}
			if (luacursor->iterator_ref != LUA_NOREF) {
				luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->iterator_ref);
				luacursor->iterator_ref = LUA_NOREF;
			}
			if (luacursor->value_ref != LUA_NOREF) {
				luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->value_ref);
				luacursor->value_ref = LUA_NOREF;
			}
		}
		break;
	case EL_RESULT_FUNC:

		lua_pushinteger(L, luacursor->row + 1);
		if (lua_pcall(L, 1, 2, NULL)) {

			if (luacursor->iterator_ref != LUA_NOREF) {
				luaL_unref(L, LUA_REGISTRYINDEX, luacursor->iterator_ref);
				luacursor->iterator_ref = LUA_NOREF;
			}

			if (luacursor->value_ref == LUA_NOREF) {
				luacursor->value_ref = luaL_ref(L, LUA_REGISTRYINDEX);
			}
			else {
				lua_rawseti(L, LUA_REGISTRYINDEX, luacursor->value_ref);
			}

			lua_pop(L, 1);
		}
		else {
			if (luacursor->value_ref == LUA_NOREF) {
				luacursor->value_ref = luaL_ref(L, LUA_REGISTRYINDEX);
			}
			else {
				lua_rawseti(L, LUA_REGISTRYINDEX, luacursor->value_ref);
			}

			if (luacursor->iterator_ref == LUA_NOREF) {
				luacursor->iterator_ref = luaL_ref(L, LUA_REGISTRYINDEX);
			}
			else {
				lua_rawseti(L, LUA_REGISTRYINDEX, luacursor->iterator_ref);
			}
		}

		if (luacursor->iterator_ref == LUA_NOREF) {
			lua_pushnil(L);
		}
		else {
			lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->iterator_ref);
			if (lua_type(L, -1) == LUA_TNIL) {
				if (luacursor->result_ref != LUA_NOREF) {
					luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->result_ref);
					luacursor->result_ref = LUA_NOREF;
				}
				if (luacursor->iterator_ref != LUA_NOREF) {
					luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->iterator_ref);
					luacursor->iterator_ref = LUA_NOREF;
				}
				if (luacursor->value_ref != LUA_NOREF) {
					luaL_unref(luacursor->L, LUA_REGISTRYINDEX, luacursor->value_ref);
					luacursor->value_ref = LUA_NOREF;
				}
			}
		}

		break;
	default:
		if (luacursor->row > 0) {
			luaL_unref(L, LUA_REGISTRYINDEX, luacursor->result_ref);
			luacursor->result_ref = LUA_NOREF;
		}
		break;
	}

	lua_pop(L, 1);

	return luacursor->result_ref == LUA_NOREF;
}

int executeluastring_column(sqlite3_vtab_cursor* cursor, sqlite3_context* context, int N) {

	ExecuteLuaFunctionCursor* luacursor = (ExecuteLuaFunctionCursor*)cursor;
	lua_State* L = luacursor->L;
	lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->result_ref);

	if (N == 0) {
		switch (luacursor->elresult) {
		case EL_RESULT_TABLE:
		case EL_RESULT_FUNC:
		case EL_RESULT_THREAD:
			if (luacursor->iterator_ref == LUA_NOREF && luacursor->value_ref != LUA_NOREF) {
				lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->value_ref);
				sqlite3_result_error(context, lua_tostring(L, -1), -1);
				lua_pop(L, 1);
				return SQLITE_ERROR;
			}
			else {
				lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->iterator_ref);
				lua_tosqlite3value(L, -1, context);
				lua_pop(L, 1);
			}
			break;
		default:
			lua_pushinteger(L, luacursor->row + 1);
			lua_tosqlite3value(L, -1, context);
			lua_pop(L, 1);
			break;
		}
	}
	else if (N == 1) {
		switch (luacursor->elresult) {
		case EL_RESULT_TABLE:
		case EL_RESULT_FUNC:
		case EL_RESULT_THREAD:
			lua_rawgeti(L, LUA_REGISTRYINDEX, luacursor->value_ref);
			lua_tosqlite3value(L, -1, context);
			lua_pop(L, 1);
			break;
		case EL_RESULT_ARRAY:
			lua_rawgeti(L, -1, luacursor->row + 1);
			lua_tosqlite3value(L, -1, context);
			lua_pop(L, 1);
			break;
		default:
			DumpStack(L);
			lua_tosqlite3value(L, -1, context);
			break;
		}
	}

	lua_pop(L, 1);

	return SQLITE_OK;
}

int executeluastring_next(sqlite3_vtab_cursor* cursor) {
	ExecuteLuaFunctionCursor* luacursor = (ExecuteLuaFunctionCursor*)cursor;
	luacursor->row++;
	return SQLITE_OK;
}

int executeluastring_rowid(sqlite3_vtab_cursor* cursor, sqlite_int64* pRowid) {

	ExecuteLuaFunctionCursor* luacursor = (ExecuteLuaFunctionCursor*)cursor;
	*pRowid = luacursor->row;

	return SQLITE_OK;
}

int executeluastring_findfunction(sqlite3_vtab* pVtab, int nArg, const char* zName, void (**pxFunc)(sqlite3_context*, int, sqlite3_value**), void** ppArg) {

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
	0,
	0,
	0,
	0,
	0,
	executeluastring_findfunction,
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
		luaL_unref(data->L, LUA_REGISTRYINDEX, data->function_ref);
		data->function_ref = -1;
		sqlite3_free(data);
	}
}

int sqlite3_createfunction(lua_State* L, sqlite3* db) {

	if (lua_type(L, -1) != LUA_TFUNCTION) {
		luaL_error(L, "Second argument is not a function");
		return 0;
	}

	const char* name = luaL_checkstring(L, -2);

	ExecuteLuaFunction* data = (ExecuteLuaFunction*)sqlite3_malloc(sizeof(ExecuteLuaFunction));
	if (!data) {
		luaL_error(L, "Out of memory");
		return 0;
	}

	data->function_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	data->L = L;
	lua_pop(L, 1);

	return sqlite3_create_module_v2(db, name, &executeluastringModule, data, destroyfunction);
}
