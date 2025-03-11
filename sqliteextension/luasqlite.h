#pragma once
#include "../SQLite/sqlite3ext.h"
#include "../lua_main_incl.h"

SQLITE_EXTENSION_INIT3

#define EL_RESULT_NONE 0
#define EL_RESULT_SIMPLE 1
#define EL_RESULT_ARRAY 2
#define EL_RESULT_TABLE 3
#define EL_RESULT_FUNC 4
#define EL_RESULT_THREAD 5

void lua_pushsqlite3value(lua_State* L, sqlite3_value* value);
void lua_tosqlite3value(lua_State* L, int idx, sqlite3_context* context);

int sqlite3_createfunction(lua_State* L, sqlite3* db);
int sqlite3_registertable(lua_State* L, sqlite3* db);