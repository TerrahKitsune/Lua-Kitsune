#pragma once
#include "LuaMainInclude.h"
#include <inttypes.h>
#include <mysql.h>

static const char* LUAMYSQL = "LuaMySQL";

typedef struct LuaMySQL {
	MYSQL*  connection;
	int     queryRef;      // Lua registry ref to active query coroutine; LUA_NOREF = idle
	void*   activeQuery;   // LuaMySQLQuery* of the running query, or NULL
	char*   error;         // last connection-level error
	int     aliveTokenRef; // LUA_NOREF when not set
	void*   appToken;      // cached LuaAliveToken* for the engine app token; NULL if not present
} LuaMySQL;

LuaMySQL* lua_tomysql(lua_State* L, int index);
LuaMySQL* lua_pushmysql(lua_State* L);

int MySQL_IsBusy(lua_State* L);
int MySQL_Connect(lua_State* L);
int MySQL_Query(lua_State* L);
int MySQL_NonQuery(lua_State* L);
int MySQL_Scalar(lua_State* L);
int MySQL_QueryAll(lua_State* L);
int MySQL_EscapeValue(lua_State* L);

int MySQL_SetAliveToken(lua_State* L);

int MySQL_Close(lua_State* L);
int MySQL_ToString(lua_State* L);