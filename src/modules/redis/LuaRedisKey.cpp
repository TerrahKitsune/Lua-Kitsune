#include "redis/LuaRedisKey.h"

const char* PushRedisKeyName(lua_State* L, LuaRedisKey* key) {
	return lua_pushlstring(L, key->key, key->keylen);
}

void PushRedisRef(lua_State* L, LuaRedisKey* key) {
	if (key->redisRef <= 0) {
		luaL_error(L, "Redis key not available");
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, key->redisRef);
}

void CleanRedisKey(lua_State* L, LuaRedisKey* key) {
	if (key->redisRef > 0) {
		luaL_unref(L, LUA_REGISTRYINDEX, key->redisRef);
		key->redisRef = LUA_NOREF;
	}

	if (key->key) {
		kitsune_free(key->key);
		key->key = NULL;
		key->keylen = 0;
	}
}

int SetRedisTtl(lua_State* L, LuaRedisKey* key, lua_Integer expireTime) {

	LuaRedis* redis;

	if (expireTime > 0) {
		PushRedisRef(L, key);
		lua_pushliteral(L, "PEXPIRE");
		PushRedisKeyName(L, key);
		lua_pushinteger(L, expireTime);

		redis = RedisCommandInternal(L);
		lua_pop(L, 4);
	}
	else {
		PushRedisRef(L, key);
		lua_pushliteral(L, "PERSIST");
		PushRedisKeyName(L, key);

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

int PushRedisTtl(lua_State* L, LuaRedisKey* key) {

	PushRedisRef(L, key);
	lua_pushliteral(L, "PTTL");
	PushRedisKeyName(L, key);

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

LuaRedisKey* lua_pushrediskey(lua_State* L, int redisIdx, const char* key, size_t keylen) {

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
	redisKey->redisRef = ref;
	
	redisKey->key = (char*)kitsune_malloc(keylen + 1);
	if (!redisKey->key) {
		luaL_error(L, "Out of memory");
		return 0;
	}

	redisKey->key[keylen] = '\0';
	memcpy(redisKey->key, key, keylen);
	redisKey->keylen = keylen;

	return redisKey;
}

LuaRedisKey* luaL_checkrediskey(lua_State* L, int idx) {
	LuaRedisKey* redis = (LuaRedisKey*)luaL_checkudata(L, idx, REDISKEY);
	if (redis == NULL) {
		luaL_error(L, "parameter is not a %s", REDISKEY);
		return NULL;
	}
	return redis;
}

int RedisKey_Delete(lua_State* L) {
	LuaRedisKey* key = luaL_checkrediskey(L, -1);
	PushRedisRef(L, key);
	lua_pushliteral(L, "DEL");
	PushRedisKeyName(L, key);
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 3);
	lua_pushboolean(L, redis->reply->type == REDIS_REPLY_INTEGER && redis->reply->integer != 0);
	CleanReply(redis);
	return 1;
}

int RedisKey_SetTTL(lua_State* L) {

	LuaRedisKey* redisKey = luaL_checkrediskey(L, 1);
	return SetRedisTtl(L, redisKey, luaL_checkinteger(L, 2));
}

int RedisKey_GetTTL(lua_State* L) {

	LuaRedisKey* redisKey = luaL_checkrediskey(L, -1);
	return PushRedisTtl(L, redisKey);
}

int RedisKey_Type(lua_State* L) {

	LuaRedisKey* key = luaL_checkrediskey(L, -1);
	PushRedisRef(L, key);
	lua_pushliteral(L, "TYPE");
	PushRedisKeyName(L, key);
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 3);
	lua_pushlstring(L, redis->reply->str, redis->reply->len);
	CleanReply(redis);
	return 1;
}

int RedisKey_GC(lua_State* L) {
	LuaRedisKey* key = luaL_checkrediskey(L, -1);
	CleanRedisKey(L, key);
	return 0;
}

int RedisKey_ToString(lua_State* L) {
	LuaRedisKey* redis = (LuaRedisKey*)luaL_checkudata(L, -1, REDISKEY);
	lua_pushlstring(L, redis->key, redis->keylen);
	return 1;
}