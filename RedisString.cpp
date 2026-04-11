#include "Redis.h"
#include "RedisString.h"

int RedisPushStringInternal(lua_State* L, int redisIdx, const char* key, size_t keylength) {

	luaL_checkudata(L, redisIdx, REDIS);
	redisIdx = lua_absindex(L, redisIdx);

	LuaRedisString* redisString = (LuaRedisString*)lua_newuserdata(L, sizeof(LuaRedisString));
	luaL_getmetatable(L, REDISSTRING);
	lua_setmetatable(L, -2);
	memset(redisString, 0, sizeof(LuaRedisString));

	lua_pushvalue(L, redisIdx);
	redisString->key.redis_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	redisString->key.key = (char*)kitsune_malloc(keylength + sizeof(char));
	if (!redisString->key.key) {
		luaL_error(L, "Out of memory");
	}
	redisString->key.key[keylength] = '\0';
	memcpy(redisString->key.key, key, keylength);
	redisString->key.keylen = keylength;

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

	lua_pushredisref(L, &redisString->key);
	lua_pushstring(L, "SET");
	lua_pushrediskey(L, &redisString->key);
	lua_pushvalue(L, 2);
	lua_pushstring(L, "NX");
	lua_pushstring(L, "GET");
	lua_pushstring(L, "KEEPTTL");
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 7);

	if (redis->reply->type == REDIS_REPLY_STRING) {
		lua_pushlstring(L, redis->reply->str, redis->reply->len);
	}
	else {
		lua_pushvalue(L, 2);
	}
	CleanReply(redis);

	return 1;
}

int redisstring_set(lua_State* L) {

	LuaRedisString* redisString = lua_toredisstring(L, 1);

	lua_pushredisref(L, &redisString->key);
	lua_pushstring(L, "SET");
	lua_pushrediskey(L, &redisString->key);
	lua_pushvalue(L, 2);
	lua_pushstring(L, "GET");
	lua_pushstring(L, "KEEPTTL");
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 6);

	if (redis->reply->type == REDIS_REPLY_NIL) {
		lua_pushnil(L);
	}
	else {
		lua_pushlstring(L, redis->reply->str, redis->reply->len);
	}
	CleanReply(redis);

	return 1;
}

int redisstring_setat(lua_State* L) {

	LuaRedisString* redisString = lua_toredisstring(L, -3);

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

	lua_pushredisref(L, &redisString->key);
	lua_pushstring(L, "SETRANGE");
	lua_pushrediskey(L, &redisString->key);
	lua_pushinteger(L, idx - 1);
	char byte_val = (char)(unsigned char)data;
	lua_pushlstring(L, &byte_val, 1);
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 5);

	return 0;
}

int redisstring_at(lua_State* L) {

	LuaRedisString* redisString = lua_toredisstring(L, 1);

	lua_Integer idx = luaL_checkinteger(L, 2);
	if (idx <= 0) {
		luaL_error(L, "Index out of range");
		return 0;
	}

	lua_pushredisref(L, &redisString->key);
	lua_pushstring(L, "GETRANGE");
	lua_pushrediskey(L, &redisString->key);
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

int redisstring_setttl(lua_State* L) {

	LuaRedisString* redisString = lua_toredisstring(L, 1);
	return lua_setredisttl(L, &redisString->key, luaL_checkinteger(L, 2));
}

int redisstring_getttl(lua_State* L) {

	LuaRedisString* redisString = lua_toredisstring(L, -1);
	return lua_pushredisttl(L, &redisString->key);
}

int redisstring_delete(lua_State* L) {

	LuaRedisString* redisString = lua_toredisstring(L, -1);

	lua_pushredisref(L, &redisString->key);
	lua_pushstring(L, "GETDEL");
	lua_pushrediskey(L, &redisString->key);
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 3);

	if (redis->reply->type == REDIS_REPLY_STRING) {
		lua_pushlstring(L, redis->reply->str, redis->reply->len);
	}
	else {
		lua_pushnil(L);
	}

	CleanReply(redis);

	return 1;
}

int redisstring_len(lua_State* L) {

	LuaRedisString* redisString = lua_toredisstring(L, -1);

	lua_pushredisref(L, &redisString->key);
	lua_pushstring(L, "STRLEN");
	lua_pushrediskey(L, &redisString->key);
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

	char* concat = (char*)kitsune_malloc(len1 + len2);
	if (!concat) {
		lua_pop(L, 2);
		luaL_error(L, "Out of memory");
		return 0;
	}

	memcpy(concat, first, len1);
	memcpy(&concat[len1], second, len2);
	lua_pop(L, 2);

	lua_pushlstring(L, concat, len1 + len2);
	kitsune_free(concat);

	return 1;
}

int redisstring_call(lua_State* L) {

	LuaRedisString* redis = (LuaRedisString*)luaL_checkudata(L, -1, REDISSTRING);
	lua_pushredisref(L, &redis->key);
	lua_createrediskey(L, -1, redis->key.key, redis->key.keylen);

	return 1;
}

int redisstring_gc(lua_State* L) {

	LuaRedisString* redisString = lua_toredisstring(L, -1);

	CleanRedisKey(L, &redisString->key);

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
	lua_pushredisref(L, &redisString->key);
	lua_pushstring(L, "GET");
	lua_pushrediskey(L, &redisString->key);
	LuaRedis* redis = RedisCommandInternal(L);

	if (redis->reply->type != REDIS_REPLY_STRING) {
		CleanReply(redis);
		lua_pop(L, 3);
		lua_pushliteral(L, "");
		return 1;
	}
	else {
		lua_pop(L, 3);
		lua_pushlstring(L, redis->reply->str, redis->reply->len);
		CleanReply(redis);
	}

	return 1;
}