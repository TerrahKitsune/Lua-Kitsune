#include "Redis.h"
#include "RedisMain.h"
#include "RedisString.h"
#include "RedisKey.h"
#include "RedisValue.h"
#include "RedisStream.h"

static const struct luaL_Reg redisfunctions[] = {
	{ "Command", RedisCommand },
	{ "GetString", RedisGetString },
	{ "GetHashset", RedisGetHashset },
	{ "GetList", RedisGetList },
	{ "GetSortedSet", RedisGetSortedSet },
	{ "GetStream", RedisGetStream },
	{ "GetSet", RedisGetSet },
	{ "GetKey", RedisGetKey },
	{ "Poll", RedisPoll },
	{ "Open", RedisOpen },
	{ NULL, NULL }
};

static const luaL_Reg redismeta[] = {
	{ "__call", RedisGetKeyIterator },
	{ "__gc",  redis_gc },
	{ "__tostring",  redis_tostring },
	{ NULL, NULL }
};

int luaopen_redis(lua_State* L) {

	redisInitOpenSSL();

	luaL_newlibtable(L, redisfunctions);
	luaL_setfuncs(L, redisfunctions, 0);

	luaL_newmetatable(L, REDIS);
	luaL_setfuncs(L, redismeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	internal_open_redisstring(L);
	lua_pop(L, 2);

	internal_luaopen_rediskey(L);
	lua_pop(L, 2);

	internal_luaopen_redisvalue(L);
	lua_pop(L, 2);

	internal_luaopen_redisstream(L);
	lua_pop(L, 2);

	return 1;
}