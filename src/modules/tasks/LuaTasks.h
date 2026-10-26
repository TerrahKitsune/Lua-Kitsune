#pragma once
#include "LuaMainInclude.h"

#define LUATASK_META "LuaTask"

struct LuaTask {
    int id; // coroutine id; 0 once this handle has released its refcount share
};

int Tasks_New(lua_State* L);
int Tasks_Open(lua_State* L);
int Tasks_Resume(lua_State* L);
int Tasks_Cancel(lua_State* L);
int Tasks_Dispose(lua_State* L);
int Tasks_ToString(lua_State* L);
int Tasks_GetId(lua_State* L);
int Tasks_GetStatus(lua_State* L);
int Tasks_SetName(lua_State* L);
int Tasks_GetName(lua_State* L);
int Tasks_Finished(lua_State* L);
int Tasks_Wait(lua_State* L);
int Tasks_GetError(lua_State* L);
int Tasks_GetResult(lua_State* L);
int Tasks_ConsumeResult(lua_State* L);
int Tasks_GetAllIds(lua_State* L);
int Tasks_SetErrorHandler(lua_State* L);
int Tasks_GetCurrentId(lua_State* L);
int Tasks_OnError(lua_State* L);
int Tasks_ActiveCount(lua_State* L);
int Tasks_GetIdByName(lua_State* L);
