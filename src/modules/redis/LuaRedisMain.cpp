#include "redis/LuaRedis.h"
#include "redis/LuaRedisMain.h"
#include "redis/LuaRedisString.h"
#include "redis/LuaRedisKey.h"
#include "redis/LuaRedisValue.h"
#include "redis/LuaRedisStream.h"
#include "redis/LuaRedisJson.h"
#include "redis/LuaRedisPubSub.h"

static const struct luaL_Reg redisfunctions[] = {
	{ "Command", Redis_Command },
	{ "GetString", Redis_GetString },
	{ "GetHashset", Redis_GetHashset },
	{ "GetList", Redis_GetList },
	{ "GetSortedSet", Redis_GetSortedSet },
	{ "GetStream", Redis_GetStream },
	{ "GetSet", Redis_GetSet },
	{ "GetJson", Redis_GetJson },
	{ "GetKey", Redis_GetKey },
	{ "Subscribe", Redis_Subscribe },
	{ "PSubscribe", Redis_PSubscribe },
	{ "Open", Redis_Open },
	{ "Dispose", Redis_Dispose },
	{ NULL, NULL }
};

static const luaL_Reg redismeta[] = {
	{ "__call", Redis_Call },
	{ "__gc",  Redis_GC },
	{ "__tostring",  Redis_ToString },
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

	InternalOpenRedisString(L);
	lua_pop(L, 2);

	InternalLuaOpenRedisKey(L);
	lua_pop(L, 2);

	InternalLuaOpenRedisValue(L);
	lua_pop(L, 2);

	InternalLuaOpenRedisStream(L);
	lua_pop(L, 2);

	InternalLuaOpenRedisJson(L);
	lua_pop(L, 2);

	// REDISPUBSUBSTATE
	luaL_newmetatable(L, REDISPUBSUBSTATE);
	lua_pushcfunction(L, RedisPubSubState_GC);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	// REDISPUBSUBCOROUTINE — coroutine thread metatable
	// Methods table so co:SetAliveToken() is reachable on the thread.
	lua_newtable(L);                              // methods table
	lua_pushcfunction(L, RedisPubSubCoroutine_SetAliveToken);
	lua_setfield(L, -2, "SetAliveToken");
	int methodsIdx = lua_gettop(L);

	luaL_newmetatable(L, REDISPUBSUBCOROUTINE);
	lua_pushcfunction(L, RedisPubSubCoroutine_GC);
	lua_setfield(L, -2, "__gc");
	lua_pushvalue(L, methodsIdx);                // __index = methods table
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);
	lua_pop(L, 1);

	return 1;
}