#pragma once
#include "LuaMainInclude.h"
#include "stream/LuaStream.h"
#include <event2/http.h>

#define LUAHTTPREQUEST  "HTTPREQUEST"
#define LUAHTTPRESPONSE "HTTPRESPONSE"
#define LUAHTTPSERVER   "HTTPSERVER"

typedef struct LuaHttpResponse    LuaHttpResponse;
typedef struct LuaHttpRequest     LuaHttpRequest;
typedef struct HttpOpenConnection HttpOpenConnection;
typedef struct LuaHttpServer      LuaHttpServer;

/*
 * Per-event queue node — lives on LuaHttpServer.queueHead/tail and
 * LuaHttpServer.senders.  Never pushed to Lua directly.
 */
typedef struct HttpOpenConnection {
	int                        requestRef; /* luaL_ref to LuaHttpRequest userdata —
											   prevents __gc while queued or sending */
	struct HttpOpenConnection* next;
} HttpOpenConnection;

/*
 * One LuaHttpRequest per HTTP request.  Stored in the Lua registry keyed by
 * the evhttp_request pointer.  evhttp_request_own() is called on arrival so
 * the request stays alive until we call evhttp_request_free().
 */
typedef struct LuaHttpRequest {
	struct evhttp_request*    req;    /* owned — evhttp_request_own() was called;
										NULL after response sent (evhttp_send_done frees it) */
	struct evhttp_connection* con;    /* stable connection pointer kept for disconnect tracking;
										NULL after ConnCloseCb fires                        */
	LuaHttpServer*            server; /* back-pointer to owning server                       */
	char*                  url;
	char*                  method;
	char*                  body;
	size_t                 body_len;
	bool                   isFinished;
	bool                   is_error;
	char*                  errorMsg;
	int                    headersRef; /* luaL_ref to {[name]=value} table */
	int                    contextRef; /* luaL_ref to context table        */
	int                    responseRef;/* luaL_ref to LuaHttpResponse      */
	bool                   upgraded;   /* true after UpgradeToWebSocket(); guards cleanup */
} LuaHttpRequest;

/*
 * One LuaHttpResponse per request, reached via request->responseRef.
 * Headers are set directly on the evhttp_request output headers via
 * evhttp_add_header(), so no extra_headers buffer is needed here.
 */
typedef struct LuaHttpResponse {
	LuaStream*      stream;      /* NULL = not streaming; non-NULL = active sender */
	bool            chunked;     /* true = evhttp_send_reply_start/chunk/end path  */
	bool            finalized;
	int             statusCode; /* HTTP status, default 200                       */
	int             streamRef;  /* luaL_ref keeping stream userdata alive during chunked send */
	LuaHttpRequest* connection;  /* back-pointer, nulled before responseRef unref */
} LuaHttpResponse;

/* push/check helpers */
LuaHttpRequest*  lua_pushhttprequest (lua_State* L);
LuaHttpRequest*  luaL_checkhttprequest(lua_State* L, int idx);
LuaHttpResponse* lua_pushhttpresponse(lua_State* L);
LuaHttpResponse* luaL_checkhttpresponse(lua_State* L, int idx);

/* Direct cleanup — take a pointer, no stack slot dependency.
   Idempotent: safe to call multiple times on the same object.
   HttpRequestCleanup calls HttpResponseCleanup on the owned response. */
void HttpRequestCleanup (lua_State* L, LuaHttpRequest*  r);
void HttpResponseCleanup(lua_State* L, LuaHttpResponse* r);

/* Request methods */
int HttpRequest_GetId      (lua_State* L);
int HttpRequest_GetUrl     (lua_State* L);
int HttpRequest_GetMethod  (lua_State* L);
int HttpRequest_GetHeaders (lua_State* L);
int HttpRequest_GetIp      (lua_State* L);
int HttpRequest_IsFinished (lua_State* L);
int HttpRequest_GetBody    (lua_State* L);
int HttpRequest_GetContext (lua_State* L);
int HttpRequest_GetResponse(lua_State* L);
int HttpRequest_GetError   (lua_State* L);
int HttpRequest_GC         (lua_State* L);
int HttpRequest_ToString   (lua_State* L);

/* Response methods */
int HttpResponse_SetCode   (lua_State* L);
int HttpResponse_SetHeader (lua_State* L);
int HttpResponse_Send      (lua_State* L);
int HttpResponse_Reject    (lua_State* L);
int HttpResponse_Close     (lua_State* L);
int HttpResponse_GC        (lua_State* L);
int HttpResponse_ToString  (lua_State* L);
