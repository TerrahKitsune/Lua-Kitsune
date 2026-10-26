#include "yaml/LuaYaml.h"
#include "yaml/LuaYamlMain.h"

static const struct luaL_Reg yamlFunctions[] = {
    { "New",     Yaml_New    },  // Yaml.New([pretty])
    { "Decode",  Yaml_Decode },  // yaml:Decode(str)
    { "Encode",  Yaml_Encode },  // yaml:Encode(value)
    { "Dispose", Yaml_Dispose     },  // yaml:Dispose()
    { NULL, NULL }
};

static const struct luaL_Reg yamlMeta[] = {
    { "__gc",       Yaml_Dispose       },
    { "__tostring", Yaml_ToString },
    { NULL, NULL }
};

int luaopen_yaml(lua_State* L) {
    luaL_newlibtable(L, yamlFunctions);
    luaL_setfuncs(L, yamlFunctions, 0);

    // Instance metatable: __gc, __tostring, and __index = module table so all
    // yamlFunctions are reachable as both Yaml.Xxx() and yaml:Xxx().
    luaL_newmetatable(L, LUAYAML);
    luaL_setfuncs(L, yamlMeta, 0);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -3);   // module table
    lua_rawset(L, -3);
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);   // module table
    lua_rawset(L, -3);

    lua_pop(L, 1);   // pop metatable
    return 1;        // return module table
}
