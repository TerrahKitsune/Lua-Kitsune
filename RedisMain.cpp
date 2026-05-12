#include "Redis.h"
#include "RedisMain.h"
#include "RedisString.h"
#include "RedisKey.h"
#include "RedisValue.h"
#include "RedisStream.h"
#include "RedisJson.h"
#include "RedisPubSub.h"

static const struct luaL_Reg redisfunctions[] = {
	{ "Command", RedisCommand },
	{ "GetString", RedisGetString },
	{ "GetHashset", RedisGetHashset },
	{ "GetList", RedisGetList },
	{ "GetSortedSet", RedisGetSortedSet },
	{ "GetStream", RedisGetStream },
	{ "GetSet", RedisGetSet },
	{ "GetJson", RedisGetJson },
	{ "GetKey", RedisGetKey },
	{ "Subscribe", RedisSubscribe },
	{ "PSubscribe", RedisPSubscribe },
	{ "Open", RedisOpen },
	{ "Dispose", redis_dispose },
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

	internal_luaopen_redisjson(L);
	lua_pop(L, 2);

	// REDISPUBSUBSTATE
	luaL_newmetatable(L, REDISPUBSUBSTATE);
	lua_pushcfunction(L, PubSubStateGC);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	// REDISPUBSUBCOROUTINE — coroutine thread metatable
	// Methods table so co:SetAliveToken() is reachable on the thread.
	lua_newtable(L);                              // methods table
	lua_pushcfunction(L, PubSubSetAliveToken);
	lua_setfield(L, -2, "SetAliveToken");
	int methods_idx = lua_gettop(L);

	luaL_newmetatable(L, REDISPUBSUBCOROUTINE);
	lua_pushcfunction(L, PubSubCoroutineGC);
	lua_setfield(L, -2, "__gc");
	lua_pushvalue(L, methods_idx);                // __index = methods table
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);   // pop metatable
	lua_pop(L, 1);   // pop methods table

	return 1;
}