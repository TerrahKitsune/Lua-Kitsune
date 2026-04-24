#include "luaalivetoken.h"
LuaAliveToken* lua_alivetoken_check(lua_State* L, int idx) {
    return (LuaAliveToken*)luaL_checkudata(L, idx, LUAALIVETOKEN);
}

int lua_alivetoken_isalive(lua_State* L, int idx) {
    LuaAliveToken* t = (LuaAliveToken*)luaL_testudata(L, idx, LUAALIVETOKEN);
    if (!t)
        return -1;
    return t->alive;
}

int lua_alivetoken_new(lua_State* L) {
    LuaAliveToken* t = (LuaAliveToken*)lua_newuserdata(L, sizeof(LuaAliveToken));
    t->alive = 1;
    luaL_setmetatable(L, LUAALIVETOKEN);
    return 1;
}

int lua_alivetoken_isalive_method(lua_State* L) {
    LuaAliveToken* t = lua_alivetoken_check(L, 1);
    lua_pushboolean(L, t->alive);
    return 1;
}

int lua_alivetoken_dispose(lua_State* L) {
    LuaAliveToken* t = lua_alivetoken_check(L, 1);
    t->alive = 0;
    return 0;
}

int lua_alivetoken_error_if_dead(lua_State* L) {
    LuaAliveToken* t = lua_alivetoken_check(L, 1);
    if (!t->alive) {
        const char* msg = luaL_optstring(L, 2, "Cancellation token was cancelled");
        return luaL_error(L, "%s", msg);
    }
    return 0;
}

int lua_alivetoken_gc(lua_State* L) {
    LuaAliveToken* t = lua_alivetoken_check(L, 1);
    t->alive = 0;
    return 0;
}

int lua_alivetoken_tostring(lua_State* L) {
    LuaAliveToken* t = lua_alivetoken_check(L, 1);
    lua_pushfstring(L, "AliveToken(%s)", t->alive ? "alive" : "disposed");
    return 1;
}
