#include "LuaHttpRequest.h"
#include "LuaHttpServer.h"
#include "stream.h"
#include <string.h>
#include <stdlib.h>
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

/* ── header buffer helpers ────────────────────────────────────────────────── */

static bool append_header(LuaHttpResponse* r, const char* name, const char* value) {
    /* build "Name: Value\r\n" */
    size_t needed = strlen(name) + 2 + strlen(value) + 2 + 1;
    size_t cur    = r->extra_headers ? strlen(r->extra_headers) : 0;
    char*  buf    = (char*)realloc(r->extra_headers, cur + needed);
    if (!buf)
        return false;
    r->extra_headers = buf;
    snprintf(buf + cur, needed, "%s: %s\r\n", name, value);
    return true;
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
    if (!append_header(r, name, value))
        luaL_error(L, "HttpResponse: out of memory adding header");
    return 0;
}

/* ── Send() / Send(string) / Send(Stream) ─────────────────────────────────── */

int HttpResponse_Send(lua_State* L) {
    LuaHttpResponse* r = response_guard(L, true);

    if (!r->connection->is_finished) {
        lua_pushboolean(L, false);
        return 1;
    }

    struct mg_connection* conn = r->connection->conn;
    const char* headers = r->extra_headers ? r->extra_headers : "";

    int type = lua_type(L, 2);

    if (type == LUA_TNONE || type == LUA_TNIL) {
        /* Send() — no body */
        mg_http_reply(conn, r->status_code, headers, "");
        r->finalized = true;
        lua_pushboolean(L, true);
        return 1;
    }

    if (type == LUA_TSTRING) {
        /* Send(string) */
        size_t len;
        const char* body = lua_tolstring(L, 2, &len);
        mg_http_reply(conn, r->status_code, headers, "%.*s", (int)len, body);
        r->finalized = true;
        lua_pushboolean(L, true);
        return 1;
    }

    if (type == LUA_TUSERDATA) {
        /* Send(Stream) */
        LuaStream* stream = (LuaStream*)luaL_checkudata(L, 2, "STREAM");
        if (!stream)
            luaL_error(L, "HttpResponse:Send expects a string or Stream");
        if (!(stream->Caps & STREAM_CAP_READ))
            luaL_error(L, "HttpResponse:Send — stream must have STREAM_CAP_READ");

        /* Determine transfer mode */
        bool seekable = (stream->Caps & STREAM_CAP_SEEK) != 0;
        r->chunked    = !seekable;

        if (seekable) {
            lua_Integer content_len = lua_stream_getlen(L, stream);
            char len_str[32];
            snprintf(len_str, sizeof(len_str), "%lld", (long long)content_len);
            /* Build header block: existing headers + Content-Length */
            size_t hlen   = headers ? strlen(headers) : 0;
            size_t needed = hlen + 64;
            char*  hbuf   = (char*)malloc(needed);
            if (!hbuf)
                luaL_error(L, "HttpResponse:Send — out of memory");
            snprintf(hbuf, needed, "%sContent-Length: %lld\r\n",
                headers, (long long)content_len);
            mg_printf(conn, "HTTP/1.1 %d OK\r\n%s\r\n",
                r->status_code, hbuf);
            free(hbuf);
        } else {
            /* Transfer-Encoding: chunked */
            char hbuf[1024];
            snprintf(hbuf, sizeof(hbuf), "%sTransfer-Encoding: chunked\r\n", headers);
            mg_printf(conn, "HTTP/1.1 %d OK\r\n%s\r\n",
                r->status_code, hbuf);
        }

        /* Store stream on response and register as active sender */
        r->stream    = stream;
        r->finalized = true; /* headers sent; body in progress */

        /* Get the server via conn->fn_data and enqueue a senders node.
           The request is in the registry keyed by conn* — ref it so the
           node keeps the request (and its response) alive while sending. */
        LuaHttpServer* server = (LuaHttpServer*)conn->fn_data;
        if (server) {
            lua_pushlightuserdata(L, (void*)conn);
            lua_rawget(L, LUA_REGISTRYINDEX);
            int req_ref = luaL_ref(L, LUA_REGISTRYINDEX);

            HttpOpenConnection* node = (HttpOpenConnection*)malloc(sizeof(HttpOpenConnection));
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
    LuaHttpResponse* r = response_guard(L, true);
    int         code = (int)luaL_checkinteger(L, 2);
    const char* msg  = luaL_checkstring(L, 3);
    mg_http_reply(r->connection->conn, code,
        r->extra_headers ? r->extra_headers : "",
        "%s", msg);
    r->connection->conn->is_draining = 1;
    r->finalized = true;
    return 0;
}

/* ── Close ────────────────────────────────────────────────────────────────── */

int HttpResponse_Close(lua_State* L) {
    LuaHttpResponse* r = response_guard(L, true);
    r->connection->conn->is_closing = 1;
    r->finalized = true;
    return 0;
}
