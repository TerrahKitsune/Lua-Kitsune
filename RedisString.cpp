#include "Redis.h"
#include "RedisString.h"

void* buffer = NULL;

int RedisPushStringInternal(lua_State* L, int redisIdx, const char* key, size_t keylength) {

	luaL_checkudata(L, redisIdx, REDIS);
	lua_pushvalue(L, redisIdx);
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);

	LuaRedisString* redisString = (LuaRedisString*)lua_newuserdata(L, sizeof(LuaRedisString));
	if (redisString == NULL) {
		luaL_error(L, "Unable to push redisstring");
		return NULL;
	}
	luaL_getmetatable(L, REDISSTRING);
	lua_setmetatable(L, -2);
	memset(redisString, 0, sizeof(LuaRedisString));
	redisString->redis_ref = ref;

	redisString->key = (char*)gff_malloc(keylength + sizeof(char));
	if (!redisString->key) {
		luaL_error(L, "Out of memory");
		return NULL;
	}
	redisString->key[keylength] = '\0';
	memcpy(redisString->key, key, keylength);
	redisString->keylen = keylength;

	return 1;
}

LuaRedisString* lua_toredisstring(lua_State* L, int idx) {
	LuaRedisString* redis = (LuaRedisString*)luaL_checkudata(L, idx, REDISSTRING);
	if (redis == NULL) {
		luaL_error(L, "parameter is not a %s", REDISSTRING);
	}
	return redis;
}

int redisstring_getorset(lua_State* L) {

	LuaRedisString* redisString = lua_toredisstring(L, 1);

	if (redisString->redis_ref == LUA_NOREF || redisString->keylen == 0) {
		luaL_error(L, "Invalid redis string");
		return 0;
	}

	lua_rawgeti(L, LUA_REGISTRYINDEX, redisString->redis_ref);
	lua_pushstring(L, "SET");
	lua_pushlstring(L, redisString->key, redisString->keylen);
	lua_pushvalue(L, 2);
	lua_pushstring(L, "NX");
	lua_pushstring(L, "GET");
	lua_pushstring(L, "KEEPTTL");
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 7);

	CleanReply(redis);

	lua_pushvalue(L, 1);
	redisstring_tostring(L);

	return 1;
}

int redisstring_set(lua_State* L) {

	LuaRedisString* redisString = lua_toredisstring(L, 1);

	if (redisString->redis_ref == LUA_NOREF || redisString->keylen == 0) {
		luaL_error(L, "Invalid redis string");
		return 0;
	}

	lua_rawgeti(L, LUA_REGISTRYINDEX, redisString->redis_ref);
	lua_pushstring(L, "SET");
	lua_pushlstring(L, redisString->key, redisString->keylen);
	lua_pushvalue(L, 2);
	lua_pushstring(L, "GET");
	lua_pushstring(L, "KEEPTTL");
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 6);

	lua_pushlstring(L, redis->reply->str, redis->reply->len);
	CleanReply(redis);

	return 1;
}

int redisstring_setat(lua_State* L) {

	LuaRedisString* redisString = lua_toredisstring(L, -3);

	if (redisString->redis_ref == LUA_NOREF || redisString->keylen == 0) {
		luaL_error(L, "Invalid redis string");
		return 0;
	}

	lua_Integer idx = luaL_checkinteger(L, -2);
	if (idx <= 0) {
		luaL_error(L, "Index out of range");
		return 0;
	}

	lua_Integer data = luaL_checkinteger(L, -1);
	if (data < 0 || data > 255) {
		luaL_error(L, "Byte value out of range");
		return 0;
	}

	lua_rawgeti(L, LUA_REGISTRYINDEX, redisString->redis_ref);
	lua_pushstring(L, "SETRANGE");
	lua_pushlstring(L, redisString->key, redisString->keylen);
	lua_pushinteger(L, idx - 1);
	lua_pushlstring(L, (const char*)&data, 1);
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 5);

	return 0;
}

int redisstring_at(lua_State* L) {

	LuaRedisString* redisString = lua_toredisstring(L, 1);

	if (redisString->redis_ref == LUA_NOREF || redisString->keylen == 0) {
		luaL_error(L, "Invalid redis string");
		return 0;
	}

	lua_Integer idx = luaL_checkinteger(L, 2);
	if (idx <= 0) {
		luaL_error(L, "Index out of range");
		return 0;
	}

	lua_rawgeti(L, LUA_REGISTRYINDEX, redisString->redis_ref);
	lua_pushstring(L, "GETRANGE");
	lua_pushlstring(L, redisString->key, redisString->keylen);
	lua_pushinteger(L, idx - 1);
	lua_pushinteger(L, idx - 1);
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 5);

	if (redis->reply->type == REDIS_REPLY_STRING && redis->reply->len == 1) {
		lua_pushinteger(L, redis->reply->str[0]);
	}
	else {
		lua_pushnil(L);
	}

	return 1;
}

int redisstring_iter(lua_State* L) {

	lua_Integer idx = lua_tointeger(L, -1);
	lua_pop(L, 1);
	lua_pushinteger(L, ++idx);
	redisstring_at(L);

	if (lua_isnil(L, -1)) {
		return 0;
	}

	return 2;
}

int redisstring_pairs(lua_State* L) {

	lua_pushcfunction(L, redisstring_iter);
	lua_pushvalue(L, 1);
	lua_pushinteger(L, 0);
	return 3;
}

int redisstring_len(lua_State* L) {

	LuaRedisString* redisString = lua_toredisstring(L, -1);
	if (redisString->redis_ref == LUA_NOREF || redisString->keylen == 0) {
		lua_pushinteger(L, 0);
		return 1;
	}

	lua_rawgeti(L, LUA_REGISTRYINDEX, redisString->redis_ref);
	lua_pushstring(L, "STRLEN");
	lua_pushlstring(L, redisString->key, redisString->keylen);
	LuaRedis* redis = RedisCommandInternal(L);

	lua_Integer result = redis->reply->type == REDIS_REPLY_INTEGER ? redis->reply->integer : 0;
	lua_pop(L, 3);
	CleanReply(redis);

	lua_pushinteger(L, result);

	return 1;
}

int redisstring_concat(lua_State* L) {

	//First arg
	if (luaL_testudata(L, -2, REDISSTRING)) {
		lua_pushvalue(L, -2);
		redisstring_tostring(L);
		lua_remove(L, -2);
	}
	else {
		lua_pushvalue(L, -2);
	}

	// Second arg; +1 to stack
	if (luaL_testudata(L, -2, REDISSTRING)) {
		lua_pushvalue(L, -2);
		redisstring_tostring(L);
		lua_remove(L, -2);
	}
	else {
		lua_pushvalue(L, -2);
	}

	size_t len1;
	size_t len2;

	const char* first = lua_tolstring(L, -2, &len1);
	const char* second = lua_tolstring(L, -1, &len2);

	if (!first || !second) {
		lua_pop(L, 2);
		luaL_error(L, "Cannot concatinate redis string");
		return 0;
	}

	if (buffer) {
		gff_free(buffer);
		buffer = NULL;
	}

	char* concat = (char*)gff_malloc(len1 + len2);
	if (!concat) {
		lua_pop(L, 2);
		luaL_error(L, "Out of memory");
		return 0;
	}

	buffer = concat;

	memcpy(concat, first, len1);
	memcpy(&concat[len1], second, len2);
	lua_pop(L, 2);

	lua_pushlstring(L, concat, len1 + len2);

	if (buffer) {
		gff_free(buffer);
		buffer = NULL;
	}

	return 1;
}

int redisstring_gc(lua_State* L) {

	if (buffer) {
		gff_free(buffer);
		buffer = NULL;
	}

	LuaRedisString* redisString = lua_toredisstring(L, -1);
	if (redisString->redis_ref) {
		luaL_unref(L, LUA_REGISTRYINDEX, redisString->redis_ref);
		redisString->redis_ref = LUA_NOREF;
	}

	if (redisString->key) {
		gff_free(redisString->key);
		redisString->key = NULL;
		redisString->keylen = 0;
	}

	return 0;
}

int redisstring_tostring(lua_State* L) {

	LuaRedisString* redisString = lua_toredisstring(L, -1);

	//if (antiRecursion > 0) {
	//	char tim[200];
	//	sprintf(tim, "RedisString: 0x%016X", (void*)redisString);
	//	lua_pushstring(L, tim);
	//	return 1;
	//}
	if (redisString->redis_ref == LUA_NOREF || redisString->keylen == 0) {
		lua_pushliteral(L);
		return 1;
	}

	lua_rawgeti(L, LUA_REGISTRYINDEX, redisString->redis_ref);
	lua_pushstring(L, "GET");
	lua_pushlstring(L, redisString->key, redisString->keylen);
	LuaRedis* redis = RedisCommandInternal(L);

	if (redis->reply->type != REDIS_REPLY_STRING) {
		CleanReply(redis);
		lua_pop(L, 3);
		lua_pushliteral(L);
		return 1;
	}
	else {
		lua_pop(L, 3);
		lua_pushlstring(L, redis->reply->str, redis->reply->len);
		CleanReply(redis);
	}

	return 1;
}