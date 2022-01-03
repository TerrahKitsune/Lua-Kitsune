#include "luacsv.h"
#include "LuaCsvMain.h"

static const struct luaL_Reg csvfunctions[] = {

	{ "DecodeString", DecodeString },
	{ "DecodeFile", DecodeFile },
	{ "Create", CreateCsv },
	{ NULL, NULL }
};

static const luaL_Reg csvmeta[] = {
	{ "__gc",  csv_gc },
	{ "__tostring",  csv_tostring },
{ NULL, NULL }
};

int luaopen_csv(lua_State* L) {

	luaL_newlibtable(L, csvfunctions);
	luaL_setfuncs(L, csvfunctions, 0);

	luaL_newmetatable(L, LUACSV);
	luaL_setfuncs(L, csvmeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}