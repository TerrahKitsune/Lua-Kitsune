#include "Redis.h"
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h> 
#include <windows.h> 

int RedisOpen(lua_State* L) {

	LuaRedis* redis = lua_pushredis(L);

	return 1;
}

LuaRedis* lua_pushredis(lua_State* L) {
	LuaRedis* redis = (LuaRedis*)lua_newuserdata(L, sizeof(LuaRedis));
	if (redis == NULL) {
		luaL_error(L, "Unable to push redis");
		return NULL;
	}
	luaL_getmetatable(L, REDIS);
	lua_setmetatable(L, -2);
	memset(redis, 0, sizeof(LuaRedis));

	return redis;
}

LuaRedis* lua_toredis(lua_State* L, int index) {
	LuaRedis* redis = (LuaRedis*)luaL_checkudata(L, index, REDIS);
	if (redis == NULL)
		luaL_error(L, "parameter is not a %s", REDIS);
	return redis;
}

int redis_gc(lua_State* L) {

	LuaRedis* redis = lua_toredis(L, 1);

	return 0;
}

int redis_tostring(lua_State* L) {
	char tim[100];
	sprintf(tim, "Redis: 0x%08X", lua_toredis(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}