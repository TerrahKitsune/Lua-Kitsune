#pragma once
#include "LuaMainInclude.h"

#define LUATASK_META "LuaTask"

struct LuaTask {
    int id; // coroutine id; 0 once this handle has released its refcount share
};

int task_new(lua_State* L);
int task_open(lua_State* L);
int task_resume(lua_State* L);
int task_cancel(lua_State* L);
int task_gc(lua_State* L);
int task_tostring(lua_State* L);
int task_getid(lua_State* L);
int task_getstatus(lua_State* L);
int task_setname(lua_State* L);
int task_getname(lua_State* L);
int task_finished(lua_State* L);
int task_wait(lua_State* L);
int task_geterror(lua_State* L);
int task_getresult(lua_State* L);
int task_consumeresult(lua_State* L);
int task_getallids(lua_State* L);
int task_seterrorhandler(lua_State* L);
int task_getcurrentid(lua_State* L);
int task_onerror(lua_State* L);
int task_activecount(lua_State* L);
int task_getidbyname(lua_State* L);
