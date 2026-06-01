#include "LuaDuckDB.h"
#include "mem.h"
#include <string.h>
#include <stdlib.h>
#ifndef _MAX_PATH
#include <limits.h>
#define _MAX_PATH PATH_MAX
#endif

LuaDuckDB *luaL_checkduckdb(lua_State *L, int index) {
LuaDuckDB *db = (LuaDuckDB *)luaL_checkudata(L, index, LUADUCKDB);
if (db == NULL)
luaL_error(L, "parameter is not a %s", LUADUCKDB);
return db;
}

LuaDuckDB *lua_pushduckdb(lua_State *L) {
LuaDuckDB *db = (LuaDuckDB *)lua_newuserdata(L, sizeof(LuaDuckDB));
if (db == NULL) {
luaL_error(L, "Unable to create DuckDB connection");
return NULL;
}
luaL_getmetatable(L, LUADUCKDB);
lua_setmetatable(L, -2);
memset(db, 0, sizeof(LuaDuckDB));
return db;
}

void DuckDB_FinalizeStmt(LuaDuckDB *state) {
if (state->has_result) {
duckdb_destroy_result(&state->result);
memset(&state->result, 0, sizeof(state->result));
state->has_result = false;
state->current_row = 0;
state->row_count = 0;
}
if (state->stmt) {
duckdb_destroy_prepare(&state->stmt);
state->stmt = NULL;
}
}

static void push_duckdb_value(lua_State *L, duckdb_result *result, idx_t col, idx_t row) {
if (duckdb_value_is_null(result, col, row)) {
lua_pushnil(L);
return;
}
duckdb_type dtype = duckdb_column_type(result, col);
switch (dtype) {
case DUCKDB_TYPE_BOOLEAN:
lua_pushboolean(L, duckdb_value_boolean(result, col, row) ? 1 : 0);
break;
case DUCKDB_TYPE_TINYINT:
lua_pushinteger(L, (lua_Integer)duckdb_value_int8(result, col, row));
break;
case DUCKDB_TYPE_SMALLINT:
lua_pushinteger(L, (lua_Integer)duckdb_value_int16(result, col, row));
break;
case DUCKDB_TYPE_INTEGER:
lua_pushinteger(L, (lua_Integer)duckdb_value_int32(result, col, row));
break;
case DUCKDB_TYPE_BIGINT:
lua_pushinteger(L, (lua_Integer)duckdb_value_int64(result, col, row));
break;
case DUCKDB_TYPE_UTINYINT:
lua_pushinteger(L, (lua_Integer)duckdb_value_uint8(result, col, row));
break;
case DUCKDB_TYPE_USMALLINT:
lua_pushinteger(L, (lua_Integer)duckdb_value_uint16(result, col, row));
break;
case DUCKDB_TYPE_UINTEGER:
lua_pushinteger(L, (lua_Integer)duckdb_value_uint32(result, col, row));
break;
case DUCKDB_TYPE_UBIGINT:
lua_pushnumber(L, (lua_Number)duckdb_value_uint64(result, col, row));
break;
case DUCKDB_TYPE_FLOAT:
lua_pushnumber(L, (lua_Number)duckdb_value_float(result, col, row));
break;
case DUCKDB_TYPE_DOUBLE:
lua_pushnumber(L, (lua_Number)duckdb_value_double(result, col, row));
break;
case DUCKDB_TYPE_BLOB: {
duckdb_blob blob = duckdb_value_blob(result, col, row);
lua_pushlstring(L, (const char *)blob.data, blob.size);
duckdb_free(blob.data);
break;
}
default: {
char *str = duckdb_value_varchar(result, col, row);
if (str) {
lua_pushstring(L, str);
duckdb_free(str);
}
else {
lua_pushnil(L);
}
break;
}
}
}

static int bind_params_from_table(lua_State *L, LuaDuckDB *db, int tableIdx) {
idx_t nparams = duckdb_nparams(db->stmt);
for (idx_t n = 0; n < nparams; n++) {
lua_rawgeti(L, tableIdx, (lua_Integer)(n + 1));
idx_t pidx = n + 1;
switch (lua_type(L, -1)) {
case LUA_TNIL:
duckdb_bind_null(db->stmt, pidx);
break;
case LUA_TBOOLEAN:
duckdb_bind_boolean(db->stmt, pidx, lua_toboolean(L, -1) != 0);
break;
case LUA_TNUMBER:
if (lua_isinteger(L, -1))
duckdb_bind_int64(db->stmt, pidx, (int64_t)lua_tointeger(L, -1));
else
duckdb_bind_double(db->stmt, pidx, (double)lua_tonumber(L, -1));
break;
case LUA_TSTRING: {
size_t len;
const char *s = lua_tolstring(L, -1, &len);
duckdb_bind_varchar_length(db->stmt, pidx, s, len);
break;
}
default:
duckdb_bind_null(db->stmt, pidx);
break;
}
lua_pop(L, 1);
}
return 0;
}

static int bind_params_from_function(lua_State *L, LuaDuckDB *db, int fnIdx) {
idx_t nparams = duckdb_nparams(db->stmt);
for (idx_t n = 0; n < nparams; n++) {
lua_pushvalue(L, fnIdx);
lua_pushinteger(L, (lua_Integer)(n + 1));
if (lua_pcall_nohook(L, 1, 1, 0) != 0) {
lua_error(L);
return 0;
}
idx_t pidx = n + 1;
switch (lua_type(L, -1)) {
case LUA_TNIL:
duckdb_bind_null(db->stmt, pidx);
break;
case LUA_TBOOLEAN:
duckdb_bind_boolean(db->stmt, pidx, lua_toboolean(L, -1) != 0);
break;
case LUA_TNUMBER:
if (lua_isinteger(L, -1))
duckdb_bind_int64(db->stmt, pidx, (int64_t)lua_tointeger(L, -1));
else
duckdb_bind_double(db->stmt, pidx, (double)lua_tonumber(L, -1));
break;
case LUA_TSTRING: {
size_t len;
const char *s = lua_tolstring(L, -1, &len);
duckdb_bind_varchar_length(db->stmt, pidx, s, len);
break;
}
default:
duckdb_bind_null(db->stmt, pidx);
break;
}
lua_pop(L, 1);
}
return 0;
}

int DuckDB_Open(lua_State *L) {
size_t len;
const char *path = luaL_optlstring(L, 1, ":memory:", &len);
char *file = (char *)kitsune_malloc(len + 1);
if (!file) {
luaL_error(L, "Unable to allocate memory for DuckDB");
return 0;
}
file[len] = 0;
memcpy(file, path, len);

lua_pop(L, lua_gettop(L));
LuaDuckDB *db = lua_pushduckdb(L);
if (!db) {
kitsune_free(file);
return 0;
}
db->file = file;

if (duckdb_open(file, &db->db) == DuckDBError) {
const char *msg = "DuckDB: failed to open database";
kitsune_free(db->file);
db->file = NULL;
return luaL_error(L, msg);
}

kitsune_snapshot_permanent_allocs();

if (duckdb_connect(db->db, &db->con) == DuckDBError) {
duckdb_close(&db->db);
kitsune_free(db->file);
db->file = NULL;
return luaL_error(L, "DuckDB: failed to connect to database");
}

return 1;
}

int DuckDB_Execute(lua_State *L) {
LuaDuckDB *db = luaL_checkduckdb(L, 1);
if (!db->con)
return luaL_error(L, "DuckDB instance has been closed");

const char *query = luaL_checkstring(L, 2);

DuckDB_FinalizeStmt(db);

if (duckdb_prepare(db->con, query, &db->stmt) == DuckDBError) {
const char *err = duckdb_prepare_error(db->stmt);
lua_pop(L, lua_gettop(L));
lua_pushboolean(L, 0);
lua_pushstring(L, err ? err : "prepare error");
DuckDB_FinalizeStmt(db);
return 2;
}

if (lua_istable(L, 3))
bind_params_from_table(L, db, 3);
else if (lua_isfunction(L, 3))
bind_params_from_function(L, db, 3);

if (duckdb_execute_prepared(db->stmt, &db->result) == DuckDBError) {
const char *err = duckdb_result_error(&db->result);
lua_pop(L, lua_gettop(L));
lua_pushboolean(L, 0);
lua_pushstring(L, err ? err : "execute error");
DuckDB_FinalizeStmt(db);
return 2;
}

db->has_result = true;
db->row_count = duckdb_row_count(&db->result);
db->current_row = 0;

lua_pop(L, lua_gettop(L));

if (db->row_count > 0) {
	lua_pushboolean(L, 1);
	lua_pushstring(L, "ROW");
}
else {
	lua_pushboolean(L, 1);
	lua_pushstring(L, "DONE");
	DuckDB_FinalizeStmt(db);
}

return 2;
}

int DuckDB_Fetch(lua_State *L) {
LuaDuckDB *db = luaL_checkduckdb(L, 1);
if (!db->con)
return luaL_error(L, "DuckDB instance has been closed");

lua_pop(L, 1);

if (!db->has_result) {
	lua_pushboolean(L, 0);
	return 1;
}

db->current_row++;
if (db->current_row <= db->row_count) {
	lua_pushboolean(L, 1);
	return 1;
}

DuckDB_FinalizeStmt(db);
lua_pushboolean(L, 0);
return 1;
}

int DuckDB_GetRow(lua_State *L) {
LuaDuckDB *db = luaL_checkduckdb(L, 1);
if (!db->con)
return luaL_error(L, "DuckDB instance has been closed");

if (!db->has_result || db->current_row == 0) {
lua_pop(L, lua_gettop(L));
lua_pushnil(L);
return 1;
}

idx_t row = db->current_row - 1;

int idx = (int)luaL_optinteger(L, 2, -1);
if (idx > 0) {
idx--;
idx_t ncols = duckdb_column_count(&db->result);
lua_pop(L, lua_gettop(L));
if ((idx_t)idx >= ncols)
lua_pushnil(L);
else
push_duckdb_value(L, &db->result, (idx_t)idx, row);
return 1;
}

idx_t ncols = duckdb_column_count(&db->result);
lua_pop(L, lua_gettop(L));
lua_createtable(L, 0, (int)ncols);
for (idx_t n = 0; n < ncols; n++) {
lua_pushstring(L, duckdb_column_name(&db->result, n));
push_duckdb_value(L, &db->result, n, row);
lua_settable(L, -3);
}
return 1;
}

int DuckDB_Finish(lua_State *L) {
LuaDuckDB *db = luaL_checkduckdb(L, 1);
DuckDB_FinalizeStmt(db);
return 0;
}

int DuckDB_GC(lua_State *L) {
LuaDuckDB *db = luaL_checkduckdb(L, 1);
DuckDB_FinalizeStmt(db);
if (db->con) {
duckdb_disconnect(&db->con);
db->con = NULL;
}
if (db->db) {
duckdb_close(&db->db);
db->db = NULL;
}
if (db->file) {
kitsune_free(db->file);
db->file = NULL;
}
return 0;
}

int DuckDB_ToString(lua_State *L) {
LuaDuckDB *db = luaL_checkduckdb(L, 1);
char buf[_MAX_PATH + 20];
sprintf(buf, "DuckDB: %p File: %s", (void *)db, db->file ? db->file : "(closed)");
lua_pushstring(L, buf);
return 1;
}
