#include "RedisValue.h"
#include "RedisKey.h"

int JsonRef = LUA_NOREF;

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

static int InternalPushValue(lua_State* L, int index) {

	if (lua_istable(L, index)) {
		if (JsonRef == LUA_NOREF) {
			lua_getglobal(L, "Json");
			lua_pushliteral(L, "Create");
			lua_gettable(L, -2);
			if (lua_pcall(L, 0, 1, NULL)) {
				lua_error(L);
				return 0;
			}
			JsonRef = luaL_ref(L, LUA_REGISTRYINDEX);
			lua_pop(L, 1);
		}

		lua_pushvalue(L, index);
		lua_rawgeti(L, LUA_REGISTRYINDEX, JsonRef);
		lua_pushliteral(L, "Encode");
		lua_gettable(L, -2);
		lua_pushvalue(L, -2);
		lua_pushvalue(L, -4);

		if (lua_pcall(L, 2, 1, NULL)) {
			lua_error(L);
			return 0;
		}

		lua_rotate(L, -2, -1);
		lua_rotate(L, -3, -1);
		lua_pop(L, 2);
	}
	else if (lua_isstring(L, index)) {
		lua_pushvalue(L, index);
	}
	else if (lua_isnil(L, index)) {
		lua_pushnil(L);
	}
	else {
		size_t len;
		luaL_tolstring(L, index, &len);
	}

	return 1;
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
	else if (type == REDIS_VALUE_TYPE_SET) {

		LuaRedisKey* key = InternalGetRedisKey(L, 1);

		lua_pushliteral(L, "set");
		lua_pushnil(L);
		lua_rawset(L, 1);

		lua_pushredisref(L, key);
		lua_pushliteral(L, "SCARD");
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
	else if (type == REDIS_VALUE_TYPE_LIST || type == REDIS_VALUE_TYPE_SET) {

		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);
			lua_pushinteger(L, 0);
		}

		lua_Integer idx = lua_tointeger(L, -1) + 1;
		lua_pop(L, 1);
		lua_pushinteger(L, idx);
		redisvalue_index(L);

		if (lua_isnil(L, -1)) {
			lua_pop(L, 2);
			return 0;
		}

		return 2;
	}
	else if (type == REDIS_VALUE_TYPE_SORTEDSET) {

		lua_pop(L, 1);
		lua_pushvalue(L, lua_upvalueindex(2));
		if (lua_isnil(L, -1)) {
			lua_pop(L, 1);
			lua_pushinteger(L, 0);
		}

		LuaRedisKey* key = InternalGetRedisKey(L, 1);
		lua_pushredisref(L, key);
		lua_pushliteral(L, "ZRANGE");
		lua_pushrediskey(L, key);
		lua_pushvalue(L, -4);
		lua_pushvalue(L, -5);
		lua_pushliteral(L, "WITHSCORES");
		LuaRedis* redis = RedisCommandInternal(L);
		lua_pop(L, 6);

		lua_Integer i = lua_tointeger(L, -1)+1;
		lua_pop(L, 1);
		lua_pushinteger(L, i);
		lua_replace(L, lua_upvalueindex(2));

		if (redis->reply->elements == 2) {
			for (size_t i = 0; i < 2; i++)
			{
				if (redis->reply->element[i]->type == REDIS_REPLY_INTEGER) {
					lua_pushinteger(L, redis->reply->element[i]->integer);
				}
				else if (redis->reply->element[i]->type == REDIS_REPLY_STRING) {
					lua_pushlstring(L, redis->reply->element[i]->str, redis->reply->element[i]->len);
				}
				else {
					lua_pushnil(L);
				}
			}
		}
		else {
			lua_pushnil(L);
			lua_pushnil(L);
		}

		CleanReply(redis);

		if (lua_isnil(L, -1)) {
			lua_pop(L, 2);
			return 0;
		}
		else{
			lua_Integer score = atoll(lua_tostring(L, -1));
			lua_pop(L, 1);
			lua_pushinteger(L, score);
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
	else if (type == REDIS_VALUE_TYPE_SORTEDSET) {

		int top = lua_gettop(L);

		if (lua_isinteger(L, 2)) {
			lua_pushredisref(L, key);
			lua_pushliteral(L, "ZRANGE");
			lua_pushrediskey(L, key);
			lua_Integer idx = lua_tointeger(L, 2);
			lua_pushinteger(L, idx - 1);
			lua_pushinteger(L, idx - 1);
		}
		else if (lua_isstring(L, 2)) {
			lua_pushredisref(L, key);
			lua_pushliteral(L, "ZMSCORE");
			lua_pushrediskey(L, key);
			lua_pushvalue(L, 2);
		}
		else {
			luaL_error(L, "Invalid key type for sorted set");
			return 0;
		}

		LuaRedis* redis = RedisCommandInternal(L);
		lua_settop(L, top);

		redisReply* reply = redis->reply;

		if (redis->reply->type == REDIS_REPLY_ARRAY) {

			if (redis->reply->elements <= 0) {
				reply = NULL;
			}
			else {
				reply = redis->reply->element[0];
			}
		}

		if (!reply || reply->type == REDIS_REPLY_NIL) {
			lua_pushnil(L);
		}
		else if (reply->type == REDIS_REPLY_INTEGER) {
			lua_pushinteger(L, reply->integer);
		}
		else {
			lua_pushlstring(L, reply->str, reply->len);
		}

		CleanReply(redis);
		return 1;
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
	else if (type == REDIS_VALUE_TYPE_SET) {

		if (lua_isinteger(L, -1)) {

			int setidx = lua_tointeger(L, -1);

			if (setidx < -1) {
				luaL_error(L, "Index for sets cannot be negative");
				return 0;
			}
			else if (setidx <= 0) {

				lua_pushredisref(L, key);
				if (setidx == 0) {
					lua_pushliteral(L, "SRANDMEMBER");
				}
				else {
					lua_pushliteral(L, "SPOP");
				}
				lua_pushrediskey(L, key);
				LuaRedis* redis = RedisCommandInternal(L);
				lua_pop(L, 3);

				if (redis->reply->type == REDIS_REPLY_NIL) {
					lua_pushnil(L);
				}
				else {
					lua_pushlstring(L, redis->reply->str, redis->reply->len);
				}

				return 1;
			}

			lua_pushliteral(L, "set");
			lua_rawget(L, -3);

			int len = 0;

			if (lua_istable(L, -1)) {
				len = lua_rawlen(L, -1);
			}

			if (setidx == 1 || len <= 0) {
				lua_pop(L, 1);
				lua_newtable(L);
				lua_pushredisref(L, key);
				lua_pushliteral(L, "SSCAN");
				lua_pushrediskey(L, key);
				lua_pushliteral(L, "0");
				lua_pushliteral(L, "COUNT");
				lua_pushliteral(L, "100");
				LuaRedis* redis = RedisCommandInternal(L);
				len = 0;
				while (true) {

					if (redis->reply->elements != 2) {
						luaL_error(L, "Failed to scan set");
						return 0;
					}

					for (size_t i = 0; i < redis->reply->element[1]->elements; i++)
					{
						lua_pushlstring(L, redis->reply->element[1]->element[i]->str, redis->reply->element[1]->element[i]->len);
						lua_rawseti(L, -8, ++len);
					}

					if (strcmp(redis->reply->element[0]->str, "0") == 0) {
						CleanReply(redis);
						lua_pop(L, 6);

						lua_pushliteral(L, "set");
						lua_pushvalue(L, -2);
						lua_rawset(L, -5);

						break;
					}
					else {
						lua_pushlstring(L, redis->reply->element[0]->str, redis->reply->element[0]->len);
						lua_rotate(L, -4, -1);
						lua_pop(L, 1);
						lua_rotate(L, -3, 1);
						redis = RedisCommandInternal(L);
					}
				}
			}

			lua_rawgeti(L, -1, setidx);
			lua_remove(L, -2);
			if (setidx >= len) {
				lua_pushliteral(L, "set");
				lua_pushnil(L);
				lua_rawset(L, -5);
			}
			return 1;
		}
		else if (!lua_isstring(L, -1)) {
			luaL_error(L, "Set key must be a string");
			return 0;
		}
		else {
			lua_pushredisref(L, key);
			lua_pushliteral(L, "SISMEMBER");
			lua_pushrediskey(L, key);
			lua_pushvalue(L, -4);
			LuaRedis* redis = RedisCommandInternal(L);
			lua_pop(L, 4);
			lua_pushboolean(L, redis->reply->integer);
			CleanReply(redis);

			return 1;
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
		InternalPushValue(L, 3);
		CleanReply(RedisCommandInternal(L));
		lua_pop(L, 5);
	}
	else if (type == REDIS_VALUE_TYPE_SORTEDSET) {

		if (lua_isinteger(L, 3)) {
			lua_pushredisref(L, key);
			lua_pushliteral(L, "ZADD");
			lua_pushrediskey(L, key);
			lua_pushvalue(L, 3);
			lua_pushvalue(L, 2);
			CleanReply(RedisCommandInternal(L));
			lua_pop(L, 5);
		}
		else {
			lua_pushredisref(L, key);
			lua_pushliteral(L, "ZREM");
			lua_pushrediskey(L, key);
			lua_pushvalue(L, 2);
			CleanReply(RedisCommandInternal(L));
			lua_pop(L, 4);
		}

		return 0;
	}
	else if (type == REDIS_VALUE_TYPE_SET) {

		lua_pushredisref(L, key);

		if (lua_isnil(L, 3) || (lua_isboolean(L, 3) && !lua_toboolean(L, 3))) {
			lua_pushliteral(L, "SREM");
		}
		else {
			lua_pushliteral(L, "SADD");
		}

		lua_pushrediskey(L, key);
		lua_pushvalue(L, 2);
		CleanReply(RedisCommandInternal(L));
		lua_pop(L, 4);
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
			InternalPushValue(L, 3);
			CleanReply(RedisCommandInternal(L));
			lua_pop(L, 4);
		}
		else {
			lua_pushredisref(L, key);
			lua_pushliteral(L, "LSET");
			lua_pushrediskey(L, key);
			lua_pushinteger(L, realIdx);
			InternalPushValue(L, 3);
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
	else if (type == REDIS_VALUE_TYPE_SET) {
		lua_pushfstring(L, "RedisValue: set (%s)", key->key);
	}
	else if (type == REDIS_VALUE_TYPE_SORTEDSET) {
		lua_pushfstring(L, "RedisValue: sortedset (%s)", key->key);
	}
	else {
		lua_pushfstring(L, "RedisValue: unknown (%s)", key->key);
	}

	return 1;
}