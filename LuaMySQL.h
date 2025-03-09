#pragma once
#include "lua_main_incl.h"
#include <Windows.h>
#include <mysql.h>

static const char * LUAMYSQL = "LuaMySQL";

typedef struct LuaMySQL {

	MYSQL* connection;
	volatile bool busy;
	volatile bool alive;
	char* error;
	MYSQL_RES* result;
	char* query;
	size_t querylen;
	HANDLE thread;
	HANDLE interrupt;

} LuaMySQL;

LuaMySQL * lua_tomysql(lua_State *L, int index);
LuaMySQL * lua_pushmysql(lua_State *L);

int MySqlIsBusy(lua_State* L);
int MySqlConnect(lua_State* L);
int MySqlQuery(lua_State* L);
int MySqlGetResult(lua_State* L);
int MySqlGetFields(lua_State* L);
int MySqlGetRow(lua_State* L);
int EscapeValue(lua_State* L);

int luamysql_gc(lua_State *L);
int luamysql_tostring(lua_State *L);