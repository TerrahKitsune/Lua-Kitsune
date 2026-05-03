#pragma once
#include "../xp_lua_incl.h"
#include "../Lua/lua.h"

/*
** _real declarations — the actual implementations in ldebug.c.
** These are the only symbols that touch L->hook directly.
*/
LUA_API void (lua_sethook_real) (lua_State *L, lua_Hook func, int mask, int count);
LUA_API lua_Hook (lua_gethook_real) (lua_State *L);
LUA_API int (lua_gethookmask_real) (lua_State *L);
LUA_API int (lua_gethookcount_real) (lua_State *L);

/*
** lua_sethook / lua_gethook / lua_gethookmask / lua_gethookcount
** Public Lua API surface — forwards straight to _real for now.
** A future implementation will merge the scheduler hook and the
** debugger hook transparently behind these symbols.
*/
static inline void lua_sethook(lua_State *L, lua_Hook func, int mask, int count) {
    lua_sethook_real(L, func, mask, count);
}
static inline lua_Hook lua_gethook(lua_State *L) {
    return lua_gethook_real(L);
}
static inline int lua_gethookmask(lua_State *L) {
    return lua_gethookmask_real(L);
}
static inline int lua_gethookcount(lua_State *L) {
    return lua_gethookcount_real(L);
}

/*
** kitsune_sethook / kitsune_gethook / kitsune_gethookmask / kitsune_gethookcount
** Internal engine hook API — used by all Kitsune call sites (scheduler, nohook
** helpers, etc.).  Kept separate from the public lua_* surface so that the two
** hook states (scheduler vs. debugger) can be merged here later without touching
** every call site.
*/
static inline void kitsune_sethook(lua_State *L, lua_Hook func, int mask, int count) {
    lua_sethook_real(L, func, mask, count);
}
static inline lua_Hook kitsune_gethook(lua_State *L) {
    return lua_gethook_real(L);
}
static inline int kitsune_gethookmask(lua_State *L) {
    return lua_gethookmask_real(L);
}
static inline int kitsune_gethookcount(lua_State *L) {
    return lua_gethookcount_real(L);
}
