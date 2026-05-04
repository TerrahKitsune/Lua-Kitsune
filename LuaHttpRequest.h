#pragma once
#include "lua_main_incl.h"
#include "stream.h"
#include <event2/http.h>

#define LUAHTTPREQUEST  "HTTPREQUEST"
#define LUAHTTPRESPONSE "HTTPRESPONSE"
#define LUAHTTPSERVER   "HTTPSERVER"

/* Forward declarations */
typedef struct LuaHttpResponse    LuaHttpResponse;
typedef struct LuaHttpRequest     LuaHttpRequest;
typedef struct HttpOpenConnection HttpOpenConnection;
typedef struct LuaHttpServer      LuaHttpServer;

/*
 * Per-event queue node — lives on LuaHttpServer.queue_head/tail and
 * LuaHttpServer.senders.  Never pushed to Lua directly.
 */
typedef struct HttpOpenConnection {
    int                        request_ref; /* luaL_ref to LuaHttpRequest userdata —
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
                                        NULL after conn_close_cb fires                        */
    LuaHttpServer*            server; /* back-pointer to owning server                       */
    char*                  url;
    char*                  method;
    char*                  body;
    size_t                 body_len;
    bool                   is_finished;
    bool                   is_error;
    char*                  error_msg;
    int                    headers_ref; /* luaL_ref to {[name]=value} table */
    int                    context_ref; /* luaL_ref to context table        */
    int                    response_ref;/* luaL_ref to LuaHttpResponse      */
    bool                   upgraded;   /* true after UpgradeToWebSocket(); guards cleanup */
} LuaHttpRequest;

/*
 * One LuaHttpResponse per request, reached via request->response_ref.
 * Headers are set directly on the evhttp_request output headers via
 * evhttp_add_header(), so no extra_headers buffer is needed here.
 */
typedef struct LuaHttpResponse {
    LuaStream*      stream;      /* NULL = not streaming; non-NULL = active sender */
    bool            chunked;     /* true = evhttp_send_reply_start/chunk/end path  */
    bool            finalized;
    int             status_code; /* HTTP status, default 200                       */
    int             stream_ref;  /* luaL_ref keeping stream userdata alive during chunked send */
    LuaHttpRequest* connection;  /* back-pointer, nulled before response_ref unref */
} LuaHttpResponse;

/* push/check helpers */
LuaHttpRequest*  lua_pushhttprequest (lua_State* L);
LuaHttpRequest*  lua_checkhttprequest(lua_State* L, int idx);
LuaHttpResponse* lua_pushhttpresponse(lua_State* L);
LuaHttpResponse* lua_checkhttpresponse(lua_State* L, int idx);

/* Direct cleanup — take a pointer, no stack slot dependency.
   Idempotent: safe to call multiple times on the same object.
   HttpRequest_Cleanup calls HttpResponse_Cleanup on the owned response. */
void HttpRequest_Cleanup (lua_State* L, LuaHttpRequest*  r);
void HttpResponse_Cleanup(lua_State* L, LuaHttpResponse* r);

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
