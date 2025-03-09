#include "dlllua.h"

lua_State* OpenLuaState(lua_Alloc memoryAllocator) {

	lua_State* L = lua_newstate(memoryAllocator, NULL);
	luaL_openlibs(L);
	return L;
}