#pragma once
#include "lua_main_incl.h"

int luaopen_http(lua_State* L);
char* GetHttpBuffer(size_t len);