#pragma once
#include "redis/LuaRedis.h"

int RedisValue_GC(lua_State* L);
int RedisValue_ToString(lua_State* L);
int RedisValue_Index(lua_State* L);
int RedisValue_NewIndex(lua_State* L);
int RedisValue_Call(lua_State* L);
int RedisValue_Pairs(lua_State* L);
int RedisValue_Len(lua_State* L);

static const struct luaL_Reg redisvaluefunctions[] = {
	{ NULL, NULL }
};

static const luaL_Reg redisvaluemeta[] = {
	{ "__len", RedisValue_Len },
	{ "__pairs", RedisValue_Pairs },
	{ "__gc",  RedisValue_GC },
	{ "__tostring",  RedisValue_ToString },
	{ "__call",  RedisValue_Call },
	{ NULL, NULL }
};

static int InternalLuaOpenRedisValue(lua_State* L) {

	lua_pushcfunction(L, RedisValue_Index);

	luaL_newmetatable(L, REDISVALUE);
	luaL_setfuncs(L, redisvaluemeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__newindex");
	lua_pushcfunction(L, RedisValue_NewIndex);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	return 1;
}