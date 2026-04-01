#include "luacsv.h"
#include "LuaCsvMain.h"

static const struct luaL_Reg csv_functions[] = {
	{ "New",               lua_csv_new                   },  // CSV.New([delim])
	{ "Create",            lua_csv_new                   },  // backward-compat alias
	{ "Decode",            lua_csv_decode                },  // csv:Decode(str)
	{ "Encode",            lua_csv_encode                },  // csv:Encode(rows)
	{ "DecodeFromFunction", lua_csv_decode_from_function },  // csv:DecodeFromFunction(fn_or_stream)
	{ NULL, NULL }
};

static const struct luaL_Reg csv_meta[] = {
	{ "__gc",       lua_csv_gc       },
	{ "__tostring", lua_csv_tostring },
	{ NULL, NULL }
};

int luaopen_csv(lua_State* L) {
	luaL_newlibtable(L, csv_functions);
	luaL_setfuncs(L, csv_functions, 0);

	// Instance metatable: __gc, __tostring, and __index = module table so all
	// csv_functions are reachable as both CSV.Xxx() and csv:Xxx().
	luaL_newmetatable(L, LUACSV);
	luaL_setfuncs(L, csv_meta, 0);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);   // module table
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);   // module table
	lua_rawset(L, -3);

	lua_pop(L, 1);   // pop metatable
	return 1;        // return module table
}
