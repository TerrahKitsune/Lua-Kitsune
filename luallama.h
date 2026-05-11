#pragma once
#include "lua_main_incl.h"
#ifdef KITSUNE_LLAMA
#include "LlamaContext.h"

#define LUALLAMA "LUALLAMA"

typedef struct LuaLlama {
	LlamaContext* context;
	int           messages_ref; // LUA_NOREF when unused
} LuaLlama;

LuaLlama* lua_llama_push(lua_State* L);
LuaLlama* lua_llama_check(lua_State* L, int idx);

int lua_llama_new(lua_State* L);
int lua_llama_gc(lua_State* L);
int lua_llama_tostring(lua_State* L);
int lua_llama_setmodel(lua_State* L);
int lua_llama_loadmodel(lua_State* L);
int lua_llama_unloadmodel(lua_State* L);
int lua_llama_ismodelloaded(lua_State* L);
int lua_llama_isready(lua_State* L);
int lua_llama_generate(lua_State* L);
int lua_llama_poll(lua_State* L);
int lua_llama_stop(lua_State* L);
int lua_llama_reset(lua_State* L);
int lua_llama_embed(lua_State* L);
int lua_llama_info(lua_State* L);
int lua_llama_dispose(lua_State* L);
int lua_llama_getlogs(lua_State* L);
int lua_llama_peekmodel(lua_State* L);

#endif // KITSUNE_LLAMA
