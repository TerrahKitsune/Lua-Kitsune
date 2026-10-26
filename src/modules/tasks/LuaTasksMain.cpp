#include "tasks/LuaTasks.h"
#include "tasks/LuaTasksMain.h"
#include "KitsuneInternal.h"

int luaopen_tasks(lua_State* L) {
    static const struct luaL_Reg taskfunctions[] = {
        { "New",             Tasks_New             },
        { "Open",            Tasks_Open            },
        { "Resume",          Tasks_Resume          },
        { "Cancel",          Tasks_Cancel          },
        { "Dispose",         Tasks_Dispose              },
        { "GetId",           Tasks_GetId          },
        { "GetStatus",       Tasks_GetStatus       },
        { "SetName",         Tasks_SetName         },
        { "GetName",         Tasks_GetName         },
        { "Finished",        Tasks_Finished        },
        { "Wait",            Tasks_Wait            },
        { "GetError",        Tasks_GetError        },
        { "GetResult",       Tasks_GetResult       },
        { "ConsumeResult",   Tasks_ConsumeResult   },
        { "GetAllIds",       Tasks_GetAllIds       },
        { "SetErrorHandler", Tasks_SetErrorHandler },
        { "GetCurrentId",    Tasks_GetCurrentId    },
        { "OnError",         Tasks_OnError         },
        { "ActiveCount",     Tasks_ActiveCount     },
        { "GetIdByName",     Tasks_GetIdByName     },
        { NULL, NULL }
    };
    static const struct luaL_Reg taskmeta[] = {
        { "__gc",       Tasks_Dispose       },
        { "__tostring", Tasks_ToString },
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
    lua_pushinteger(L, KITSUNE_STATUS_WAITING);   lua_setfield(L, -2, "Waiting");
    lua_setglobal(L, "TaskStatus");
    // Expose MaxSlots as a constant on the Tasks table itself.
    lua_pushinteger(L, KITSUNE_MAX_COROUTINES);
    lua_setfield(L, methods_idx, "MaxSlots");
    lua_pushvalue(L, methods_idx);
    return 1;
}
