#include "RedisStream.h"
#include "RedisKey.h"

int RedisPushStreamInternal(lua_State* L, int redisIdx, const char* key, size_t keylength) {

	luaL_checkudata(L, redisIdx, REDIS);
	redisIdx = lua_absindex(L, redisIdx);

	LuaRedisStream* redisStream = (LuaRedisStream*)lua_newuserdata(L, sizeof(LuaRedisStream));
	luaL_getmetatable(L, REDISSTREAM);
	lua_setmetatable(L, -2);
	memset(redisStream, 0, sizeof(LuaRedisStream));

	lua_pushvalue(L, redisIdx);
	redisStream->key.redis_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	redisStream->key.key = (char*)kitsune_malloc(keylength + 1);
	if (!redisStream->key.key) {
		luaL_error(L, "Out of memory");
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

int lua_pairs(lua_State* L) {

	if (lua_isnil(L, -1) && lua_isnil(L, -2)) {

		lua_pop(L, 2);

		lua_getglobal(L, "pairs");
		lua_pushvalue(L, -2);

		if (lua_pcall_nohook(L, 1, 3, NULL)) {
			lua_error(L);
			return 0;
		}

		lua_pushvalue(L, -3);
		lua_rotate(L, -2, -1);
		lua_rotate(L, -3, -1);
		lua_rotate(L, -2, -1);

		if (lua_pcall_nohook(L, 2, 2, NULL)) {
			lua_error(L);
			return 0;
		}

		return 3;
	}
	else if (!lua_isfunction(L, -2)) {
		return 0;
	}

	lua_pushvalue(L, -2);
	lua_pushvalue(L, -4);
	lua_pushvalue(L, -3);

	if (lua_pcall_nohook(L, 2, 2, NULL)) {
		lua_error(L);
		return 0;
	}

	lua_remove(L, -3);

	if (lua_isnil(L, -1) && lua_isnil(L, -2)) {

		lua_pop(L, 3);
		return 0;
	}

	return 3;
}

int redisstream_trim(lua_State* L) {

	LuaRedisStream* redisStream = lua_toredisstream(L, 1); 
	lua_pushredisref(L, &redisStream->key);
	lua_pushliteral(L, "XTRIM");
	lua_pushrediskey(L, &redisStream->key);
	if (lua_isinteger(L, 2)) {
		lua_pushliteral(L, "MAXLEN");
		lua_pushvalue(L, 2);
	}
	else {
		lua_pushliteral(L, "MINID");
		lua_pushvalue(L, 2);
	}
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 5);

	lua_pushinteger(L, redis->reply->integer);
	CleanReply(redis);
	return 1;
}

int redisstream_read(lua_State* L) {

	LuaRedisStream* redisStream = lua_toredisstream(L, 1);
	size_t len;
	const char * id = luaL_optlstring(L, 2, "0-0", &len);
	int blocking = (int)luaL_optinteger(L, 3, 0);
	int expectedTop = lua_gettop(L);

	lua_pushredisref(L, &redisStream->key);
	lua_pushliteral(L, "XREAD");
	lua_pushliteral(L, "COUNT");
	lua_pushliteral(L, "1");
	if (blocking > 0) {
		lua_pushliteral(L, "BLOCK");
		lua_pushinteger(L, blocking);
	}
	lua_pushliteral(L, "STREAMS");
	lua_pushrediskey(L, &redisStream->key);
	lua_pushlstring(L, id, len);
	LuaRedis* redis = RedisCommandInternal(L);
	lua_settop(L, expectedTop);

	if (redis->reply->type != REDIS_REPLY_ARRAY || redis->reply->elements != 1) {
		CleanReply(redis);
		return 0;
	}

	redisReply* reply = redis->reply->element[0];

	if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 2) {
		CleanReply(redis);
		return 0;
	}

	reply = reply->element[1];

	if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 1) {
		CleanReply(redis);
		return 0;
	}

	reply = reply->element[0];

	if (reply->type != REDIS_REPLY_ARRAY || reply->elements != 2) {
		CleanReply(redis);
		return 0;
	}

	lua_pushlstring(L, reply->element[0]->str, reply->element[0]->len);
	reply = reply->element[1];

	if (reply->type != REDIS_REPLY_ARRAY || reply->elements % 2 != 0) {
		CleanReply(redis);
		return 0;
	}

	lua_createtable(L, (int)reply->elements, 0);
	for (size_t i = 0; i < reply->elements; i+=2)
	{
		lua_pushlstring(L, reply->element[i]->str, reply->element[i]->len);
		lua_pushlstring(L, reply->element[i+1]->str, reply->element[i+1]->len);
		lua_rawset(L, -3);
	}

	CleanReply(redis);
	return 2;
}

int redisstream_add(lua_State* L) {

	luaL_checktype(L, 2, LUA_TTABLE);
	int expectedTop = lua_gettop(L);
	LuaRedisStream* redisStream = lua_toredisstream(L, 1);

	lua_pushredisref(L, &redisStream->key);
	lua_pushliteral(L, "XADD");
	lua_pushrediskey(L, &redisStream->key);
	lua_pushliteral(L, "*");

	lua_pushvalue(L, 2);
	lua_pushnil(L);
	lua_pushnil(L);
	while (lua_pairs(L)) {

		lua_checkstack(L, lua_gettop(L) + 5);
		InternalPushValue(L, -1);
		lua_remove(L, -2);
		lua_rotate(L, -4, 1);
		lua_rotate(L, -4, 1);
		lua_pushvalue(L, -4);
	}

	lua_pop(L, 1);

	LuaRedis* redis = RedisCommandInternal(L);
	lua_settop(L, expectedTop);
	lua_pushlstring(L, redis->reply->str, redis->reply->len);
	CleanReply(redis);

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