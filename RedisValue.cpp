#include "RedisValue.h"
#include "RedisKey.h"

static LuaRedisKey* InternalGetRedisKey(lua_State* L, int index) {

	lua_pushvalue(L, index);
	lua_pushliteral(L, "Key");
	lua_rawget(L, -2);
	LuaRedisKey* result = lua_torediskey(L, -1);
	lua_pop(L, 2);
	return result;
}

static int InternalGetRedisType(lua_State* L, int index) {

	lua_pushvalue(L, index);
	lua_pushliteral(L, "Type");
	lua_rawget(L, -2);
	int type = lua_tointeger(L, -1);
	lua_pop(L, 2);
	return type;
}

int redisvalue_len(lua_State* L) {

	int type = InternalGetRedisType(L, 1);

	if (type == REDIS_VALUE_TYPE_LIST) {

		LuaRedisKey* key = InternalGetRedisKey(L, 1);

		lua_pushredisref(L, key);
		lua_pushliteral(L, "LLEN");
		lua_pushrediskey(L, key);
		LuaRedis* redis = RedisCommandInternal(L);
		lua_pop(L, 3);

		lua_pushinteger(L, redis->reply->integer);
		CleanReply(redis);
	}
	else {
		lua_pushinteger(L, 0);
	}

	return 1;
}

int redisvalue_iter(lua_State* L) {

	int type = InternalGetRedisType(L, 1);

	if (type == REDIS_VALUE_TYPE_HASHSET) {

		lua_pop(L, 1);
		const char* cursor = lua_tostring(L, lua_upvalueindex(1));
		lua_pushvalue(L, lua_upvalueindex(2));

		if (!lua_isnil(L, -1)) {
			lua_Integer len = lua_rawlen(L, -1);
			lua_pushvalue(L, lua_upvalueindex(3));
			lua_Integer nth = lua_tointeger(L, -1);
			lua_pop(L, 1);
			if (nth >= len) {
				if (strcmp(cursor, "0") == 0) {
					return 0;
				}
				else {
					lua_pop(L, 1);
					lua_pushnil(L);
				}
			}
		}

		if (lua_isnil(L, -1)) {

			LuaRedisKey* key = InternalGetRedisKey(L, 1);

			lua_pushredisref(L, key);
			lua_pushliteral(L, "HSCAN");
			lua_pushrediskey(L, key);
			lua_pushstring(L, cursor);
			lua_pushliteral(L, "COUNT");
			lua_pushinteger(L, 1);
			LuaRedis* redis = RedisCommandInternal(L);
			lua_pop(L, 6);

			if (redis->reply->elements != 2 || redis->reply->element[1]->elements % 2 != 0) {
				CleanReply(redis);
				return 0;
			}

			lua_pushlstring(L, redis->reply->element[0]->str, redis->reply->element[0]->len);
			lua_replace(L, lua_upvalueindex(1));

			lua_createtable(L, 0, 0);

			for (size_t i = 0; i < redis->reply->element[1]->elements; i += 2)
			{
				redisReply* k = redis->reply->element[1]->element[i];
				redisReply* v = redis->reply->element[1]->element[i + 1];

				lua_createtable(L, 2, 0);
				lua_pushlstring(L, k->str, k->len);
				lua_rawseti(L, -2, 1);
				lua_pushlstring(L, v->str, v->len);
				lua_rawseti(L, -2, 2);
				lua_rawseti(L, -2, (i / 2) + 1);
			}

			CleanReply(redis);

			lua_replace(L, lua_upvalueindex(2));
			lua_pushinteger(L, 0);
			lua_replace(L, lua_upvalueindex(3));
		}

		lua_pushvalue(L, lua_upvalueindex(2));
		lua_pushvalue(L, lua_upvalueindex(3));
		lua_Integer nth = lua_tointeger(L, -1) + 1;
		lua_pop(L, 1);
		lua_rawgeti(L, -1, nth);
		lua_rawgeti(L, -1, 1);
		lua_rawgeti(L, -2, 2);

		lua_pushinteger(L, nth);
		lua_replace(L, lua_upvalueindex(3));

		return 2;
	}
	else if (type == REDIS_VALUE_TYPE_LIST) {

		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);
			lua_pushinteger(L, 0);
		}
		
		lua_Integer idx = lua_tointeger(L, -1)+1;
		lua_pop(L, 1);
		lua_pushinteger(L, idx);
		redisvalue_index(L);

		if (lua_isnil(L, -1)) {
			lua_pop(L, 2);
			return 0;
		}

		return 2;
	}

	return 0;
}

int redisvalue_pairs(lua_State* L) {

	lua_pushliteral(L, "0");
	lua_pushnil(L);
	lua_pushnil(L);
	lua_pushcclosure(L, redisvalue_iter, 3);
	lua_pushvalue(L, 1);
	lua_pushnil(L);
	return 3;
}

int push_redisvalue(lua_State* L, int redisIdx, int type, const char* key, size_t keylen) {

	luaL_checkudata(L, redisIdx, REDIS);
	lua_pushvalue(L, redisIdx);

	lua_createtable(L, 0, 0);
	luaL_getmetatable(L, REDISVALUE);
	lua_setmetatable(L, -2);

	lua_pushliteral(L, "Key");
	lua_createrediskey(L, -3, key, keylen);
	lua_rawset(L, -3);

	lua_remove(L, -2);

	lua_pushliteral(L, "Type");
	lua_pushinteger(L, type);
	lua_rawset(L, -3);

	return 1;
}

int redisvalue_call(lua_State* L) {

	lua_pushvalue(L, 1);
	lua_pushliteral(L, "Key");
	lua_rawget(L, -2);
	lua_remove(L, -2);

	lua_pushvalue(L, 1);
	lua_pushliteral(L, "Type");
	lua_rawget(L, -2);
	lua_remove(L, -2);

	return 2;
}

int redisvalue_index(lua_State* L) {

	LuaRedisKey* key = InternalGetRedisKey(L, 1);
	int type = InternalGetRedisType(L, 1);

	if (type == REDIS_VALUE_TYPE_HASHSET) {

		lua_pushredisref(L, key);
		lua_pushliteral(L, "HGET");
		lua_pushrediskey(L, key);
		lua_pushvalue(L, 2);
		LuaRedis* redis = RedisCommandInternal(L);
		lua_pop(L, 4);
		if (redis->reply->type == REDIS_REPLY_NIL) {
			lua_pushnil(L);
		}
		else {
			lua_pushlstring(L, redis->reply->str, redis->reply->len);
		}
		CleanReply(redis);
	}
	else if (type == REDIS_VALUE_TYPE_LIST) {

		lua_Integer idx = luaL_checkinteger(L, 2);
		lua_Integer realIdx = idx > 0 ? idx - 1 : idx;

		if (idx == 0) {
			lua_pushredisref(L, key);
			lua_pushliteral(L, "LPOP");
			lua_pushrediskey(L, key);
			LuaRedis* redis = RedisCommandInternal(L);
			lua_pop(L, 3);
			if (redis->reply->type == REDIS_REPLY_NIL) {
				lua_pushnil(L);
			}
			else {
				lua_pushlstring(L, redis->reply->str, redis->reply->len);
			}
			CleanReply(redis);
		}
		else {

			lua_pushredisref(L, key);
			lua_pushliteral(L, "LINDEX");
			lua_pushrediskey(L, key);
			lua_pushinteger(L, realIdx);
			LuaRedis* redis = RedisCommandInternal(L);
			lua_pop(L, 4);

			if (redis->reply->type == REDIS_REPLY_NIL) {
				lua_pushnil(L);
			}
			else {
				lua_pushlstring(L, redis->reply->str, redis->reply->len);
			}
			CleanReply(redis);
		}
	}
	else {
		luaL_error(L, "Cannot index redisvalue with type %d", type);
		return 0;
	}

	return 1;
}

int redisvalue_newindex(lua_State* L) {

	LuaRedisKey* key = InternalGetRedisKey(L, 1);
	int type = InternalGetRedisType(L, 1);

	if (type == REDIS_VALUE_TYPE_HASHSET) {

		lua_pushredisref(L, key);
		lua_pushliteral(L, "HSET");
		lua_pushrediskey(L, key);
		lua_pushvalue(L, 2);
		lua_pushvalue(L, 3);
		CleanReply(RedisCommandInternal(L));
		lua_pop(L, 5);
	}
	else if (type == REDIS_VALUE_TYPE_LIST) {

		redisvalue_len(L);
		int len = lua_tointeger(L, -1);
		lua_pop(L, 1);
		lua_Integer idx = luaL_checkinteger(L, 2);
		lua_Integer realIdx = idx > 0 ? idx - 1 : idx;

		if (idx == 0 || abs(realIdx) >= len) {
			lua_pushredisref(L, key);
			if (idx >= 0) {
				lua_pushliteral(L, "RPUSH");
			}
			else {
				lua_pushliteral(L, "LPUSH");
			}
			lua_pushrediskey(L, key);
			lua_pushvalue(L, 3);
			CleanReply(RedisCommandInternal(L));
			lua_pop(L, 4);
		}
		else {
			lua_pushredisref(L, key);
			lua_pushliteral(L, "LSET");
			lua_pushrediskey(L, key);
			lua_pushinteger(L, realIdx);
			lua_pushvalue(L, 3);		
			CleanReply(RedisCommandInternal(L));
			lua_pop(L, 5);
		}
	}
	else {
		luaL_error(L, "Cannot index redisvalue with type %d", type);
		return 0;
	}

	return 0;
}

int redisvalue_gc(lua_State* L) {

	// Unset key to speed up gc
	lua_pushliteral(L, "Key");
	lua_pushnil(L);
	lua_rawset(L, -3);

	return 0;
}

int redisvalue_tostring(lua_State* L) {

	LuaRedisKey* key = InternalGetRedisKey(L, -1);
	int type = InternalGetRedisType(L, -1);

	if (!key) {
		lua_pushfstring(L, "RedisValue: none");
		return 1;
	}

	if (type == REDIS_VALUE_TYPE_HASHSET) {
		lua_pushfstring(L, "RedisValue: hash (%s)", key->key);
	}
	else if (type == REDIS_VALUE_TYPE_LIST) {
		lua_pushfstring(L, "RedisValue: list (%s)", key->key);
	}
	else {
		lua_pushfstring(L, "RedisValue: none (%s)", key->key);
	}

	return 1;
}