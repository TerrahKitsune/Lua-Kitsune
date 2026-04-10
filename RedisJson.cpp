#include "RedisJson.h"
#include "RedisKey.h"
#include "luajson.h"
#include <string.h>
#include <stdio.h>

// Encodes a Lua value at value_idx to a JSON string, pushed on top.
static void push_json_encoded(lua_State* L, int value_idx) {

	value_idx = lua_absindex(L, value_idx);

	// Both Lua nil and the Json.Null sentinel map to JSON null without a Lua call.
	if (lua_isnil(L, value_idx) ||
		(lua_islightuserdata(L, value_idx) &&
		 lua_topointer(L, value_idx) == lua_json_null())) {
		lua_pushliteral(L, "null");
		return;
	}

	lua_pushcfunction(L, lua_json_encode);
	lua_rawgetp(L, LUA_REGISTRYINDEX, lua_json_bridge_registry_key());
	lua_pushvalue(L, value_idx);
	if (lua_pcall_nohook(L, 2, 1, NULL)) {
		lua_error(L);
	}
}

// Decodes a JSON string to a Lua value, pushed on top.
// JSONPath responses are always array-wrapped: [value]. Unwraps the first element.
static void push_decoded_json(lua_State* L, const char* str, size_t len) {

	lua_pushcfunction(L, lua_json_decode);
	lua_rawgetp(L, LUA_REGISTRYINDEX, lua_json_bridge_registry_key());
	lua_pushlstring(L, str, len);
	if (lua_pcall_nohook(L, 2, 1, NULL)) {
		lua_error(L);
		return;
	}

	// JSONPath always wraps the result: [value] → unwrap to value
	if (lua_istable(L, -1)) {
		lua_rawgeti(L, -1, 1);
		lua_remove(L, -2);
	}
}

LuaRedisJson* lua_toredisjon(lua_State* L, int index) {
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
	json->key.redis_ref = LUA_NOREF;

	lua_pushvalue(L, redisIdx);
	json->key.redis_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	json->key.key = (char*)gff_malloc(rediskeylen + 1);
	if (!json->key.key) {
		luaL_error(L, "Out of memory");
	}
	memcpy(json->key.key, rediskey, rediskeylen);
	json->key.key[rediskeylen] = '\0';
	json->key.keylen = rediskeylen;

	json->path = (char*)gff_malloc(pathlen + 1);
	if (!json->path) {
		luaL_error(L, "Out of memory");
	}
	memcpy(json->path, path, pathlen);
	json->path[pathlen] = '\0';
	json->pathlen = pathlen;

	return 1;
}

// Returns a new LuaRedisJson with path extended by ".field".
static void push_json_field(lua_State* L, LuaRedisJson* json, const char* field, size_t fieldlen) {

	size_t new_pathlen = json->pathlen + 1 + fieldlen;
	char*  new_path    = (char*)gff_malloc(new_pathlen + 1);
	if (!new_path) {
		luaL_error(L, "Out of memory");
	}

	memcpy(new_path, json->path, json->pathlen);
	new_path[json->pathlen] = '.';
	memcpy(new_path + json->pathlen + 1, field, fieldlen);
	new_path[new_pathlen] = '\0';

	lua_rawgeti(L, LUA_REGISTRYINDEX, json->key.redis_ref);
	RedisPushJsonInternal(L, -1, json->key.key, json->key.keylen, new_path, new_pathlen);
	lua_remove(L, -2);

	gff_free(new_path);
}

// Returns a new LuaRedisJson with path extended by "[lua_idx-1]" (Lua 1-indexed → JSONPath 0-indexed).
static void push_json_index(lua_State* L, LuaRedisJson* json, lua_Integer lua_idx) {

	char buf[32];
	int  n = snprintf(buf, sizeof(buf), "[%lld]", (long long)(lua_idx - 1));

	size_t new_pathlen = json->pathlen + (size_t)n;
	char*  new_path    = (char*)gff_malloc(new_pathlen + 1);
	if (!new_path) {
		luaL_error(L, "Out of memory");
	}

	memcpy(new_path, json->path, json->pathlen);
	memcpy(new_path + json->pathlen, buf, (size_t)n);
	new_path[new_pathlen] = '\0';

	lua_rawgeti(L, LUA_REGISTRYINDEX, json->key.redis_ref);
	RedisPushJsonInternal(L, -1, json->key.key, json->key.keylen, new_path, new_pathlen);
	lua_remove(L, -2);

	gff_free(new_path);
}

int redisjson_index(lua_State* L) {

	LuaRedisJson* json = lua_toredisjon(L, 1);

	if (lua_type(L, 2) == LUA_TSTRING) {
		size_t      fieldlen;
		const char* field = lua_tolstring(L, 2, &fieldlen);

		for (size_t i = 0; redisjsonfunctions[i].name; i++) {
			if (strcmp(redisjsonfunctions[i].name, field) == 0) {
				lua_pushcfunction(L, redisjsonfunctions[i].func);
				return 1;
			}
		}

		push_json_field(L, json, field, fieldlen);
		return 1;
	}
	else if (lua_isinteger(L, 2)) {
		lua_Integer idx = lua_tointeger(L, 2);
		if (idx < 1) {
			luaL_error(L, "JSON array index must be >= 1 (got %lld)", (long long)idx);
			return 0;
		}
		push_json_index(L, json, idx);
		return 1;
	}

	luaL_error(L, "Invalid index type for JSON path: %s", luaL_typename(L, 2));
	return 0;
}

int redisjson_newindex(lua_State* L) {

	// L: [json(1), key(2), value(3)]
	LuaRedisJson* json = lua_toredisjon(L, 1);

	char*  new_path    = NULL;
	size_t new_pathlen = 0;

	if (lua_type(L, 2) == LUA_TSTRING) {
		size_t      fieldlen;
		const char* field = lua_tolstring(L, 2, &fieldlen);

		new_pathlen = json->pathlen + 1 + fieldlen;
		new_path    = (char*)gff_malloc(new_pathlen + 1);
		if (!new_path) {
			luaL_error(L, "Out of memory");
		}
		memcpy(new_path, json->path, json->pathlen);
		new_path[json->pathlen] = '.';
		memcpy(new_path + json->pathlen + 1, field, fieldlen);
		new_path[new_pathlen] = '\0';
	}
	else if (lua_isinteger(L, 2)) {
		lua_Integer idx = lua_tointeger(L, 2);
		if (idx < 1) {
			luaL_error(L, "JSON array index must be >= 1 (got %lld)", (long long)idx);
			return 0;
		}
		char buf[32];
		int  n       = snprintf(buf, sizeof(buf), "[%lld]", (long long)(idx - 1));
		new_pathlen  = json->pathlen + (size_t)n;
		new_path     = (char*)gff_malloc(new_pathlen + 1);
		if (!new_path) {
			luaL_error(L, "Out of memory");
		}
		memcpy(new_path, json->path, json->pathlen);
		memcpy(new_path + json->pathlen, buf, (size_t)n);
		new_path[new_pathlen] = '\0';
	}
	else {
		luaL_error(L, "Invalid key type for JSON assignment: %s", luaL_typename(L, 2));
		return 0;
	}

	push_json_encoded(L, 3);
	int encoded_pos = lua_gettop(L);  // absolute position of encoded JSON string

	lua_pushredisref(L, &json->key);
	lua_pushliteral(L, "JSON.SET");
	lua_pushrediskey(L, &json->key);
	lua_pushlstring(L, new_path, new_pathlen);
	lua_pushvalue(L, encoded_pos);
	gff_free(new_path);

	LuaRedis* redis = RedisCommandInternal(L);
	lua_settop(L, 3);
	CleanReply(redis);
	return 0;
}

int redisjson_get(lua_State* L) {

	LuaRedisJson* json = lua_toredisjon(L, 1);

	lua_pushredisref(L, &json->key);
	lua_pushliteral(L, "JSON.GET");
	lua_pushrediskey(L, &json->key);
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

	push_decoded_json(L, str, len);
	CleanReply(redis);
	return 1;
}

int redisjson_set(lua_State* L) {

	// L: [json(1), value(2)]
	LuaRedisJson* json = lua_toredisjon(L, 1);

	push_json_encoded(L, 2);
	int encoded_pos = lua_gettop(L);

	lua_pushredisref(L, &json->key);
	lua_pushliteral(L, "JSON.SET");
	lua_pushrediskey(L, &json->key);
	lua_pushlstring(L, json->path, json->pathlen);
	lua_pushvalue(L, encoded_pos);

	LuaRedis* redis = RedisCommandInternal(L);
	lua_settop(L, 2);
	CleanReply(redis);
	return 0;
}

int redisjson_call(lua_State* L) {
	return redisjson_get(L);
}

int redisjson_delete(lua_State* L) {

	LuaRedisJson* json = lua_toredisjon(L, 1);

	lua_pushredisref(L, &json->key);
	lua_pushliteral(L, "JSON.DEL");
	lua_pushrediskey(L, &json->key);
	lua_pushlstring(L, json->path, json->pathlen);
	LuaRedis* redis = RedisCommandInternal(L);
	lua_pop(L, 4);

	lua_pushinteger(L, redis->reply->integer);
	CleanReply(redis);
	return 1;
}

int redisjson_type(lua_State* L) {

	LuaRedisJson* json = lua_toredisjon(L, 1);

	lua_pushredisref(L, &json->key);
	lua_pushliteral(L, "JSON.TYPE");
	lua_pushrediskey(L, &json->key);
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

int redisjson_length(lua_State* L) {

	LuaRedisJson* json = lua_toredisjon(L, 1);

	lua_pushredisref(L, &json->key);
	lua_pushliteral(L, "JSON.ARRLEN");
	lua_pushrediskey(L, &json->key);
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

int redisjson_gc(lua_State* L) {

	LuaRedisJson* json = (LuaRedisJson*)luaL_checkudata(L, 1, REDISJSON);
	CleanRedisKey(L, &json->key);
	if (json->path) {
		gff_free(json->path);
		json->path    = NULL;
		json->pathlen = 0;
	}
	return 0;
}

int redisjson_tostring(lua_State* L) {

	LuaRedisJson* json = lua_toredisjon(L, 1);
	lua_pushfstring(L, "RedisJson: %s %s", json->key.key, json->path);
	return 1;
}

// Empty iterator: terminates a for-in loop immediately.
static int redisjson_empty_iter(lua_State* L) {
	(void)L;
	return 0;
}

// __pairs / Pairs(): fetches the value at this path and returns (next, table, nil)
// so that  for k, v in pairs(json.Foo) do  works like iterating a plain Lua table.
// If the path is missing or resolves to a non-table value, returns an empty iterator.
int redisjson_pairs(lua_State* L) {

	// Fetch the JSON value at this path onto the stack.
	redisjson_get(L);  // pushes one value

	if (!lua_istable(L, -1)) {
		// nil, Json.Null, or a scalar: produce an empty iterator.
		lua_pop(L, 1);
		lua_pushcfunction(L, redisjson_empty_iter);
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
