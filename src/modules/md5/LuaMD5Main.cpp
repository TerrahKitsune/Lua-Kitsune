#include "md5/LuaMD5Main.h"
#include "md5/LuaMD5.h"

static const struct luaL_Reg md5functions[] = {
	{ "New", MD5_New },
	{ "Update", MD5_Update },
	{ "Finish", MD5_Finish },
	{ NULL, NULL }
};

static const luaL_Reg md5meta[] = {
	{ "__gc", MD5_GC },
	{ "__tostring", MD5_ToString },
	{ NULL, NULL }
};

int luaopen_md5(lua_State *L) {

	luaL_newlibtable(L, md5functions);
	luaL_setfuncs(L, md5functions, 0);

	luaL_newmetatable(L, LUAMD5);
	luaL_setfuncs(L, md5meta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}