#pragma once
#include "../SQLite/sqlite3ext.h"
#include "../lua_main_incl.h"

SQLITE_EXTENSION_INIT3

void lua_pushsqlite3value(lua_State* L, sqlite3_value* value);
void lua_tosqlite3value(lua_State* L, int idx, sqlite3_context* context);

int sqlite3_createfunction(lua_State* L, sqlite3* db);