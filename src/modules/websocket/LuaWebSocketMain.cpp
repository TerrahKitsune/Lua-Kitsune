#ifdef KITSUNE_HTTP

#include "websocket/LuaWebSocket.h"
#include "websocket/LuaWebSocketMain.h"

// =============================================================================
// Module init — called by luaopen_httpclient (in HttpCurlMain.cpp or equivalent)
// =============================================================================

int luaopen_websocket(lua_State* L) {
	// WSMESSAGE metatable
	luaL_newmetatable(L, LUAWSMESSAGE);
	lua_pushcfunction(L, WsMessage_GC);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, WsMessage_ToString);
	lua_setfield(L, -2, "__tostring");
	lua_newtable(L);
	lua_pushcfunction(L, WsMessage_GetData);
	lua_setfield(L, -2, "GetData");
	lua_pushcfunction(L, WsMessage_GetType);
	lua_setfield(L, -2, "GetType");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	// WEBSOCKET metatable
	luaL_newmetatable(L, LUAWEBSOCKET);
	lua_pushcfunction(L, WebSocket_GC);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, WebSocket_ToString);
	lua_setfield(L, -2, "__tostring");
	lua_newtable(L);
	lua_pushcfunction(L, WebSocket_IsConnected);
	lua_setfield(L, -2, "IsConnected");
	lua_pushcfunction(L, WebSocket_Poll);
	lua_setfield(L, -2, "Poll");
	lua_pushcfunction(L, WebSocket_Read);
	lua_setfield(L, -2, "Read");
	lua_pushcfunction(L, WebSocket_Send);
	lua_setfield(L, -2, "Send");
	lua_pushcfunction(L, WebSocket_Dispose);
	lua_setfield(L, -2, "Dispose");
	lua_pushcfunction(L, WebSocket_GetId);
	lua_setfield(L, -2, "GetId");
	lua_pushcfunction(L, WebSocket_GetContext);
	lua_setfield(L, -2, "GetContext");
	lua_pushcfunction(L, WebSocket_SetMaxMessageSize);
	lua_setfield(L, -2, "SetMaxMessageSize");
	lua_pushcfunction(L, WebSocket_Ping);
	lua_setfield(L, -2, "Ping");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	return 0;
}

#endif // KITSUNE_HTTP
