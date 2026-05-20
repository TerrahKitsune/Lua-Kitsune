#pragma once
#include "lua_main_incl.h"
#include <inttypes.h>
#include <libpq-fe.h>

static const char* LUAPOSTGRES = "LuaPostgres";

typedef struct LuaPostgres {
	PGconn* connection;
	int     queryRef;      // Lua registry ref to active query coroutine; LUA_NOREF = idle
	void*   activeQuery;   // LuaPostgresQuery* of the running query, or NULL
	char*   error;         // last connection-level error
	int     aliveTokenRef; // LUA_NOREF when not set
	void*   appToken;      // cached LuaAliveToken* for the engine app token; NULL if not present
} LuaPostgres;

LuaPostgres* lua_topostgres(lua_State* L, int index);
LuaPostgres* lua_pushpostgres(lua_State* L);

int PostgresIsBusy(lua_State* L);
int PostgresConnect(lua_State* L);
int PostgresQuery(lua_State* L);
int PostgresNonQuery(lua_State* L);
int PostgresScalar(lua_State* L);
int PostgresQueryAll(lua_State* L);
int PostgresEscapeValue(lua_State* L);

int PostgresSetAliveToken(lua_State* L);

int luapostgres_gc(lua_State* L);
int luapostgres_tostring(lua_State* L);
