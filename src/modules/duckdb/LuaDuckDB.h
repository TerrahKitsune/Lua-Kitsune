#pragma once
#include "LuaMainInclude.h"
#include "DuckDB/duckdb.h"

static const char *LUADUCKDB = "DuckDB";

typedef struct LuaDuckDB {
	duckdb_database db;
	duckdb_connection con;
	duckdb_prepared_statement stmt;
	duckdb_result result;
	idx_t current_row;
	idx_t row_count;
	bool has_result;
	char *file;
} LuaDuckDB;

LuaDuckDB *luaL_checkduckdb(lua_State *L, int index);
LuaDuckDB *lua_pushduckdb(lua_State *L);

void DuckDBFinalizeStmt(LuaDuckDB *state);

int DuckDB_Open(lua_State *L);
int DuckDB_Execute(lua_State *L);
int DuckDB_Fetch(lua_State *L);
int DuckDB_GetRow(lua_State *L);
int DuckDB_Finish(lua_State *L);
int DuckDB_ToString(lua_State *L);
int DuckDB_Close(lua_State *L);
