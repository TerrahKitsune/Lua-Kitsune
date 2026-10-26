#pragma once
#ifdef KITSUNE_HTTP
#include "LuaMainInclude.h"
#include <curl/curl.h>
int luaopen_httpclient(lua_State* L);
#endif
