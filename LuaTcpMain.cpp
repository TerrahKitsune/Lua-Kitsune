#ifdef KITSUNE_HTTP

#include "LuaTcpMain.h"
#include "LuaTcpListener.h"

static const luaL_Reg tcp_functions[] = {
    { "StartListener", Tcp_StartListener },
    { "Connect",       Tcp_Connect       },
    { NULL, NULL }
};

static const luaL_Reg tcplistener_meta[] = {
    { "__gc",       TcpListener_GC      },
    { "__tostring", TcpListener_ToString },
    { NULL, NULL }
};

static const luaL_Reg tcplistener_methods[] = {
    { "Accept",     TcpListener_Accept     },
    { "Dispose",    TcpListener_Dispose    },
    { "GetContext", TcpListener_GetContext },
    { NULL, NULL }
};

static const luaL_Reg tcpclient_meta[] = {
    { "__gc",       TcpClient_GC      },
    { "__tostring", TcpClient_ToString },
    { NULL, NULL }
};

static const luaL_Reg tcpclient_methods[] = {
    { "Poll",        TcpClient_Poll        },
    { "Send",        TcpClient_Send        },
    { "GetIP",       TcpClient_GetIP       },
    { "GetPort",     TcpClient_GetPort     },
    { "IsConnected", TcpClient_IsConnected },
    { "Dispose",     TcpClient_Dispose     },
    { "GetContext",  TcpClient_GetContext  },
    { NULL, NULL }
};

int luaopen_tcp(lua_State* L) {
    /* TCP module table */
    luaL_newlibtable(L, tcp_functions);
    luaL_setfuncs(L, tcp_functions, 0);

    /* TCPLISTENER metatable */
    luaL_newmetatable(L, LUATCPLISTENER);
    luaL_setfuncs(L, tcplistener_meta, 0);
    lua_pushliteral(L, "__index");
    lua_newtable(L);
    luaL_setfuncs(L, tcplistener_methods, 0);
    lua_rawset(L, -3);
    lua_pop(L, 1);

    /* TCPCLIENT metatable */
    luaL_newmetatable(L, LUATCPCLIENT);
    luaL_setfuncs(L, tcpclient_meta, 0);
    lua_pushliteral(L, "__index");
    lua_newtable(L);
    luaL_setfuncs(L, tcpclient_methods, 0);
    lua_rawset(L, -3);
    lua_pop(L, 1);

    return 1; /* return module table */
}

#endif /* KITSUNE_HTTP */

