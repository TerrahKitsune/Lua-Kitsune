#pragma once
#include "Redis.h"

static const char* REDISPUBSUBSTATE     = "REDISPUBSUBSTATE";
static const char* REDISPUBSUBCOROUTINE = "REDISPUBSUBCOROUTINE";

typedef struct LuaRedisPubSubState {
	redisContext*    context;   // dedicated connection owned by this state; freed by PubSubStateGC
	redisSSLContext* ssl;       // borrowed from parent LuaRedis; not freed here
	bool             is_pattern;
	int              redis_ref; // registry ref keeping parent LuaRedis alive while context uses its ssl
} LuaRedisPubSubState;

int RedisSubscribe(lua_State* L);
int RedisPSubscribe(lua_State* L);
int PubSubCoroutineBody(lua_State* L);
int PubSubCoroutineGC(lua_State* L);
int PubSubStateGC(lua_State* L);
