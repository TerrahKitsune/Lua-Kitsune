#include "csv/LuaCSV.h"
#include "csv/LuaCSVMain.h"

static const struct luaL_Reg csvFunctions[] = {
	{ "New",               CSV_New                   },  // CSV.New([delim])
	{ "Decode",            CSV_Decode               },  // csv:Decode(str) / CSV.Decode(str [, delim])
	{ "Encode",            CSV_Encode                },  // csv:Encode(rows) / CSV.Encode(rows [, delim])
	{ "DecodeFromFunction", CSV_DecodeFromFunction },  // csv:DecodeFromFunction(fn_or_stream) / CSV.DecodeFromFunction(fn_or_stream [, delim])
	{ NULL, NULL }
};

static const struct luaL_Reg csvMeta[] = {
	{ "__gc",       CSV_GC       },
	{ "__tostring", CSV_ToString },
	{ NULL, NULL }
};

int luaopen_csv(lua_State* L) {
	luaL_newlibtable(L, csvFunctions);
	luaL_setfuncs(L, csvFunctions, 0);

	// Instance metatable: __gc, __tostring, and __index = module table so all
	// csvFunctions are reachable as both CSV.Xxx() and csv:Xxx().
	luaL_newmetatable(L, LUACSV);
	luaL_setfuncs(L, csvMeta, 0);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);   // module table
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);   // module table
	lua_rawset(L, -3);

	lua_pop(L, 1);   // pop metatable
	return 1;        // return module table
}
