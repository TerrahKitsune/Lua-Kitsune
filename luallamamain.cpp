#include "luallama.h"
#include "luallamamain.h"

static const struct luaL_Reg llama_functions[] = {
    { "CreateContext", lua_llama_new },
    { NULL, NULL }
};

static const struct luaL_Reg llama_meta[] = {
    { "__gc",       lua_llama_gc       },
    { "__tostring", lua_llama_tostring },
    { NULL, NULL }
};

int luaopen_llama(lua_State* L) {

    luaL_newlibtable(L, llama_functions);
    luaL_setfuncs(L, llama_functions, 0);

    luaL_newmetatable(L, LUALLAMA);
    luaL_setfuncs(L, llama_meta, 0);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -3);   // module table
    lua_rawset(L, -3);
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);   // module table
    lua_rawset(L, -3);

    lua_pop(L, 1);   // pop metatable
    return 1;        // return module table
}
