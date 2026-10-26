#include "mutex/LuaMutex.h"
#include "mutex/LuaMutexMain.h"

static const struct luaL_Reg mutexfunctions[] = {
	{ "Open", Mutex_Open },
	{ "Lock", Mutex_Lock },
	{ "Info", Mutex_Info },
	{ "Unlock", Mutex_Unlock },
	{ NULL, NULL }
};

static const luaL_Reg mutexmeta[] = {
	{ "__gc",  Mutex_GC },
	{ "__tostring",  Mutex_ToString },
{ NULL, NULL }
};

int luaopen_mutex(lua_State* L) {

	luaL_newlibtable(L, mutexfunctions);
	luaL_setfuncs(L, mutexfunctions, 0);

	luaL_newmetatable(L, LUAMUTEX);
	luaL_setfuncs(L, mutexmeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}