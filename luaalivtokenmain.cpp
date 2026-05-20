#include "luaalivetoken.h"

static const struct luaL_Reg alivetoken_functions[] = {
    { "New",         lua_alivetoken_new           },
    { "IsAlive",     lua_alivetoken_isalive_method },
    { "Dispose",     lua_alivetoken_dispose       },
    { "ErrorIfDead", lua_alivetoken_error_if_dead  },
    { "Link",        lua_alivetoken_link          },
    { NULL, NULL }
};

static const struct luaL_Reg alivetoken_meta[] = {
    { "__gc",       lua_alivetoken_gc       },
    { "__tostring", lua_alivetoken_tostring },
    { NULL, NULL }
};

int luaopen_alivetoken(lua_State* L) {
    luaL_newlibtable(L, alivetoken_functions);
    luaL_setfuncs(L, alivetoken_functions, 0);

    luaL_newmetatable(L, LUAALIVETOKEN);
    luaL_setfuncs(L, alivetoken_meta, 0);

    // __index = module table so all functions are reachable as both
    // AliveToken.Xxx() and token:Xxx() — the standard wchar pattern.
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);

    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);

    lua_pop(L, 1);   // pop metatable — module table is now on top

    // Create the app-level token, store it in the registry and expose it as AliveToken.App.
    lua_alivetoken_new(L);   // pushes a new token onto the stack
    lua_pushvalue(L, -1);    // dup for registry
    lua_rawsetp(L, LUA_REGISTRYINDEX, lua_alivetoken_app_registry_key());
    lua_pushliteral(L, "App");
    lua_insert(L, -2);       // swap: key below value
    lua_rawset(L, -3);       // module["App"] = token

    return 1;        // return module table
}
