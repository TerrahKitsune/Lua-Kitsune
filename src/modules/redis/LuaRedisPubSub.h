#pragma once
#include "redis/LuaRedis.h"

static const char* REDISPUBSUBSTATE     = "REDISPUBSUBSTATE";
static const char* REDISPUBSUBCOROUTINE = "REDISPUBSUBCOROUTINE";

typedef struct LuaRedisPubSubState {
	redisContext*    context;   // dedicated connection owned by this state; freed by RedisPubSubState_GC
	redisSSLContext* ssl;       // borrowed from parent LuaRedis; not freed here
	bool             is_pattern;
	int              redis_ref; // registry ref keeping parent LuaRedis alive while context uses its ssl
	int              aliveTokenRef; // LUA_NOREF when not set
} LuaRedisPubSubState;

int Redis_Subscribe(lua_State* L);
int Redis_PSubscribe(lua_State* L);
int PubSubCoroutineBody(lua_State* L);
int RedisPubSubCoroutine_GC(lua_State* L);
int RedisPubSubState_GC(lua_State* L);
int RedisPubSubCoroutine_SetAliveToken(lua_State* L);
