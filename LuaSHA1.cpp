#include "LuaSHA1.h"
#include <stdlib.h>
#include <string.h>
#include "luawchar.h"
#include "stream.h"

int NewSHA1(lua_State* L) {

	LuaSHA1* luasha1 = lua_pushsha1(L);
	if (!luasha1) {
		luaL_error(L, "Unable to push sha1 instance");
		return NULL;
	}

	SHA1Init(&luasha1->ctx);

	return 1;
}

int UpdateSHA1(lua_State* L) {

	LuaSHA1* luasha1 = lua_tosha1(L, 1);
	if (!luasha1) {
		luaL_error(L, "Unable to push sha1 instance");
		return NULL;
	}
	else if (luasha1->finished) {
		luaL_error(L, "Cannot update already finished sha1 digest");
		return 0;
	}

	int type = lua_type(L, 2);

	if (type == LUA_TUSERDATA && luaL_testudata(L, -1, LUAWCHAR)) {
		LuaWChar* wchar = lua_towchar(L, -1);
		if (wchar && wchar->str) {
			SHA1Update(&luasha1->ctx, (unsigned char*)wchar->str, wchar->len * sizeof(wchar_t));
		}
	}
	else if (type == LUA_TUSERDATA && luaL_testudata(L, -1, STREAM)) {
		LuaStream* stream = lua_toluastream(L, -1);
		if (stream && stream->data) {
			SHA1Update(&luasha1->ctx, (unsigned char*)stream->data, stream->len);
		}
	}
	else if (type == LUA_TSTRING) {
		size_t len;
		const char* data = lua_tolstring(L, 2, &len);
		if (data) {
			SHA1Update(&luasha1->ctx, (unsigned char*)data, len);
		}
	}
	else {
		size_t len;
		const char* data = luaL_tolstring(L, 2, &len);
		lua_pop(L, 1);
		if (data) {
			SHA1Update(&luasha1->ctx, (unsigned char*)data, len);
		}
	}

	lua_pop(L, 2);

	return 0;
}


int FinalSHA1(lua_State* L) {

	LuaSHA1* luasha1 = lua_tosha1(L, 1);
	if (!luasha1) {
		luaL_error(L, "Unable to push sha1 instance");
		return NULL;
	}
	else if (!luasha1->finished) {
		SHA1Final(luasha1->hash, &luasha1->ctx);
		luasha1->finished = 1;
	}

	char sha1string[41];
	for (int i = 0; i < 20; ++i) {
		sprintf((sha1string + (2 * i)), "%02x", luasha1->hash[i] & 0xff);
	}

	lua_pop(L, 1);
	lua_pushstring(L, sha1string);
	lua_pushlstring(L, (const char*)luasha1->hash, 20);

	return 2;
}

LuaSHA1* lua_tosha1(lua_State* L, int index) {

	LuaSHA1* luasha1 = (LuaSHA1*)luaL_checkudata(L, index, LUASHA1);
	if (luasha1 == NULL) {
		luaL_error(L, "parameter is not a %s", LUASHA1);
		return NULL;
	}
	return luasha1;
}

LuaSHA1* lua_pushsha1(lua_State* L) {

	LuaSHA1* luasha1 = (LuaSHA1*)lua_newuserdata(L, sizeof(LuaSHA1));
	if (luasha1 == NULL) {
		luaL_error(L, "Unable to create sha1 instance");
		return NULL;
	}

	luaL_getmetatable(L, LUASHA1);
	lua_setmetatable(L, -2);
	memset(luasha1, 0, sizeof(LuaSHA1));
	return luasha1;
}

int sha1_gc(lua_State* L) {

	LuaSHA1* luasha1 = lua_tosha1(L, 1);

	return 0;
}

int sha1_tostring(lua_State* L) {

	char sha1[100];
	sprintf(sha1, "SHA1: 0x%016X", lua_tosha1(L, 1));
	lua_pushfstring(L, sha1);
	return 1;
}