#include "aes/LuaAes.h"
#include "aes/LuaAesMain.h"

static const struct luaL_Reg luaaesfunctions[] = {
	{ "Encrypt", Aes_Encrypt },
	{ "Decrypt", Aes_Decrypt },
	{ "New",     Aes_New },
	{ "SetIV",  Aes_SetIV },
	{ NULL, NULL }
};

static const luaL_Reg luaaesmeta[] = {
	{ "__gc",  Aes_GC },
	{ "__tostring",  Aes_ToString },
{ NULL, NULL }
};

int luaopen_aes(lua_State *L) {

	luaL_newlibtable(L, luaaesfunctions);
	luaL_setfuncs(L, luaaesfunctions, 0);

	luaL_newmetatable(L, LUAES);
	luaL_setfuncs(L, luaaesmeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}