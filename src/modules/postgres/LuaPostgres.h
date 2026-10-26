#pragma once
#include "LuaMainInclude.h"
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

int Postgres_IsBusy(lua_State* L);
int Postgres_Connect(lua_State* L);
int Postgres_Query(lua_State* L);
int Postgres_NonQuery(lua_State* L);
int Postgres_Scalar(lua_State* L);
int Postgres_QueryAll(lua_State* L);
int Postgres_EscapeValue(lua_State* L);

int Postgres_SetAliveToken(lua_State* L);

int Postgres_Close(lua_State* L);
int Postgres_ToString(lua_State* L);
