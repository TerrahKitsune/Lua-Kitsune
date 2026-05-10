#ifdef KITSUNE_HTTP

#include "LuaTcpListener.h"
#include "mem.h"
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
static lua_Integer g_next_tcp_id = 1;

/* -- Forward declarations ---------------------------------------------------- */
static void tcp_client_dispose_native(lua_State* L, LuaTcpClient* client);

/* =============================================================================
   Message queue helpers
   ============================================================================= */

static TcpMsgNode* tcp_msg_alloc(const char* data, size_t len) {
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

static void tcp_msg_free(TcpMsgNode* node) {
    if (!node) return;
    kitsune_free(node->data);
    kitsune_free(node);
}

static void tcp_enqueue(LuaTcpClient* client, TcpMsgNode* node) {
    if (!node) return;
    if (client->msg_tail) {
        client->msg_tail->next = node;
        client->msg_tail = node;
    } else {
        client->msg_head = node;
        client->msg_tail = node;
    }
}

static TcpMsgNode* tcp_dequeue(LuaTcpClient* client) {
    TcpMsgNode* node = client->msg_head;
    if (!node) return NULL;
    client->msg_head = node->next;
    if (!client->msg_head)
        client->msg_tail = NULL;
    node->next = NULL;
    return node;
}

static void tcp_drain_queue(LuaTcpClient* client) {
    TcpMsgNode* node = client->msg_head;
    while (node) {
        TcpMsgNode* next = node->next;
        tcp_msg_free(node);
        node = next;
    }
    client->msg_head = NULL;
    client->msg_tail = NULL;
}

/* =============================================================================
   Push / check helpers
   ============================================================================= */

static LuaTcpListener* tcp_push_listener(lua_State* L) {
    LuaTcpListener* s = (LuaTcpListener*)lua_newuserdata(L, sizeof(LuaTcpListener));
    luaL_getmetatable(L, LUATCPLISTENER);
    lua_setmetatable(L, -2);
    memset(s, 0, sizeof(LuaTcpListener));
    s->self_ref = 0;
    s->context_ref = 0;
    return s;
}

static LuaTcpClient* tcp_push_client(lua_State* L) {
    LuaTcpClient* c = (LuaTcpClient*)lua_newuserdata(L, sizeof(LuaTcpClient));
    luaL_getmetatable(L, LUATCPCLIENT);
    lua_setmetatable(L, -2);
    memset(c, 0, sizeof(LuaTcpClient));
    c->self_ref = 0;
    c->context_ref = 0;
    c->id = g_next_tcp_id++;
    c->L = L;
    return c;
}

/* =============================================================================
   Bufferevent callbacks
   ============================================================================= */

static void tcp_read_cb(struct bufferevent* bev, void* ctx) {
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

        tcp_enqueue(client, node);
}

static void tcp_event_cb(struct bufferevent* bev, short what, void* ctx) {
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
            kitsune_free(client->error_msg);
            client->error_msg = kitsune_strdup(msg);
            if (!client->error_msg) client->error_msg = kitsune_strdup("unknown socket error");
        } else {
            client->error_msg = kitsune_strdup("unknown socket error");
        }
    } else if (what & BEV_EVENT_EOF) {
        /* Clean close — no error message needed */
        if (!client->error_msg)
            client->error_msg = NULL;
    }
}

/* =============================================================================
   Accept callback — called by libevent when a new connection arrives
   ============================================================================= */

static void tcp_accept_cb(struct evconnlistener* listener,
    evutil_socket_t fd, struct sockaddr* addr, int socklen, void* ctx)
{
    LuaTcpListener* s = (LuaTcpListener*)ctx;
    if (!s || s->disposed) {
        evutil_closesocket(fd);
        return;
    }
    lua_State* L = s->L;

    /* -- Create LuaTcpClient -- */
    LuaTcpClient* client = tcp_push_client(L);
    // Stack: [client_ud]

    /* Store a registry ref so the client survives until Accept() hands it out */
    lua_pushvalue(L, -1);
    client->self_ref = luaL_ref(L, LUA_REGISTRYINDEX);

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
    client->owns_base = false; /* borrowed from listener */

    bufferevent_setcb(bev, tcp_read_cb, NULL, tcp_event_cb, client);
    bufferevent_enable(bev, EV_READ | EV_WRITE);
    client->connected = true;

    /* Enqueue into the listener's pending queue */
    if (s->pending_count >= s->pending_cap) {
        int new_cap = s->pending_cap ? s->pending_cap * 2 : 8;
        LuaTcpClient** new_q = (LuaTcpClient**)kitsune_realloc(
            s->pending_queue, (size_t)new_cap * sizeof(LuaTcpClient*));
        if (!new_q) {
            /* Out of memory — dispose the client */
            tcp_client_dispose_native(L, client);
            lua_pop(L, 1);
            return;
        }
        s->pending_queue = new_q;
        s->pending_cap = new_cap;
    }
    s->pending_queue[s->pending_count++] = client;

    /* Pop client from Lua stack — it's kept alive by self_ref */
    lua_pop(L, 1);
}

/* =============================================================================
   Listener teardown
   ============================================================================= */

static void tcp_listener_dispose_native(lua_State* L, LuaTcpListener* s) {
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
    for (int i = 0; i < s->pending_count; i++) {
        LuaTcpClient* c = s->pending_queue[i];
        if (c) {
            tcp_client_dispose_native(L, c);
            if (c->self_ref > 0) {
                luaL_unref(L, LUA_REGISTRYINDEX, c->self_ref);
                c->self_ref = 0;
            }
        }
    }
    kitsune_free(s->pending_queue);
    s->pending_queue = NULL;
    s->pending_count = 0;
    s->pending_cap = 0;

    if (s->context_ref > 0) {
        luaL_unref(L, LUA_REGISTRYINDEX, s->context_ref);
        s->context_ref = 0;
    }
}

/* =============================================================================
   Client dispose
   ============================================================================= */

static void tcp_client_dispose_native(lua_State* L, LuaTcpClient* client) {
    if (client->disposed) return;
    client->disposed = true;
    client->connected = false;

    if (client->bev) {
        bufferevent_free(client->bev);
        client->bev = NULL;
    }
    /* Free evbase only if we own it (client-initiated connection) */
    if (client->owns_base && client->evbase) {
        event_base_free(client->evbase);
        client->evbase = NULL;
    }

    tcp_drain_queue(client);

    kitsune_free(client->ip);
    client->ip = NULL;
    kitsune_free(client->error_msg);
    client->error_msg = NULL;

    if (client->context_ref > 0) {
        luaL_unref(L, LUA_REGISTRYINDEX, client->context_ref);
        client->context_ref = 0;
    }
}

/* =============================================================================
   TCP module functions
   ============================================================================= */

int Tcp_StartListener(lua_State* L) {
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
        tcp_accept_cb, NULL,
        LEV_OPT_CLOSE_ON_FREE | LEV_OPT_REUSEABLE,
        128, (struct sockaddr*)&sin, sizeof(sin));

    if (!listener) {
        event_base_free(base);
        lua_pushnil(L);
        lua_pushfstring(L, "failed to bind to port %d", port);
        return 2;
    }

    LuaTcpListener* s = tcp_push_listener(L);
    s->base = base;
    s->listener = listener;
    s->L = L;
    s->port = port;

    evconnlistener_set_cb(listener, tcp_accept_cb, s);

    return 1;
}

int Tcp_Connect(lua_State* L) {
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
    char addr_str[256];
    snprintf(addr_str, sizeof(addr_str), "%s:%d", host, port);
    struct sockaddr_storage ss;
    int ss_len = sizeof(ss);
    if (evutil_parse_sockaddr_port(addr_str, (struct sockaddr*)&ss, &ss_len) == 0) {
        /* Use the resolved address */
        if (bufferevent_socket_connect(bev, (struct sockaddr*)&ss, ss_len) < 0) {
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
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", port);

        int gai_err = getaddrinfo(host, port_str, &hints, &ai);
        if (gai_err != 0 || !ai) {
            bufferevent_free(bev);
            event_base_free(base);
            lua_pushnil(L);
            lua_pushfstring(L, "failed to resolve '%s': %s", host,
                gai_strerror(gai_err));
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

    LuaTcpClient* client = tcp_push_client(L);
    client->bev = bev;
    client->evbase = base;
    client->owns_base = true; /* client-initiated connection owns its base */
    client->connected = false;
    client->ip = kitsune_strdup(host);
    client->port = port;

    bufferevent_setcb(bev, tcp_read_cb, NULL, tcp_event_cb, client);
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
    if (s->pending_count == 0) {
        lua_pushnil(L);
        lua_pushnil(L);
        return 2;
    }

    /* Dequeue the oldest pending client (FIFO) */
    LuaTcpClient* client = s->pending_queue[0];
    for (int i = 1; i < s->pending_count; i++)
        s->pending_queue[i - 1] = s->pending_queue[i];
    s->pending_count--;

    /* Push the client; release the registry self-ref (Lua now owns it via the stack) */
    lua_rawgeti(L, LUA_REGISTRYINDEX, client->self_ref);
    luaL_unref(L, LUA_REGISTRYINDEX, client->self_ref);
    client->self_ref = 0;
    return 1;
}

int TcpListener_Dispose(lua_State* L) {
    LuaTcpListener* s = (LuaTcpListener*)luaL_checkudata(L, 1, LUATCPLISTENER);
    tcp_listener_dispose_native(L, s);
    return 0;
}

int TcpListener_GetContext(lua_State* L) {
    LuaTcpListener* s = (LuaTcpListener*)luaL_checkudata(L, 1, LUATCPLISTENER);
    if (s->context_ref <= 0) {
        lua_newtable(L);
        lua_pushvalue(L, -1);
        s->context_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_rawgeti(L, LUA_REGISTRYINDEX, s->context_ref);
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

    tcp_listener_dispose_native(L, s);
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
    TcpMsgNode* node = tcp_dequeue(client);
    if (node) {
        lua_pushlstring(L, node->data, node->len);
        tcp_msg_free(node);
        return 1;  /* data, nil */
    }

    /* No data in queue */
    if (!client->connected) {
        if (client->error_msg) {
            lua_pushnil(L);
            lua_pushstring(L, client->error_msg);
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
        lua_pushstring(L, client->error_msg ? client->error_msg : "not connected");
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
    if (client->error_msg) {
        lua_pushstring(L, client->error_msg);
        return 2;
    }
    return 1;
}

int TcpClient_Dispose(lua_State* L) {
    LuaTcpClient* client = (LuaTcpClient*)luaL_checkudata(L, 1, LUATCPCLIENT);
    tcp_client_dispose_native(L, client);
    return 0;
}

int TcpClient_GetContext(lua_State* L) {
    LuaTcpClient* client = (LuaTcpClient*)luaL_checkudata(L, 1, LUATCPCLIENT);
    if (client->context_ref <= 0) {
        lua_newtable(L);
        lua_pushvalue(L, -1);
        client->context_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_rawgeti(L, LUA_REGISTRYINDEX, client->context_ref);
    }
    return 1;
}

int TcpClient_GC(lua_State* L) {
    LuaTcpClient* client = (LuaTcpClient*)luaL_checkudata(L, 1, LUATCPCLIENT);
    tcp_client_dispose_native(L, client);
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
