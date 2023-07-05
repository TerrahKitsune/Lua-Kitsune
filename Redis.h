#pragma once
#include "lua_main_incl.h"
#include <Windows.h>
static const char* REDIS = "REDIS";

#pragma comment (lib, "crypt32")
#include "hiredis/hiredis.h"
#include "hiredis/hiredis_ssl.h"

typedef struct LuaRedis {

	redisSSLContext* ssl;
	redisContext* context;
	redisReply* reply;
	int argc;
	char** argv;
	size_t* argvlen;
	
} LuaRedis;


LuaRedis* lua_pushredis(lua_State* L);
LuaRedis* lua_toredis(lua_State* L, int index);

int RedisOpen(lua_State* L);
int RedisCommand(lua_State* L);

int redis_gc(lua_State* L);
int redis_tostring(lua_State* L);