#pragma once
#include "Redis.h"

int redisvalue_gc(lua_State* L);
int redisvalue_tostring(lua_State* L);
int redisvalue_index(lua_State* L);
int redisvalue_newindex(lua_State* L);
int redisvalue_call(lua_State* L);
int redisvalue_pairs(lua_State* L);
int redisvalue_len(lua_State* L);

static const struct luaL_Reg redisvaluefunctions[] = {
	{ NULL, NULL }
};

static const luaL_Reg redisvaluemeta[] = {
	{ "__len", redisvalue_len },
	{ "__pairs", redisvalue_pairs },
	{ "__gc",  redisvalue_gc },
	{ "__tostring",  redisvalue_tostring },
	{ "__call",  redisvalue_call },
	{ NULL, NULL }
};

static int internal_luaopen_redisvalue(lua_State* L) {

	lua_pushcfunction(L, redisvalue_index);

	luaL_newmetatable(L, REDISVALUE);
	luaL_setfuncs(L, redisvaluemeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__newindex");
	lua_pushcfunction(L, redisvalue_newindex);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	return 1;
}