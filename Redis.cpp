#include "Redis.h"
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h> 
#include <windows.h> 

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

unsigned __stdcall threadPollFunc(void* data) {

	LuaRedis* luaRedis = (LuaRedis*)data;
	redisReply* pollReply = NULL;
	bool hasReply;

	while (luaRedis->isAlive) {

		redisGetReply(luaRedis->context, (void**)&pollReply);

		EnterCriticalSection(&luaRedis->CriticalSection);

		while (luaRedis->pollReply != NULL) {
			LeaveCriticalSection(&luaRedis->CriticalSection);
			if (!luaRedis->isAlive) {
				return 0;
			}
			Sleep(1);
			EnterCriticalSection(&luaRedis->CriticalSection);
		}

		luaRedis->pollReply = pollReply;
		LeaveCriticalSection(&luaRedis->CriticalSection);
		pollReply = NULL;
	}

	return 0;
}

int RedisPushPollReply(lua_State* L, redisReply* reply) {

	if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements == 3) {

		for (int n = 0; n < 3; n++) {
			if (reply->element[n]->type != REDIS_REPLY_STRING) {
				return FALSE;
			}
		}

		if (reply->element[0]->len != 7 || strncmp(reply->element[0]->str, "message", 7) != 0) {
			return FALSE;
		}

		lua_pushlstring(L, reply->element[1]->str, reply->element[1]->len);
		lua_pushlstring(L, reply->element[2]->str, reply->element[2]->len);
		return TRUE;
	}

	return FALSE;
}

int RedisPoll(lua_State* L) {

	LuaRedis* luaRedis = lua_toredis(L, 1);

	if (!luaRedis->isAlive) {
		luaL_error(L, "Context is disposed");
		return 0;
	}

	if (luaRedis->thread == INVALID_HANDLE_VALUE) {
		InitializeCriticalSectionAndSpinCount(&luaRedis->CriticalSection, 0x00000400);
		luaRedis->thread = (HANDLE)_beginthreadex(NULL, 0, &threadPollFunc, luaRedis, 0, NULL);
	}
	else if (WaitForSingleObject(luaRedis->thread, 0) != WAIT_TIMEOUT) {
		luaRedis->thread = (HANDLE)_beginthreadex(NULL, 0, &threadPollFunc, luaRedis, 0, NULL);
	}

	CleanReply(luaRedis);
	EnterCriticalSection(&luaRedis->CriticalSection);
	luaRedis->reply = luaRedis->pollReply;
	luaRedis->pollReply = NULL;
	LeaveCriticalSection(&luaRedis->CriticalSection);

	if (RedisPushPollReply(L, luaRedis->reply)) {
		return 2;
	}

	return PushReply(L, luaRedis->reply);
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
	else if (!luaRedis->isAlive) {
		luaL_error(L, "Context is disposed");
		return NULL;
	}
	else if (luaRedis->thread != INVALID_HANDLE_VALUE) {
		luaL_error(L, "Cannot run redis commands on context that is polling");
		return NULL;
	}

	CleanReply(luaRedis);

	int top = lua_gettop(L) - idx;

	if (top > 0)
	{
		luaRedis->argv = (char**)gff_calloc(top, sizeof(char*));
		luaRedis->argvlen = (size_t*)gff_calloc(top, sizeof(size_t));

		if (!luaRedis->argv) {
			luaL_error(L, "Out of memory");
			return NULL;
		}

		for (int n = 0; n < top; n++) {

			command = lua_tolstring(L, n + idx + 1, &paramLen);
			luaRedis->argv[n] = (char*)gff_malloc(paramLen);

			if (!luaRedis->argv[n]) {
				luaL_error(L, "Out of memory");
				return NULL;
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
		int len = lua_rawlen(L, -1);
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

		lua_createtable(L, luaRedis->reply->element[1]->elements, 0);
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

	luaL_checkudata(L, -2, REDIS);
	lua_pushvalue(L, -2);
	lua_pushstring(L, "TYPE");
	lua_pushvalue(L, -3);

	LuaRedis* luaRedis = RedisCommandInternal(L);
	lua_pop(L, 3);

	if (luaRedis->reply->str && (strcmp(luaRedis->reply->str, "none") == 0 || strcmp(luaRedis->reply->str, "stream") == 0)) {
		CleanReply(luaRedis);
		size_t len;
		const char* key = luaL_tolstring(L, -1, &len);
		lua_pop(L, 1);
		RedisPushStreamInternal(L, -2, key, len);
	}
	else {
		CleanReply(luaRedis);
		lua_pushnil(L);
	}

	return 1;
}

int RedisGetSortedSet(lua_State* L) {

	luaL_checkudata(L, -2, REDIS);
	lua_pushvalue(L, -2);
	lua_pushstring(L, "TYPE");
	lua_pushvalue(L, -3);

	LuaRedis* luaRedis = RedisCommandInternal(L);
	lua_pop(L, 3);

	if (luaRedis->reply->str && (strcmp(luaRedis->reply->str, "none") == 0 || strcmp(luaRedis->reply->str, "zset") == 0)) {
		CleanReply(luaRedis);
		size_t len;
		const char* key = luaL_tolstring(L, -1, &len);
		lua_pop(L, 1);
		push_redisvalue(L, -2, REDIS_VALUE_TYPE_SORTEDSET, key, len);
	}
	else {
		CleanReply(luaRedis);
		lua_pushnil(L);
	}

	return 1;
}

int RedisGetSet(lua_State* L) {

	luaL_checkudata(L, -2, REDIS);
	lua_pushvalue(L, -2);
	lua_pushstring(L, "TYPE");
	lua_pushvalue(L, -3);

	LuaRedis* luaRedis = RedisCommandInternal(L);
	lua_pop(L, 3);

	if (luaRedis->reply->str && (strcmp(luaRedis->reply->str, "none") == 0 || strcmp(luaRedis->reply->str, "set") == 0)) {
		CleanReply(luaRedis);
		size_t len;
		const char* key = luaL_tolstring(L, -1, &len);
		lua_pop(L, 1);
		push_redisvalue(L, -2, REDIS_VALUE_TYPE_SET, key, len);
	}
	else {
		CleanReply(luaRedis);
		lua_pushnil(L);
	}

	return 1;
}

int RedisGetList(lua_State* L) {

	luaL_checkudata(L, -2, REDIS);
	lua_pushvalue(L, -2);
	lua_pushstring(L, "TYPE");
	lua_pushvalue(L, -3);

	LuaRedis* luaRedis = RedisCommandInternal(L);
	lua_pop(L, 3);

	if (luaRedis->reply->str && (strcmp(luaRedis->reply->str, "none") == 0 || strcmp(luaRedis->reply->str, "list") == 0)) {
		CleanReply(luaRedis);
		size_t len;
		const char* key = luaL_tolstring(L, -1, &len);
		lua_pop(L, 1);
		push_redisvalue(L, -2, REDIS_VALUE_TYPE_LIST, key, len);
	}
	else {
		CleanReply(luaRedis);
		lua_pushnil(L);
	}

	return 1;
}

int RedisGetHashset(lua_State* L) {

	luaL_checkudata(L, -2, REDIS);
	lua_pushvalue(L, -2);
	lua_pushstring(L, "TYPE");
	lua_pushvalue(L, -3);

	LuaRedis* luaRedis = RedisCommandInternal(L);
	lua_pop(L, 3);

	if (luaRedis->reply->str && (strcmp(luaRedis->reply->str, "none") == 0 || strcmp(luaRedis->reply->str, "hash") == 0)) {
		CleanReply(luaRedis);
		size_t len;
		const char* key = luaL_tolstring(L, -1, &len);
		lua_pop(L, 1);
		push_redisvalue(L, -2, REDIS_VALUE_TYPE_HASHSET, key, len);
	}
	else {
		CleanReply(luaRedis);
		lua_pushnil(L);
	}

	return 1;
}

int RedisGetString(lua_State* L) {

	luaL_checkudata(L, -2, REDIS);
	lua_pushvalue(L, -2);
	lua_pushstring(L, "TYPE");
	lua_pushvalue(L, -3);

	LuaRedis* luaRedis = RedisCommandInternal(L);
	lua_pop(L, 3);

	if (luaRedis->reply->str && (strcmp(luaRedis->reply->str, "none") == 0 || strcmp(luaRedis->reply->str, "string") == 0)) {
		CleanReply(luaRedis);
		size_t len;
		const char* key = luaL_tolstring(L, -1, &len);
		lua_pop(L, 1);
		RedisPushStringInternal(L, -2, key, len);
	}
	else {
		CleanReply(luaRedis);
		lua_pushnil(L);
	}

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
	redis->thread = INVALID_HANDLE_VALUE;
	redis->isAlive = true;
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

	LuaRedis* redis = lua_toredis(L, 1);

	CleanReply(redis);

	if (redis->ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, redis->ref);
		redis->ref = LUA_NOREF;
	}

	if (redis->thread != INVALID_HANDLE_VALUE) {

		redis->isAlive = false;

		redisCommand(redis->context, "QUIT");

		WaitForSingleObject(redis->thread, INFINITE);
		CloseHandle(redis->thread);
		DeleteCriticalSection(&redis->CriticalSection);
	}

	if (redis->pollReply) {
		freeReplyObject(redis->pollReply);
	}

	if (redis->context) {
		redisFree(redis->context);
	}

	if (redis->ssl) {
		redisFreeSSLContext(redis->ssl);
	}

	memset(redis, 0, sizeof(LuaRedis));
	redis->thread = INVALID_HANDLE_VALUE;

	return 0;
}

int redis_tostring(lua_State* L) {
	char tim[200];
	sprintf(tim, "Redis: 0x%016X", lua_toredis(L, 1));
	lua_pushstring(L, tim);
	return 1;
}