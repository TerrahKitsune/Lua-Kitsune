#include "luaxml.h"
#include "luaxmlmain.h"

static const struct luaL_Reg xml_functions[] = {
    { "New",     lua_xml_new    },  // Xml.New([indent])
    { "Create",  lua_xml_new    },  // backward-compat alias
    { "Decode",  lua_xml_decode },  // xml:Decode(string)
    { "Encode",  lua_xml_encode },  // xml:Encode(table)
    { "Dispose", lua_xml_gc     },  // xml:Dispose()
    { NULL, NULL }
};

static const struct luaL_Reg xml_meta[] = {
    { "__gc",       lua_xml_gc       },
    { "__tostring", lua_xml_tostring },
    { NULL, NULL }
};

int luaopen_xml(lua_State* L) {
    ensure_allocator();
    luaL_newlibtable(L, xml_functions);
    luaL_setfuncs(L, xml_functions, 0);

    // Instance metatable: __gc, __tostring, and __index = module table so all
    // xml_functions are reachable as both Xml.Xxx() and xml:Xxx().
    luaL_newmetatable(L, LUAXML);
    luaL_setfuncs(L, xml_meta, 0);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -3);   // module table
    lua_rawset(L, -3);
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);   // module table
    lua_rawset(L, -3);

    lua_pop(L, 1);   // pop metatable
    return 1;        // return module table
}
