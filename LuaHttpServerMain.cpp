#include "LuaHttpServerMain.h"
#include "LuaHttpServer.h"
#include "LuaHttpRequest.h"

static const luaL_Reg httpserver_functions[] = {
    { "Listen", HttpServer_Listen },
    { NULL, NULL }
};

static const luaL_Reg httpserver_meta[] = {
    { "__gc",       HttpServer_GC      },
    { "__tostring", HttpServer_ToString },
    { NULL, NULL }
};

static const luaL_Reg httpserver_methods[] = {
    { "Accept",          HttpServer_Accept          },
    { "SetOnDisconnect", HttpServer_SetOnDisconnect },
    { "Close",           HttpServer_Close           },
    { NULL, NULL }
};

static const luaL_Reg httprequest_meta[] = {
    { "__gc",       HttpRequest_GC      },
    { "__tostring", HttpRequest_ToString },
    { NULL, NULL }
};

static const luaL_Reg httprequest_methods[] = {
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

static const luaL_Reg httpresponse_meta[] = {
    { "__gc",       HttpResponse_GC      },
    { "__tostring", HttpResponse_ToString },
    { NULL, NULL }
};

static const luaL_Reg httpresponse_methods[] = {
    { "SetCode",   HttpResponse_SetCode   },
    { "SetHeader", HttpResponse_SetHeader },
    { "Send",      HttpResponse_Send      },
    { "Reject",    HttpResponse_Reject    },
    { "Close",     HttpResponse_Close     },
    { NULL, NULL }
};

int luaopen_httpserver(lua_State* L) {
    /* HttpServer module table */
    luaL_newlibtable(L, httpserver_functions);
    luaL_setfuncs(L, httpserver_functions, 0);

    luaL_newmetatable(L, LUAHTTPSERVER);
    luaL_setfuncs(L, httpserver_meta, 0);
    lua_pushliteral(L, "__index");
    lua_newtable(L);
    luaL_setfuncs(L, httpserver_methods, 0);
    lua_rawset(L, -3);
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);
    lua_pop(L, 1);

    /* HttpRequest metatable */
    luaL_newmetatable(L, LUAHTTPREQUEST);
    luaL_setfuncs(L, httprequest_meta, 0);
    lua_pushliteral(L, "__index");
    lua_newtable(L);
    luaL_setfuncs(L, httprequest_methods, 0);
    lua_rawset(L, -3);
    lua_pop(L, 1);

    /* HttpResponse metatable */
    luaL_newmetatable(L, LUAHTTPRESPONSE);
    luaL_setfuncs(L, httpresponse_meta, 0);
    lua_pushliteral(L, "__index");
    lua_newtable(L);
    luaL_setfuncs(L, httpresponse_methods, 0);
    lua_rawset(L, -3);
    lua_pop(L, 1);

    return 1; /* return module table */
}
