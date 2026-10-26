#pragma once

#ifdef KITSUNE_HTTP

#include "LuaMainInclude.h"
#include "stream/LuaStream.h"
#include <curl/curl.h>

#define LUAHTTPCLIENT      "LuaHTTPClient"
#define LUAHTTPCURLREQUEST "LuaHTTPRequest"

// Per-easy-handle completion record.
// Allocated alongside each easy handle; stored as CURLOPT_PRIVATE so that
// DrainCurlm can dispatch CURLMSG_DONE to the correct struct even when
// multiple concurrent coroutines are pumping the same CURLM*.
typedef struct CurlMsg {
	bool     done;    // true once CURLMSG_DONE has been dispatched here
	CURLcode result;  // the CURLcode from the CURLMSG_DONE message
} CurlMsg;

// Streaming chunk queue
typedef struct ChunkNode {
	char*             data;
	size_t            len;
	struct ChunkNode* next;
} ChunkNode;

typedef struct LuaHttpClient {
	int    defaultHeadersRef;
	long   timeoutMs;
	bool   followRedirects;
	bool   verifySsl;

	int      aliveTokenRef;            // LUA_NOREF when not set; registry ref to an AliveToken userdata
	int64_t  lastRequestElapsedTicks;  // duration of the last completed request in 100-ns ticks
} LuaHttpClient;

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

typedef struct LuaHttpStreamNative {
	CURL*              easy;
	CURLM*             multi;           // borrowed
	struct curl_slist* requestHdrs;     // owned; freed in HttpStreamClose
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
	CurlMsg*           curlMsg;        // heap-allocated; freed in HttpStreamClose
} LuaHttpStreamNative;

// WebSocket types are defined in LuaWebSocket.h

// Functions defined in LuaHttpClient.cpp; referenced by LuaHttpClientMain.cpp
extern const char CURLM_KEY;
extern const char SENTINEL_KEY;
int HttpSentinel_GC(lua_State* L);
int HttpClient_UrlEncode(lua_State* L);
int HttpClient_UrlDecode(lua_State* L);
int HttpClient_New(lua_State* L);
int HttpClient_SetTimeout(lua_State* L);
int HttpClient_SetDefaultHeader(lua_State* L);
int HttpClient_SetFollowRedirects(lua_State* L);
int HttpClient_SetVerifySSL(lua_State* L);
int HttpClient_GC(lua_State* L);
int HttpClient_ToString(lua_State* L);
int HttpCurlRequest_GC(lua_State* L);
int HttpClient_Request(lua_State* L);
int HttpClient_Call(lua_State* L);     // blocking helper: drives Request and returns the result table directly
int HttpClient_Stream(lua_State* L);
int HttpClient_SetAliveToken(lua_State* L); // called as client:SetAliveToken(token)
int HttpClient_GetTimestamp(lua_State* L); // returns last request duration as a TimeSpan

#endif  // KITSUNE_HTTP
