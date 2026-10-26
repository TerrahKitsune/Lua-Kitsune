#pragma once
#include "redis/LuaRedis.h"

const char* PushRedisKeyName(lua_State* L, LuaRedisKey* key);
void PushRedisRef(lua_State* L, LuaRedisKey* key);
void CleanRedisKey(lua_State* L, LuaRedisKey* key);
int PushRedisTtl(lua_State* L, LuaRedisKey* key);
int SetRedisTtl(lua_State* L, LuaRedisKey* key, lua_Integer expireTime);
LuaRedisKey* luaL_checkrediskey(lua_State* L, int idx);

int RedisKey_GC(lua_State* L);
int RedisKey_ToString(lua_State* L);

int RedisKey_GetTTL(lua_State* L);
int RedisKey_SetTTL(lua_State* L);
int RedisKey_Type(lua_State* L);
int RedisKey_Delete(lua_State* L);

static const struct luaL_Reg rediskeyfunctions[] = {
	{ "Delete", RedisKey_Delete },
	{ "Type", RedisKey_Type },
	{ "SetTTL", RedisKey_SetTTL },
	{ "GetTTL", RedisKey_GetTTL },
	{ NULL, NULL }
};

static const luaL_Reg rediskeymeta[] = {
	{ "__gc",  RedisKey_GC },
	{ "__tostring",  RedisKey_ToString },
	{ NULL, NULL }
};

static int InternalLuaOpenRedisKey(lua_State* L) {

	luaL_newlibtable(L, rediskeyfunctions);
	luaL_setfuncs(L, rediskeyfunctions, 0);

	luaL_newmetatable(L, REDISKEY);
	luaL_setfuncs(L, rediskeymeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	return 1;
}