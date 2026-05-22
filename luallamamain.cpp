#include "platform.h"
#ifdef KITSUNE_LLAMA
#include "luallama.h"
#include "luallama_prompt.h"
#include "luatoolsuite.h"
#include "luallamamain.h"

static const struct luaL_Reg llama_functions[] = {
    { "CreateContext",    lua_llama_new           },
    { "CreateToolSuite",  lua_toolsuite_new        },
    { "CreatePrompt",     lua_llama_prompt_new     },
    { "GetLogs",          lua_llama_getlogs        },
    { "PeekModel",        lua_llama_peekmodel      },
    { "SetModel",       lua_llama_setmodel    },
    { "LoadModel",      lua_llama_loadmodel   },
    { "UnloadModel",    lua_llama_unloadmodel },
    { "IsModelLoaded",  lua_llama_ismodelloaded },
    { "IsReady",        lua_llama_isready     },
    { "Generate",       lua_llama_generate    },
    { "Poll",           lua_llama_poll        },
    { "Stop",           lua_llama_stop        },
    { "Reset",          lua_llama_reset       },
    { "Embed",          lua_llama_embed       },
    { "Info",           lua_llama_info        },
    { "Dispose",        lua_llama_dispose     },
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

    lua_pop(L, 1);   // pop LlamaContext metatable

    // ── ToolSuite metatable ────────────────────────────────────────────────────
    static const struct luaL_Reg toolsuite_meta[] = {
        { "__gc",       lua_toolsuite_gc       },
        { "__tostring", lua_toolsuite_tostring },
        { NULL, NULL }
    };
    static const struct luaL_Reg toolsuite_methods[] = {
        { "AddTool",  lua_toolsuite_addtool     },
        { "Call",     lua_toolsuite_call        },
        { "GetJson",  lua_toolsuite_getjson     },
        { "Callback", lua_toolsuite_setcallback },
        { NULL, NULL }
    };

    luaL_newmetatable(L, LUATOOLSUITE);
    luaL_setfuncs(L, toolsuite_meta, 0);
    lua_newtable(L);                            // __index table with methods
    luaL_setfuncs(L, toolsuite_methods, 0);
    lua_pushliteral(L, "__index");
    lua_insert(L, -2);
    lua_rawset(L, -3);                          // meta.__index = methods table
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);                       // module table as __metatable guard
    lua_rawset(L, -3);
    lua_pop(L, 1);   // pop ToolSuite metatable

    // ── LlamaPrompt metatable ──────────────────────────────────────────────────
    lua_llama_prompt_register(L);

    return 1;        // return module table
}

#endif // KITSUNE_LLAMA
