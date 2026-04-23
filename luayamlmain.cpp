#include "luayaml.h"
#include "luayamlmain.h"

static const struct luaL_Reg yaml_functions[] = {
    { "New",     lua_yaml_new    },  // Yaml.New([pretty])
    { "Decode",  lua_yaml_decode },  // yaml:Decode(str)
    { "Encode",  lua_yaml_encode },  // yaml:Encode(value)
    { "Dispose", lua_yaml_gc     },  // yaml:Dispose()
    { NULL, NULL }
};

static const struct luaL_Reg yaml_meta[] = {
    { "__gc",       lua_yaml_gc       },
    { "__tostring", lua_yaml_tostring },
    { NULL, NULL }
};

int luaopen_yaml(lua_State* L) {
    luaL_newlibtable(L, yaml_functions);
    luaL_setfuncs(L, yaml_functions, 0);

    // Instance metatable: __gc, __tostring, and __index = module table so all
    // yaml_functions are reachable as both Yaml.Xxx() and yaml:Xxx().
    luaL_newmetatable(L, LUAYAML);
    luaL_setfuncs(L, yaml_meta, 0);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -3);   // module table
    lua_rawset(L, -3);
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);   // module table
    lua_rawset(L, -3);

    lua_pop(L, 1);   // pop metatable
    return 1;        // return module table
}
