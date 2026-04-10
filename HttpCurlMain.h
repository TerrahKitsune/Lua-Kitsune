#pragma once
#ifdef KITSUNE_HTTP
#include "lua_main_incl.h"
#include <curl/curl.h>
int luaopen_http(lua_State* L);
#endif
