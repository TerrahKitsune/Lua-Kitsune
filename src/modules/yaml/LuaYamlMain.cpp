#include "yaml/LuaYaml.h"
#include "yaml/LuaYamlMain.h"

static const struct luaL_Reg yaml_functions[] = {
    { "New",     Yaml_New    },  // Yaml.New([pretty])
    { "Decode",  Yaml_Decode },  // yaml:Decode(str)
    { "Encode",  Yaml_Encode },  // yaml:Encode(value)
    { "Dispose", Yaml_Dispose     },  // yaml:Dispose()
    { NULL, NULL }
};

static const struct luaL_Reg yaml_meta[] = {
    { "__gc",       Yaml_Dispose       },
    { "__tostring", Yaml_ToString },
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
