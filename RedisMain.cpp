#include "Redis.h"
#include "RedisMain.h"

static const struct luaL_Reg redisfunctions[] = {
	{ "Open", RedisOpen },
	{ NULL, NULL }
};

static const luaL_Reg redismeta[] = {
	{ "__gc",  redis_gc },
	{ "__tostring",  redis_tostring },
{ NULL, NULL }
};

int luaopen_redis(lua_State* L) {

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

	lua_pop(L, 1);
	return 1;
}