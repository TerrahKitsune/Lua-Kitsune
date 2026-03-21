#pragma once
#include "lua_main_incl.h"
#include <Windows.h>
#include <libpq-fe.h>

static const char* LUAPOSTGRES = "LuaPostgres";

typedef struct LuaPostgres {

PGconn* connection;
volatile bool busy;
volatile bool alive;
char* error;
PGresult* result;
int currentRow;
char* query;
size_t querylen;
char** paramValues;
int* paramLengths;
int nParams;
bool isParamQuery;
HANDLE thread;
HANDLE interrupt;

} LuaPostgres;

LuaPostgres* lua_topostgres(lua_State* L, int index);
LuaPostgres* lua_pushpostgres(lua_State* L);

int PostgresIsBusy(lua_State* L);
int PostgresConnect(lua_State* L);
int PostgresQuery(lua_State* L);
int PostgresQueryParams(lua_State* L);
int PostgresGetResult(lua_State* L);
int PostgresGetResultFields(lua_State* L);
int PostgresGetResultRow(lua_State* L);
int PostgresEscapeValue(lua_State* L);

int luapostgres_gc(lua_State* L);
int luapostgres_tostring(lua_State* L);