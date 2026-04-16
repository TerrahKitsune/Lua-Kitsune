#pragma once
#include "lua_main_incl.h"

int  luaopen_mongo(lua_State* L);
void MongoGlobalCleanup();
void MongoEagerInit();
void MongoExplicitCleanup();
