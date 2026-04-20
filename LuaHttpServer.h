#pragma once
#include "lua_main_incl.h"
#include "LuaHttpRequest.h"

typedef struct LuaHttpServer {
    struct mg_mgr        mgr;
    HttpOpenConnection*  queue_head;    /* oldest event — dequeue from here */
    HttpOpenConnection*  queue_tail;    /* newest event  — enqueue here     */
    HttpOpenConnection*  senders;       /* connections with active stream send */
    int                  coroutine_ref; /* LUA_NOREF until Accept() first called */
    int                  disconnect_ref;/* LUA_NOREF or server-wide disconnect fn */
    bool                 mgr_init;      /* true after mg_mgr_init, guards double-free */
} LuaHttpServer;

LuaHttpServer* lua_pushhttpserver (lua_State* L);
LuaHttpServer* lua_checkhttpserver(lua_State* L, int idx);

int HttpServer_Listen          (lua_State* L);
int HttpServer_Accept          (lua_State* L);
int HttpServer_SetOnDisconnect (lua_State* L);
int HttpServer_Close           (lua_State* L);
int HttpServer_GC              (lua_State* L);
int HttpServer_ToString        (lua_State* L);
