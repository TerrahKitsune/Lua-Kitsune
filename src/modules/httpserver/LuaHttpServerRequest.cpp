#include "httpserver/LuaHttpServerRequest.h"
#include "Mem.h"
#include <string.h>
#include <stdlib.h>

/* ── push / check helpers ─────────────────────────────────────────────────── */

LuaHttpRequest* lua_pushhttprequest(lua_State* L) {
    LuaHttpRequest* r = (LuaHttpRequest*)lua_newuserdata(L, sizeof(LuaHttpRequest));
    luaL_getmetatable(L, LUAHTTPREQUEST);
    lua_setmetatable(L, -2);
    memset(r, 0, sizeof(LuaHttpRequest));
    r->headersRef  = LUA_NOREF;
    r->contextRef  = LUA_NOREF;
    r->responseRef = LUA_NOREF;
    return r;
}

LuaHttpRequest* luaL_checkhttprequest(lua_State* L, int idx) {
    LuaHttpRequest* r = (LuaHttpRequest*)luaL_checkudata(L, idx, LUAHTTPREQUEST);
    if (!r)
        luaL_error(L, "parameter is not a %s", LUAHTTPREQUEST);
    return r;
}

LuaHttpResponse* lua_pushhttpresponse(lua_State* L) {
    LuaHttpResponse* r = (LuaHttpResponse*)lua_newuserdata(L, sizeof(LuaHttpResponse));
    luaL_getmetatable(L, LUAHTTPRESPONSE);
    lua_setmetatable(L, -2);
    memset(r, 0, sizeof(LuaHttpResponse));
    r->statusCode = 200;
    r->streamRef  = LUA_NOREF;
    return r;
}

LuaHttpResponse* luaL_checkhttpresponse(lua_State* L, int idx) {
    LuaHttpResponse* r = (LuaHttpResponse*)luaL_checkudata(L, idx, LUAHTTPRESPONSE);
    if (!r)
        luaL_error(L, "parameter is not a %s", LUAHTTPRESPONSE);
    return r;
}

/* ── defensive helpers ────────────────────────────────────────────────────── */

static void SafeFree(char** p) {
    if (*p) {
        free(*p);
        *p = NULL;
    }
}

static void SafeUnref(lua_State* L, int* ref) {
    if (*ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, *ref);
        *ref = LUA_NOREF;
    }
}

/* ── request methods ──────────────────────────────────────────────────────── */

int HttpRequest_GetId(lua_State* L) {
    LuaHttpRequest* r = luaL_checkhttprequest(L, 1);
    lua_pushinteger(L, (lua_Integer)(uintptr_t)r->req);
    return 1;
}

int HttpRequest_GetUrl(lua_State* L) {
    LuaHttpRequest* r = luaL_checkhttprequest(L, 1);
    lua_pushstring(L, r->url ? r->url : "");
    return 1;
}

int HttpRequest_GetMethod(lua_State* L) {
    LuaHttpRequest* r = luaL_checkhttprequest(L, 1);
    lua_pushstring(L, r->method ? r->method : "");
    return 1;
}

int HttpRequest_GetHeaders(lua_State* L) {
    LuaHttpRequest* r = luaL_checkhttprequest(L, 1);
    if (r->headersRef != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, r->headersRef);
    } else {
        lua_newtable(L);
    }
    return 1;
}

int HttpRequest_GetIp(lua_State* L) {
    LuaHttpRequest* r = luaL_checkhttprequest(L, 1);
    if (!r->req) {
        lua_pushstring(L, "");
        return 1;
    }
    struct evhttp_connection* con = evhttp_request_get_connection(r->req);
    if (!con) {
        lua_pushstring(L, "");
        return 1;
    }
    /* libevent 2.2 declares evhttp_connection_get_peer with (const char**). */
        const char* host = NULL;
        ev_uint16_t port = 0;
        evhttp_connection_get_peer(con, &host, &port);
    char buf[256];
    if (host)
        snprintf(buf, sizeof(buf), "%s:%u", host, (unsigned)port);
    else
        buf[0] = '\0';
    lua_pushstring(L, buf);
    return 1;
}

int HttpRequest_IsFinished(lua_State* L) {
    LuaHttpRequest* r = luaL_checkhttprequest(L, 1);
    lua_pushboolean(L, r->isFinished);
    return 1;
}

int HttpRequest_GetBody(lua_State* L) {
    LuaHttpRequest* r = luaL_checkhttprequest(L, 1);
    if (r->body && r->body_len > 0) {
        lua_pushlstring(L, r->body, r->body_len);
    } else {
        lua_pushstring(L, "");
    }
    return 1;
}

int HttpRequest_GetContext(lua_State* L) {
    LuaHttpRequest* r = luaL_checkhttprequest(L, 1);
    if (r->contextRef == LUA_NOREF) {
        lua_newtable(L);
        lua_pushvalue(L, -1);
        r->contextRef = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_rawgeti(L, LUA_REGISTRYINDEX, r->contextRef);
    }
    return 1;
}

int HttpRequest_GetResponse(lua_State* L) {
    LuaHttpRequest* r = luaL_checkhttprequest(L, 1);
    if (r->responseRef == LUA_NOREF)
        luaL_error(L, "HttpRequest: no response object");
    lua_rawgeti(L, LUA_REGISTRYINDEX, r->responseRef);
    return 1;
}

int HttpRequest_GetError(lua_State* L) {
    LuaHttpRequest* r = luaL_checkhttprequest(L, 1);
    if (r->is_error && r->errorMsg) {
        lua_pushstring(L, r->errorMsg);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

void HttpRequestCleanup(lua_State* L, LuaHttpRequest* r) {
    SafeFree(&r->url);
    SafeFree(&r->method);
    // body is allocated via kitsune_malloc, not strdup — must match
    kitsune_free(r->body);
    r->body = NULL;
    SafeFree(&r->errorMsg);
    SafeUnref(L, &r->headersRef);

    /* Teardown response before releasing its ref — mirrors build order */
    if (r->responseRef != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, r->responseRef);
        LuaHttpResponse* resp = (LuaHttpResponse*)lua_touserdata(L, -1);
        lua_pop(L, 1);
        if (resp)
            HttpResponseCleanup(L, resp);
        SafeUnref(L, &r->responseRef);
    }

    SafeUnref(L, &r->contextRef);

    /* Do NOT call evhttp_request_free here.
       Send paths null r->req immediately so evhttp_send_done handles the free.
       Teardown drain loops explicitly free orphaned requests after evhttp_free
       has done TAILQ_REMOVE, making the memory-only free safe. */
    r->req    = NULL;
    r->con    = NULL;
    r->server = NULL;
}

int HttpRequest_GC(lua_State* L) {
    LuaHttpRequest* r = luaL_checkhttprequest(L, 1);
    HttpRequestCleanup(L, r);
    return 0;
}

int HttpRequest_ToString(lua_State* L) {
    LuaHttpRequest* r = luaL_checkhttprequest(L, 1);
    lua_pushfstring(L, "HttpRequest(%s %s)",
        r->method ? r->method : "?",
        r->url    ? r->url    : "?");
    return 1;
}

/* ── response cleanup / gc / tostring ────────────────────────────────────── */

void HttpResponseCleanup(lua_State* L, LuaHttpResponse* r) {
    r->connection = NULL;
    r->stream     = NULL;
    SafeUnref(L, &r->streamRef);
}

int HttpResponse_GC(lua_State* L) {
    LuaHttpResponse* r = luaL_checkhttpresponse(L, 1);
    HttpResponseCleanup(L, r);
    return 0;
}

int HttpResponse_ToString(lua_State* L) {
    LuaHttpResponse* r = luaL_checkhttpresponse(L, 1);
    lua_pushfstring(L, "HttpResponse(%s)", r->finalized ? "finalized" : "open");
    return 1;
}
