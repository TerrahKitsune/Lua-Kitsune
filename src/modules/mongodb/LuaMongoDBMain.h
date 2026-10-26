#pragma once
#include "LuaMainInclude.h"

int  luaopen_mongodb(lua_State* L);
void MongoGlobalCleanup();
void MongoEagerInit();
void MongoExplicitCleanup();
