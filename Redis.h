#pragma once
#include "lua_main_incl.h"
#include <Windows.h>
static const char* REDIS = "REDIS";
static const char* REDISSTRING = "REDISSTRING";
#include <process.h>

#pragma comment (lib, "crypt32")
#include "hiredis/hiredis.h"
#include "hiredis/hiredis_ssl.h"

#pragma comment(lib, "hiredis/hiredis.lib")
#pragma comment(lib, "hiredis/hiredis_ssl.lib")

typedef struct LuaRedisKey {
	int redis_ref;
	char* key;
	size_t keylen;
} LuaRedisKey;

typedef struct LuaRedis {

	redisSSLContext* ssl;
	redisContext* context;
	redisReply* reply;
	int argc;
	char** argv;
	size_t* argvlen;
	HANDLE thread;
	CRITICAL_SECTION CriticalSection;
	redisReply* pollReply;
	bool isAlive;
} LuaRedis;

void CleanReply(LuaRedis* luaRedis);
LuaRedis* RedisCommandInternal(lua_State* L);
int RedisPushStringInternal(lua_State* L, int redisIdx, const char* key, size_t keylength);

LuaRedis* lua_pushredis(lua_State* L);
LuaRedis* lua_toredis(lua_State* L, int index);

int RedisOpen(lua_State* L);
int RedisCommand(lua_State* L);
int RedisPoll(lua_State* L);
int RedisGetString(lua_State* L);

int redis_gc(lua_State* L);
int redis_tostring(lua_State* L);