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

/* ── Reason phrase ────────────────────────────────────────────────────────── */

/* Standard reason phrase for a status code (RFC 9110 and common extensions).
   Passed to libevent explicitly: it only documents a NULL default for
   evhttp_send_error, not for evhttp_send_reply / evhttp_send_reply_start. */
static const char* http_reason_phrase(int code) {
    switch (code) {
    case 100: return "Continue";
    case 101: return "Switching Protocols";
    case 102: return "Processing";
    case 103: return "Early Hints";
    case 200: return "OK";
    case 201: return "Created";
    case 202: return "Accepted";
    case 203: return "Non-Authoritative Information";
    case 204: return "No Content";
    case 205: return "Reset Content";
    case 206: return "Partial Content";
    case 207: return "Multi-Status";
    case 208: return "Already Reported";
    case 226: return "IM Used";
    case 300: return "Multiple Choices";
    case 301: return "Moved Permanently";
    case 302: return "Found";
    case 303: return "See Other";
    case 304: return "Not Modified";
    case 305: return "Use Proxy";
    case 307: return "Temporary Redirect";
    case 308: return "Permanent Redirect";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 402: return "Payment Required";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 406: return "Not Acceptable";
    case 407: return "Proxy Authentication Required";
    case 408: return "Request Timeout";
    case 409: return "Conflict";
    case 410: return "Gone";
    case 411: return "Length Required";
    case 412: return "Precondition Failed";
    case 413: return "Content Too Large";
    case 414: return "URI Too Long";
    case 415: return "Unsupported Media Type";
    case 416: return "Range Not Satisfiable";
    case 417: return "Expectation Failed";
    case 418: return "I'm a teapot";
    case 421: return "Misdirected Request";
    case 422: return "Unprocessable Content";
    case 423: return "Locked";
    case 424: return "Failed Dependency";
    case 425: return "Too Early";
    case 426: return "Upgrade Required";
    case 428: return "Precondition Required";
    case 429: return "Too Many Requests";
    case 431: return "Request Header Fields Too Large";
    case 451: return "Unavailable For Legal Reasons";
    case 500: return "Internal Server Error";
    case 501: return "Not Implemented";
    case 502: return "Bad Gateway";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    case 506: return "Variant Also Negotiates";
    case 507: return "Insufficient Storage";
    case 508: return "Loop Detected";
    case 510: return "Not Extended";
    case 511: return "Network Authentication Required";
    }
    /* Unlisted codes: the class name, so the status line is still meaningful. */
    if (code >= 100 && code < 200) return "Informational";
    if (code >= 200 && code < 300) return "Success";
    if (code >= 300 && code < 400) return "Redirection";
    if (code >= 400 && code < 500) return "Client Error";
    if (code >= 500 && code < 600) return "Server Error";
    return "Unknown";
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
        evhttp_send_reply(req, r->status_code, http_reason_phrase(r->status_code), buf);
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
        evhttp_send_reply(req, r->status_code, http_reason_phrase(r->status_code), buf);
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
        evhttp_send_reply_start(req, r->status_code, http_reason_phrase(r->status_code));

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
    evhttp_send_reply(req, code, http_reason_phrase(code), buf);
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
