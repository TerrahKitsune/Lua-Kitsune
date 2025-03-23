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
	if (key->redis_ref != LUA_NOREF) {
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

LuaRedisKey* lua_createrediskey(lua_State* L, int redisIdx, const char* key, size_t keylen) {

	luaL_checkudata(L, redisIdx, REDIS);
	lua_pushvalue(L, redisIdx);
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);

	LuaRedisKey* redisKey = (LuaRedisKey*)lua_newuserdata(L, sizeof(LuaRedisKey));
	if (redisKey == NULL) {
		luaL_error(L, "Unable to push rediskey");
		return NULL;
	}
	luaL_getmetatable(L, REDISKEY);
	lua_setmetatable(L, -2);
	memset(redisKey, 0, sizeof(LuaRedisKey));
	redisKey->redis_ref = ref;
	
	redisKey->key = (char*)gff_malloc(keylen + 1);
	if (!redisKey->key) {
		luaL_error(L, "Out of memory");
		return 0;
	}

	redisKey->key[keylen] = '\0';
	memcpy(redisKey->key, key, keylen);
	redisKey->keylen = keylen;

	return redisKey;
}

LuaRedisKey* lua_torediskey(lua_State* L, int idx) {
	LuaRedisKey* redis = (LuaRedisKey*)luaL_checkudata(L, idx, REDISKEY);
	if (redis == NULL) {
		luaL_error(L, "parameter is not a %s", REDISKEY);
		return NULL;
	}
	return redis;
}

int rediskey_delete(lua_State* L) {
	LuaRedisKey* key = lua_torediskey(L, -1);
	lua_pushredisref(L, key);
	lua_pushliteral(L, "DEL");
	lua_pushrediskey(L, key);
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 3);
	lua_pushboolean(L, redis->reply->type == REDIS_REPLY_INTEGER && redis->reply->integer != 0);
	CleanReply(redis);
	return 1;
}

int rediskey_setttl(lua_State* L) {

	LuaRedisKey* redisKey = lua_torediskey(L, 1);
	return lua_setredisttl(L, redisKey, luaL_checkinteger(L, 2));
}

int rediskey_getttl(lua_State* L) {

	LuaRedisKey* redisKey = lua_torediskey(L, -1);
	return lua_pushredisttl(L, redisKey);
}

int rediskey_gettype(lua_State* L) {

	LuaRedisKey* key = lua_torediskey(L, -1);
	lua_pushredisref(L, key);
	lua_pushliteral(L, "TYPE");
	lua_pushrediskey(L, key);
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 3);
	lua_pushlstring(L, redis->reply->str, redis->reply->len);
	CleanReply(redis);
	return 1;
}

int rediskey_gc(lua_State* L) {
	LuaRedisKey* key = lua_torediskey(L, -1);
	CleanRedisKey(L, key);
	return 0;
}

int rediskey_tostring(lua_State* L) {
	LuaRedisKey* redis = (LuaRedisKey*)luaL_checkudata(L, -1, REDISKEY);
	lua_pushlstring(L, redis->key, redis->keylen);
	return 1;
}