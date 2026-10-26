#include "sha1/LuaSHA1Main.h"
#include "sha1/LuaSHA1.h"

static const struct luaL_Reg sha1functions[] = {
	{ "New", SHA1_New },
	{ "Update", SHA1_Update },
	{ "Finish", SHA1_Finish },
	{ NULL, NULL }
};

static const luaL_Reg sha1meta[] = {
	{ "__gc", SHA1_GC },
	{ "__tostring", SHA1_ToString },
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