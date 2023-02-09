#include "vhd.h"
#include "VhdMain.h"

static const struct luaL_Reg vhdfunctions[] = {

	{ "Create", VhdCreate },
	{ NULL, NULL }
};

static const luaL_Reg vhdmeta[] = {
	{ "__gc",  vhd_gc },
	{ "__tostring",  vhd_tostring },
{ NULL, NULL }
};

int luaopen_vhd(lua_State* L) {

	luaL_newlibtable(L, vhdfunctions);
	luaL_setfuncs(L, vhdfunctions, 0);

	luaL_newmetatable(L, VHD);
	luaL_setfuncs(L, vhdmeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}