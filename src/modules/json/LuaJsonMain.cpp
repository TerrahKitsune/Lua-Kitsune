#include "json/LuaJson.h"
#include "json/LuaJsonMain.h"

static const struct luaL_Reg jsonFunctions[] = {
	{ "New",                  Json_New                    },  // Json.New([pretty])
	{ "Decode",               Json_Decode                },  // json:Decode(str | fn)
	{ "Encode",               Json_Encode                },  // json:Encode(value)
	{ "EncodeIntoStream",     Json_EncodeIntoStream     },  // json:EncodeIntoStream(stream, value)
	{ "DecodeFromStream",     Json_DecodeFromStream     },  // json:DecodeFromStream(stream)
	{ "Dispose",              Json_Dispose                     },  // json:Dispose()
	{ "SetDecodeNull",        Json_SetDecodeNull        },  // json:SetDecodeNull(bool)
	{ "SetEncodeEmptyObject", Json_SetEncodeEmptyObject },  // json:SetEncodeEmptyObject(bool)
	{ NULL, NULL }
};

static const struct luaL_Reg jsonMeta[] = {
	{ "__gc",       Json_Dispose       },
	{ "__tostring", Json_ToString },
	{ NULL, NULL }
};

int luaopen_json(lua_State* L) {
	luaL_newlibtable(L, jsonFunctions);
	luaL_setfuncs(L, jsonFunctions, 0);

	// Json.Null — the unique lightuserdata sentinel for JSON null values.
	// Lua code compares with:  if value == Json.Null then
	lua_pushlightuserdata(L, LuaJsonNull());
	lua_setfield(L, -2, "Null");

	// Json.EmptyObject — the unique lightuserdata sentinel for empty JSON objects.
	// Only produced during decode when SetEncodeEmptyObject(true) is active.
	// Lua code compares with:  if value == Json.EmptyObject then
	lua_pushlightuserdata(L, LuaJsonEmptyObject());
	lua_setfield(L, -2, "EmptyObject");

	// Instance metatable: __gc, __tostring, and __index = module table so all
	// jsonFunctions are reachable as both Json.Xxx() and json:Xxx().
	luaL_newmetatable(L, LUAJSON);
	luaL_setfuncs(L, jsonMeta, 0);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}