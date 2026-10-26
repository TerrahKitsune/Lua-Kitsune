#include "redis/LuaRedisValue.h"
#include "redis/LuaRedisKey.h"
#include "json/LuaJson.h"

LuaRedisValue* luaL_checkredisvalue(lua_State* L, int index) {
	LuaRedisValue* val = (LuaRedisValue*)luaL_checkudata(L, index, REDISVALUE);
	if (!val) {
		luaL_error(L, "parameter is not a %s", REDISVALUE);
	}
	return val;
}

int InternalPushValue(lua_State* L, int index) {

	if (lua_istable(L, index)) {
		lua_pushcfunction(L, Json_Encode);
		lua_rawgetp(L, LUA_REGISTRYINDEX, LuaJsonBridgeRegistryKey());
		lua_pushvalue(L, lua_absindex(L, index));
		if (lua_pcall_nohook(L, 2, 1, NULL)) {
			lua_error(L);
			return 0;
		}
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

int RedisValue_Len(lua_State* L) {

	LuaRedisValue* val = luaL_checkredisvalue(L, 1);

	if (val->type == REDIS_VALUE_TYPE_LIST) {

		PushRedisRef(L, &val->key);
		lua_pushliteral(L, "LLEN");
		PushRedisKeyName(L, &val->key);
		LuaRedis* redis = RedisCommandInternal(L);
		lua_pop(L, 3);

		lua_pushinteger(L, redis->reply->integer);
		CleanReply(redis);
	}
	else if (val->type == REDIS_VALUE_TYPE_SET) {

		if (val->scanCacheRef != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, val->scanCacheRef);
			val->scanCacheRef = LUA_NOREF;
		}

		PushRedisRef(L, &val->key);
		lua_pushliteral(L, "SCARD");
		PushRedisKeyName(L, &val->key);
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

int RedisValueIter(lua_State* L) {

	LuaRedisValue* val = luaL_checkredisvalue(L, 1);
	int type = val->type;

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

			LuaRedisKey* key = &val->key;
			LuaRedis* redis;

			while (true) {

				PushRedisRef(L, key);
				lua_pushliteral(L, "HSCAN");
				PushRedisKeyName(L, key);
				lua_pushstring(L, cursor);
				lua_pushliteral(L, "COUNT");
				lua_pushinteger(L, 1);
				redis = RedisCommandInternal(L);
				lua_pop(L, 6);

				if (redis->reply->elements != 2 || redis->reply->element[1]->elements % 2 != 0) {
					CleanReply(redis);
					return 0;
				}

				lua_pushlstring(L, redis->reply->element[0]->str, redis->reply->element[0]->len);
				lua_replace(L, lua_upvalueindex(1));
			
				if (redis->reply->element[1]->elements > 0) {
					break;
				}
				else {
					CleanReply(redis);
					cursor = lua_tostring(L, lua_upvalueindex(1));

					if (strcmp(cursor, "0") == 0) {
						CleanReply(redis);
						return 0;
					}
				}
			}

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
		if (lua_isnil(L, -1)) {
			return 0;
		}
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
		RedisValue_Index(L);

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

		LuaRedisKey* key = &val->key;
		PushRedisRef(L, key);
		lua_pushliteral(L, "ZRANGE");
		PushRedisKeyName(L, key);
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
		else {
			const char* scoreStr = lua_tostring(L, -1);
			lua_pop(L, 1);
			if (!lua_stringtonumber(L, scoreStr))
				lua_pushnumber(L, 0);
		}

		return 2;
	}

	return 0;
}

int RedisValue_Pairs(lua_State* L) {

	lua_pushliteral(L, "0");
	lua_pushnil(L);
	lua_pushnil(L);
	lua_pushcclosure(L, RedisValueIter, 3);
	lua_pushvalue(L, 1);
	lua_pushnil(L);
	return 3;
}

int lua_pushredisvalue(lua_State* L, int redisIdx, int type, const char* key, size_t keylen) {

	luaL_checkudata(L, redisIdx, REDIS);
	redisIdx = lua_absindex(L, redisIdx);

	LuaRedisValue* val = (LuaRedisValue*)lua_newuserdata(L, sizeof(LuaRedisValue));
	luaL_getmetatable(L, REDISVALUE);
	lua_setmetatable(L, -2);
	memset(val, 0, sizeof(LuaRedisValue));
	val->type           = type;
	val->scanCacheRef = LUA_NOREF;
	val->key.redisRef  = LUA_NOREF;

	lua_pushvalue(L, redisIdx);
	val->key.redisRef = luaL_ref(L, LUA_REGISTRYINDEX);

	val->key.key = (char*)kitsune_malloc(keylen + 1);
	if (!val->key.key) {
		luaL_error(L, "Out of memory");
	}
	memcpy(val->key.key, key, keylen);
	val->key.key[keylen] = '\0';
	val->key.keylen = keylen;

	return 1;
}

int RedisValue_Call(lua_State* L) {

	LuaRedisValue* val = luaL_checkredisvalue(L, 1);
	PushRedisRef(L, &val->key);
	lua_pushrediskey(L, -1, val->key.key, val->key.keylen);
	lua_remove(L, -2);
	lua_pushinteger(L, val->type);
	return 2;
}

int RedisValue_Index(lua_State* L) {

	LuaRedisValue* val = luaL_checkredisvalue(L, 1);
	LuaRedisKey* key = &val->key;
	int type = val->type;

	if (type == REDIS_VALUE_TYPE_HASHSET) {

		PushRedisRef(L, key);
		lua_pushliteral(L, "HGET");
		PushRedisKeyName(L, key);
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
			PushRedisRef(L, key);
			lua_pushliteral(L, "ZRANGE");
			PushRedisKeyName(L, key);
			lua_Integer idx = lua_tointeger(L, 2);
			lua_pushinteger(L, idx - 1);
			lua_pushinteger(L, idx - 1);
		}
		else if (lua_isstring(L, 2)) {
			PushRedisRef(L, key);
			lua_pushliteral(L, "ZMSCORE");
			PushRedisKeyName(L, key);
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
			PushRedisRef(L, key);
			lua_pushliteral(L, "LPOP");
			PushRedisKeyName(L, key);
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

			PushRedisRef(L, key);
			lua_pushliteral(L, "LINDEX");
			PushRedisKeyName(L, key);
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

			int setidx = (int)lua_tointeger(L, -1);

			if (setidx < -1) {
				luaL_error(L, "Index for sets cannot be negative");
				return 0;
			}
			else if (setidx <= 0) {

				PushRedisRef(L, key);
				if (setidx == 0) {
					lua_pushliteral(L, "SRANDMEMBER");
				}
				else {
					lua_pushliteral(L, "SPOP");
				}
				PushRedisKeyName(L, key);
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

			int len = 0;
			if (val->scanCacheRef != LUA_NOREF) {
				lua_rawgeti(L, LUA_REGISTRYINDEX, val->scanCacheRef);
				len = (int)lua_rawlen(L, -1);
			}
			else {
				lua_pushnil(L);
			}

			if (setidx == 1 || len <= 0) {
				lua_pop(L, 1);
				lua_newtable(L);
				PushRedisRef(L, key);
				lua_pushliteral(L, "SSCAN");
				PushRedisKeyName(L, key);
				lua_pushliteral(L, "0");
				lua_pushliteral(L, "COUNT");
				lua_pushliteral(L, "100");
				LuaRedis* redis = RedisCommandInternal(L);
				len = 0;
				while (true) {

					if (redis->reply->elements != 2) {
						CleanReply(redis);
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

						if (val->scanCacheRef != LUA_NOREF) {
							luaL_unref(L, LUA_REGISTRYINDEX, val->scanCacheRef);
						}
						lua_pushvalue(L, -1);
						val->scanCacheRef = luaL_ref(L, LUA_REGISTRYINDEX);

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
				if (val->scanCacheRef != LUA_NOREF) {
					luaL_unref(L, LUA_REGISTRYINDEX, val->scanCacheRef);
					val->scanCacheRef = LUA_NOREF;
				}
			}
			return 1;
		}
		else if (!lua_isstring(L, -1)) {
			luaL_error(L, "Set key must be a string");
			return 0;
		}
		else {
			PushRedisRef(L, key);
			lua_pushliteral(L, "SISMEMBER");
			PushRedisKeyName(L, key);
			lua_pushvalue(L, -4);
			LuaRedis* redis = RedisCommandInternal(L);
			lua_pop(L, 4);
			lua_pushboolean(L, (int)redis->reply->integer);
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

int RedisValue_NewIndex(lua_State* L) {

	LuaRedisValue* val = luaL_checkredisvalue(L, 1);
	LuaRedisKey* key = &val->key;
	int type = val->type;

	if (type == REDIS_VALUE_TYPE_HASHSET) {

		if (lua_isnil(L, 3)) {
			PushRedisRef(L, key);
			lua_pushliteral(L, "HDEL");
			PushRedisKeyName(L, key);
			lua_pushvalue(L, 2);
			CleanReply(RedisCommandInternal(L));
			lua_pop(L, 4);
		}
		else {
			PushRedisRef(L, key);
			lua_pushliteral(L, "HSET");
			PushRedisKeyName(L, key);
			lua_pushvalue(L, 2);
			InternalPushValue(L, 3);
			CleanReply(RedisCommandInternal(L));
			lua_pop(L, 5);
		}

		return 0;
	}
	if (type == REDIS_VALUE_TYPE_SORTEDSET) {

		if (lua_isnumber(L, 3)) {
			PushRedisRef(L, key);
			lua_pushliteral(L, "ZADD");
			PushRedisKeyName(L, key);
			lua_pushvalue(L, 3);
			lua_pushvalue(L, 2);
			CleanReply(RedisCommandInternal(L));
			lua_pop(L, 5);
		}
		else {
			PushRedisRef(L, key);
			lua_pushliteral(L, "ZREM");
			PushRedisKeyName(L, key);
			lua_pushvalue(L, 2);
			CleanReply(RedisCommandInternal(L));
			lua_pop(L, 4);
		}

		return 0;
	}
	else if (type == REDIS_VALUE_TYPE_SET) {

		PushRedisRef(L, key);

		if (lua_isnil(L, 3) || (lua_isboolean(L, 3) && !lua_toboolean(L, 3))) {
			lua_pushliteral(L, "SREM");
		}
		else {
			lua_pushliteral(L, "SADD");
		}

		PushRedisKeyName(L, key);
		lua_pushvalue(L, 2);
		CleanReply(RedisCommandInternal(L));
		lua_pop(L, 4);

		if (val->scanCacheRef != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, val->scanCacheRef);
			val->scanCacheRef = LUA_NOREF;
		}
	}
	else if (type == REDIS_VALUE_TYPE_LIST) {

		RedisValue_Len(L);
		int len = (int)lua_tointeger(L, -1);
		lua_pop(L, 1);
		lua_Integer idx = luaL_checkinteger(L, 2);
		lua_Integer realIdx = idx > 0 ? idx - 1 : idx;

		if (idx == 0 || abs(realIdx) >= len) {
			PushRedisRef(L, key);
			if (idx >= 0) {
				lua_pushliteral(L, "RPUSH");
			}
			else {
				lua_pushliteral(L, "LPUSH");
			}
			PushRedisKeyName(L, key);
			InternalPushValue(L, 3);
			CleanReply(RedisCommandInternal(L));
			lua_pop(L, 4);
		}
		else {
			PushRedisRef(L, key);
			lua_pushliteral(L, "LSET");
			PushRedisKeyName(L, key);
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

int RedisValue_GC(lua_State* L) {

	LuaRedisValue* val = luaL_checkredisvalue(L, 1);
	CleanRedisKey(L, &val->key);
	if (val->scanCacheRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, val->scanCacheRef);
		val->scanCacheRef = LUA_NOREF;
	}
	return 0;
}

int RedisValue_ToString(lua_State* L) {

	LuaRedisValue* val = luaL_checkredisvalue(L, 1);

	switch (val->type) {
	case REDIS_VALUE_TYPE_HASHSET:   lua_pushfstring(L, "RedisValue: hash (%s)",      val->key.key); break;
	case REDIS_VALUE_TYPE_LIST:      lua_pushfstring(L, "RedisValue: list (%s)",      val->key.key); break;
	case REDIS_VALUE_TYPE_SET:       lua_pushfstring(L, "RedisValue: set (%s)",       val->key.key); break;
	case REDIS_VALUE_TYPE_SORTEDSET: lua_pushfstring(L, "RedisValue: sortedset (%s)", val->key.key); break;
	default:                         lua_pushfstring(L, "RedisValue: unknown (%s)",   val->key.key); break;
	}

	return 1;
}