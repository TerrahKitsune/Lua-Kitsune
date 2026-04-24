#pragma once
#include "lua_main_incl.h"

#define LUAALIVETOKEN "AliveToken"

typedef struct {
    int alive;   // 1 = alive, 0 = disposed/cancelled
} LuaAliveToken;

LuaAliveToken* lua_alivetoken_check(lua_State* L, int idx);
// Returns 1 if the token at idx is alive, 0 if disposed, -1 if not a token.
int lua_alivetoken_isalive(lua_State* L, int idx);

int lua_alivetoken_new(lua_State* L);
int lua_alivetoken_isalive_method(lua_State* L);
int lua_alivetoken_dispose(lua_State* L);
int lua_alivetoken_error_if_dead(lua_State* L);
int lua_alivetoken_gc(lua_State* L);
int lua_alivetoken_tostring(lua_State* L);

int luaopen_alivetoken(lua_State* L);
