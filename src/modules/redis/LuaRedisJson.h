#pragma once
#include "redis/LuaRedis.h"

static const char* REDISJSON = "REDISJSON";

typedef struct LuaRedisJson {
	LuaRedisKey key;     // redisRef + Redis key name
	char*       path;    // JSONPath string, e.g. "$", "$.SomeNumber"
	size_t      pathlen;
} LuaRedisJson;

LuaRedisJson* luaL_checkredisjson(lua_State* L, int index);
int RedisPushJsonInternal(lua_State* L, int redisIdx, const char* rediskey, size_t rediskeylen, const char* path, size_t pathlen);

int RedisJson_GC(lua_State* L);
int RedisJson_ToString(lua_State* L);
int RedisJson_Index(lua_State* L);
int RedisJson_NewIndex(lua_State* L);
int RedisJson_Call(lua_State* L);
int RedisJson_Get(lua_State* L);
int RedisJson_Set(lua_State* L);
int RedisJson_Delete(lua_State* L);
int RedisJson_Type(lua_State* L);
int RedisJson_Length(lua_State* L);
int RedisJson_Pairs(lua_State* L);

static const struct luaL_Reg redisjsonfunctions[] = {
	{ "Get",    RedisJson_Get    },
	{ "Set",    RedisJson_Set    },
	{ "Delete", RedisJson_Delete },
	{ "Type",   RedisJson_Type   },
	{ "Length", RedisJson_Length },
	{ "Pairs",  RedisJson_Pairs  },
	{ NULL, NULL }
};

static const luaL_Reg redisjsonmeta[] = {
	{ "__gc",       RedisJson_GC       },
	{ "__tostring", RedisJson_ToString },
	{ "__call",     RedisJson_Call     },
	{ "__len",      RedisJson_Length   },
	{ "__pairs",    RedisJson_Pairs    },
	{ NULL, NULL }
};

static int InternalLuaOpenRedisJson(lua_State* L) {

	lua_pushcfunction(L, RedisJson_Index);

	luaL_newmetatable(L, REDISJSON);
	luaL_setfuncs(L, redisjsonmeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pushliteral(L, "__newindex");
	lua_pushcfunction(L, RedisJson_NewIndex);
	lua_rawset(L, -3);

	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	return 1;
}
