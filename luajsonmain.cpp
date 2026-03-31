#include "luajson.h"
#include "luajsonmain.h"

static const struct luaL_Reg json_functions[] = {
	{ "New",     lua_json_new    },  // Json.New([pretty])
	{ "Create",  lua_json_new    },  // backward-compat alias
	{ "Decode",  lua_json_decode },  // Json.Decode(str)  or  json:Decode(str)
	{ "Encode",  lua_json_encode },  // Json.Encode(value [, pretty])  or  json:Encode(value)
	{ "Dispose", lua_json_gc     },  // json:Dispose()
	{ NULL, NULL }
};

static const struct luaL_Reg json_meta[] = {
	{ "__gc",       lua_json_gc       },
	{ "__tostring", lua_json_tostring },
	{ NULL, NULL }
};

int luaopen_json(lua_State* L) {
	luaL_newlibtable(L, json_functions);
	luaL_setfuncs(L, json_functions, 0);

	// Json.Null — the unique lightuserdata sentinel for JSON null values.
	// Lua code compares with:  if value == Json.Null then
	lua_pushlightuserdata(L, lua_json_null());
	lua_setfield(L, -2, "Null");

	// Instance metatable: __gc, __tostring, and __index = module table so all
	// json_functions are reachable as both Json.Xxx() and json:Xxx().
	luaL_newmetatable(L, LUAJSON);
	luaL_setfuncs(L, json_meta, 0);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);   // module table
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);   // module table
	lua_rawset(L, -3);

	lua_pop(L, 1);   // pop metatable
	return 1;        // return module table
}