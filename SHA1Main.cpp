#include "SHA1Main.h"
#include "LuaSHA1.h"

static const struct luaL_Reg sha1functions[] = {
	{ "New", NewSHA1 },
	{ "Update", UpdateSHA1 },
	{ "Finish", FinalSHA1 },
	{ NULL, NULL }
};

static const luaL_Reg sha1meta[] = {
	{ "__gc", sha1_gc },
	{ "__tostring", sha1_tostring },
	{ NULL, NULL }
};

int luaopen_sha1(lua_State* L) {

	luaL_newlibtable(L, sha1functions);
	luaL_setfuncs(L, sha1functions, 0);

	luaL_newmetatable(L, LUASHA1);
	luaL_setfuncs(L, sha1meta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}