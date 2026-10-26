#ifdef KITSUNE_HTTP

#include "httpclient/LuaHttpClient.h"
#include "httpclient/LuaHttpClientMain.h"
#include "websocket/LuaWebSocket.h"
#include "websocket/LuaWebSocketMain.h"

// All Http module-level functions AND LuaHttpClient instance methods in one table.
// The table is returned as the Http global so both Http.Create() and
// client:Request() resolve through the same __index chain.
static const luaL_Reg httpclientFunctions[] = {
	{ "New",               HttpClient_New },
	{ "UrlEncode",          HttpClient_UrlEncode },
	{ "UrlDecode",          HttpClient_UrlDecode },
	{ "Request",            HttpClient_Request },
	{ "Call",               HttpClient_Call },
	{ "Stream",             HttpClient_Stream },
	{ "Connect",            HttpClient_Connect },
	{ "SetTimeout",         HttpClient_SetTimeout },
	{ "SetDefaultHeader",   HttpClient_SetDefaultHeader },
	{ "SetFollowRedirects", HttpClient_SetFollowRedirects },
	{ "SetVerifySSL",       HttpClient_SetVerifySSL },
	{ "SetAliveToken",      HttpClient_SetAliveToken },
	{ "GetTimestamp",       HttpClient_GetTimestamp },
	{ NULL, NULL }
};

static const luaL_Reg httpclientMeta[] = {
	{ "__gc",       HttpClient_GC },
	{ "__tostring", HttpClient_ToString },
	{ NULL, NULL }
};

int luaopen_httpclient(lua_State* L) {
	CURLM* multi = curl_multi_init();
	if (!multi)
		return luaL_error(L, "curl_multi_init failed");

	lua_pushlightuserdata(L, multi);
	lua_rawsetp(L, LUA_REGISTRYINDEX, &CURLM_KEY);

	// Sentinel userdata: __gc fires curl_multi_cleanup when the Lua state closes
	lua_newuserdata(L, 1);
	lua_newtable(L);
	lua_pushcfunction(L, HttpSentinel_GC);
	lua_setfield(L, -2, "__gc");
	lua_setmetatable(L, -2);
	lua_rawsetp(L, LUA_REGISTRYINDEX, &SENTINEL_KEY);

	// Create the Http module table first; it doubles as the __index for
	// LuaHttpClient instances so both Http.Create() and client:Request() work.
	luaL_newlibtable(L, httpclientFunctions);
	luaL_setfuncs(L, httpclientFunctions, 0);

	luaL_newmetatable(L, LUAHTTPCLIENT);
	luaL_setfuncs(L, httpclientMeta, 0);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pop(L, 1);

	luaL_newmetatable(L, LUAHTTPCURLREQUEST);
	lua_pushcfunction(L, HttpCurlRequest_GC);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	luaopen_websocket(L);

	return 1;
}

#endif  // KITSUNE_HTTP
