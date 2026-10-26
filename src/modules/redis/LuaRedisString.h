#pragma once
#include "LuaMainInclude.h"
#include "redis/LuaRedis.h"
#include "redis/LuaRedisKey.h"

typedef struct LuaRedisString {
	LuaRedisKey key;
} LuaRedisString;

int RedisString_GC(lua_State* L);
int RedisString_ToString(lua_State* L);
int RedisString_Concat(lua_State* L);
int RedisString_len(lua_State* L);
int RedisString_Set(lua_State* L);
int RedisString_GetOrSet(lua_State* L);
int RedisString_At(lua_State* L);
int RedisString_NewIndex(lua_State* L);
int RedisString_Pairs(lua_State* L);
int RedisString_GetTTL(lua_State* L);
int RedisString_SetTTL(lua_State* L);
int RedisString_Delete(lua_State* L);
int RedisString_Call(lua_State* L);

static const struct luaL_Reg redisstringfunctions[] = {
	{ "At", RedisString_At },
	{ "len", RedisString_len },
	{ "Set", RedisString_Set },
	{ "Delete", RedisString_Delete },
	{ "GetOrSet", RedisString_GetOrSet },
	{ "GetSet", RedisString_GetOrSet },
	{ "SetTTL", RedisString_SetTTL },
	{ "GetTTL", RedisString_GetTTL },
	{ NULL, NULL }
};

static const luaL_Reg redisstringmeta[] = {
	{ "__call", RedisString_Call },
	{ "__pairs", RedisString_Pairs },
	{ "__len",  RedisString_len },
	{ "__gc",  RedisString_GC },
	{ "__tostring",  RedisString_ToString },
	{ "__concat",  RedisString_Concat },
	{ NULL, NULL }
};

static int RedisString_Index(lua_State* L) {
	
	if (lua_type(L, -1) == LUA_TSTRING) 
	{
		const char* value = lua_tostring(L, -1);

		for (size_t i = 0; true; i++)
		{
			if (redisstringfunctions[i].name == NULL) {
				break;
			}
			else if (strcmp(redisstringfunctions[i].name, value) == 0) {
				lua_pushcfunction(L, redisstringfunctions[i].func);
				return 1;
			}
		}

		luaL_error(L, "Unknown field: %s", value);
	}

	return RedisString_At(L);
}

static int InternalOpenRedisString(lua_State* L) {

	lua_pushcfunction(L, RedisString_Index);

	luaL_newmetatable(L, REDISSTRING);
	luaL_setfuncs(L, redisstringmeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pushliteral(L, "__newindex");
	lua_pushcfunction(L, RedisString_NewIndex);
	lua_rawset(L, -3);

	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);

	return 1;
}