#pragma once
#include "Redis.h"

static const char* REDISJSON = "REDISJSON";

typedef struct LuaRedisJson {
	LuaRedisKey key;     // redis_ref + Redis key name
	char*       path;    // JSONPath string, e.g. "$", "$.SomeNumber"
	size_t      pathlen;
} LuaRedisJson;

LuaRedisJson* lua_toredisjon(lua_State* L, int index);
int RedisPushJsonInternal(lua_State* L, int redisIdx, const char* rediskey, size_t rediskeylen, const char* path, size_t pathlen);

int redisjson_gc(lua_State* L);
int redisjson_tostring(lua_State* L);
int redisjson_index(lua_State* L);
int redisjson_newindex(lua_State* L);
int redisjson_call(lua_State* L);
int redisjson_get(lua_State* L);
int redisjson_set(lua_State* L);
int redisjson_delete(lua_State* L);
int redisjson_type(lua_State* L);
int redisjson_length(lua_State* L);
int redisjson_pairs(lua_State* L);

static const struct luaL_Reg redisjsonfunctions[] = {
	{ "Get",    redisjson_get    },
	{ "Set",    redisjson_set    },
	{ "Delete", redisjson_delete },
	{ "Type",   redisjson_type   },
	{ "Length", redisjson_length },
	{ "Pairs",  redisjson_pairs  },
	{ NULL, NULL }
};

static const luaL_Reg redisjsonmeta[] = {
	{ "__gc",       redisjson_gc       },
	{ "__tostring", redisjson_tostring },
	{ "__call",     redisjson_call     },
	{ "__len",      redisjson_length   },
	{ "__pairs",    redisjson_pairs    },
	{ NULL, NULL }
};

static int internal_luaopen_redisjson(lua_State* L) {

	lua_pushcfunction(L, redisjson_index);

	luaL_newmetatable(L, REDISJSON);
	luaL_setfuncs(L, redisjsonmeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pushliteral(L, "__newindex");
	lua_pushcfunction(L, redisjson_newindex);
	lua_rawset(L, -3);

	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	return 1;
}
