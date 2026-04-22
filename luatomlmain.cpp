#include "luatoml.h"
#include "luatomlmain.h"

static const struct luaL_Reg toml_functions[] = {
    { "New",     lua_toml_new    },  // Toml.New([pretty])
    { "Create",  lua_toml_new    },  // backward-compat alias
    { "Decode",  lua_toml_decode },  // toml:Decode(str) -> table, or nil, errmsg
    { "Encode",  lua_toml_encode },  // toml:Encode(table) -> str
    { "Dispose", lua_toml_gc     },  // toml:Dispose()
    { NULL, NULL }
};

static const struct luaL_Reg toml_meta[] = {
    { "__gc",       lua_toml_gc       },
    { "__tostring", lua_toml_tostring },
    { NULL, NULL }
};

int luaopen_toml(lua_State* L) {
    luaL_newlibtable(L, toml_functions);
    luaL_setfuncs(L, toml_functions, 0);

    // Instance metatable: __gc, __tostring, and __index = module table so all
    // toml_functions are reachable as both Toml.Xxx() and toml:Xxx().
    luaL_newmetatable(L, LUATOML);
    luaL_setfuncs(L, toml_meta, 0);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -3);   // module table
    lua_rawset(L, -3);
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);   // module table
    lua_rawset(L, -3);

    lua_pop(L, 1);   // pop metatable
    return 1;        // return module table
}
