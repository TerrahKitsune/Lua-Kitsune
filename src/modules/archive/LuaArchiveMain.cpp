#include "archive/LuaArchive.h"
#include "archive/LuaArchiveMain.h"

static const struct luaL_Reg archivefunctions[] = {

	{ "Read", Archive_Read },
	{ "ReadAll", Archive_ReadAll },
	{ "SetEntry", Archive_SetEntry },
	{ "Entries", Archive_Entries },
	{ "OpenRead", Archive_OpenRead },
	{ NULL, NULL }
};

static const luaL_Reg archivemeta[] = {
	{ "__gc",  Archive_GC },
	{ "__tostring",  Archive_ToString },
	{ NULL, NULL }
};

int luaopen_archive(lua_State* L) {

	luaL_newlibtable(L, archivefunctions);
	luaL_setfuncs(L, archivefunctions, 0);

	luaL_newmetatable(L, ARCHIVE);
	luaL_setfuncs(L, archivemeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}