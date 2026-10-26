#pragma once
#include "redis/LuaRedis.h"

int RedisStream_ToString(lua_State* L);
int RedisStream_GC(lua_State* L);
int RedisStream_Call(lua_State* L);

int RedisStream_Add(lua_State* L);
int RedisStream_Read(lua_State* L);
int RedisStream_Trim(lua_State* L);

typedef struct LuaRedisStream {
	LuaRedisKey key;
} LuaRedisStream;

static const struct luaL_Reg redisstreamfunctions[] = {
	{ "Add", RedisStream_Add },
	{ "Read", RedisStream_Read },
	{ "Trim", RedisStream_Trim },
	{ NULL, NULL }
};

static const luaL_Reg redisstreammeta[] = {
	{ "__call",  RedisStream_Call },
	{ "__gc",  RedisStream_GC },
	{ "__tostring",  RedisStream_ToString },
	{ NULL, NULL }
};

static int InternalLuaOpenRedisStream(lua_State* L) {

	luaL_newlibtable(L, redisstreamfunctions);
	luaL_setfuncs(L, redisstreamfunctions, 0);

	luaL_newmetatable(L, REDISSTREAM);
	luaL_setfuncs(L, redisstreammeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	return 1;
}