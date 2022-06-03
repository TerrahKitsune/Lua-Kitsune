#include "LuaArchive.h"
#include "LuaArchiveMain.h"

static const struct luaL_Reg archivefunctions[] = {

	{ "OpenRead", OpenReadArchive },
	{ NULL, NULL }
};

static const luaL_Reg archivemeta[] = {
	{ "__gc",  archive_gc },
	{ "__tostring",  archive_tostring },
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