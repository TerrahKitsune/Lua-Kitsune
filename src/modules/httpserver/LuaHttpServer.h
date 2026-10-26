#pragma once
#include "LuaMainInclude.h"
#include "httpserver/LuaHttpServerRequest.h"
#include <event2/event.h>
#include <event2/http.h>

typedef struct LuaHttpServer {
	struct event_base*   base;
	struct evhttp*       http;
	lua_State*           L;            /* stored for use inside libevent callbacks */
	HttpOpenConnection*  queueHead;   /* oldest event — dequeue from here */
	HttpOpenConnection*  queueTail;   /* newest event  — enqueue here     */
	HttpOpenConnection*  senders;      /* connections with active stream send */
	int                  coroutineRef;/* LUA_NOREF until Accept() first called */
	int                  disconnectRef;/* LUA_NOREF or server-wide disconnect fn */
	int                  aliveTokenRef; /* LUA_NOREF when not set */
	bool                 httpInit;    /* true after evhttp_new, guards double-free */
} LuaHttpServer;

LuaHttpServer* lua_pushhttpserver (lua_State* L);
LuaHttpServer* luaL_checkhttpserver(lua_State* L, int idx);

int HttpServer_Listen          (lua_State* L);
int HttpServer_Accept          (lua_State* L);
int HttpServer_SetOnDisconnect (lua_State* L);
int HttpServer_SetAliveToken   (lua_State* L);
int HttpServer_Close           (lua_State* L);
int HttpServer_GC              (lua_State* L);
int HttpServer_ToString        (lua_State* L);
