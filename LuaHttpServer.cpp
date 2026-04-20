#include "LuaHttpServer.h"
#include "LuaHttpRequest.h"
#include "stream.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ── defensive helpers ────────────────────────────────────────────────────── */

static void safe_free_s(char** p) {
    if (*p) {
        free(*p);
        *p = NULL;
    }
}

/* ── node release ─────────────────────────────────────────────────────────── */

/* Tears down one HttpOpenConnection node by walking the ownership chain
   in the same order it was built:
     1. Push the LuaHttpRequest userdata from request_ref.
     2. Call HttpRequest_GC — which in turn calls HttpResponse_GC.
     3. Unref request_ref (so Lua's GC can collect the userdata).
     4. Free the node.
   Idempotent: if request_ref is already LUA_NOREF the call is a no-op. */
static void release_node(lua_State* L, HttpOpenConnection* node) {
    if (node->request_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, node->request_ref);
        LuaHttpRequest* req = (LuaHttpRequest*)lua_touserdata(L, -1);
        lua_pop(L, 1);
        if (req)
            HttpRequest_Cleanup(L, req);
        luaL_unref(L, LUA_REGISTRYINDEX, node->request_ref);
        node->request_ref = LUA_NOREF;
    }
    free(node);
}

/* ── push / check ─────────────────────────────────────────────────────────── */

LuaHttpServer* lua_pushhttpserver(lua_State* L) {
    LuaHttpServer* s = (LuaHttpServer*)lua_newuserdata(L, sizeof(LuaHttpServer));
    luaL_getmetatable(L, LUAHTTPSERVER);
    lua_setmetatable(L, -2);
    memset(s, 0, sizeof(LuaHttpServer));
    s->coroutine_ref  = LUA_NOREF;
    s->disconnect_ref = LUA_NOREF;
    return s;
}

LuaHttpServer* lua_checkhttpserver(lua_State* L, int idx) {
    LuaHttpServer* s = (LuaHttpServer*)luaL_checkudata(L, idx, LUAHTTPSERVER);
    if (!s)
        luaL_error(L, "parameter is not a %s", LUAHTTPSERVER);
    return s;
}

/* ── per-connection cleanup ───────────────────────────────────────────────── */

static void queue_remove_conn(LuaHttpServer* s, lua_State* L, struct mg_connection* conn) {
    HttpOpenConnection* prev = NULL;
    HttpOpenConnection* cur  = s->queue_head;
    while (cur) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cur->request_ref);
        LuaHttpRequest* req = (LuaHttpRequest*)lua_touserdata(L, -1);
        lua_pop(L, 1);
        HttpOpenConnection* next = cur->next;
        if (req && req->conn == conn) {
            if (prev)
                prev->next = next;
            else
                s->queue_head = next;
            if (s->queue_tail == cur)
                s->queue_tail = prev;
            release_node(L, cur);
        } else {
            prev = cur;
        }
        cur = next;
    }
}

static void senders_remove_conn(LuaHttpServer* s, lua_State* L, struct mg_connection* conn,
                                LuaHttpResponse* resp) {
    HttpOpenConnection* prev = NULL;
    HttpOpenConnection* cur  = s->senders;
    while (cur) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, cur->request_ref);
        LuaHttpRequest* req = (LuaHttpRequest*)lua_touserdata(L, -1);
        lua_pop(L, 1);
        HttpOpenConnection* next = cur->next;
        if (req && req->conn == conn) {
            if (prev)
                prev->next = next;
            else
                s->senders = next;
            if (resp)
                resp->stream = NULL;
            release_node(L, cur);
            break;
        } else {
            prev = cur;
        }
        cur = next;
    }
}

/* Full teardown for one connection on close/error. */
static void conn_close_cleanup(LuaHttpServer* s, lua_State* L,
                               struct mg_connection* conn, const char* errmsg) {
    /* 1. Remove stale queue and sender nodes */
    queue_remove_conn(s, L, conn);

    lua_pushlightuserdata(L, (void*)conn);
    lua_rawget(L, LUA_REGISTRYINDEX);
    LuaHttpRequest*  req  = (LuaHttpRequest*)lua_touserdata(L, -1);
    lua_pop(L, 1);

    LuaHttpResponse* resp = NULL;
    if (req && req->response_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, req->response_ref);
        resp = (LuaHttpResponse*)lua_touserdata(L, -1);
        lua_pop(L, 1);
    }

    senders_remove_conn(s, L, conn, resp);

    /* 2. Mark finalized */
    if (resp)
        resp->finalized = true;

    /* 3. Disconnect handler or error event */
    if (req) {
        if (errmsg) {
            safe_free_s(&req->error_msg);
            req->error_msg = _strdup(errmsg);
            req->is_error  = true;
        }

        if (s->disconnect_ref != LUA_NOREF) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, s->disconnect_ref);
            lua_pushlightuserdata(L, (void*)conn);
            lua_rawget(L, LUA_REGISTRYINDEX);
            lua_pcall_nohook(L, 1, 0, 0);
        } else if (req->is_error) {
            /* Enqueue a final event so GetError() surfaces it */
            lua_pushlightuserdata(L, (void*)conn);
            lua_rawget(L, LUA_REGISTRYINDEX);
            int ref = luaL_ref(L, LUA_REGISTRYINDEX);
            HttpOpenConnection* node = (HttpOpenConnection*)malloc(sizeof(HttpOpenConnection));
            if (node) {
                node->request_ref = ref;
                node->next        = NULL;
                if (s->queue_tail)
                    s->queue_tail->next = node;
                else
                    s->queue_head = node;
                s->queue_tail = node;
            } else {
                luaL_unref(L, LUA_REGISTRYINDEX, ref);
            }
        }
    }

    /* 4. Release registry entry — triggers __gc chain */
    lua_pushlightuserdata(L, (void*)conn);
    lua_pushnil(L);
    lua_rawset(L, LUA_REGISTRYINDEX);
}

/* ── Mongoose event handler ───────────────────────────────────────────────── */

static void http_event_handler(struct mg_connection* conn, int ev, void* ev_data) {
    LuaHttpServer* s = (LuaHttpServer*)conn->fn_data;
    if (!s)
        return;
    lua_State* L = (lua_State*)s->mgr.userdata;
    if (!L)
        return;

    if (ev == MG_EV_HTTP_MSG) {
        struct mg_http_message* hm = (struct mg_http_message*)ev_data;

        /* Look up or create the LuaHttpRequest for this connection */
        lua_pushlightuserdata(L, (void*)conn);
        lua_rawget(L, LUA_REGISTRYINDEX);
        LuaHttpRequest* req = (LuaHttpRequest*)lua_touserdata(L, -1);
        lua_pop(L, 1);

        if (!req) {
            /* First event for this connection — create request + response pair */
            lua_pushhttprequest(L);
            req = (LuaHttpRequest*)lua_touserdata(L, -1);
            req->conn = conn;

            /* Store in registry keyed by conn* */
            lua_pushlightuserdata(L, (void*)conn);
            lua_pushvalue(L, -2);
            lua_rawset(L, LUA_REGISTRYINDEX);
            lua_pop(L, 1);

            /* Create and link response */
            lua_pushhttpresponse(L);
            LuaHttpResponse* resp = (LuaHttpResponse*)lua_touserdata(L, -1);
            resp->connection = req;
            req->response_ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }

        /* Update request fields in-place */
        safe_free_s(&req->url);
        if (hm->uri.buf && hm->uri.len > 0) {
            req->url = (char*)malloc(hm->uri.len + hm->query.len + 2);
            if (req->url) {
                memcpy(req->url, hm->uri.buf, hm->uri.len);
                if (hm->query.len > 0) {
                    req->url[hm->uri.len] = '?';
                    memcpy(req->url + hm->uri.len + 1, hm->query.buf, hm->query.len);
                    req->url[hm->uri.len + 1 + hm->query.len] = '\0';
                } else {
                    req->url[hm->uri.len] = '\0';
                }
            }
        }

        safe_free_s(&req->method);
        if (hm->method.buf && hm->method.len > 0) {
            req->method = (char*)malloc(hm->method.len + 1);
            if (req->method) {
                memcpy(req->method, hm->method.buf, hm->method.len);
                req->method[hm->method.len] = '\0';
            }
        }

        safe_free_s(&req->body);
        req->body_len = 0;
        if (hm->body.buf && hm->body.len > 0) {
            req->body = (char*)malloc(hm->body.len);
            if (req->body) {
                memcpy(req->body, hm->body.buf, hm->body.len);
                req->body_len = hm->body.len;
            }
        }

        req->is_finished = true;
        req->is_error    = false;
        safe_free_s(&req->error_msg);

        /* Parse headers into a Lua table */
        if (req->headers_ref != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, req->headers_ref);
            req->headers_ref = LUA_NOREF;
        }
        lua_newtable(L);
        for (int i = 0; i < (int)(sizeof(hm->headers) / sizeof(hm->headers[0])); i++) {
            if (!hm->headers[i].name.buf || hm->headers[i].name.len == 0)
                break;
            /* lowercase name */
            char name[256];
            size_t nlen = hm->headers[i].name.len < sizeof(name) - 1
                ? hm->headers[i].name.len : sizeof(name) - 1;
            for (size_t k = 0; k < nlen; k++)
                name[k] = (char)tolower((unsigned char)hm->headers[i].name.buf[k]);
            name[nlen] = '\0';
            lua_pushlstring(L, name, nlen);
            lua_pushlstring(L, hm->headers[i].value.buf, hm->headers[i].value.len);
            lua_rawset(L, -3);
        }
        req->headers_ref = luaL_ref(L, LUA_REGISTRYINDEX);

        /* Enqueue */
        lua_pushlightuserdata(L, (void*)conn);
        lua_rawget(L, LUA_REGISTRYINDEX);
        int ref = luaL_ref(L, LUA_REGISTRYINDEX);

        HttpOpenConnection* node = (HttpOpenConnection*)malloc(sizeof(HttpOpenConnection));
        if (node) {
            node->request_ref = ref;
            node->next        = NULL;
            if (s->queue_tail)
                s->queue_tail->next = node;
            else
                s->queue_head = node;
            s->queue_tail = node;
        } else {
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
        }

    } else if (ev == MG_EV_ERROR) {
        const char* errmsg = ev_data ? (const char*)ev_data : "network error";
        conn_close_cleanup(s, L, conn, errmsg);

    } else if (ev == MG_EV_CLOSE) {
        conn_close_cleanup(s, L, conn, NULL);
    }
}

/* ── server teardown — idempotent, shared by Close/__gc/stop-flag ─────────── */

static void server_teardown(LuaHttpServer* s, lua_State* L) {
    if (!s->mgr_init)
        return;
    s->mgr_init = false;

    /* Only null fn_data on the listening connection — accepted connections
       still need it so MG_EV_CLOSE fires their conn_close_cleanup normally. */
    for (struct mg_connection* c = s->mgr.conns; c != NULL; c = c->next) {
        if (c->is_listening)
            c->fn_data = NULL;
    }

    /* mg_mgr_free fires MG_EV_CLOSE for every accepted connection — their
       conn_close_cleanup removes and releases any nodes for those connections. */
    mg_mgr_free(&s->mgr);

    /* Drain any nodes that weren't cleaned up by MG_EV_CLOSE (edge cases).
       release_node calls HttpRequest_GC -> HttpResponse_GC so Lua owns cleanup. */
    for (HttpOpenConnection* cur = s->queue_head; cur; ) {
        HttpOpenConnection* next = cur->next;
        release_node(L, cur);
        cur = next;
    }
    s->queue_head = NULL;
    s->queue_tail = NULL;

    for (HttpOpenConnection* cur = s->senders; cur; ) {
        HttpOpenConnection* next = cur->next;
        release_node(L, cur);
        cur = next;
    }
    s->senders = NULL;

    /* Clear coroutine + server anchor */
    if (s->coroutine_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, s->coroutine_ref);
        lua_State* co = lua_tothread(L, -1);
        lua_pop(L, 1);
        if (co) {
            lua_pushlightuserdata(L, (void*)co);
            lua_pushnil(L);
            lua_rawset(L, LUA_REGISTRYINDEX);
        }
        luaL_unref(L, LUA_REGISTRYINDEX, s->coroutine_ref);
        s->coroutine_ref = LUA_NOREF;
    }

    if (s->disconnect_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, s->disconnect_ref);
        s->disconnect_ref = LUA_NOREF;
    }
}

/* ── coroutine continuation ───────────────────────────────────────────────── */

static int accept_cont(lua_State* L, int status, lua_KContext ctx) {
    LuaHttpServer* s = (LuaHttpServer*)ctx;

    /* Stop flag passed as first resume arg */
    if (lua_toboolean(L, 1)) {
        server_teardown(s, L);
        return 0;
    }

    /* Step 1 — advance active stream senders */
    HttpOpenConnection* prev   = NULL;
    HttpOpenConnection* sender = s->senders;
    while (sender) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, sender->request_ref);
        LuaHttpRequest* req = (LuaHttpRequest*)lua_touserdata(L, -1);
        lua_pop(L, 1);

        HttpOpenConnection* next = sender->next;
        bool remove = false;

        if (req && req->response_ref != LUA_NOREF) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, req->response_ref);
            LuaHttpResponse* resp = (LuaHttpResponse*)lua_touserdata(L, -1);
            lua_pop(L, 1);

            if (resp && resp->stream && req->conn) {
                lua_stream_read_chunk(L, resp->stream, 65536);
                const char* chunk = lua_tostring(L, -1);
                size_t      clen  = chunk ? lua_rawlen(L, -1) : 0;
                lua_pop(L, 1);

                if (chunk && clen > 0) {
                    if (resp->chunked) {
                        mg_http_write_chunk(req->conn, chunk, clen);
                    } else {
                        mg_send(req->conn, chunk, clen);
                    }
                } else {
                    /* EOF */
                    if (resp->chunked)
                        mg_http_write_chunk(req->conn, NULL, 0);
                    remove = true;
                }
            } else {
                remove = true;
            }
        } else {
            remove = true;
        }

        if (remove) {
            if (prev)
                prev->next = next;
            else
                s->senders = next;
            release_node(L, sender);
        } else {
            prev = sender;
        }
        sender = next;
    }

    /* Step 2 — poll */
    mg_mgr_poll(&s->mgr, 0);

    /* Step 3 — dispatch one queued event */
    if (s->queue_head) {
        HttpOpenConnection* node = s->queue_head;
        s->queue_head = node->next;
        if (!s->queue_head)
            s->queue_tail = NULL;
        /* Push the userdata before releasing the node's ref.
           We do NOT call HttpRequest_GC here — the request is being handed
           to Lua which then becomes the owner and its __gc will clean it up. */
        lua_rawgeti(L, LUA_REGISTRYINDEX, node->request_ref);
        luaL_unref(L, LUA_REGISTRYINDEX, node->request_ref);
        node->request_ref = LUA_NOREF;
        free(node);
        return lua_yieldk(L, 1, ctx, accept_cont);
    }

    return lua_yieldk(L, 0, ctx, accept_cont);
}

static int accept_body(lua_State* L) {
    LuaHttpServer* s = lua_checkhttpserver(L, lua_upvalueindex(1));
    return accept_cont(L, LUA_OK, (lua_KContext)s);
}

/* ── HttpServer.Listen ────────────────────────────────────────────────────── */

int HttpServer_Listen(lua_State* L) {
    const char* addr = luaL_checkstring(L, 1);

    LuaHttpServer* s = lua_pushhttpserver(L);

    mg_mgr_init(&s->mgr);
    s->mgr.userdata = L;
    s->mgr_init     = true;

    /* TLS options */
    struct mg_tls_opts tls_opts;
    memset(&tls_opts, 0, sizeof(tls_opts));
    bool use_tls = false;

    if (lua_istable(L, 2)) {
        lua_getfield(L, 2, "cert");
        if (!lua_isnil(L, -1))
            tls_opts.cert = mg_str(lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 2, "key");
        if (!lua_isnil(L, -1))
            tls_opts.key = mg_str(lua_tostring(L, -1));
        lua_pop(L, 1);
        lua_getfield(L, 2, "ca");
        if (!lua_isnil(L, -1))
            tls_opts.ca = mg_str(lua_tostring(L, -1));
        lua_pop(L, 1);
        use_tls = true;
    }

    struct mg_connection* conn = mg_http_listen(&s->mgr, addr, http_event_handler, s);
    if (!conn) {
        server_teardown(s, L);
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushfstring(L, "HttpServer: failed to bind '%s'", addr);
        return 2;
    }

    if (use_tls) {
        mg_tls_init(conn, &tls_opts);
    }

    return 1;
}

/* ── server:Accept ────────────────────────────────────────────────────────── */

int HttpServer_Accept(lua_State* L) {
    LuaHttpServer* s = lua_checkhttpserver(L, 1);

    if (s->coroutine_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, s->coroutine_ref);
        return 1;
    }

    /* Create coroutine wrapping accept_body (closure over server userdata) */
    lua_pushvalue(L, 1); /* server userdata as upvalue */
    lua_pushcclosure(L, accept_body, 1);
    lua_State* co = lua_newthread(L);
    lua_insert(L, -2);
    lua_xmove(L, co, 1); /* move closure to coroutine */

    /* Store coroutine in registry */
    lua_pushvalue(L, -1); /* dup thread */
    s->coroutine_ref = luaL_ref(L, LUA_REGISTRYINDEX);

    /* Anchor server keyed by co pointer */
    lua_pushlightuserdata(L, (void*)co);
    lua_pushvalue(L, 1);
    lua_rawset(L, LUA_REGISTRYINDEX);

    return 1; /* return coroutine thread */
}

/* ── server:SetOnDisconnect ───────────────────────────────────────────────── */

int HttpServer_SetOnDisconnect(lua_State* L) {
    LuaHttpServer* s = lua_checkhttpserver(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    if (s->disconnect_ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, s->disconnect_ref);
        s->disconnect_ref = LUA_NOREF;
    }
    lua_pushvalue(L, 2);
    s->disconnect_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return 0;
}

/* ── server:Close ─────────────────────────────────────────────────────────── */

int HttpServer_Close(lua_State* L) {
    LuaHttpServer* s = lua_checkhttpserver(L, 1);
    server_teardown(s, L);
    return 0;
}

/* ── __gc ─────────────────────────────────────────────────────────────────── */

int HttpServer_GC(lua_State* L) {
    LuaHttpServer* s = lua_checkhttpserver(L, 1);
    server_teardown(s, L);
    return 0;
}

/* ── __tostring ───────────────────────────────────────────────────────────── */

int HttpServer_ToString(lua_State* L) {
    LuaHttpServer* s = lua_checkhttpserver(L, 1);
    lua_pushfstring(L, "HttpServer(%s)", s->mgr_init ? "running" : "closed");
    return 1;
}
