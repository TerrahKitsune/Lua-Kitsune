#ifdef KITSUNE_HTTP

#include "tcp/LuaTCPMain.h"
#include "tcp/LuaTCPListener.h"

static const luaL_Reg tcpFunctions[] = {
	{ "StartListener", TCP_StartListener },
	{ "Connect",       TCP_Connect       },
	{ NULL, NULL }
};

static const luaL_Reg tcplistenerMeta[] = {
	{ "__gc",       TcpListener_GC      },
	{ "__tostring", TcpListener_ToString },
	{ NULL, NULL }
};

static const luaL_Reg tcplistenerMethods[] = {
	{ "Accept",     TcpListener_Accept     },
	{ "Dispose",    TcpListener_Dispose    },
	{ "GetContext", TcpListener_GetContext },
	{ NULL, NULL }
};

static const luaL_Reg tcpclientMeta[] = {
	{ "__gc",       TcpClient_GC      },
	{ "__tostring", TcpClient_ToString },
	{ NULL, NULL }
};

static const luaL_Reg tcpclientMethods[] = {
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
	luaL_newlibtable(L, tcpFunctions);
	luaL_setfuncs(L, tcpFunctions, 0);

	luaL_newmetatable(L, LUATCPLISTENER);
	luaL_setfuncs(L, tcplistenerMeta, 0);
	lua_pushliteral(L, "__index");
	lua_newtable(L);
	luaL_setfuncs(L, tcplistenerMethods, 0);
	lua_rawset(L, -3);
	lua_pop(L, 1);

	luaL_newmetatable(L, LUATCPCLIENT);
	luaL_setfuncs(L, tcpclientMeta, 0);
	lua_pushliteral(L, "__index");
	lua_newtable(L);
	luaL_setfuncs(L, tcpclientMethods, 0);
	lua_rawset(L, -3);
	lua_pop(L, 1);

	return 1;
}

#endif /* KITSUNE_HTTP */

