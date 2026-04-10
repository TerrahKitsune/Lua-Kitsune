#include "Redis.h"
#include "RedisJson.h"
#include <string.h>
#include <stdlib.h>

static void RedisDispose(lua_State* L, LuaRedis* redis) {

	CleanReply(redis);

	if (redis->ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, redis->ref);
		redis->ref = LUA_NOREF;
	}

	if (redis->context) {
		redisFree(redis->context);
		redis->context = NULL;
	}

	if (redis->ssl) {
		redisFreeSSLContext(redis->ssl);
		redis->ssl = NULL;
	}

	gff_free(redis->host);
	gff_free(redis->password);
	memset(redis, 0, sizeof(LuaRedis));
	redis->ref = LUA_NOREF;  // memset zeros to 0; restore to proper sentinel
}

void CleanReply(LuaRedis* luaRedis) {

	if (luaRedis) {

		if (luaRedis->reply) {
			freeReplyObject(luaRedis->reply);
			luaRedis->reply = NULL;
		}

		if (luaRedis->argv) {

			for (int n = 0; n < luaRedis->argc; n++) {
				if (luaRedis->argv[n]) {
					gff_free(luaRedis->argv[n]);
				}
			}

			gff_free(luaRedis->argv);
			luaRedis->argv = NULL;
			luaRedis->argc = 0;
		}

		if (luaRedis->argvlen) {
			gff_free(luaRedis->argvlen);
			luaRedis->argvlen = NULL;
		}
	}
}

int PushReply(lua_State* L, redisReply* reply) {

	if (!reply) {
		lua_pushnil(L);
		return 1;
	}

	lua_createtable(L, 0, 2);

	lua_pushstring(L, "Type");
	lua_pushinteger(L, reply->type);
	lua_settable(L, -3);

	lua_pushstring(L, "Value");

	if (reply->type == REDIS_REPLY_DOUBLE) {

		lua_pushnumber(L, reply->dval);
	}
	else if (reply->type == REDIS_REPLY_INTEGER) {

		lua_pushinteger(L, reply->integer);
	}
	else if (reply->type == REDIS_REPLY_ARRAY ||
			 reply->type == REDIS_REPLY_MAP   ||
			 reply->type == REDIS_REPLY_SET   ||
			 reply->type == REDIS_REPLY_PUSH) {

		lua_createtable(L, (int)reply->elements, 0);

		for (int n = 0; n < (int)reply->elements; n++) {
			PushReply(L, reply->element[n]);
			lua_rawseti(L, -2, n + 1);
		}
	}
	else if (reply->type == REDIS_REPLY_NIL) {
		lua_pushnil(L);
	}
	else {
		lua_pushlstring(L, reply->str, reply->len);
	}

	lua_settable(L, -3);

	return 1;
}

int RedisOpen(lua_State* L) {

	const char* host = luaL_checkstring(L, 1);
	int port = (int)luaL_optinteger(L, 2, 6379);
	const char* data;
	int useTls = lua_toboolean(L, 3);
	long timeout = (long)luaL_optinteger(L, 4, 10);
	const char* password = luaL_optstring(L, 6, NULL);

	LuaRedis* redis = lua_pushredis(L);
	// Store connection parameters for pub/sub cloning; freed by RedisDispose on any exit path.
	size_t hostlen = strlen(host);
	redis->host = (char*)gff_malloc(hostlen + 1);
	if (redis->host) memcpy(redis->host, host, hostlen + 1);
	redis->port = port;
	redis->timeout_sec = timeout;
	if (password) {
		size_t passlen = strlen(password);
		redis->password = (char*)gff_malloc(passlen + 1);
		if (redis->password) memcpy(redis->password, password, passlen + 1);
	}
	redisSSLContextError ssl_error = REDIS_SSL_CTX_NONE;

	if (useTls) {

		redisSSLOptions sslOptions = { 0 };

		if (lua_istable(L, 5)) {

			lua_pushvalue(L, 5);

			lua_pushstring(L, "cacert");
			lua_gettable(L, -2);
			data = lua_tostring(L, -1);
			lua_pop(L, 1);

			if (data) {
				sslOptions.cacert_filename = data;
			}

			lua_pushstring(L, "capath");
			lua_gettable(L, -2);
			data = lua_tostring(L, -1);
			lua_pop(L, 1);

			if (data) {
				sslOptions.capath = data;
			}

			lua_pushstring(L, "cert");
			lua_gettable(L, -2);
			data = lua_tostring(L, -1);
			lua_pop(L, 1);

			if (data) {
				sslOptions.cert_filename = data;
			}

			lua_pushstring(L, "privatekey");
			lua_gettable(L, -2);
			data = lua_tostring(L, -1);
			lua_pop(L, 1);

			if (data) {
				sslOptions.private_key_filename = data;
			}

			lua_pushstring(L, "servername");
			lua_gettable(L, -2);
			data = lua_tostring(L, -1);
			lua_pop(L, 1);

			if (data) {
				sslOptions.server_name = data;
			}

			lua_pushstring(L, "verifymode");
			lua_gettable(L, -2);
			sslOptions.verify_mode = (int)luaL_optinteger(L, -1, sslOptions.verify_mode);
			lua_pop(L, 2);
		}

		redis->ssl = redisCreateSSLContextWithOptions(&sslOptions, &ssl_error);

		if (!redis->ssl || ssl_error != REDIS_SSL_CTX_NONE) {
			RedisDispose(L, redis);
			luaL_error(L, "SSL Context error: %s", redisSSLContextGetError(ssl_error));
			return 0;
		}
	}

	struct timeval tv = { timeout, 0 };
	redisOptions options = { 0 };
	REDIS_OPTIONS_SET_TCP(&options, host, port);
	options.connect_timeout = &tv;

	redis->context = redisConnectWithOptions(&options);

	if (redis->context == NULL || redis->context->err) {
		if (redis->context) {
			char buf[256];
			strncpy(buf, redis->context->errstr, sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = '\0';
			RedisDispose(L, redis);
			luaL_error(L, "Connection error: %s", buf);
		}
		else {
			luaL_error(L, "Connection error: can't allocate redis context");
		}
		return 0;
	}

	if (redis->ssl) {

		if (redisInitiateSSLWithContext(redis->context, redis->ssl) != REDIS_OK) {
			char buf[256];
			strncpy(buf, redis->context->errstr, sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = '\0';
			RedisDispose(L, redis);
			luaL_error(L, "Error: %s", buf);
			return 0;
		}
	}

	if (password) {

		redisReply* reply = (redisReply*)redisCommand(redis->context, "AUTH %s", password);
		if (!reply) {
			RedisDispose(L, redis);
			luaL_error(L, "AUTH error: no reply from server");
			return 0;
		}
		if (reply->type == REDIS_REPLY_ERROR) {
			char buf[256];
			strncpy(buf, reply->str, sizeof(buf) - 1);
			buf[sizeof(buf) - 1] = '\0';
			freeReplyObject(reply);
			RedisDispose(L, redis);
			luaL_error(L, "AUTH failed: %s", buf);
			return 0;
		}
		freeReplyObject(reply);
	}

	return 1;
}

LuaRedis* RedisCommandInternal(lua_State* L) {

	int idx = 0;
	for (int i = lua_gettop(L); i > 0; i--)
	{
		if (luaL_testudata(L, i, REDIS)) {

			idx = i;
			break;
		}
	}

	if (idx < 1) {
		luaL_error(L, "parameter is not a %s", REDIS);
		return NULL;
	}

	LuaRedis* luaRedis = lua_toredis(L, idx);
	size_t paramLen = 0;
	const char* command;

	if (!luaRedis->context) {
		luaL_error(L, "Redis not connected");
		return NULL;
	}

	CleanReply(luaRedis);

	int top = lua_gettop(L) - idx;

	if (top > 0)
	{
		luaRedis->argv = (char**)gff_calloc(top, sizeof(char*));
		luaRedis->argvlen = (size_t*)gff_calloc(top, sizeof(size_t));

		if (!luaRedis->argv || !luaRedis->argvlen) {
			luaL_error(L, "Out of memory");
			return NULL;
		}

		for (int n = 0; n < top; n++) {

			command = lua_tolstring(L, n + idx + 1, &paramLen);
			if (!command) {
				luaL_error(L, "Redis: command argument %d must be a string or number (got %s)",
					n + 1, luaL_typename(L, n + idx + 1));
				return NULL;
			}
			luaRedis->argv[n] = (char*)gff_malloc(paramLen);

			if (!luaRedis->argv[n]) {
				luaL_error(L, "Out of memory");
				return NULL;
			}

			luaRedis->argvlen[n] = paramLen;
			memcpy(luaRedis->argv[n], command, paramLen);
			luaRedis->argc++;
		}
	}

	luaRedis->reply = (redisReply*)redisCommandArgv(luaRedis->context, luaRedis->argc, (const char**)luaRedis->argv, luaRedis->argvlen);

	if (!luaRedis->reply) {

		luaL_error(L, "Redis connection failed: %s", luaRedis->context->errstr);
		return NULL;
	}
	else if (luaRedis->reply->type == REDIS_REPLY_ERROR) {

		luaL_error(L, "Redis error: %s", luaRedis->reply->str);
		return NULL;
	}

	return luaRedis;
}

int RedisCommand(lua_State* L) {
	LuaRedis* luaRedis = RedisCommandInternal(L);

	int result = PushReply(L, luaRedis->reply);
	CleanReply(luaRedis);

	return 1;
}

int RedisGetKey(lua_State* L) {

	size_t len;
	const char* str = luaL_checklstring(L, 2, &len);
	if (len == 0) {
		luaL_error(L, "Key cannot be string empty");
		return 0;
	}
	lua_createrediskey(L, 1, str, len);
	return 1;
}

int RedisGetKeyIterator(lua_State* L) {

	int idx = 0;
	for (int i = lua_gettop(L); i > 0; i--)
	{
		if (luaL_testudata(L, i, REDIS)) {

			idx = i;
			break;
		}
	}

	if (idx == 0) {
		return 0;
	}

	LuaRedis* luaRedis = lua_toredis(L, idx);

	if (lua_isnil(L, -1)) {

		luaRedis->cursor = 0;

		if (luaRedis->ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, luaRedis->ref);
			luaRedis->ref = LUA_NOREF;
		}
	}
	else if (luaRedis->ref != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, luaRedis->ref);
		int len = (int)lua_rawlen(L, -1);
		if (luaRedis->nth >= len) {
			luaL_unref(L, LUA_REGISTRYINDEX, luaRedis->ref);
			luaRedis->ref = LUA_NOREF;

			// If cursor is 0 but we had a chunk that means iteration is over.
			if (luaRedis->cursor == 0) {
				return 0;
			}
		}
	}

	lua_settop(L, idx);

	if (luaRedis->ref == LUA_NOREF) {

		lua_pushliteral(L, "SCAN");
		lua_pushinteger(L, luaRedis->cursor);
		lua_pushliteral(L, "COUNT");
		lua_pushinteger(L, 1000);
		luaRedis = RedisCommandInternal(L);
		lua_pop(L, 4);

		if (luaRedis->reply->elements != 2) {
			CleanReply(luaRedis);
			return 0;
		}

		luaRedis->cursor = strtoull(luaRedis->reply->element[0]->str, NULL, 10);

		lua_createtable(L, (int)luaRedis->reply->element[1]->elements, 0);
		for (size_t i = 0; i < luaRedis->reply->element[1]->elements; i++)
		{
			redisReply* reply = luaRedis->reply->element[1]->element[i];
			lua_pushlstring(L, reply->str, reply->len);
			lua_rawseti(L, -2, i + 1);
		}

		luaRedis->ref = luaL_ref(L, LUA_REGISTRYINDEX);
		luaRedis->nth = 0;
		CleanReply(luaRedis);
	}

	lua_rawgeti(L, LUA_REGISTRYINDEX, luaRedis->ref);
	lua_rawgeti(L, -1, ++luaRedis->nth);

	if (lua_isnil(L, -1)) {
		luaRedis->cursor = 0;

		if (luaRedis->ref != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, luaRedis->ref);
			luaRedis->ref = LUA_NOREF;
		}

		return 0;
	}

	size_t len;
	const char* str = lua_tolstring(L, -1, &len);
	lua_createrediskey(L, idx, str, len);

	return 1;
}

int RedisGetStream(lua_State* L) {

	luaL_checkudata(L, 1, REDIS);
	size_t len;
	const char* key = luaL_checklstring(L, 2, &len);
	if (len == 0) {
		luaL_error(L, "Key cannot be empty");
		return 0;
	}
	RedisPushStreamInternal(L, 1, key, len);
	return 1;
}

int RedisGetSortedSet(lua_State* L) {

	luaL_checkudata(L, 1, REDIS);
	size_t len;
	const char* key = luaL_checklstring(L, 2, &len);
	if (len == 0) {
		luaL_error(L, "Key cannot be empty");
		return 0;
	}
	push_redisvalue(L, 1, REDIS_VALUE_TYPE_SORTEDSET, key, len);
	return 1;
}

int RedisGetSet(lua_State* L) {

	luaL_checkudata(L, 1, REDIS);
	size_t len;
	const char* key = luaL_checklstring(L, 2, &len);
	if (len == 0) {
		luaL_error(L, "Key cannot be empty");
		return 0;
	}
	push_redisvalue(L, 1, REDIS_VALUE_TYPE_SET, key, len);
	return 1;
}

int RedisGetList(lua_State* L) {

	luaL_checkudata(L, 1, REDIS);
	size_t len;
	const char* key = luaL_checklstring(L, 2, &len);
	if (len == 0) {
		luaL_error(L, "Key cannot be empty");
		return 0;
	}
	push_redisvalue(L, 1, REDIS_VALUE_TYPE_LIST, key, len);
	return 1;
}

int RedisGetHashset(lua_State* L) {

	luaL_checkudata(L, 1, REDIS);
	size_t len;
	const char* key = luaL_checklstring(L, 2, &len);
	if (len == 0) {
		luaL_error(L, "Key cannot be empty");
		return 0;
	}
	push_redisvalue(L, 1, REDIS_VALUE_TYPE_HASHSET, key, len);
	return 1;
}

int RedisGetString(lua_State* L) {

	luaL_checkudata(L, 1, REDIS);
	size_t len;
	const char* key = luaL_checklstring(L, 2, &len);
	if (len == 0) {
		luaL_error(L, "Key cannot be empty");
		return 0;
	}
	RedisPushStringInternal(L, 1, key, len);
	return 1;
}

int RedisGetJson(lua_State* L) {

	luaL_checkudata(L, 1, REDIS);
	size_t len;
	const char* key = luaL_checklstring(L, 2, &len);
	if (len == 0) {
		luaL_error(L, "Key cannot be empty");
		return 0;
	}
	RedisPushJsonInternal(L, 1, key, len, "$", 1);
	return 1;
}

LuaRedis* lua_pushredis(lua_State* L) {
	LuaRedis* redis = (LuaRedis*)lua_newuserdata(L, sizeof(LuaRedis));
	if (redis == NULL) {
		luaL_error(L, "Unable to push redis");
		return NULL;
	}
	luaL_getmetatable(L, REDIS);
	lua_setmetatable(L, -2);
	memset(redis, 0, sizeof(LuaRedis));
	redis->ref = LUA_NOREF;

	return redis;
}

LuaRedis* lua_toredis(lua_State* L, int index) {
	LuaRedis* redis = (LuaRedis*)luaL_checkudata(L, index, REDIS);
	if (redis == NULL) {
		luaL_error(L, "parameter is not a %s", REDIS);
	}
	return redis;
}

int redis_gc(lua_State* L) {

	RedisDispose(L, lua_toredis(L, 1));
	return 0;
}

int redis_tostring(lua_State* L) {
	char tim[200];
	sprintf(tim, "Redis: %p", (void*)lua_toredis(L, 1));
	lua_pushstring(L, tim);
	return 1;
}