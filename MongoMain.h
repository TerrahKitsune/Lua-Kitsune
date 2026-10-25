#pragma once
#include "LuaMainInclude.h"

int  luaopen_mongo(lua_State* L);
void MongoGlobalCleanup();
void MongoEagerInit();
void MongoExplicitCleanup();
