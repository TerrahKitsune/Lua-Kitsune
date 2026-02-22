#include "LuaMD5.h"
#include <stdlib.h>
#include <string.h>
#include "luawchar.h"
#include "stream.h"

LuaMD5 * lua_tomd5(lua_State *L, int index){

	LuaMD5 * luamd5 = (LuaMD5*)luaL_checkudata(L, index, LUAMD5);
	if (luamd5 == NULL)
		luaL_error(L, "parameter is not a %s", LUAMD5);
	return luamd5;
}

LuaMD5 * lua_pushmd5(lua_State *L){

	LuaMD5 * luamd5 = (LuaMD5*)lua_newuserdata(L, sizeof(LuaMD5));
	if (luamd5 == NULL) {
		luaL_error(L, "Unable to create md5 instance");
		return NULL;
	}

	luaL_getmetatable(L, LUAMD5);
	lua_setmetatable(L, -2);
	memset(luamd5, 0, sizeof(LuaMD5));
	return luamd5;
}

int NewMD5(lua_State *L){

	LuaMD5 * luamd5 = lua_pushmd5(L);
	if (!luamd5) {
		luaL_error(L, "Unable to push md5 instance");
		return NULL;
	}

	MD5Init(&luamd5->MD5);

	return 1;
}

int UpdateMD5(lua_State *L){

	LuaMD5 * luamd5 = lua_tomd5(L, 1);
	if (!luamd5) {
		luaL_error(L, "Unable to get md5 instance");
		return 0;
	}
	else if (luamd5->hash && luamd5->hash[0] != '\0') {
		luaL_error(L, "Cannot update already finished md5 digest");
		return 0;
	}

	int type = lua_type(L, 2);

	if (type == LUA_TUSERDATA && luaL_testudata(L, -1, LUAWCHAR)) {
		LuaWChar* wchar = lua_towchar(L, -1);
		if (wchar && wchar->str) {
			MD5Update(&luamd5->MD5, (unsigned char*)wchar->str, (unsigned int)(wchar->len * sizeof(wchar_t)));
		}
	}
	else if (type == LUA_TUSERDATA && luaL_testudata(L, -1, STREAM)) {
		LuaStream* stream = lua_toluastream(L, -1);
		if (stream && stream->data) {
			MD5Update(&luamd5->MD5, (unsigned char*)stream->data, (unsigned int)stream->len);
		}
	}
	else if (type == LUA_TSTRING) {
		size_t len;
		const char* data = lua_tolstring(L, 2, &len);
		if (data) {
			MD5Update(&luamd5->MD5, (unsigned char*)data, (unsigned int)len);
		}
	}
	else {
		size_t len;
		const char* data = luaL_tolstring(L, 2, &len);
		lua_pop(L, 1);
		if (data) {
			MD5Update(&luamd5->MD5, (unsigned char*)data, (unsigned int)len);
		}		
	}
	
	lua_pop(L, 2);

	return 0;
}

int FinalMD5(lua_State *L){

	LuaMD5 * luamd5 = lua_tomd5(L, 1);
	if (luamd5->hash || luamd5->hash[0] == '\0'){
		MD5Final((unsigned char *)luamd5->hash, &luamd5->MD5);
	}

	char md5string[33];
	for (int i = 0; i < 16; ++i)
		sprintf(&md5string[i * 2], "%02x", (unsigned int)luamd5->hash[i]);

	lua_pop(L, 1);
	lua_pushstring(L,md5string);
	lua_pushlstring(L, (const char *)luamd5->hash, 16);

	return 2;
}

int md5_gc(lua_State *L){

	LuaMD5 * luamd5 = lua_tomd5(L, 1);

	if (!luamd5->hash){
		MD5Final(luamd5->hash, &luamd5->MD5);
	}

	return 0;
}

int md5_tostring(lua_State *L){

	char md5s[100];
	sprintf(md5s, "MD5: %p", (void*)lua_tomd5(L, 1));
	lua_pushfstring(L, md5s);
	return 1;
}