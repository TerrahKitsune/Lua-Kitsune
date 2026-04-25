#pragma once
#include "lua_main_incl.h"

#define LUATASK_META "LuaTask"

struct LuaTask {
    int id; // coroutine id; 0 once this handle has released its refcount share
};

int luaopen_tasks(lua_State* L);
