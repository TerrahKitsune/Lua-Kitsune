#pragma once
#include "lua_main_incl.h"
#include <Windows.h>
static const char* REDIS = "REDIS";
static const char* REDISSTRING = "REDISSTRING";
static const char* REDISKEY = "REDISKEY";
static const char* REDISVALUE = "REDISVALUE";
static const char* REDISSTREAM = "REDISSTREAM";
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
	unsigned long long cursor;
	int ref;
	int nth;
} LuaRedis;

#define REDIS_VALUE_TYPE_UNKNOWN 0
#define REDIS_VALUE_TYPE_HASHSET 1
#define REDIS_VALUE_TYPE_LIST 2
#define REDIS_VALUE_TYPE_SET 3
#define REDIS_VALUE_TYPE_SORTEDSET 4

void CleanReply(LuaRedis* luaRedis);
LuaRedis* RedisCommandInternal(lua_State* L);
int RedisPushStringInternal(lua_State* L, int redisIdx, const char* key, size_t keylength);
int RedisPushStreamInternal(lua_State* L, int redisIdx, const char* key, size_t keylength);
LuaRedisKey* lua_createrediskey(lua_State* L, int redisIdx, const char* key, size_t keylen);
int push_redisvalue(lua_State* L, int redisIdx, int type, const char* key, size_t keylen);

LuaRedis* lua_pushredis(lua_State* L);
LuaRedis* lua_toredis(lua_State* L, int index);

int RedisOpen(lua_State* L);
int RedisCommand(lua_State* L);
int RedisPoll(lua_State* L);
int RedisGetString(lua_State* L);
int RedisGetKey(lua_State* L);
int RedisGetKeyIterator(lua_State* L);
int RedisGetHashset(lua_State* L);
int RedisGetList(lua_State* L);
int RedisGetSet(lua_State* L);
int RedisGetSortedSet(lua_State* L);
int RedisGetStream(lua_State* L);

int redis_gc(lua_State* L);
int redis_tostring(lua_State* L);