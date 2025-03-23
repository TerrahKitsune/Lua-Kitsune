#pragma once
#include "Redis.h"

const char* lua_pushrediskey(lua_State* L, LuaRedisKey* key);
void lua_pushredisref(lua_State* L, LuaRedisKey* key);
void CleanRedisKey(lua_State* L, LuaRedisKey* key);
int lua_pushredisttl(lua_State* L, LuaRedisKey* key);
int lua_setredisttl(lua_State* L, LuaRedisKey* key, lua_Integer expireTime);

int rediskey_gc(lua_State* L);
int rediskey_tostring(lua_State* L);

int rediskey_getttl(lua_State* L);
int rediskey_setttl(lua_State* L);
int rediskey_gettype(lua_State* L);
int rediskey_delete(lua_State* L);

static const struct luaL_Reg rediskeyfunctions[] = {
	{ "Delete", rediskey_delete },
	{ "Type", rediskey_gettype },
	{ "SetTTL", rediskey_setttl },
	{ "GetTTL", rediskey_getttl },
	{ NULL, NULL }
};

static const luaL_Reg rediskeymeta[] = {
	{ "__gc",  rediskey_gc },
	{ "__tostring",  rediskey_tostring },
	{ NULL, NULL }
};

static int internal_luaopen_rediskey(lua_State* L) {

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