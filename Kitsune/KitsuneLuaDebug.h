#pragma once
#include "../xp_lua_incl.h"
#include "../Lua/lua.h"
#include <mutex>
#include <unordered_map>

// Forward declaration only. Full definition is in kitsune_internal.h.
// This header must NOT include kitsune_internal.h to avoid a circular
// dependency:  kitsune_internal.h -> lua_main_incl.h -> KitsuneLuaDebug.h
struct KitsuneState;

// ---------------------------------------------------------------------------
// Hook slot structs (defined here; kitsune_internal.h uses them via this header
// being included through lua_main_incl.h -> KitsuneLuaDebug.h chain)
// ---------------------------------------------------------------------------
struct KitsuneHookSlot {
	lua_Hook func = nullptr;
	int      mask = 0;
	int      count = 0;
};

struct KitsuneHookState {
	KitsuneHookSlot kitsune;
	KitsuneHookSlot external;
};

// ---------------------------------------------------------------------------
// _real declarations — the actual ldebug.c implementations.
// Internal Lua source files call these; nothing else should.
// ---------------------------------------------------------------------------
LUA_API void     (lua_sethook_real)(lua_State* L, lua_Hook func, int mask, int count);
LUA_API lua_Hook(lua_gethook_real)     (lua_State* L);
LUA_API int      (lua_gethookmask_real)(lua_State* L);
LUA_API int      (lua_gethookcount_real)(lua_State* L);

// ---------------------------------------------------------------------------
// Module state — set once by InitLuaDebug; valid for engine lifetime.
// ---------------------------------------------------------------------------
extern KitsuneState* g_debugState;

// Call once, immediately after KitsuneState is constructed in KitsuneInit.
void InitLuaDebug(KitsuneState* state);

// Remove a lua_State* from the hook registry before it is closed/freed.
void kitsune_hook_remove_state(lua_State* L);

// Propagate the external (debugger) hook slot from src to dst.
// Call after lua_newthread so new coroutines inherit the debugger hook.
void kitsune_inherit_external_hook(lua_State* src, lua_State* dst);

// ---------------------------------------------------------------------------
// Dual-slot hook registry
//
//   kitsune slot  — owned by the scheduler (Ticker, nohook save/restore)
//   external slot — owned by outside callers (e.g. lua-debug DAP adapter)
//
// A single merged_hook dispatcher is passed to lua_sethook_real and fires
// each slot whose own mask covers the current event.
//
//   Merged mask  = kitsune_mask | external_mask
//   Merged count = min(non-zero counts)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// kitsune_* — internal engine hook API (scheduler-owned slot)
// ---------------------------------------------------------------------------
void kitsune_sethook(lua_State* L, lua_Hook func, int mask, int count);
lua_Hook kitsune_gethook(lua_State* L);
int kitsune_gethookmask(lua_State* L);
int kitsune_gethookcount(lua_State* L);

// ---------------------------------------------------------------------------
// lua_sethook / lua_gethook* — public Lua API surface (external/debugger slot)
// ---------------------------------------------------------------------------
extern "C" {
	LUA_API void     lua_sethook(lua_State* L, lua_Hook func, int mask, int count);
	LUA_API lua_Hook lua_gethook(lua_State* L);
	LUA_API int      lua_gethookmask(lua_State* L);
	LUA_API int      lua_gethookcount(lua_State* L);
}