#include "Redis.h"
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h> 
#include <windows.h> 

#pragma comment(lib, "hiredis/hiredis.lib")

void CleanReply(LuaRedis* luaRedis) {

	if (luaRedis && luaRedis->reply) {
		freeReplyObject(luaRedis->reply);
		luaRedis->reply = NULL;
	}
}

int PushReply(lua_State* L, LuaRedis* luaRedis, redisReply* reply) {

	if (luaRedis) {
		CleanReply(luaRedis);
		luaRedis->reply = reply;
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
			PushReply(L, NULL, reply->element[n]);
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
	LuaRedis* redis = lua_pushredis(L);

	redis->context = redisConnect(host, port);

	return 1;
}

char* escapeBuffer = NULL;

int RedisEscape(lua_State* L) {

	size_t inputLength = 0;
	const char* input = lua_tostring(L, 1, &inputLength);
	size_t outputLength = inputLength * 2 + 1; // Max possible length after escaping
	
	if (escapeBuffer) {
		free(escapeBuffer);
		escapeBuffer = NULL;
	}
	
	char* output = (char*)malloc(outputLength);
	escapeBuffer = output;
	if (output == NULL) {
		return NULL;
	}

	size_t j = 0;
	for (size_t i = 0; i < inputLength; i++) {
		if (input[i] == '\\' || input[i] == '\"') {
			output[j++] = '\\';
		}
		output[j++] = input[i];
	}
	output[j] = '\0';

	lua_pushlstring(L, output, j);

	if (escapeBuffer) {
		free(escapeBuffer);
		escapeBuffer = NULL;
	}

	return 1;
}

int RedisCommand(lua_State* L) {

	LuaRedis* luaRedis = lua_toredis(L, 1);
	const char* command = luaL_checkstring(L, 2);

	if (!luaRedis->context) {
		luaL_error(L, "Redis not connected");
		return 0;
	}

	redisReply* reply = (redisReply*)redisCommand(luaRedis->context, command);

	return PushReply(L, luaRedis, reply);
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

	memset(redis, 0, sizeof(LuaRedis));

	if (escapeBuffer) {
		free(escapeBuffer);
		escapeBuffer = NULL;
	}

	return 0;
}

int redis_tostring(lua_State* L) {
	char tim[100];
	sprintf(tim, "Redis: 0x%08X", lua_toredis(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}