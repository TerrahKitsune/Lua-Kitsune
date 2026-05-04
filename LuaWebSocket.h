#pragma once

#ifdef KITSUNE_HTTP

#include "lua_main_incl.h"
#include <curl/curl.h>
#include <stddef.h>
#include <stdbool.h>

// Forward declarations for libevent types used in LuaWebSocket fields.
struct event_base;
struct evws_connection;

// ---- metatable names --------------------------------------------------------
#define LUAWEBSOCKET  "WEBSOCKET"
#define LUAWSMESSAGE  "WSMESSAGE"

// ---- WebSocketType constants (mirror curl / evws opcodes) -------------------
#define WS_TYPE_TEXT   1
#define WS_TYPE_BINARY 2
#define WS_TYPE_CLOSE  8
#define WS_TYPE_PING   9
#define WS_TYPE_PONG   10

// ---- message queue node -----------------------------------------------------
typedef struct WsMsgNode {
    char*             data;    // NULL for empty payload (bare ping etc.)
    size_t            len;
    int               type;    // WS_TYPE_* constant
    struct WsMsgNode* next;
} WsMsgNode;

// ---- LuaWsMessage userdata --------------------------------------------------
// Pushed to Lua as a WSMESSAGE userdata.  __gc frees data.
typedef struct LuaWsMessage {
    char*  data;   // NULL when payload is empty
    size_t len;
    int    type;   // WS_TYPE_* constant
} LuaWsMessage;

// ---- forward declarations for platform types --------------------------------
typedef struct LuaHttpClient  LuaHttpClient;
typedef struct LuaHttpServer  LuaHttpServer;
typedef struct CurlMsg        CurlMsg;
struct event_base;
struct evws_connection;

// ---- LuaWebSocket userdata --------------------------------------------------
// Unified connection handle shared by both client (curl) and server (evws).
// Exactly one of {evws, easy} is non-NULL; the other is NULL.
typedef struct LuaWebSocket {
    // --- server side (evws non-NULL when this is a server connection) --------
    struct evws_connection* evws;
    struct event_base*      evbase;     // borrowed from LuaHttpServer; used to pump after close
    int                     server_ref; // registry ref keeping LuaHttpServer alive

    // --- client side (easy non-NULL when this is a client connection) --------
    CURL*               easy;
    CURLM*              multi;         // borrowed from the engine singleton
    struct curl_slist*  requestHdrs;   // owned; freed in ws_dispose
    char                errorBuf[256]; // CURL_ERROR_SIZE
    int                 client_ref;    // registry ref keeping LuaHttpClient alive
    CurlMsg*            curlMsg;       // heap-allocated; freed in ws_dispose

    // curl fragment reassembly: curl_ws_recv may deliver partial frames
    char*               fragBuf;
    size_t              fragLen;
    size_t              fragAlloc;

    // reusable heap buffer for curl_ws_recv (avoids 64 KB on the stack per call)
    char*               recvBuf;
    size_t              recvAlloc;

    // --- shared state --------------------------------------------------------
    bool        connected;     // false once closed or upgraded request is torn down
    bool        closed;        // true once Dispose() or GC has run
    size_t      maxMsgSize;    // 0 = uncapped; cap applied to both send and receive
    int         aliveTokenRef; // LUA_NOREF or registry ref to client's AliveToken (client WS only)
    WsMsgNode*  msg_head;      // oldest unread message (FIFO)
    WsMsgNode*  msg_tail;
    lua_State*  L;             // the Lua state; needed inside evws callbacks
    int         self_ref;      // registry ref to this userdata (server only, for callback routing)
    int         context_ref;   // LUA_NOREF until GetContext() first called
    lua_Integer id;            // stable monotonically-increasing ID assigned at creation
} LuaWebSocket;

// ---- module init ------------------------------------------------------------
int luaopen_websocket(lua_State* L);

// ---- client entry point (replaces old client_connect in HttpCurl.cpp) -------
int ws_client_connect(lua_State* L);

// ---- server entry point (called from HttpResponse_UpgradeToWebSocket) -------
int ws_server_upgrade(lua_State* L);

// ---- shared method implementations (registered via metatables) --------------
int WebSocket_IsConnected    (lua_State* L);
int WebSocket_Poll           (lua_State* L);
int WebSocket_Read           (lua_State* L);
int WebSocket_Send           (lua_State* L);
int WebSocket_Dispose        (lua_State* L);
int WebSocket_GetId          (lua_State* L);
int WebSocket_GetContext     (lua_State* L);
int WebSocket_SetMaxMessageSize(lua_State* L);
int WebSocket_GC             (lua_State* L);
int WebSocket_ToString       (lua_State* L);

// ---- WsMessage methods ------------------------------------------------------
int WsMessage_GetData        (lua_State* L);
int WsMessage_GetType        (lua_State* L);
int WsMessage_GC             (lua_State* L);
int WsMessage_ToString       (lua_State* L);

#endif // KITSUNE_HTTP
