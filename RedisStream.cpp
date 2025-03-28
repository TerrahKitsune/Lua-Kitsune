#include "RedisStream.h"
#include "RedisKey.h"

int RedisPushStreamInternal(lua_State* L, int redisIdx, const char* key, size_t keylength) {

	luaL_checkudata(L, redisIdx, REDIS);
	lua_pushvalue(L, redisIdx);
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);

	LuaRedisStream* redisStream = (LuaRedisStream*)lua_newuserdata(L, sizeof(LuaRedisStream));
	if (redisStream == NULL) {
		luaL_error(L, "Unable to push redisstream");
		return NULL;
	}
	luaL_getmetatable(L, REDISSTREAM);
	lua_setmetatable(L, -2);
	memset(redisStream, 0, sizeof(LuaRedisStream));
	redisStream->key.redis_ref = ref;

	redisStream->key.key = (char*)gff_malloc(keylength + 1);
	if (!redisStream->key.key) {
		luaL_error(L, "Out of memory");
		return 0;
	}

	redisStream->key.key[keylength] = '\0';
	memcpy(redisStream->key.key, key, keylength);
	redisStream->key.keylen = keylength;

	return 1;
}

LuaRedisStream* lua_toredisstream(lua_State* L, int idx) {

	LuaRedisStream* redis = (LuaRedisStream*)luaL_checkudata(L, idx, REDISSTREAM);
	if (redis == NULL) {
		luaL_error(L, "parameter is not a %s", REDISSTREAM);
		return NULL;
	}
	return redis;
}

int redisstream_call(lua_State* L) {

	LuaRedisStream* redis = (LuaRedisStream*)luaL_checkudata(L, -1, REDISSTREAM);
	lua_pushredisref(L, &redis->key);
	lua_createrediskey(L, -1, redis->key.key, redis->key.keylen);

	return 1;
}

int redisstream_tostring(lua_State* L) {
	LuaRedisStream* redis = (LuaRedisStream*)luaL_checkudata(L, -1, REDISSTREAM);
	lua_pushfstring(L, "RedisValue: stream (%s)", redis->key.key);
	return 1;
}

int redisstream_gc(lua_State* L) {

	LuaRedisStream* stream = lua_toredisstream(L, -1);
	CleanRedisKey(L, &stream->key);
	return 0;
}