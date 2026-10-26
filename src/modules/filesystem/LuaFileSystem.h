#pragma once
#include "LuaMainInclude.h"

int FileSystem_GetAll(lua_State* L);
int FileSystem_GetFiles(lua_State* L);
int FileSystem_GetDirectories(lua_State* L);
int FileSystem_GetFileInfo(lua_State* L);
int FileSystem_Copy(lua_State* L);
int FileSystem_Move(lua_State* L);
int FileSystem_Delete(lua_State* L);
int FileSystem_CreateDirectory(lua_State* L);
int FileSystem_RemoveDirectory(lua_State* L);
int FileSystem_Rename(lua_State* L);
int FileSystem_CurrentDirectory(lua_State* L);
int FileSystem_GetTempFileName(lua_State* L);
int FileSystem_SetCurrentDirectory(lua_State* L);
int FileSystem_GetDrives(lua_State* L);
int FileSystem_SetAttributes(lua_State* L);
int FileSystem_Open(lua_State* L);
int FileSystem_GetSpecialFolder(lua_State* L);
