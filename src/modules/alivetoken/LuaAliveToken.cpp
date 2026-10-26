#include "alivetoken/LuaAliveToken.h"
#include <chrono>

static const int g_app_token_key = 0;
const void* AliveTokenAppRegistryKey() { return &g_app_token_key; }

void AliveTokenAppKill(lua_State* L) {
    lua_rawgetp(L, LUA_REGISTRYINDEX, AliveTokenAppRegistryKey());
    LuaAliveToken* t = (LuaAliveToken*)luaL_testudata(L, -1, LUAALIVETOKEN);
    if (t)
        t->alive = 0;
    lua_pop(L, 1);
}

static int64_t AliveNowMs() {
    return (int64_t)std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

void AliveTokenTick(LuaAliveToken* t, lua_State* L) {
    if (!t->alive)
        return;

    // Timeout check
    if (t->timeoutMs > 0 && AliveNowMs() - t->createdMs >= t->timeoutMs) {
        t->alive = 0;
        return;
    }

    // Linked parent check — if any parent is dead, self dies
    if (L && t->linkedRef != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, t->linkedRef);
        int n = (int)lua_rawlen(L, -1);
        for (int i = 1; i <= n && t->alive; i++) {
            lua_rawgeti(L, -1, i);
            LuaAliveToken* parent = (LuaAliveToken*)luaL_testudata(L, -1, LUAALIVETOKEN);
            if (parent) {
                AliveTokenTick(parent, L);
                if (!parent->alive)
                    t->alive = 0;
            }
            lua_pop(L, 1);
        }
        lua_pop(L, 1); // pop linked table
    }
}

LuaAliveToken* luaL_checkalivetoken(lua_State* L, int idx) {
    return (LuaAliveToken*)luaL_checkudata(L, idx, LUAALIVETOKEN);
}

int AliveTokenIsAlive(lua_State* L, int idx) {
    LuaAliveToken* t = (LuaAliveToken*)luaL_testudata(L, idx, LUAALIVETOKEN);
    if (!t)
        return -1;
    AliveTokenTick(t, L);
    return t->alive;
}

int AliveToken_New(lua_State* L) {
    // Scan arg 1 and arg 2 for an integer timeout.
    // The wchar module pattern may pass the module table as arg 1.
    int64_t timeoutMs = 0;
    for (int i = 1; i <= 2; i++) {
        if (lua_type(L, i) == LUA_TNUMBER) {
            timeoutMs = (int64_t)lua_tointeger(L, i);
            if (timeoutMs < 0)
                timeoutMs = 0;
            break;
        }
    }

    LuaAliveToken* t = (LuaAliveToken*)lua_newuserdata(L, sizeof(LuaAliveToken));
    t->alive     = 1;
    t->timeoutMs = timeoutMs;
    t->createdMs = (timeoutMs > 0) ? AliveNowMs() : 0;
    t->linkedRef = LUA_NOREF;
    luaL_setmetatable(L, LUAALIVETOKEN);
    return 1;
}

int AliveToken_IsAlive(lua_State* L) {
    LuaAliveToken* t = luaL_checkalivetoken(L, 1);
    AliveTokenTick(t, L);
    lua_pushboolean(L, t->alive);
    return 1;
}

int AliveToken_Dispose(lua_State* L) {
    LuaAliveToken* t = luaL_checkalivetoken(L, 1);
    t->alive = 0;
    return 0;
}

int AliveToken_ErrorIfDead(lua_State* L) {
    LuaAliveToken* t = luaL_checkalivetoken(L, 1);
    AliveTokenTick(t, L);
    if (!t->alive) {
        const char* msg = luaL_optstring(L, 2, "Cancellation token was cancelled");
        return luaL_error(L, "%s", msg);
    }
    return 0;
}

// token:Link(parent1, parent2, ...)
// Appends one or more parent tokens to this token's linked list.
// The token dies when any linked parent dies. Can be called multiple
// times to add more parents incrementally.
int AliveToken_Link(lua_State* L) {
    LuaAliveToken* t = luaL_checkalivetoken(L, 1);
    int nargs = lua_gettop(L);
    if (nargs < 2)
        return 0;

    // Create linked table on first use
    if (t->linkedRef == LUA_NOREF) {
        lua_newtable(L);
        t->linkedRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }

    lua_rawgeti(L, LUA_REGISTRYINDEX, t->linkedRef);
    int n = (int)lua_rawlen(L, -1);

    for (int i = 2; i <= nargs; i++) {
        luaL_checkudata(L, i, LUAALIVETOKEN);
        lua_pushvalue(L, i);
        lua_rawseti(L, -2, ++n);
    }

    lua_pop(L, 1); // pop linked table
    return 0;
}

int AliveToken_GC(lua_State* L) {
    LuaAliveToken* t = luaL_checkalivetoken(L, 1);
    t->alive = 0;
    if (t->linkedRef != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, t->linkedRef);
        t->linkedRef = LUA_NOREF;
    }
    return 0;
}

int AliveToken_ToString(lua_State* L) {
    LuaAliveToken* t = luaL_checkalivetoken(L, 1);
    AliveTokenTick(t, L);
    if (t->timeoutMs > 0 && t->alive) {
        int64_t remaining = t->timeoutMs - (AliveNowMs() - t->createdMs);
        if (remaining < 0)
            remaining = 0;
        lua_pushfstring(L, "AliveToken(alive, %d ms remaining)", (int)remaining);
    }
    else {
        lua_pushfstring(L, "AliveToken(%s)", t->alive ? "alive" : "disposed");
    }
    return 1;
}
