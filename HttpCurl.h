#pragma once

#ifdef KITSUNE_HTTP

#include "lua_main_incl.h"
#include "stream.h"
#include <curl/curl.h>

#define LUAHTTPCLIENT      "LuaHTTPClient"
#define LUAHTTPCURLREQUEST "LuaHTTPRequest"

// -- CurlMsg — per-easy-handle completion record ------------------------------
// Allocated alongside each easy handle; stored as CURLOPT_PRIVATE so that
// drain_curlm can dispatch CURLMSG_DONE to the correct struct even when
// multiple concurrent coroutines are pumping the same CURLM*.
typedef struct CurlMsg {
	bool     done;    // true once CURLMSG_DONE has been dispatched here
	CURLcode result;  // the CURLcode from the CURLMSG_DONE message
} CurlMsg;

// -- ChunkNode — streaming chunk queue ----------------------------------------
typedef struct ChunkNode {
	char*             data;
	size_t            len;
	struct ChunkNode* next;
} ChunkNode;

// -- LuaHttpClient -------------------------------------------------------------
typedef struct LuaHttpClient {
	int    defaultHeadersRef;
	long   timeoutMs;
	bool   followRedirects;
	bool   verifySsl;

	int      aliveTokenRef;            // LUA_NOREF when not set; registry ref to an AliveToken userdata
	int64_t  lastRequestElapsedTicks;  // duration of the last completed request in 100-ns ticks
} LuaHttpClient;

// -- LuaHttpCurlRequest -------------------------------------------------------
typedef struct LuaHttpCurlRequest {
	CURL*              easy;
	CURLM*             multi;           // borrowed; do NOT free here
	// callbackL: Lua state set just before curl_multi_perform so ReadBodyCallback
	// can call vtbl->read.  Always valid during callbacks because Kitsune is
	// single-threaded and curl_multi_perform is only called from within a Lua continuation.
	lua_State*         callbackL;
	LuaHttpClient*     client;          // borrowed; used to write lastRequestElapsedTicks on completion
	int64_t            startNs;         // steady_clock ns when the request was submitted
	// Response body — one of the two is active, never both
	char*              body;            // NULL when streamOutput is set
	size_t             bodyLen;
	size_t             bodyAlloc;
	LuaStream*         streamOutput;    // optional; write callback pipes here instead of body
	int                streamOutputRef; // registry ref keeping the stream alive during the request
	// Request body from a stream
	LuaStream*         streamInput;     // optional; read callback reads from here instead of string body
	int                streamInputRef;  // registry ref
	// Request headers (owned; freed in __gc)
	struct curl_slist* requestHdrs;
	// Accumulated response headers (parallel arrays; each entry is kitsune_malloc'd)
	char**             headerKeys;
	char**             headerVals;
	int                headerCount;
	int                headerAlloc;
	// Status
	long               httpCode;        // 0 until curl reports completion
	char               statusText[256]; // reason phrase parsed from the HTTP status line
	char               errorBuf[CURL_ERROR_SIZE];
	bool               addedToMulti;
	CurlMsg*           curlMsg;        // heap-allocated; freed in __gc
} LuaHttpCurlRequest;

// -- LuaHttpStreamNative -------------------------------------------------------
typedef struct LuaHttpStreamNative {
	CURL*              easy;
	CURLM*             multi;           // borrowed
	struct curl_slist* requestHdrs;     // owned; freed in http_stream_close
	ChunkNode*         chunkHead;
	ChunkNode*         chunkTail;
	bool               headersComplete; // blank separator line received
	long               httpCode;
	char               statusText[256];
	char**             headerKeys;
	char**             headerVals;
	int                headerCount;
	int                headerAlloc;
	char               errorBuf[CURL_ERROR_SIZE];
	bool               addedToMulti;
	bool               done;            // CURLMSG_DONE received
	CurlMsg*           curlMsg;        // heap-allocated; freed in http_stream_close
} LuaHttpStreamNative;

// WebSocket types are defined in LuaWebSocket.h

int luaopen_http(lua_State* L);

// -- Functions defined in HttpCurl.cpp; referenced by HttpCurlMain.cpp ---------
extern const char g_curlm_key;
extern const char g_sentinel_key;
int http_sentinel_gc(lua_State* L);
int UrlEncode(lua_State* L);
int UrlDecode(lua_State* L);
int http_create(lua_State* L);
int client_set_timeout(lua_State* L);
int client_set_default_header(lua_State* L);
int client_set_follow_redirects(lua_State* L);
int client_set_verify_ssl(lua_State* L);
int luahttpclient_gc(lua_State* L);
int luahttpclient_tostring(lua_State* L);
int luahttprequest_gc(lua_State* L);
int client_request(lua_State* L);
int client_call(lua_State* L);     // blocking helper: drives Request and returns the result table directly
int client_stream(lua_State* L);
int client_set_alive_token(lua_State* L); // called as client:SetAliveToken(token)
int client_get_timestamp(lua_State* L); // returns last request duration as a TimeSpan

#endif  // KITSUNE_HTTP
