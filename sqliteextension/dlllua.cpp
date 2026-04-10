#include <windows.h>
#include <clocale>
#include "dlllua.h"


static int dostring(lua_State* L) {

	if (luaL_loadstring(L, luaL_checkstring(L, -1))) {
		lua_error(L);
		return 0;
	}
	else if (lua_pcall(L, 0 , 1, NULL)) {
		lua_error(L);
		return 0;
	}

	return 1;
}

lua_State* OpenLuaState(lua_Alloc memoryAllocator) {

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		return NULL;
	}

	lua_State* L = lua_newstate(memoryAllocator, NULL);
	lua_gc(L, LUA_GCGEN, 20, 100);
	luaL_openlibs(L);
	setlocale(LC_NUMERIC, "C");

	lua_pushcfunction(L, dostring);
	lua_setglobal(L, "dostring");

	return L;
}