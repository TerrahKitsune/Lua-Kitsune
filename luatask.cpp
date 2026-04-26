#include "luatask.h"
#include "kitsune_internal.h"
#include "mem.h"
#include <cstdio>
#include <cstring>

static LuaTask* lua_pushtask(lua_State* L) {
    LuaTask* t = (LuaTask*)lua_newuserdata(L, sizeof(LuaTask));
    t->id = 0;
    luaL_setmetatable(L, LUATASK_META);
    return t;
}

static LuaTask* lua_totask(lua_State* L, int index) {
    return (LuaTask*)luaL_checkudata(L, index, LUATASK_META);
}

static int task_gc(lua_State* L) {
    LuaTask* task = lua_totask(L, 1);
    if (task->id == 0)
        return 0;
    if (!g_state) {
        task->id = 0;
        return 0;
    }
    g_state->slotsLock.lock();
    KitsuneCoroutine* slot = FindSlot(g_state, task->id);
    task->id = 0;
    if (slot && !slot->apiOwned) {
        if (--slot->luaRefCount <= 0) {
            long st = slot->state.load();
            if (st == KITSUNE_COROUTINE_STATE_DONE || st == KITSUNE_COROUTINE_STATE_RELEASED)
                slot->state.store(KITSUNE_COROUTINE_STATE_RELEASED);
            else if (st == KITSUNE_COROUTINE_STATE_PAUSED) {
                slot->state.store(KITSUNE_COROUTINE_STATE_ABORTED); // paused and abandoned: cancel so it doesn't hang
            }
            else
                slot->fireAndForget.store(1);
        }
    }
    g_state->slotsLock.unlock();
    return 0;
}

static int task_tostring(lua_State* L) {
    LuaTask* task = lua_totask(L, 1);
    if (task->id == 0) {
        lua_pushliteral(L, "Task(released)");
        return 1;
    }
    g_state->slotsLock.lock();
    KitsuneCoroutine* slot = FindSlot(g_state, task->id);
    int status = GetSlotStatus(g_state, slot);
    g_state->slotsLock.unlock();
    char buf[64];
    snprintf(buf, sizeof(buf), "Task(id=%d, status=%d)", task->id, status);
    lua_pushstring(L, buf);
    return 1;
}

static int task_new(lua_State* L) {
    if (!lua_isfunction(L, 1))
        return luaL_error(L, "Tasks.New: first argument must be a function");
    int n = lua_gettop(L); // fn + args
    KitsuneCoroutine* slot = AcquireSlot(g_state, false, false);
    if (!slot)
        return luaL_error(L, "Tasks.New: no available coroutine slots");
    slot->threadRef = LUA_NOREF;
    slot->argsRef = LUA_NOREF;
    slot->apiOwned = false;
    slot->luaRefCount = 1;
    slot->initialNArgs = n - 1; // fn is on the stack but not counted as an arg for lua_resume
    lua_State* T = CreateCoroutineThread(g_state, slot);
    lua_xmove(L, T, n);
    int id = slot->id;
    LaunchTaskSlot(g_state);
    LuaTask* task = lua_pushtask(L);
    task->id = id;
    return 1;
}

static int task_resume(lua_State* L) {
    LuaTask* task = lua_totask(L, 1);
    lua_pushboolean(L, task->id != 0 && KitsuneResume(task->id) ? 1 : 0);
    return 1;
}

static int task_open(lua_State* L) {
    int id = (int)luaL_checkinteger(L, 1);
    if (!g_state || id <= 0) {
        lua_pushnil(L);
        return 1;
    }
    g_state->slotsLock.lock();
    KitsuneCoroutine* slot = FindSlot(g_state, id);
    if (!slot || slot->state.load() == KITSUNE_COROUTINE_STATE_RELEASED) {
        g_state->slotsLock.unlock();
        lua_pushnil(L);
        return 1;
    }
    slot->luaRefCount++;
    slot->fireAndForget.store(0);
    g_state->slotsLock.unlock();
    LuaTask* task = lua_pushtask(L);
    task->id = id;
    return 1;
}

static int task_getstatus(lua_State* L) {
    LuaTask* task = lua_totask(L, 1);
    if (task->id == 0 || !g_state) {
        lua_pushinteger(L, KITSUNE_STATUS_NONE);
        return 1;
    }
    g_state->slotsLock.lock();
    KitsuneCoroutine* slot = FindSlot(g_state, task->id);
    int status = slot ? GetSlotStatus(g_state, slot) : KITSUNE_STATUS_NONE;
    g_state->slotsLock.unlock();
    lua_pushinteger(L, status);
    return 1;
}

static int task_setname(lua_State* L) {
    LuaTask* task = lua_totask(L, 1);
    if (task->id == 0 || !g_state) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const char* name = luaL_checkstring(L, 2);
    g_state->slotsLock.lock();
    KitsuneCoroutine* slot = FindSlot(g_state, task->id);
    if (!slot) {
        g_state->slotsLock.unlock();
        lua_pushboolean(L, 0);
        return 1;
    }
    for (int i = 0; i < g_state->slotCount; i++) {
        KitsuneCoroutine* s = g_state->slots[i];
        if (s != slot && s->name && strcmp(s->name, name) == 0) {
            g_state->slotsLock.unlock();
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    kitsune_free(slot->name);
    slot->name = NULL;
    size_t len = strlen(name);
    slot->name = (char*)kitsune_malloc(len + 1);
    if (slot->name)
        memcpy(slot->name, name, len + 1);
    g_state->slotsLock.unlock();
    lua_pushboolean(L, 1);
    return 1;
}

static int task_getname(lua_State* L) {
    LuaTask* task = lua_totask(L, 1);
    if (task->id == 0 || !g_state) {
        lua_pushnil(L);
        return 1;
    }
    g_state->slotsLock.lock();
    KitsuneCoroutine* slot = FindSlot(g_state, task->id);
    if (!slot || !slot->name) {
        g_state->slotsLock.unlock();
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, slot->name);
    g_state->slotsLock.unlock();
    return 1;
}

static int task_getallids(lua_State* L) {
    lua_newtable(L);
    if (!g_state)
        return 1;
    g_state->slotsLock.lock();
    int n = 0;
    for (int i = 0; i < g_state->slotCount; i++) {
        KitsuneCoroutine* slot = g_state->slots[i];
        if (slot->id != 0 && slot->state.load() != KITSUNE_COROUTINE_STATE_RELEASED) {
            lua_pushinteger(L, slot->id);
            lua_rawseti(L, -2, ++n);
        }
    }
    g_state->slotsLock.unlock();
    return 1;
}

static int task_cancel(lua_State* L) {
    LuaTask* task = lua_totask(L, 1);
    if (task->id == 0 || !g_state)
        return 0;
    g_state->slotsLock.lock();
    KitsuneCoroutine* slot = FindSlot(g_state, task->id);
    long st = slot ? slot->state.load() : KITSUNE_COROUTINE_STATE_NOT_USED;
    if (slot && st != KITSUNE_COROUTINE_STATE_DONE && st != KITSUNE_COROUTINE_STATE_RELEASED)
        slot->state.store(KITSUNE_COROUTINE_STATE_ABORTED);
    g_state->slotsLock.unlock();
    g_state->workEvent.Set();
    return 0;
}

static int task_getid(lua_State* L) {
    LuaTask* task = lua_totask(L, 1);
    lua_pushinteger(L, task->id);
    return 1;
}

static int task_finished(lua_State* L) {
    LuaTask* task = lua_totask(L, 1);
    if (task->id == 0 || !g_state) {
        lua_pushboolean(L, 1);
        return 1;
    }
    g_state->slotsLock.lock();
    KitsuneCoroutine* slot = FindSlot(g_state, task->id);
    long st = slot ? slot->state.load() : KITSUNE_COROUTINE_STATE_NOT_USED;
    int finished = (!slot || st == KITSUNE_COROUTINE_STATE_DONE || st == KITSUNE_COROUTINE_STATE_RELEASED) ? 1 : 0;
    g_state->slotsLock.unlock();
    lua_pushboolean(L, finished);
    return 1;
}

static int task_geterror(lua_State* L) {
    LuaTask* task = lua_totask(L, 1);
    if (task->id == 0 || !g_state) {
        lua_pushnil(L);
        return 1;
    }
    g_state->slotsLock.lock();
    KitsuneCoroutine* slot = FindSlot(g_state, task->id);
    long st = slot ? slot->state.load() : KITSUNE_COROUTINE_STATE_NOT_USED;
    if (!slot || (st != KITSUNE_COROUTINE_STATE_DONE && st != KITSUNE_COROUTINE_STATE_RELEASED) || !slot->error) {
        g_state->slotsLock.unlock();
        lua_pushnil(L);
        return 1;
    }
    lua_pushstring(L, slot->error);
    g_state->slotsLock.unlock();
    return 1;
}

static int task_getresult(lua_State* L) {
    LuaTask* task = lua_totask(L, 1);
    if (task->id == 0 || !g_state) {
        lua_pushnil(L);
        return 1;
    }
    g_state->slotsLock.lock();
    KitsuneCoroutine* slot = FindSlot(g_state, task->id);
    long st = slot ? slot->state.load() : KITSUNE_COROUTINE_STATE_NOT_USED;
    if (!slot || (st != KITSUNE_COROUTINE_STATE_DONE && st != KITSUNE_COROUTINE_STATE_RELEASED)) {
        g_state->slotsLock.unlock();
        lua_pushnil(L);
        return 1;
    }
    PushKitsuneVariable(L, &slot->result);
    g_state->slotsLock.unlock();
    return 1;
}

static int task_seterrorhandler(lua_State* L) {
    if (!g_state)
        return 0;
    if (g_state->taskErrorHandlerRef != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, g_state->taskErrorHandlerRef);
        g_state->taskErrorHandlerRef = LUA_NOREF;
    }
    if (lua_isfunction(L, 1))
        g_state->taskErrorHandlerRef = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

int luaopen_tasks(lua_State* L) {
    static const struct luaL_Reg taskfunctions[] = {
        { "New",             task_new             },
        { "Open",            task_open            },
        { "Resume",          task_resume          },
        { "Cancel",          task_cancel          },
        { "Dispose",         task_gc              },
        { "GetId",           task_getid           },
        { "GetStatus",       task_getstatus       },
        { "SetName",         task_setname         },
        { "GetName",         task_getname         },
        { "Finished",        task_finished        },
        { "GetError",        task_geterror        },
        { "GetResult",       task_getresult       },
        { "GetAllIds",       task_getallids       },
        { "SetErrorHandler", task_seterrorhandler },
        { NULL, NULL }
    };
    static const struct luaL_Reg taskmeta[] = {
        { "__gc",       task_gc       },
        { "__tostring", task_tostring },
        { NULL, NULL }
    };
    luaL_newlibtable(L, taskfunctions);
    luaL_setfuncs(L, taskfunctions, 0);
    int methods_idx = lua_gettop(L);
    luaL_newmetatable(L, LUATASK_META);
    luaL_setfuncs(L, taskmeta, 0);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, methods_idx);
    lua_rawset(L, -3);
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, methods_idx);
    lua_rawset(L, -3);
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushinteger(L, KITSUNE_STATUS_NONE);      lua_setfield(L, -2, "None");
    lua_pushinteger(L, KITSUNE_STATUS_IDLE);      lua_setfield(L, -2, "Idle");
    lua_pushinteger(L, KITSUNE_STATUS_SLEEPING);  lua_setfield(L, -2, "Sleeping");
    lua_pushinteger(L, KITSUNE_STATUS_RUNNING);   lua_setfield(L, -2, "Running");
    lua_pushinteger(L, KITSUNE_STATUS_DONE);      lua_setfield(L, -2, "Done");
    lua_pushinteger(L, KITSUNE_STATUS_FAULTED);   lua_setfield(L, -2, "Faulted");
    lua_pushinteger(L, KITSUNE_STATUS_CANCELLED); lua_setfield(L, -2, "Cancelled");
    lua_pushinteger(L, KITSUNE_STATUS_INLINE);    lua_setfield(L, -2, "Inline");
    lua_pushinteger(L, KITSUNE_STATUS_PAUSED);    lua_setfield(L, -2, "Paused");
    lua_setglobal(L, "TaskStatus");
    lua_pushvalue(L, methods_idx);
    return 1;
}
