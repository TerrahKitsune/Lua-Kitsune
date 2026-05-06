#include "platform.h"
#ifdef KITSUNE_LLAMA

#include "LuaLlama.h"
#include "LuaLlamaMain.h"

// ── Llama module table (Llama.*) ──────────────────────────────────────────────

static const luaL_Reg llama_functions[] = {
    { "CreateContext",  LlamaCreateContext  },
    { "GetLogs",        LlamaGetLogs        },
    { NULL, NULL }
};

// ── LuaLlamaContext method table ──────────────────────────────────────────────

static const luaL_Reg llamactx_functions[] = {
    { "SetModel",      LlamaSetModel      },
    { "LoadModel",     LlamaLoadModel     },
    { "UnloadModel",   LlamaUnloadModel   },
    { "IsModelLoaded", LlamaIsModelLoaded },
    { "IsReady",       LlamaIsReady       },
    { "Info",          LlamaInfo          },
    { "Generate",      LlamaGenerate      },
    { "Embed",         LlamaEmbed         },
    { "Poll",          LlamaPoll          },
    { "Stop",          LlamaStop          },
    { "Reset",         LlamaReset         },
    { "Dispose",       LlamaDispose       },
    { NULL, NULL }
};

static const luaL_Reg llamactx_meta[] = {
    { "__gc",       llama_ctx_gc       },
    { "__tostring", llama_ctx_tostring },
    { NULL, NULL }
};

// ── luaopen_llama ─────────────────────────────────────────────────────────────

int luaopen_llama(lua_State* L) {
    // Build the Llama module table.
    luaL_newlibtable(L, llama_functions);
    luaL_setfuncs(L, llama_functions, 0);

    // Register the LuaLlamaContext metatable.
    luaL_newmetatable(L, LUALLAMA_CTX);
    luaL_setfuncs(L, llamactx_meta, 0);

    // Build the method table and set as __index so ctx:Method() works.
    luaL_newlibtable(L, llamactx_functions);
    luaL_setfuncs(L, llamactx_functions, 0);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -2);
    lua_rawset(L, -4); // metatable.__index = method table

    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -2);
    lua_rawset(L, -4); // metatable.__metatable = method table (hides metatable)

    lua_pop(L, 2); // pop method table + metatable

    return 1; // return the Llama module table
}

#endif // KITSUNE_LLAMA
