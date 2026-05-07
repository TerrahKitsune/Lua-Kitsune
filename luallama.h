#pragma once
#include "lua_main_incl.h"
#include "LlamaContext.h"

#define LUALLAMA "LUALLAMA"

typedef struct LuaLlama {
	LlamaContext* context;
} LuaLlama;

int lua_llama_new(lua_State* L);
int lua_llama_gc(lua_State* L);
int lua_llama_tostring(lua_State* L);