#pragma once
#include "Redis.h"

const char* lua_pushrediskey(lua_State* L, LuaRedisKey* key);
void lua_pushredisref(lua_State* L, LuaRedisKey* key);
void CleanRedisKey(lua_State* L, LuaRedisKey* key);
int lua_pushredisttl(lua_State* L, LuaRedisKey* key);
int lua_setredisttl(lua_State* L, LuaRedisKey* key, lua_Integer expireTime);