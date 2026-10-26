#include "toml/LuaToml.h"
#include "toml/LuaTomlMain.h"

static const struct luaL_Reg tomlFunctions[] = {
	{ "New",     Toml_New    },  // Toml.New([pretty])
	{ "Decode",  Toml_Decode },  // toml:Decode(str) -> table, or nil, errmsg
	{ "Encode",  Toml_Encode },  // toml:Encode(table) -> str
	{ "Dispose", Toml_Dispose     },  // toml:Dispose()
	{ NULL, NULL }
};

static const struct luaL_Reg tomlMeta[] = {
	{ "__gc",       Toml_Dispose       },
	{ "__tostring", Toml_ToString },
	{ NULL, NULL }
};

int luaopen_toml(lua_State* L) {
	luaL_newlibtable(L, tomlFunctions);
	luaL_setfuncs(L, tomlFunctions, 0);

	// Instance metatable: __gc, __tostring, and __index = module table so all
	// tomlFunctions are reachable as both Toml.Xxx() and toml:Xxx().
	luaL_newmetatable(L, LUATOML);
	luaL_setfuncs(L, tomlMeta, 0);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);   // module table
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);   // module table
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}
