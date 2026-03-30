#include "luajson.h"
#include "luajsonmain.h"

// Module table: Json.xxx static functions
static const struct luaL_Reg json_static[] = {
{ "New",    lua_json_new            },  // Json.New([pretty])
{ "Create", lua_json_new            },  // backward-compat alias
{ "Decode", lua_json_static_decode  },  // Json.Decode(str)
{ "Encode", lua_json_static_encode  },  // Json.Encode(value [, pretty])
{ NULL, NULL }
};

// Instance methods: json:xxx
static const struct luaL_Reg json_methods[] = {
{ "Decode",  lua_json_decode },
{ "Encode",  lua_json_encode },
{ "Dispose", lua_json_gc     },
{ NULL, NULL }
};

int luaopen_json(lua_State* L) {
// Module table
luaL_newlibtable(L, json_static);
luaL_setfuncs(L, json_static, 0);

// Json.Null — the unique lightuserdata sentinel for JSON null values.
// Decoders push this for every JSON null; encoders output "null" when they see it.
// Lua code can compare with:  if value == Json.Null then
lua_pushlightuserdata(L, lua_json_null());
lua_setfield(L, -2, "Null");

// Instance metatable
luaL_newmetatable(L, LUAJSON);
lua_pushcfunction(L, lua_json_gc);       lua_setfield(L, -2, "__gc");
lua_pushcfunction(L, lua_json_tostring); lua_setfield(L, -2, "__tostring");

// Separate __index table so Json.Decode (static) and json:Decode (instance)
// resolve to different functions.
lua_newtable(L);
luaL_setfuncs(L, json_methods, 0);
lua_setfield(L, -2, "__index");

lua_pushliteral(L, "__metatable");
lua_pushvalue(L, -4);   // module table
lua_rawset(L, -3);

lua_pop(L, 1);   // pop metatable
return 1;        // return module table
}