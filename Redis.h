#pragma once
#include "lua_main_incl.h"
#include <Windows.h>
static const char* REDIS = "REDIS";

typedef struct LuaRedis {

} LuaRedis;


LuaRedis* lua_pushredis(lua_State* L);
LuaRedis* lua_toredis(lua_State* L, int index);

int RedisOpen(lua_State* L);

int redis_gc(lua_State* L);
int redis_tostring(lua_State* L);