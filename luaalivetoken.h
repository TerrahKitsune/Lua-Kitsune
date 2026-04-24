#pragma once
#include "lua_main_incl.h"
#include <stdint.h>

#define LUAALIVETOKEN "AliveToken"

typedef struct {
	int      alive;      // 1 = alive, 0 = disposed/cancelled
	int64_t  timeoutMs;  // 0 = no timeout; positive = expire after this many ms from createdMs
	int64_t  createdMs;  // steady_clock ms at creation time; only valid when timeoutMs > 0
	int      linkedRef;  // LUA_NOREF or registry ref to a Lua array of parent AliveToken userdata
} LuaAliveToken;

// Central liveness check: updates alive (timeout + linked parents) then returns alive.
// Pass L to enable parent-chain walking. All internal C++ callsites should pass L.
void alivetoken_tick(LuaAliveToken* t, lua_State* L);

LuaAliveToken* lua_alivetoken_check(lua_State* L, int idx);
// Returns 1 if alive, 0 if disposed/timed-out, -1 if not a token.
int lua_alivetoken_isalive(lua_State* L, int idx);

int lua_alivetoken_new(lua_State* L);
int lua_alivetoken_isalive_method(lua_State* L);
int lua_alivetoken_dispose(lua_State* L);
int lua_alivetoken_error_if_dead(lua_State* L);
int lua_alivetoken_link(lua_State* L);
int lua_alivetoken_gc(lua_State* L);
int lua_alivetoken_tostring(lua_State* L);

int luaopen_alivetoken(lua_State* L);

