#include "RedisKey.h"

const char* lua_pushrediskey(lua_State* L, LuaRedisKey* key) {
	return lua_pushlstring(L, key->key, key->keylen);
}

void lua_pushredisref(lua_State* L, LuaRedisKey* key) {
	if (key->redis_ref == LUA_NOREF) {
		luaL_error(L, "Redis key not available");
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, key->redis_ref);
}

void CleanRedisKey(lua_State* L, LuaRedisKey* key) {
	if (key->redis_ref) {
		luaL_unref(L, LUA_REGISTRYINDEX, key->redis_ref);
		key->redis_ref = LUA_NOREF;
	}

	if (key->key) {
		gff_free(key->key);
		key->key = NULL;
		key->keylen = 0;
	}
}

int lua_setredisttl(lua_State* L, LuaRedisKey* key, lua_Integer expireTime) {

	LuaRedis* redis;

	if (expireTime > 0) {
		lua_pushredisref(L, key);
		lua_pushliteral(L, "PEXPIRE");
		lua_pushrediskey(L, key);
		lua_pushinteger(L, expireTime);

		redis = RedisCommandInternal(L);
		lua_pop(L, 4);
	}
	else {
		lua_pushredisref(L, key);
		lua_pushliteral(L, "PERSIST");
		lua_pushrediskey(L, key);

		redis = RedisCommandInternal(L);
		lua_pop(L, 3);
	}

	if (redis->reply->type == REDIS_REPLY_INTEGER && redis->reply->integer == 1) {
		lua_pushboolean(L, true);
	}
	else {
		lua_pushboolean(L, false);
	}

	CleanReply(redis);

	return 1;
}

int lua_pushredisttl(lua_State* L, LuaRedisKey* key) {

	lua_pushredisref(L, key);
	lua_pushliteral(L, "PTTL");
	lua_pushrediskey(L, key);

	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 3);

	if (redis->reply->type == REDIS_REPLY_INTEGER && redis->reply->integer >= -1) {
		lua_pushinteger(L, redis->reply->integer);
	}
	else {
		lua_pushnil(L);
	}

	CleanReply(redis);

	return 1;
}