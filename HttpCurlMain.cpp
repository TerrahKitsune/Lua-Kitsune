#ifdef KITSUNE_HTTP

#include "HttpCurl.h"

// ─────────────────────────────────────────────────────────────────────────────
// Module registration tables  (wchar pattern)
// ─────────────────────────────────────────────────────────────────────────────

// All Http module-level functions AND LuaHttpClient instance methods in one table.
// The table is returned as the Http global so both Http.Create() and
// client:Request() resolve through the same __index chain.
static const luaL_Reg httpclient_functions[] = {
	{ "New",               http_create },
	{ "UrlEncode",          UrlEncode },
	{ "UrlDecode",          UrlDecode },
	{ "Request",            client_request },
	{ "Stream",             client_stream },
	{ "Connect",            client_connect },
	{ "SetTimeout",         client_set_timeout },
	{ "SetDefaultHeader",   client_set_default_header },
	{ "SetFollowRedirects", client_set_follow_redirects },
	{ "SetVerifySSL",       client_set_verify_ssl },
	{ "SetBinary",          client_set_binary },
	{ NULL, NULL }
};

static const luaL_Reg httpclient_meta[] = {
	{ "__gc",       luahttpclient_gc },
	{ "__tostring", luahttpclient_tostring },
	{ NULL, NULL }
};

// ─────────────────────────────────────────────────────────────────────────────
// luaopen_http  (wchar pattern)
// ─────────────────────────────────────────────────────────────────────────────

int luaopen_http(lua_State* L) {
	// ── CURLM* lifetime ───────────────────────────────────────────────────────
	CURLM* multi = curl_multi_init();
	if (!multi)
		return luaL_error(L, "curl_multi_init failed");

	lua_pushlightuserdata(L, multi);
	lua_rawsetp(L, LUA_REGISTRYINDEX, &g_curlm_key);

	// Sentinel userdata: __gc fires curl_multi_cleanup when the Lua state closes
	lua_newuserdata(L, 1);
	lua_newtable(L);
	lua_pushcfunction(L, http_sentinel_gc);
	lua_setfield(L, -2, "__gc");
	lua_setmetatable(L, -2);
	lua_rawsetp(L, LUA_REGISTRYINDEX, &g_sentinel_key);

	// ── LUAHTTPCLIENT — wchar pattern ─────────────────────────────────────────
	// Create the Http module table first; it doubles as the __index for
	// LuaHttpClient instances so both Http.Create() and client:Request() work.
	luaL_newlibtable(L, httpclient_functions);
	luaL_setfuncs(L, httpclient_functions, 0);

	luaL_newmetatable(L, LUAHTTPCLIENT);
	luaL_setfuncs(L, httpclient_meta, 0);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);       // the Http module table
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);       // the Http module table
	lua_rawset(L, -3);
	lua_pop(L, 1);              // pop LUAHTTPCLIENT metatable

	// ── LUAHTTPREQUEST metatable (internal; __gc only) ────────────────────────
	luaL_newmetatable(L, LUAHTTPREQUEST);
	lua_pushcfunction(L, luahttprequest_gc);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	// WebSocket userdata uses the STREAM metatable — no separate LUAWEBSOCKET
	// registration needed.  ws:Read, ws:Write, ws:WriteBinary, ws:Close etc. are
	// all found via STREAM.__index and dispatch through the vtable.

	// Http module table is on top — return it as the Http global
	return 1;
}

#endif  // KITSUNE_HTTP
