#pragma once
#include "lua_main_incl.h"
#include <inttypes.h>
#include <mysql.h>

static const char* LUAMYSQL = "LuaMySQL";

typedef struct LuaMySQL {
	MYSQL*  connection;
	int     queryRef;      // Lua registry ref to active query coroutine; LUA_NOREF = idle
	void*   activeQuery;   // LuaMySQLQuery* of the running query, or NULL
	char*   error;         // last connection-level error
	int     aliveTokenRef; // LUA_NOREF when not set
} LuaMySQL;

LuaMySQL* lua_tomysql(lua_State* L, int index);
LuaMySQL* lua_pushmysql(lua_State* L);

int MySqlIsBusy(lua_State* L);
int MySqlConnect(lua_State* L);
int MySqlQuery(lua_State* L);
int MySqlNonQuery(lua_State* L);
int MySqlScalar(lua_State* L);
int MySqlQueryAll(lua_State* L);
int MySqlEscapeValue(lua_State* L);

int MySqlSetAliveToken(lua_State* L);

int luamysql_gc(lua_State* L);
int luamysql_tostring(lua_State* L);