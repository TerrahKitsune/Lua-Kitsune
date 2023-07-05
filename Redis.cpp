#include "Redis.h"
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h> 
#include <windows.h> 

#pragma comment(lib, "hiredis/hiredis.lib")
#pragma comment(lib, "hiredis/hiredis_ssl.lib")

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
	else if (reply->type == REDIS_REPLY_ARRAY) {

		lua_createtable(L, reply->elements, 0);

		for (int n = 0; n < reply->elements; n++) {
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
	int port = luaL_optinteger(L, 2, 5257);
	const char* data;
	BOOL useTls = lua_toboolean(L, 3);
	long timeout = luaL_optinteger(L, 4, 10);

	LuaRedis* redis = lua_pushredis(L);
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
			sslOptions.verify_mode = luaL_optinteger(L, -1, sslOptions.verify_mode);
			lua_pop(L, 2);
		}

		redis->ssl = redisCreateSSLContextWithOptions(&sslOptions, &ssl_error);

		if (!redis->ssl || ssl_error != REDIS_SSL_CTX_NONE) {
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
			luaL_error(L, "Connection error: %s", redis->context->errstr);
		}
		else {
			luaL_error(L, "Connection error: can't allocate redis context");
		}
		return 0;
	}

	if (redis->ssl) {

		if (redisInitiateSSLWithContext(redis->context, redis->ssl) != REDIS_OK) {
			luaL_error(L, "Error: %s", redis->context->errstr);
			return 0;
		}
	}

	return 1;
}

int RedisCommand(lua_State* L) {

	LuaRedis* luaRedis = lua_toredis(L, 1);
	size_t paramLen = 0;
	const char* command;

	if (!luaRedis->context) {
		luaL_error(L, "Redis not connected");
		return 0;
	}

	CleanReply(luaRedis);

	int top = lua_gettop(L) - 1;

	if (top > 0)
	{
		luaRedis->argv = (char**)gff_calloc(top, sizeof(char*));
		luaRedis->argvlen = (size_t*)gff_calloc(top, sizeof(size_t));

		if (!luaRedis->argv) {
			luaL_error(L, "Out of memory");
			return 0;
		}

		for (int n = 0; n < top; n++) {

			command = lua_tolstring(L, n + 2, &paramLen);
			luaRedis->argv[n] = (char*)gff_malloc(paramLen);

			if (!luaRedis->argv[n]) {
				luaL_error(L, "Out of memory");
				return 0;
			}

			luaRedis->argvlen[n] = paramLen;
			memcpy(luaRedis->argv[n], command, paramLen);
		}

		luaRedis->argc = top;
	}

	luaRedis->reply = (redisReply*)redisCommandArgv(luaRedis->context, luaRedis->argc, (const char**)luaRedis->argv, luaRedis->argvlen);

	if (!luaRedis->reply) {

		luaL_error(L, "Redis connection failed: %s", luaRedis->context->errstr);
		return 0;
	}
	else if (luaRedis->reply->type == REDIS_REPLY_ERROR) {

		luaL_error(L, "Redis error: %s", luaRedis->reply->str);
		return 0;
	}

	int result = PushReply(L, luaRedis->reply);
	CleanReply(luaRedis);

	return result;
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

	return redis;
}

LuaRedis* lua_toredis(lua_State* L, int index) {
	LuaRedis* redis = (LuaRedis*)luaL_checkudata(L, index, REDIS);
	if (redis == NULL)
		luaL_error(L, "parameter is not a %s", REDIS);
	return redis;
}

int redis_gc(lua_State* L) {

	LuaRedis* redis = lua_toredis(L, 1);

	CleanReply(redis);

	if (redis->context) {
		redisFree(redis->context);
	}

	if (redis->ssl) {
		redisFreeSSLContext(redis->ssl);
	}

	memset(redis, 0, sizeof(LuaRedis));

	return 0;
}

int redis_tostring(lua_State* L) {
	char tim[100];
	sprintf(tim, "Redis: 0x%08X", lua_toredis(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}