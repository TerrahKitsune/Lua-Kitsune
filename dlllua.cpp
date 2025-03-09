#include "dlllua.h"
#include "LuaArchiveMain.h"
#include "mem.h"

lua_State* OpenLuaState(lua_Alloc memoryAllocator) {

	lua_State* L = lua_newstate(memoryAllocator, NULL);
	luaL_openlibs(L);

	luaopen_archive(L);
	lua_setglobal(L, "Archive");

	return L;
}