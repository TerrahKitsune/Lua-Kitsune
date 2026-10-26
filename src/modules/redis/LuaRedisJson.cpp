#include "redis/LuaRedisJson.h"
#include "redis/LuaRedisKey.h"
#include "json/LuaJson.h"
#include <string.h>
#include <stdio.h>

// Encodes a Lua value at valueIdx to a JSON string, pushed on top.
static void PushJsonEncoded(lua_State* L, int valueIdx) {

	valueIdx = lua_absindex(L, valueIdx);

	// Both Lua nil and the Json.Null sentinel map to JSON null without a Lua call.
	if (lua_isnil(L, valueIdx) ||
		(lua_islightuserdata(L, valueIdx) &&
		 lua_topointer(L, valueIdx) == LuaJsonNull())) {
		lua_pushliteral(L, "null");
		return;
	}

	lua_pushcfunction(L, Json_Encode);
	lua_rawgetp(L, LUA_REGISTRYINDEX, LuaJsonBridgeRegistryKey());
	lua_pushvalue(L, valueIdx);
	if (lua_pcall_nohook(L, 2, 1, 0)) {
		lua_error(L);
	}
}

// Decodes a JSON string to a Lua value, pushed on top.
// JSONPath responses are always array-wrapped: [value]. Unwraps the first element.
static void PushDecodedJson(lua_State* L, const char* str, size_t len) {

	lua_pushcfunction(L, Json_Decode);
	lua_rawgetp(L, LUA_REGISTRYINDEX, LuaJsonBridgeRegistryKey());
	lua_pushlstring(L, str, len);
	if (lua_pcall_nohook(L, 2, 1, 0)) {
		lua_error(L);
		return;
	}

	// JSONPath always wraps the result: [value] ? unwrap to value
	if (lua_istable(L, -1)) {
		lua_rawgeti(L, -1, 1);
		lua_remove(L, -2);
	}
}

LuaRedisJson* luaL_checkredisjson(lua_State* L, int index) {
	LuaRedisJson* json = (LuaRedisJson*)luaL_checkudata(L, index, REDISJSON);
	if (!json) {
		luaL_error(L, "parameter is not a %s", REDISJSON);
	}
	return json;
}

int RedisPushJsonInternal(lua_State* L, int redisIdx, const char* rediskey, size_t rediskeylen, const char* path, size_t pathlen) {

	luaL_checkudata(L, redisIdx, REDIS);
	redisIdx = lua_absindex(L, redisIdx);

	LuaRedisJson* json = (LuaRedisJson*)lua_newuserdata(L, sizeof(LuaRedisJson));
	luaL_getmetatable(L, REDISJSON);
	lua_setmetatable(L, -2);
	memset(json, 0, sizeof(LuaRedisJson));
	json->key.redisRef = LUA_NOREF;

	lua_pushvalue(L, redisIdx);
	json->key.redisRef = luaL_ref(L, LUA_REGISTRYINDEX);

	json->key.key = (char*)kitsune_malloc(rediskeylen + 1);
	if (!json->key.key) {
		luaL_error(L, "Out of memory");
	}
	memcpy(json->key.key, rediskey, rediskeylen);
	json->key.key[rediskeylen] = '\0';
	json->key.keylen = rediskeylen;

	json->path = (char*)kitsune_malloc(pathlen + 1);
	if (!json->path) {
		luaL_error(L, "Out of memory");
	}
	memcpy(json->path, path, pathlen);
	json->path[pathlen] = '\0';
	json->pathlen = pathlen;

	return 1;
}

// Returns a new LuaRedisJson with path extended by ".field".
static void PushJsonField(lua_State* L, LuaRedisJson* json, const char* field, size_t fieldlen) {

	size_t newPathlen = json->pathlen + 1 + fieldlen;
	char*  newPath    = (char*)kitsune_malloc(newPathlen + 1);
	if (!newPath) {
		luaL_error(L, "Out of memory");
	}

	memcpy(newPath, json->path, json->pathlen);
	newPath[json->pathlen] = '.';
	memcpy(newPath + json->pathlen + 1, field, fieldlen);
	newPath[newPathlen] = '\0';

	lua_rawgeti(L, LUA_REGISTRYINDEX, json->key.redisRef);
	RedisPushJsonInternal(L, -1, json->key.key, json->key.keylen, newPath, newPathlen);
	lua_remove(L, -2);

	kitsune_free(newPath);
}

// Returns a new LuaRedisJson with path extended by "[lua_idx-1]" (Lua 1-indexed ? JSONPath 0-indexed).
static void PushJsonIndex(lua_State* L, LuaRedisJson* json, lua_Integer lua_idx) {

	char buf[32];
	int  n = snprintf(buf, sizeof(buf), "[%lld]", (long long)(lua_idx - 1));

	size_t newPathlen = json->pathlen + (size_t)n;
	char*  newPath    = (char*)kitsune_malloc(newPathlen + 1);
	if (!newPath) {
		luaL_error(L, "Out of memory");
	}

	memcpy(newPath, json->path, json->pathlen);
	memcpy(newPath + json->pathlen, buf, (size_t)n);
	newPath[newPathlen] = '\0';

	lua_rawgeti(L, LUA_REGISTRYINDEX, json->key.redisRef);
	RedisPushJsonInternal(L, -1, json->key.key, json->key.keylen, newPath, newPathlen);
	lua_remove(L, -2);

	kitsune_free(newPath);
}

int RedisJson_Index(lua_State* L) {

	LuaRedisJson* json = luaL_checkredisjson(L, 1);

	if (lua_type(L, 2) == LUA_TSTRING) {
		size_t      fieldlen;
		const char* field = lua_tolstring(L, 2, &fieldlen);

		for (size_t i = 0; redisjsonfunctions[i].name; i++) {
			if (strcmp(redisjsonfunctions[i].name, field) == 0) {
				lua_pushcfunction(L, redisjsonfunctions[i].func);
				return 1;
			}
		}

		PushJsonField(L, json, field, fieldlen);
		return 1;
	}
	else if (lua_isinteger(L, 2)) {
		lua_Integer idx = lua_tointeger(L, 2);
		if (idx < 1) {
			luaL_error(L, "JSON array index must be >= 1 (got %lld)", (long long)idx);
			return 0;
		}
		PushJsonIndex(L, json, idx);
		return 1;
	}

	luaL_error(L, "Invalid index type for JSON path: %s", luaL_typename(L, 2));
	return 0;
}

int RedisJson_NewIndex(lua_State* L) {

	// L: [json(1), key(2), value(3)]
	LuaRedisJson* json = luaL_checkredisjson(L, 1);

	char*  newPath    = NULL;
	size_t newPathlen = 0;

	if (lua_type(L, 2) == LUA_TSTRING) {
		size_t      fieldlen;
		const char* field = lua_tolstring(L, 2, &fieldlen);

		newPathlen = json->pathlen + 1 + fieldlen;
		newPath    = (char*)kitsune_malloc(newPathlen + 1);
		if (!newPath) {
			luaL_error(L, "Out of memory");
		}
		memcpy(newPath, json->path, json->pathlen);
		newPath[json->pathlen] = '.';
		memcpy(newPath + json->pathlen + 1, field, fieldlen);
		newPath[newPathlen] = '\0';
	}
	else if (lua_isinteger(L, 2)) {
		lua_Integer idx = lua_tointeger(L, 2);
		if (idx < 1) {
			luaL_error(L, "JSON array index must be >= 1 (got %lld)", (long long)idx);
			return 0;
		}
		char buf[32];
		int  n       = snprintf(buf, sizeof(buf), "[%lld]", (long long)(idx - 1));
		newPathlen  = json->pathlen + (size_t)n;
		newPath     = (char*)kitsune_malloc(newPathlen + 1);
		if (!newPath) {
			luaL_error(L, "Out of memory");
		}
		memcpy(newPath, json->path, json->pathlen);
		memcpy(newPath + json->pathlen, buf, (size_t)n);
		newPath[newPathlen] = '\0';
	}
	else {
		luaL_error(L, "Invalid key type for JSON assignment: %s", luaL_typename(L, 2));
		return 0;
	}

	PushJsonEncoded(L, 3);
	int encodedPos = lua_gettop(L);  // absolute position of encoded JSON string

	PushRedisRef(L, &json->key);
	lua_pushliteral(L, "JSON.SET");
	PushRedisKeyName(L, &json->key);
	lua_pushlstring(L, newPath, newPathlen);
	lua_pushvalue(L, encodedPos);
	kitsune_free(newPath);

	LuaRedis* redis = RedisCommandInternal(L);
	lua_settop(L, 3);
	CleanReply(redis);
	return 0;
}

int RedisJson_Get(lua_State* L) {

	LuaRedisJson* json = luaL_checkredisjson(L, 1);

	PushRedisRef(L, &json->key);
	lua_pushliteral(L, "JSON.GET");
	PushRedisKeyName(L, &json->key);
	lua_pushlstring(L, json->path, json->pathlen);
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 4);

	if (redis->reply->type == REDIS_REPLY_NIL ||
		!redis->reply->str || redis->reply->len == 0) {
		lua_pushnil(L);
		CleanReply(redis);
		return 1;
	}

	const char* str = redis->reply->str;
	size_t      len = redis->reply->len;

	// JSON.GET returns the JSON source on first-found. Empty array [] = not found.
	if (len == 2 && str[0] == '[' && str[1] == ']') {
		lua_pushnil(L);
		CleanReply(redis);
		return 1;
	}

	PushDecodedJson(L, str, len);
	CleanReply(redis);
	return 1;
}

int RedisJson_Set(lua_State* L) {

	// L: [json(1), value(2)]
	LuaRedisJson* json = luaL_checkredisjson(L, 1);

	PushJsonEncoded(L, 2);
	int encodedPos = lua_gettop(L);

	PushRedisRef(L, &json->key);
	lua_pushliteral(L, "JSON.SET");
	PushRedisKeyName(L, &json->key);
	lua_pushlstring(L, json->path, json->pathlen);
	lua_pushvalue(L, encodedPos);

	LuaRedis* redis = RedisCommandInternal(L);
	lua_settop(L, 2);
	CleanReply(redis);
	return 0;
}

int RedisJson_Call(lua_State* L) {
	return RedisJson_Get(L);
}

int RedisJson_Delete(lua_State* L) {

	LuaRedisJson* json = luaL_checkredisjson(L, 1);

	PushRedisRef(L, &json->key);
	lua_pushliteral(L, "JSON.DEL");
	PushRedisKeyName(L, &json->key);
	lua_pushlstring(L, json->path, json->pathlen);
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 4);

	lua_pushinteger(L, redis->reply->integer);
	CleanReply(redis);
	return 1;
}

int RedisJson_Type(lua_State* L) {

	LuaRedisJson* json = luaL_checkredisjson(L, 1);

	PushRedisRef(L, &json->key);
	lua_pushliteral(L, "JSON.TYPE");
	PushRedisKeyName(L, &json->key);
	lua_pushlstring(L, json->path, json->pathlen);
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 4);

	// JSON.TYPE with $ path returns an array; legacy path returns a status string
	if (redis->reply->type == REDIS_REPLY_ARRAY && redis->reply->elements > 0) {
		redisReply* r = redis->reply->element[0];
		if (r && (r->type == REDIS_REPLY_STATUS || r->type == REDIS_REPLY_STRING)) {
			lua_pushlstring(L, r->str, r->len);
		}
		else {
			lua_pushnil(L);
		}
	}
	else if (redis->reply->type == REDIS_REPLY_STATUS ||
			 redis->reply->type == REDIS_REPLY_STRING) {
		lua_pushlstring(L, redis->reply->str, redis->reply->len);
	}
	else {
		lua_pushnil(L);
	}

	CleanReply(redis);
	return 1;
}

int RedisJson_Length(lua_State* L) {

	LuaRedisJson* json = luaL_checkredisjson(L, 1);

	PushRedisRef(L, &json->key);
	lua_pushliteral(L, "JSON.ARRLEN");
	PushRedisKeyName(L, &json->key);
	lua_pushlstring(L, json->path, json->pathlen);
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 4);

	if (redis->reply->type == REDIS_REPLY_ARRAY && redis->reply->elements > 0) {
		redisReply* r = redis->reply->element[0];
		if (r && r->type == REDIS_REPLY_INTEGER) {
			lua_pushinteger(L, r->integer);
		}
		else {
			lua_pushnil(L);  // path exists but is not an array
		}
	}
	else if (redis->reply->type == REDIS_REPLY_INTEGER) {
		lua_pushinteger(L, redis->reply->integer);
	}
	else {
		lua_pushnil(L);
	}

	CleanReply(redis);
	return 1;
}

int RedisJson_GC(lua_State* L) {

	LuaRedisJson* json = (LuaRedisJson*)luaL_checkudata(L, 1, REDISJSON);
	CleanRedisKey(L, &json->key);
	if (json->path) {
		kitsune_free(json->path);
		json->path    = NULL;
		json->pathlen = 0;
	}
	return 0;
}

int RedisJson_ToString(lua_State* L) {

	LuaRedisJson* json = luaL_checkredisjson(L, 1);
	lua_pushfstring(L, "RedisJson: %s %s", json->key.key, json->path);
	return 1;
}

// Empty iterator: terminates a for-in loop immediately.
static int RedisJsonEmptyIter(lua_State* L) {
	(void)L;
	return 0;
}

// __pairs / Pairs(): fetches the value at this path and returns (next, table, nil)
// so that  for k, v in pairs(json.Foo) do  works like iterating a plain Lua table.
// If the path is missing or resolves to a non-table value, returns an empty iterator.
int RedisJson_Pairs(lua_State* L) {

	// Fetch the JSON value at this path onto the stack.
	RedisJson_Get(L);  // pushes one value

	if (!lua_istable(L, -1)) {
		// nil, Json.Null, or a scalar: produce an empty iterator.
		lua_pop(L, 1);
		lua_pushcfunction(L, RedisJsonEmptyIter);
		lua_pushnil(L);
		lua_pushnil(L);
		return 3;
	}

	// Stack: [..., fetched_table]
	// Return the standard three-tuple: next, fetched_table, nil
	lua_getglobal(L, "next");  // [..., fetched_table, next_fn]
	lua_insert(L, -2);          // [..., next_fn, fetched_table]
	lua_pushnil(L);             // [..., next_fn, fetched_table, nil]
	return 3;
}
