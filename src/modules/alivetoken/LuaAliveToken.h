#pragma once
#include "LuaMainInclude.h"
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
void AliveTokenTick(LuaAliveToken* t, lua_State* L);

LuaAliveToken* luaL_checkalivetoken(lua_State* L, int idx);
// Returns 1 if alive, 0 if disposed/timed-out, -1 if not a token.
int AliveTokenIsAlive(lua_State* L, int idx);

int AliveToken_New(lua_State* L);
int AliveToken_IsAlive(lua_State* L);
int AliveToken_Dispose(lua_State* L);
int AliveToken_ErrorIfDead(lua_State* L);
int AliveToken_Link(lua_State* L);
int AliveToken_GC(lua_State* L);
int AliveToken_ToString(lua_State* L);

// Returns the lightuserdata key used to store the app-level AliveToken in the Lua registry.
const void* AliveTokenAppRegistryKey();
// Kills the app-level AliveToken stored in the registry. Call before lua_close.
void AliveTokenAppKill(lua_State* L);

