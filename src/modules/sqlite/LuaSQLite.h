#pragma once
#include "LuaMainInclude.h"
#include "SQLite/sqlite3.h"
static const char * LUASQLITE = "SQLite";

typedef struct LuaSQLiteFunction {
	char* name;
	int index;
	int args;
	bool isaggregate;
	lua_State* L;
} LuaSQLiteFunction;

typedef struct LuaSQLite {
	sqlite3 *db;
	sqlite3_stmt * stmt;
	char * file;
	int status;
	int busyhandler;
	lua_State* activeL; // state running the current prepare/step (sqlite object at stack index 1); used by the busy handler
	int funcs;
	LuaSQLiteFunction** functions;
} LuaSQLite;

int SQLite_Open(lua_State *L);
int SQLite_Query(lua_State *L);
int SQLite_Fetch(lua_State *L);
int SQLite_GetRow(lua_State *L);
int SQLite_GetColumns(lua_State *L);
int SQLite_SetBusyHandler(lua_State *L);
int SQLite_RegisterFunction(lua_State* L);
int SQLite_RegisterAggregateFunction(lua_State* L);
int SQLite_Finish(lua_State* L);

int SQLite_ToString(lua_State *L);
int SQLite_Close(lua_State *L);