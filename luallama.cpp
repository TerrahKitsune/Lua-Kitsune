#include "luallama.h"
#include "LlamaContext.h"

LuaLlama* lua_llama_push(lua_State* L) {
	LuaLlama* x = (LuaLlama*)lua_newuserdata(L, sizeof(LuaLlama));
	memset(x, 0, sizeof(LuaLlama));
	luaL_setmetatable(L, LUALLAMA);
	return x;
}

LuaLlama* lua_llama_check(lua_State* L, int idx) {
	return (LuaLlama*)luaL_checkudata(L, idx, LUALLAMA);
}

int lua_llama_new(lua_State* L) {
	LuaLlama* llama = lua_llama_push(L);

	if (!llama) {
		luaL_error(L, "out of memory");
		return 0;
	}

	llama->context = new LlamaContext();

	if (!llama->context) {
		luaL_error(L, "out of memory");
		return 0;
	}

	return 1;
}

int lua_llama_gc(lua_State* L) {

	LuaLlama* llama = lua_llama_check(L, 1);

	if (llama->context) {
		delete llama->context;
		llama->context = nullptr;
	}

	return 0;
}

int lua_llama_tostring(lua_State* L) {
	lua_pushfstring(L, "Xml: %p", lua_llama_check(L, 1));
	return 1;
}