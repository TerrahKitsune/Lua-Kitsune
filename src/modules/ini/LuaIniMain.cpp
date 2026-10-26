#include "ini/LuaIni.h"
#include "ini/LuaIniMain.h"

static const struct luaL_Reg ini_functions[] = {
    { "New",     Ini_New    },  // Ini.New()
    { "Decode",  Ini_Decode },  // ini:Decode(str) -> table
    { "Encode",  Ini_Encode },  // ini:Encode(table) -> str
    { "Dispose", Ini_Dispose     },  // ini:Dispose()
    { NULL, NULL }
};

static const struct luaL_Reg ini_meta[] = {
    { "__gc",       Ini_Dispose       },
    { "__tostring", Ini_ToString },
    { NULL, NULL }
};

int luaopen_ini(lua_State* L) {
    luaL_newlibtable(L, ini_functions);
    luaL_setfuncs(L, ini_functions, 0);

    // Instance metatable: __gc, __tostring, and __index = module table so all
    // ini_functions are reachable as both Ini.Xxx() and ini:Xxx().
    luaL_newmetatable(L, LUAINI);
    luaL_setfuncs(L, ini_meta, 0);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -3);   // module table
    lua_rawset(L, -3);
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);   // module table
    lua_rawset(L, -3);

    lua_pop(L, 1);   // pop metatable
    return 1;        // return module table
}
