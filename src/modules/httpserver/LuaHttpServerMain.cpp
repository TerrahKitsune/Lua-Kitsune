#include "httpserver/LuaHttpServerMain.h"
#include "httpserver/LuaHttpServer.h"
#include "httpserver/LuaHttpServerRequest.h"
#include "websocket/LuaWebSocket.h"

static const luaL_Reg httpserverFunctions[] = {
    { "Listen", HttpServer_Listen },
    { NULL, NULL }
};

static const luaL_Reg httpserverMeta[] = {
    { "__gc",       HttpServer_GC      },
    { "__tostring", HttpServer_ToString },
    { NULL, NULL }
};

static const luaL_Reg httpserverMethods[] = {
    { "Accept",          HttpServer_Accept          },
    { "SetOnDisconnect", HttpServer_SetOnDisconnect },
    { "SetAliveToken",   HttpServer_SetAliveToken   },
    { "Close",           HttpServer_Close           },
    { NULL, NULL }
};

static const luaL_Reg httprequestMeta[] = {
    { "__gc",       HttpRequest_GC      },
    { "__tostring", HttpRequest_ToString },
    { NULL, NULL }
};

static const luaL_Reg httprequestMethods[] = {
    { "GetId",       HttpRequest_GetId       },
    { "GetUrl",      HttpRequest_GetUrl      },
    { "GetMethod",   HttpRequest_GetMethod   },
    { "GetHeaders",  HttpRequest_GetHeaders  },
    { "GetIp",       HttpRequest_GetIp       },
    { "IsFinished",  HttpRequest_IsFinished  },
    { "GetBody",     HttpRequest_GetBody     },
    { "GetContext",  HttpRequest_GetContext  },
    { "GetResponse", HttpRequest_GetResponse },
    { "GetError",    HttpRequest_GetError    },
    { NULL, NULL }
};

static const luaL_Reg httpresponseMeta[] = {
    { "__gc",       HttpResponse_GC      },
    { "__tostring", HttpResponse_ToString },
    { NULL, NULL }
};

static const luaL_Reg httpresponseMethods[] = {
    { "SetCode",              HttpResponse_SetCode              },
    { "SetHeader",            HttpResponse_SetHeader            },
    { "Send",                 HttpResponse_Send                 },
    { "Reject",               HttpResponse_Reject               },
    { "Close",                HttpResponse_Close                },
    { "UpgradeToWebSocket",   HttpResponse_UpgradeToWebSocket                 },
    { NULL, NULL }
};

int luaopen_httpserver(lua_State* L) {
    luaL_newlibtable(L, httpserverFunctions);
    luaL_setfuncs(L, httpserverFunctions, 0);

    luaL_newmetatable(L, LUAHTTPSERVER);
    luaL_setfuncs(L, httpserverMeta, 0);
    lua_pushliteral(L, "__index");
    lua_newtable(L);
    luaL_setfuncs(L, httpserverMethods, 0);
    lua_rawset(L, -3);
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);
    lua_pop(L, 1);

    luaL_newmetatable(L, LUAHTTPREQUEST);
    luaL_setfuncs(L, httprequestMeta, 0);
    lua_pushliteral(L, "__index");
    lua_newtable(L);
    luaL_setfuncs(L, httprequestMethods, 0);
    lua_rawset(L, -3);
    lua_pop(L, 1);

    luaL_newmetatable(L, LUAHTTPRESPONSE);
    luaL_setfuncs(L, httpresponseMeta, 0);
    lua_pushliteral(L, "__index");
    lua_newtable(L);
    luaL_setfuncs(L, httpresponseMethods, 0);
    lua_rawset(L, -3);
    lua_pop(L, 1);

    return 1;
}
