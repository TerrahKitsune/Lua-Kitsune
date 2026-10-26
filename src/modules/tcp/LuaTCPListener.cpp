#ifdef KITSUNE_HTTP

#include "tcp/LuaTCPListener.h"
#include "Mem.h"
#include <event2/event.h>
#include <event2/listener.h>
#include <event2/bufferevent.h>
#include <event2/buffer.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <netdb.h>
#endif

/* kitsune_strdup — like strdup but uses the tracked allocator */
static char* kitsune_strdup(const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* copy = (char*)kitsune_malloc(len);
    if (copy) memcpy(copy, s, len);
    return copy;
}

/* -- Global next-ID counter -------------------------------------------------- */
static lua_Integer g_nextTcpId = 1;

/* -- Forward declarations ---------------------------------------------------- */
static void TcpClientDisposeNative(lua_State* L, LuaTcpClient* client);

/* =============================================================================
   Message queue helpers
   ============================================================================= */

static TcpMsgNode* TcpMsgAlloc(const char* data, size_t len) {
    TcpMsgNode* node = (TcpMsgNode*)kitsune_malloc(sizeof(TcpMsgNode));
    if (!node) return NULL;
    node->data = (char*)kitsune_malloc(len);
    if (!node->data && len > 0) {
        kitsune_free(node);
        return NULL;
    }
    if (len > 0) memcpy(node->data, data, len);
    node->len = len;
    node->next = NULL;
    return node;
}

static void TcpMsgFree(TcpMsgNode* node) {
    if (!node) return;
    kitsune_free(node->data);
    kitsune_free(node);
}

static void TcpEnqueue(LuaTcpClient* client, TcpMsgNode* node) {
    if (!node) return;
    if (client->msgTail) {
        client->msgTail->next = node;
        client->msgTail = node;
    } else {
        client->msgHead = node;
        client->msgTail = node;
    }
}

static TcpMsgNode* TcpDequeue(LuaTcpClient* client) {
    TcpMsgNode* node = client->msgHead;
    if (!node) return NULL;
    client->msgHead = node->next;
    if (!client->msgHead)
        client->msgTail = NULL;
    node->next = NULL;
    return node;
}

static void TcpDrainQueue(LuaTcpClient* client) {
    TcpMsgNode* node = client->msgHead;
    while (node) {
        TcpMsgNode* next = node->next;
        TcpMsgFree(node);
        node = next;
    }
    client->msgHead = NULL;
    client->msgTail = NULL;
}

/* =============================================================================
   Push / check helpers
   ============================================================================= */

static LuaTcpListener* lua_pushtcplistener(lua_State* L) {
    LuaTcpListener* s = (LuaTcpListener*)lua_newuserdata(L, sizeof(LuaTcpListener));
    luaL_getmetatable(L, LUATCPLISTENER);
    lua_setmetatable(L, -2);
    memset(s, 0, sizeof(LuaTcpListener));
    s->selfRef = 0;
    s->contextRef = 0;
    return s;
}

static LuaTcpClient* lua_pushtcpclient(lua_State* L) {
    LuaTcpClient* c = (LuaTcpClient*)lua_newuserdata(L, sizeof(LuaTcpClient));
    luaL_getmetatable(L, LUATCPCLIENT);
    lua_setmetatable(L, -2);
    memset(c, 0, sizeof(LuaTcpClient));
    c->selfRef = 0;
    c->contextRef = 0;
    c->id = g_nextTcpId++;
    c->L = L;
    return c;
}

/* =============================================================================
   Bufferevent callbacks
   ============================================================================= */

static void TcpReadCb(struct bufferevent* bev, void* ctx) {
    LuaTcpClient* client = (LuaTcpClient*)ctx;
    if (!client || client->disposed) return;

    struct evbuffer* input = bufferevent_get_input(bev);
    size_t len = evbuffer_get_length(input);
    if (len == 0) return;

    /* Allocate the message node and its data buffer in one go */
    TcpMsgNode* node = (TcpMsgNode*)kitsune_malloc(sizeof(TcpMsgNode));
    if (!node) return;
    node->data = (char*)kitsune_malloc(len);
    if (!node->data) {
        kitsune_free(node);
        return;
    }
    evbuffer_remove(input, node->data, len);
    node->len = len;
    node->next = NULL;

        TcpEnqueue(client, node);
}

static void TcpEventCb(struct bufferevent* bev, short what, void* ctx) {
    LuaTcpClient* client = (LuaTcpClient*)ctx;
    if (!client || client->disposed) return;

    if (what & BEV_EVENT_CONNECTED) {
        client->connected = true;
        return;
    }

    /* Disconnected / error */
    client->connected = false;

    if (what & BEV_EVENT_ERROR) {
        int err = EVUTIL_SOCKET_ERROR();
        const char* msg = evutil_socket_error_to_string(err);
        if (msg) {
            kitsune_free(client->errorMsg);
            client->errorMsg = kitsune_strdup(msg);
            if (!client->errorMsg) client->errorMsg = kitsune_strdup("unknown socket error");
        } else {
            client->errorMsg = kitsune_strdup("unknown socket error");
        }
    } else if (what & BEV_EVENT_EOF) {
        /* Clean close — no error message needed */
        if (!client->errorMsg)
            client->errorMsg = NULL;
    }
}

/* =============================================================================
   Accept callback — called by libevent when a new connection arrives
   ============================================================================= */

static void TcpAcceptCb(struct evconnlistener* listener,
    evutil_socket_t fd, struct sockaddr* addr, int socklen, void* ctx)
{
    LuaTcpListener* s = (LuaTcpListener*)ctx;
    if (!s || s->disposed) {
        evutil_closesocket(fd);
        return;
    }
    lua_State* L = s->L;

    /* -- Create LuaTcpClient -- */
    LuaTcpClient* client = lua_pushtcpclient(L);
    // Stack: [client_ud]

    /* Store a registry ref so the client survives until Accept() hands it out */
    lua_pushvalue(L, -1);
    client->selfRef = luaL_ref(L, LUA_REGISTRYINDEX);

    /* Extract remote IP / port */
    if (addr && addr->sa_family == AF_INET) {
        struct sockaddr_in* sin = (struct sockaddr_in*)addr;
        char ipbuf[64];
        evutil_inet_ntop(AF_INET, &sin->sin_addr, ipbuf, sizeof(ipbuf));
        client->ip = kitsune_strdup(ipbuf);
        client->port = ntohs(sin->sin_port);
    } else if (addr && addr->sa_family == AF_INET6) {
        struct sockaddr_in6* sin6 = (struct sockaddr_in6*)addr;
        char ipbuf[64];
        evutil_inet_ntop(AF_INET6, &sin6->sin6_addr, ipbuf, sizeof(ipbuf));
        client->ip = kitsune_strdup(ipbuf);
        client->port = ntohs(sin6->sin6_port);
    } else {
        client->ip = kitsune_strdup("unknown");
        client->port = 0;
    }

    /* Create bufferevent on the listener's event base */
    struct bufferevent* bev = bufferevent_socket_new(s->base, fd,
        BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
    if (!bev) {
        evutil_closesocket(fd);
        lua_pop(L, 1); /* pop client */
        return;
    }
    client->bev = bev;
    client->evbase = s->base;
    client->ownsBase = false; /* borrowed from listener */

    bufferevent_setcb(bev, TcpReadCb, NULL, TcpEventCb, client);
    bufferevent_enable(bev, EV_READ | EV_WRITE);
    client->connected = true;

    /* Enqueue into the listener's pending queue */
    if (s->pendingCount >= s->pendingCap) {
        int newCap = s->pendingCap ? s->pendingCap * 2 : 8;
        LuaTcpClient** newQ = (LuaTcpClient**)kitsune_realloc(
            s->pendingQueue, (size_t)newCap * sizeof(LuaTcpClient*));
        if (!newQ) {
            /* Out of memory — dispose the client */
            TcpClientDisposeNative(L, client);
            lua_pop(L, 1);
            return;
        }
        s->pendingQueue = newQ;
        s->pendingCap = newCap;
    }
    s->pendingQueue[s->pendingCount++] = client;

    /* Pop client from Lua stack — it's kept alive by selfRef */
    lua_pop(L, 1);
}

/* =============================================================================
   Listener teardown
   ============================================================================= */

static void TcpListenerDisposeNative(lua_State* L, LuaTcpListener* s) {
    if (s->disposed) return;
    s->disposed = true;

    /* Free the listener (stops accepting) */
    if (s->listener) {
        evconnlistener_free(s->listener);
        s->listener = NULL;
    }

    /* Don't free the event_base here — accepted clients may still be alive
       and share it. The base is freed in __gc when the listener is fully
       torn down and all clients should have been disposed by then. */

    /* Free pending queue */
    for (int i = 0; i < s->pendingCount; i++) {
        LuaTcpClient* c = s->pendingQueue[i];
        if (c) {
            TcpClientDisposeNative(L, c);
            if (c->selfRef > 0) {
                luaL_unref(L, LUA_REGISTRYINDEX, c->selfRef);
                c->selfRef = 0;
            }
        }
    }
    kitsune_free(s->pendingQueue);
    s->pendingQueue = NULL;
    s->pendingCount = 0;
    s->pendingCap = 0;

    if (s->contextRef > 0) {
        luaL_unref(L, LUA_REGISTRYINDEX, s->contextRef);
        s->contextRef = 0;
    }
}

/* =============================================================================
   Client dispose
   ============================================================================= */

static void TcpClientDisposeNative(lua_State* L, LuaTcpClient* client) {
    if (client->disposed) return;
    client->disposed = true;
    client->connected = false;

    if (client->bev) {
        bufferevent_free(client->bev);
        client->bev = NULL;
    }
    /* Free evbase only if we own it (client-initiated connection) */
    if (client->ownsBase && client->evbase) {
        event_base_free(client->evbase);
        client->evbase = NULL;
    }

    TcpDrainQueue(client);

    kitsune_free(client->ip);
    client->ip = NULL;
    kitsune_free(client->errorMsg);
    client->errorMsg = NULL;

    if (client->contextRef > 0) {
        luaL_unref(L, LUA_REGISTRYINDEX, client->contextRef);
        client->contextRef = 0;
    }
}

/* =============================================================================
   TCP module functions
   ============================================================================= */

int TCP_StartListener(lua_State* L) {
    int port = (int)luaL_checkinteger(L, 1);
    if (port <= 0 || port > 65535) {
        lua_pushnil(L);
        lua_pushfstring(L, "invalid port %d", port);
        return 2;
    }

    struct event_base* base = event_base_new();
    if (!base) {
        lua_pushnil(L);
        lua_pushstring(L, "failed to create event base");
        return 2;
    }

    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = htons((ev_uint16_t)port);

    struct evconnlistener* listener = evconnlistener_new_bind(base,
        TcpAcceptCb, NULL,
        LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
        128, (struct sockaddr*)&sin, sizeof(sin));

    if (!listener) {
        event_base_free(base);
        lua_pushnil(L);
        lua_pushfstring(L, "failed to bind to port %d", port);
        return 2;
    }

    LuaTcpListener* s = lua_pushtcplistener(L);
    s->base = base;
    s->listener = listener;
    s->L = L;
    s->port = port;

    evconnlistener_set_cb(listener, TcpAcceptCb, s);

    return 1;
}

int TCP_Connect(lua_State* L) {
    const char* host = luaL_checkstring(L, 1);
    int port = (int)luaL_checkinteger(L, 2);
    if (port <= 0 || port > 65535) {
        lua_pushnil(L);
        lua_pushfstring(L, "invalid port %d", port);
        return 2;
    }

    struct event_base* base = event_base_new();
    if (!base) {
        lua_pushnil(L);
        lua_pushstring(L, "failed to create event base");
        return 2;
    }

    struct bufferevent* bev = bufferevent_socket_new(base, -1,
        BEV_OPT_CLOSE_ON_FREE | BEV_OPT_DEFER_CALLBACKS);
    if (!bev) {
        event_base_free(base);
        lua_pushnil(L);
        lua_pushstring(L, "failed to create bufferevent");
        return 2;
    }

    /* Resolve address */
    struct sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons((ev_uint16_t)port);

    int resolved = 0;

    /* Try evutil_parse_sockaddr_port first (handles "1.2.3.4:port") */
    char addrStr[256];
    snprintf(addrStr, sizeof(addrStr), "%s:%d", host, port);
    struct sockaddr_storage ss;
    int ssLen = sizeof(ss);
    if (evutil_parse_sockaddr_port(addrStr, (struct sockaddr*)&ss, &ssLen) == 0) {
        /* Use the resolved address */
        if (bufferevent_socket_connect(bev, (struct sockaddr*)&ss, ssLen) < 0) {
            bufferevent_free(bev);
            event_base_free(base);
            lua_pushnil(L);
            lua_pushfstring(L, "failed to connect to %s:%d", host, port);
            return 2;
        }
        resolved = 1;
    }

    if (!resolved) {
        /* Try getaddrinfo for hostname resolution */
        struct addrinfo hints, *ai = NULL;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        char portStr[16];
        snprintf(portStr, sizeof(portStr), "%d", port);

        int gaiErr = getaddrinfo(host, portStr, &hints, &ai);
        if (gaiErr != 0 || !ai) {
            bufferevent_free(bev);
            event_base_free(base);
            lua_pushnil(L);
            lua_pushfstring(L, "failed to resolve '%s': %s", host,
                gai_strerror(gaiErr));
            return 2;
        }

        if (bufferevent_socket_connect(bev, ai->ai_addr, (int)ai->ai_addrlen) < 0) {
            freeaddrinfo(ai);
            bufferevent_free(bev);
            event_base_free(base);
            lua_pushnil(L);
            lua_pushfstring(L, "failed to connect to %s:%d", host, port);
            return 2;
        }
        freeaddrinfo(ai);
    }

    LuaTcpClient* client = lua_pushtcpclient(L);
    client->bev = bev;
    client->evbase = base;
    client->ownsBase = true; /* client-initiated connection owns its base */
    client->connected = false;
    client->ip = kitsune_strdup(host);
    client->port = port;

    bufferevent_setcb(bev, TcpReadCb, NULL, TcpEventCb, client);
    bufferevent_enable(bev, EV_READ | EV_WRITE);

    /* Pump once to kick off the non-blocking connect */
    event_base_loop(base, EVLOOP_NONBLOCK);

    return 1;
}

/* =============================================================================
   Listener methods
   ============================================================================= */

/* -- Accept coroutine continuation ------------------------------------------ */

int TcpListener_Accept(lua_State* L) {
    LuaTcpListener* s = (LuaTcpListener*)luaL_checkudata(L, 1, LUATCPLISTENER);

    if (s->disposed) {
        lua_pushnil(L);
        lua_pushstring(L, "listener closed");
        return 2;
    }

    /* Pump libevent so the accept callback fires for any pending connections */
    if (s->base) {
        s->L = L;
        event_base_loop(s->base, EVLOOP_NONBLOCK);
    }

    /* No client waiting — return nil, nil */
    if (s->pendingCount == 0) {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }

    /* Dequeue the oldest pending client (FIFO) */
    LuaTcpClient* client = s->pendingQueue[0];
    for (int i = 1; i < s->pendingCount; i++)
        s->pendingQueue[i - 1] = s->pendingQueue[i];
    s->pendingCount--;

    /* Push the client; release the registry self-ref (Lua now owns it via the stack) */
    lua_rawgeti(L, LUA_REGISTRYINDEX, client->selfRef);
    luaL_unref(L, LUA_REGISTRYINDEX, client->selfRef);
    client->selfRef = 0;
    return 1;
}

int TcpListener_Dispose(lua_State* L) {
    LuaTcpListener* s = (LuaTcpListener*)luaL_checkudata(L, 1, LUATCPLISTENER);
    TcpListenerDisposeNative(L, s);
    return 0;
}

int TcpListener_GetContext(lua_State* L) {
    LuaTcpListener* s = (LuaTcpListener*)luaL_checkudata(L, 1, LUATCPLISTENER);
    if (s->contextRef <= 0) {
        lua_newtable(L);
        lua_pushvalue(L, -1);
        s->contextRef = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_rawgeti(L, LUA_REGISTRYINDEX, s->contextRef);
    }
    return 1;
}

int TcpListener_GC(lua_State* L) {
    LuaTcpListener* s = (LuaTcpListener*)luaL_checkudata(L, 1, LUATCPLISTENER);

    /* Free the event base owned by this listener. By the time __gc runs,
       the user should have disposed all accepted clients. If some clients
       survive, their evbase pointer dangles — the user is responsible for
       disposing clients before the listener. */
    if (s->base) {
        event_base_free(s->base);
        s->base = NULL;
    }

    TcpListenerDisposeNative(L, s);
    return 0;
}

int TcpListener_ToString(lua_State* L) {
    LuaTcpListener* s = (LuaTcpListener*)luaL_checkudata(L, 1, LUATCPLISTENER);
    if (!s->disposed && s->listener)
        lua_pushfstring(L, "TcpListener(:%d)", s->port);
    else
        lua_pushfstring(L, "TcpListener(closed)");
    return 1;
}

/* =============================================================================
   Client methods
   ============================================================================= */

int TcpClient_Poll(lua_State* L) {
    LuaTcpClient* client = (LuaTcpClient*)luaL_checkudata(L, 1, LUATCPCLIENT);

    if (client->disposed) {
        lua_pushnil(L);
        lua_pushstring(L, "client disposed");
        return 2;
    }

    /* Pump events to fill the message queue */
    if (client->evbase)
        event_base_loop(client->evbase, EVLOOP_NONBLOCK);

    /* Dequeue one message */
    TcpMsgNode* node = TcpDequeue(client);
    if (node) {
        lua_pushlstring(L, node->data, node->len);
        TcpMsgFree(node);
        return 1;  /* data, nil */
    }

    /* No data in queue */
    if (!client->connected) {
        if (client->errorMsg) {
            lua_pushnil(L);
            lua_pushstring(L, client->errorMsg);
            return 2;
        }
        /* Not connected, no error — clean close */
        lua_pushnil(L);
        lua_pushstring(L, "closed");
        return 2;
    }

    /* Connected but no data */
    lua_pushliteral(L, "");
    return 1;
}

int TcpClient_Send(lua_State* L) {
    LuaTcpClient* client = (LuaTcpClient*)luaL_checkudata(L, 1, LUATCPCLIENT);
    size_t len;
    const char* data = luaL_checklstring(L, 2, &len);

    if (client->disposed) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, "client disposed");
        return 2;
    }

    /* Pump events to flush any pending disconnect */
    if (client->evbase)
        event_base_loop(client->evbase, EVLOOP_NONBLOCK);

    if (!client->connected) {
        lua_pushboolean(L, 0);
        lua_pushstring(L, client->errorMsg ? client->errorMsg : "not connected");
        return 2;
    }

    if (client->bev) {
        bufferevent_write(client->bev, data, len);
        lua_pushboolean(L, 1);
        return 1;
    }

    lua_pushboolean(L, 0);
    lua_pushstring(L, "no connection");
    return 2;
}

int TcpClient_GetIP(lua_State* L) {
    LuaTcpClient* client = (LuaTcpClient*)luaL_checkudata(L, 1, LUATCPCLIENT);
    if (client->ip)
        lua_pushstring(L, client->ip);
    else
        lua_pushliteral(L, "unknown");
    return 1;
}

int TcpClient_GetPort(lua_State* L) {
    LuaTcpClient* client = (LuaTcpClient*)luaL_checkudata(L, 1, LUATCPCLIENT);
    lua_pushinteger(L, client->port);
    return 1;
}

int TcpClient_IsConnected(lua_State* L) {
    LuaTcpClient* client = (LuaTcpClient*)luaL_checkudata(L, 1, LUATCPCLIENT);

    /* Pump events so connect/disconnect callbacks fire */
    if (client->evbase)
        event_base_loop(client->evbase, EVLOOP_NONBLOCK);

    if (client->connected) {
        lua_pushboolean(L, 1);
        return 1;
    }

    lua_pushboolean(L, 0);
    if (client->errorMsg) {
        lua_pushstring(L, client->errorMsg);
        return 2;
    }
    return 1;
}

int TcpClient_Dispose(lua_State* L) {
    LuaTcpClient* client = (LuaTcpClient*)luaL_checkudata(L, 1, LUATCPCLIENT);
    TcpClientDisposeNative(L, client);
    return 0;
}

int TcpClient_GetContext(lua_State* L) {
    LuaTcpClient* client = (LuaTcpClient*)luaL_checkudata(L, 1, LUATCPCLIENT);
    if (client->contextRef <= 0) {
        lua_newtable(L);
        lua_pushvalue(L, -1);
        client->contextRef = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_rawgeti(L, LUA_REGISTRYINDEX, client->contextRef);
    }
    return 1;
}

int TcpClient_GC(lua_State* L) {
    LuaTcpClient* client = (LuaTcpClient*)luaL_checkudata(L, 1, LUATCPCLIENT);
    TcpClientDisposeNative(L, client);
    return 0;
}

int TcpClient_ToString(lua_State* L) {
    LuaTcpClient* client = (LuaTcpClient*)luaL_checkudata(L, 1, LUATCPCLIENT);
    const char* ip = client->ip ? client->ip : "?";
    const char* state = client->disposed ? "disposed" :
                        client->connected ? "connected" : "closed";
    lua_pushfstring(L, "TcpClient(%s:%d, %s)", ip, client->port, state);
    return 1;
}

#endif /* KITSUNE_HTTP */
