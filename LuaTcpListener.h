#pragma once

#ifdef KITSUNE_HTTP

#include "lua_main_incl.h"
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <stddef.h>
#include <stdbool.h>

#define LUATCPLISTENER "TCPLISTENER"
#define LUATCPCLIENT   "TCPCLIENT"

/* ── Forward declarations ─────────────────────────────────────────────────── */
typedef struct LuaTcpListener LuaTcpListener;
typedef struct LuaTcpClient   LuaTcpClient;

/* ── Message queue node ───────────────────────────────────────────────────── */
typedef struct TcpMsgNode {
    char*              data;   /* raw bytes (may contain nulls), freed on dequeue */
    size_t             len;
    struct TcpMsgNode* next;
} TcpMsgNode;

/* ── LuaTcpListener userdata ──────────────────────────────────────────────── */
typedef struct LuaTcpListener {
    struct event_base*     base;
    struct evconnlistener* listener;   /* libevent evconnlistener */
    lua_State*             L;

    /* Pending-accept queue: dynamic array of LuaTcpClient* that have been
       accepted by libevent but not yet yielded to Lua via Accept(). */
    LuaTcpClient**         pending_queue;
    int                    pending_count;
    int                    pending_cap;

    int                    self_ref;      /* keeps this userdata alive during callbacks */
    int                    context_ref;   /* 0 until GetContext() first called */
    int                    port;          /* listen port, for __tostring */
    bool                   disposed;
} LuaTcpListener;

/* ── LuaTcpClient userdata ────────────────────────────────────────────────── */
typedef struct LuaTcpClient {
    struct bufferevent*  bev;         /* libevent bufferevent (the connection)       */
    struct event_base*   evbase;      /* borrowed from listener or self-created      */
    lua_State*           L;

    bool                 connected;
    bool                 disposed;
    bool                 owns_base;   /* true if evbase was self-created (TCP.Connect) */
    char*                ip;          /* cached remote IP string                     */
    int                  port;        /* cached remote port                          */
    char*                error_msg;   /* set when connection drops with an error     */

    /* Message queue (FIFO)                                                        */
    TcpMsgNode*          msg_head;
    TcpMsgNode*          msg_tail;

    /* Registry refs                                                                        */
    int                  self_ref;    /* keeps this userdata alive during callbacks  */
    int                  context_ref; /* 0 until GetContext() first called           */

    lua_Integer          id;
} LuaTcpClient;

/* ── TCP module functions ─────────────────────────────────────────────────── */
int Tcp_StartListener(lua_State* L);
int Tcp_Connect(lua_State* L);

/* ── Listener methods ─────────────────────────────────────────────────────── */
int TcpListener_Accept     (lua_State* L);
int TcpListener_Dispose    (lua_State* L);
int TcpListener_GetContext (lua_State* L);
int TcpListener_GC         (lua_State* L);
int TcpListener_ToString   (lua_State* L);

/* ── Client methods ───────────────────────────────────────────────────────── */
int TcpClient_Poll         (lua_State* L);
int TcpClient_Send         (lua_State* L);
int TcpClient_GetIP        (lua_State* L);
int TcpClient_GetPort      (lua_State* L);
int TcpClient_IsConnected  (lua_State* L);
int TcpClient_Dispose      (lua_State* L);
int TcpClient_GetContext   (lua_State* L);
int TcpClient_GC           (lua_State* L);
int TcpClient_ToString     (lua_State* L);

#endif /* KITSUNE_HTTP */
