#include "LuaHttpRequest.h"
#include "LuaHttpServer.h"
#include "stream.h"
#include "mem.h"
#include <event2/http.h>
#include <event2/buffer.h>
#include <string.h>
#include <stdio.h>

/* ── internal guard ───────────────────────────────────────────────────────── */

static LuaHttpResponse* response_guard(lua_State* L, bool check_finalized) {
    LuaHttpResponse* r = lua_checkhttpresponse(L, 1);
    if (!r->connection)
        luaL_error(L, "HttpResponse: connection is no longer alive");
    if (check_finalized && r->finalized)
        luaL_error(L, "HttpResponse: response already finalized");
    return r;
}

/* ── SetCode ──────────────────────────────────────────────────────────────── */

int HttpResponse_SetCode(lua_State* L) {
    LuaHttpResponse* r = response_guard(L, true);
    r->status_code = (int)luaL_checkinteger(L, 2);
    return 0;
}

/* ── SetHeader ────────────────────────────────────────────────────────────── */

int HttpResponse_SetHeader(lua_State* L) {
    LuaHttpResponse* r = response_guard(L, true);
    const char* name  = luaL_checkstring(L, 2);
    const char* value = luaL_checkstring(L, 3);
    if (!r->connection->req)
        luaL_error(L, "HttpResponse: request already sent");
    evhttp_add_header(evhttp_request_get_output_headers(r->connection->req), name, value);
    return 0;
}

/* ── Send() / Send(string) / Send(Stream) ─────────────────────────────────── */

int HttpResponse_Send(lua_State* L) {
    LuaHttpResponse* r = response_guard(L, true);

    if (!r->connection->is_finished) {
        lua_pushboolean(L, false);
        return 1;
    }

    struct evhttp_request* req = r->connection->req;
    if (!req) {
        lua_pushboolean(L, false);
        return 1;
    }

    int type = lua_type(L, 2);

    if (type == LUA_TNONE || type == LUA_TNIL) {
        /* Send() — no body */
        struct evbuffer* buf = evbuffer_new();
        evhttp_send_reply(req, r->status_code, "OK", buf);
        evbuffer_free(buf);
        /* evhttp_send_done will free req after the write drains — do NOT call
           evhttp_request_free here. Null now so HttpRequest_Cleanup skips it. */
        r->connection->req = NULL;
        r->finalized       = true;
        lua_pushboolean(L, true);
        return 1;
    }

    if (type == LUA_TSTRING) {
        /* Send(string) */
        size_t      len;
        const char* body = lua_tolstring(L, 2, &len);
        struct evbuffer* buf = evbuffer_new();
        if (buf)
            evbuffer_add(buf, body, len);
        evhttp_send_reply(req, r->status_code, "OK", buf);
        if (buf)
            evbuffer_free(buf);
        /* evhttp_send_done owns the free — do NOT call evhttp_request_free. */
        r->connection->req = NULL;
        r->finalized       = true;
        lua_pushboolean(L, true);
        return 1;
    }

    if (type == LUA_TUSERDATA) {
        /* Send(Stream) — start chunked streaming */
        LuaStream* stream = (LuaStream*)luaL_checkudata(L, 2, "STREAM");
        if (!stream)
            luaL_error(L, "HttpResponse:Send expects a string or Stream");
        if (!(stream->Caps & STREAM_CAP_READ))
            luaL_error(L, "HttpResponse:Send — stream must have STREAM_CAP_READ");

        /* evhttp always uses chunked TE for the send_reply_start path */
        evhttp_send_reply_start(req, r->status_code, "OK");

        r->stream    = stream;
        r->chunked   = true;
        r->finalized = true; /* headers sent; body in progress */

        /* Keep the stream userdata alive in the Lua registry so GC cannot
           collect it while chunked sending is still in progress. */
        lua_pushvalue(L, 2);
        r->stream_ref = luaL_ref(L, LUA_REGISTRYINDEX);

        /* Enqueue into server senders so accept_cont pumps chunks each tick */
        LuaHttpServer* server = r->connection->server;
        if (server) {
            lua_pushlightuserdata(L, (void*)req);
            lua_rawget(L, LUA_REGISTRYINDEX);
            int req_ref = luaL_ref(L, LUA_REGISTRYINDEX);

            HttpOpenConnection* node = (HttpOpenConnection*)kitsune_malloc(sizeof(HttpOpenConnection));
            if (node) {
                node->request_ref = req_ref;
                node->next        = server->senders;
                server->senders   = node;
            } else {
                luaL_unref(L, LUA_REGISTRYINDEX, req_ref);
            }
        }

        lua_pushboolean(L, true);
        return 1;
    }

    luaL_error(L, "HttpResponse:Send expects nil, string, or Stream");
    return 0;
}

/* ── Reject ───────────────────────────────────────────────────────────────── */

int HttpResponse_Reject(lua_State* L) {
    LuaHttpResponse* r    = response_guard(L, true);
    int              code = (int)luaL_checkinteger(L, 2);
    const char*      msg  = luaL_checkstring(L, 3);
    struct evhttp_request* req = r->connection->req;
    evhttp_add_header(evhttp_request_get_output_headers(req), "Connection", "close");
    struct evbuffer* buf = evbuffer_new();
    if (buf)
        evbuffer_add(buf, msg, strlen(msg));
    evhttp_send_reply(req, code, "Error", buf);
    if (buf)
        evbuffer_free(buf);
    /* evhttp_send_done owns the free. */
    r->connection->req = NULL;
    r->finalized       = true;
    return 0;
}

/* ── Close ────────────────────────────────────────────────────────────────── */

int HttpResponse_Close(lua_State* L) {
    LuaHttpResponse* r   = response_guard(L, true);
    struct evhttp_request* req = r->connection->req;
    evhttp_add_header(evhttp_request_get_output_headers(req), "Connection", "close");
    struct evbuffer* buf = evbuffer_new();
    evhttp_send_reply(req, 200, "OK", buf);
    if (buf)
        evbuffer_free(buf);
    /* evhttp_send_done owns the free. */
    r->connection->req = NULL;
    r->finalized       = true;
    return 0;
}
