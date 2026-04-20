#pragma once
#include "lua_main_incl.h"
#include "stream.h"
#include "mongoose.h"

#define LUAHTTPREQUEST  "HTTPREQUEST"
#define LUAHTTPRESPONSE "HTTPRESPONSE"
#define LUAHTTPSERVER   "HTTPSERVER"

/* Forward declarations */
typedef struct LuaHttpResponse LuaHttpResponse;
typedef struct LuaHttpRequest  LuaHttpRequest;
typedef struct HttpOpenConnection HttpOpenConnection;

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
 * One LuaHttpRequest per connection, created on first event, updated in-place.
 * Stored in Lua registry keyed by conn*.
 */
typedef struct LuaHttpRequest {
    struct mg_connection* conn;
    char*                 url;
    char*                 method;
    char*                 body;
    size_t                body_len;
    bool                  is_finished;
    bool                  is_error;
    char*                 error_msg;
    int                   headers_ref; /* luaL_ref to {[name]=value} table, LUA_NOREF initially */
    int                   context_ref; /* luaL_ref to context table,  LUA_NOREF initially */
    int                   response_ref;/* luaL_ref to LuaHttpResponse, LUA_NOREF initially */
} LuaHttpRequest;

/*
 * One LuaHttpResponse per connection, reached via request->response_ref.
 * conn->fn_data points to the owning LuaHttpServer so Send(Stream) can
 * enqueue into senders without needing a separate server reference.
 */
typedef struct LuaHttpResponse {
    LuaStream*       stream;        /* NULL = not streaming; non-NULL = active sender */
    bool             chunked;       /* true = Transfer-Encoding: chunked */
    bool             finalized;
    int              status_code;   /* HTTP status, default 200 */
    char*            extra_headers; /* heap-allocated "Name: Value\r\n" pairs, NULL initially */
    LuaHttpRequest*  connection;    /* back-pointer — nulled in LuaHttpRequest.__gc before
                                       response_ref is unref'd; every method checks != NULL */
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
