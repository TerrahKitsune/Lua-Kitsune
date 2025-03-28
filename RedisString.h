#pragma once
#include "lua_main_incl.h"
#include "Redis.h"
#include "RedisKey.h"

typedef struct LuaRedisString {
	LuaRedisKey key;
} LuaRedisString;

int redisstring_gc(lua_State* L);
int redisstring_tostring(lua_State* L);
int redisstring_concat(lua_State* L);
int redisstring_len(lua_State* L);
int redisstring_set(lua_State* L);
int redisstring_getorset(lua_State* L);
int redisstring_at(lua_State* L);
int redisstring_setat(lua_State* L);
int redisstring_pairs(lua_State* L);
int redisstring_getttl(lua_State* L);
int redisstring_setttl(lua_State* L);
int redisstring_delete(lua_State* L);
int redisstring_call(lua_State* L);

static const struct luaL_Reg redisstringfunctions[] = {
	{ "At", redisstring_at },
	{ "len", redisstring_len },
	{ "Set", redisstring_set },
	{ "Delete", redisstring_delete },
	{ "GetOrSet", redisstring_getorset },
	{ "GetSet", redisstring_getorset },
	{ "SetTTL", redisstring_setttl },
	{ "GetTTL", redisstring_getttl },
	{ NULL, NULL }
};

static const luaL_Reg redisstringmeta[] = {
	{ "__call", redisstring_call },
	{ "__pairs", redisstring_pairs },
	{ "__len",  redisstring_len },
	{ "__gc",  redisstring_gc },
	{ "__tostring",  redisstring_tostring },
	{ "__concat",  redisstring_concat },
	{ NULL, NULL }
};

static int lua_redisstringindex(lua_State* L) {
	
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

	return redisstring_at(L);
}

static int internal_open_redisstring(lua_State* L) {

	lua_pushcfunction(L, lua_redisstringindex);

	luaL_newmetatable(L, REDISSTRING);
	luaL_setfuncs(L, redisstringmeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pushliteral(L, "__newindex");
	lua_pushcfunction(L, redisstring_setat);
	lua_rawset(L, -3);

	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);

	return 1;
}