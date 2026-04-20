#include "LuaHttpRequest.h"
#include <string.h>
#include <stdlib.h>

/* ── push / check helpers ─────────────────────────────────────────────────── */

LuaHttpRequest* lua_pushhttprequest(lua_State* L) {
    LuaHttpRequest* r = (LuaHttpRequest*)lua_newuserdata(L, sizeof(LuaHttpRequest));
    luaL_getmetatable(L, LUAHTTPREQUEST);
    lua_setmetatable(L, -2);
    memset(r, 0, sizeof(LuaHttpRequest));
    r->headers_ref  = LUA_NOREF;
    r->context_ref  = LUA_NOREF;
    r->response_ref = LUA_NOREF;
    return r;
}

LuaHttpRequest* lua_checkhttprequest(lua_State* L, int idx) {
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
    r->status_code = 200;
    return r;
}

LuaHttpResponse* lua_checkhttpresponse(lua_State* L, int idx) {
    LuaHttpResponse* r = (LuaHttpResponse*)luaL_checkudata(L, idx, LUAHTTPRESPONSE);
    if (!r)
        luaL_error(L, "parameter is not a %s", LUAHTTPRESPONSE);
    return r;
}

/* ── defensive helpers ────────────────────────────────────────────────────── */

static void safe_free(char** p) {
    if (*p) {
        free(*p);
        *p = NULL;
    }
}

static void safe_unref(lua_State* L, int* ref) {
    if (*ref != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, *ref);
        *ref = LUA_NOREF;
    }
}

/* ── request methods ──────────────────────────────────────────────────────── */

int HttpRequest_GetId(lua_State* L) {
    LuaHttpRequest* r = lua_checkhttprequest(L, 1);
    lua_pushinteger(L, (lua_Integer)(uintptr_t)r->conn);
    return 1;
}

int HttpRequest_GetUrl(lua_State* L) {
    LuaHttpRequest* r = lua_checkhttprequest(L, 1);
    lua_pushstring(L, r->url ? r->url : "");
    return 1;
}

int HttpRequest_GetMethod(lua_State* L) {
    LuaHttpRequest* r = lua_checkhttprequest(L, 1);
    lua_pushstring(L, r->method ? r->method : "");
    return 1;
}

int HttpRequest_GetHeaders(lua_State* L) {
    LuaHttpRequest* r = lua_checkhttprequest(L, 1);
    if (r->headers_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, r->headers_ref);
    } else {
        lua_newtable(L);
    }
    return 1;
}

int HttpRequest_GetIp(lua_State* L) {
    LuaHttpRequest* r = lua_checkhttprequest(L, 1);
    if (!r->conn) {
        lua_pushstring(L, "");
        return 1;
    }
    char buf[64];
    mg_snprintf(buf, sizeof(buf), "%M", mg_print_ip_port, &r->conn->rem);
    lua_pushstring(L, buf);
    return 1;
}

int HttpRequest_IsFinished(lua_State* L) {
    LuaHttpRequest* r = lua_checkhttprequest(L, 1);
    lua_pushboolean(L, r->is_finished);
    return 1;
}

int HttpRequest_GetBody(lua_State* L) {
    LuaHttpRequest* r = lua_checkhttprequest(L, 1);
    if (r->body && r->body_len > 0) {
        lua_pushlstring(L, r->body, r->body_len);
    } else {
        lua_pushstring(L, "");
    }
    return 1;
}

int HttpRequest_GetContext(lua_State* L) {
    LuaHttpRequest* r = lua_checkhttprequest(L, 1);
    if (r->context_ref == LUA_NOREF) {
        lua_newtable(L);
        lua_pushvalue(L, -1);
        r->context_ref = luaL_ref(L, LUA_REGISTRYINDEX);
    } else {
        lua_rawgeti(L, LUA_REGISTRYINDEX, r->context_ref);
    }
    return 1;
}

int HttpRequest_GetResponse(lua_State* L) {
    LuaHttpRequest* r = lua_checkhttprequest(L, 1);
    if (r->response_ref == LUA_NOREF)
        luaL_error(L, "HttpRequest: no response object");
    lua_rawgeti(L, LUA_REGISTRYINDEX, r->response_ref);
    return 1;
}

int HttpRequest_GetError(lua_State* L) {
    LuaHttpRequest* r = lua_checkhttprequest(L, 1);
    if (r->is_error && r->error_msg) {
        lua_pushstring(L, r->error_msg);
    } else {
        lua_pushnil(L);
    }
    return 1;
}

void HttpRequest_Cleanup(lua_State* L, LuaHttpRequest* r) {
    safe_free(&r->url);
    safe_free(&r->method);
    safe_free(&r->body);
    safe_free(&r->error_msg);
    safe_unref(L, &r->headers_ref);

    /* Teardown response before releasing its ref — mirrors build order */
    if (r->response_ref != LUA_NOREF) {
        lua_rawgeti(L, LUA_REGISTRYINDEX, r->response_ref);
        LuaHttpResponse* resp = (LuaHttpResponse*)lua_touserdata(L, -1);
        lua_pop(L, 1);
        if (resp)
            HttpResponse_Cleanup(L, resp);
        safe_unref(L, &r->response_ref);
    }

    safe_unref(L, &r->context_ref);
    r->conn = NULL;
}

int HttpRequest_GC(lua_State* L) {
    LuaHttpRequest* r = lua_checkhttprequest(L, 1);
    HttpRequest_Cleanup(L, r);
    return 0;
}

int HttpRequest_ToString(lua_State* L) {
    LuaHttpRequest* r = lua_checkhttprequest(L, 1);
    lua_pushfstring(L, "HttpRequest(%s %s)",
        r->method ? r->method : "?",
        r->url    ? r->url    : "?");
    return 1;
}

/* ── response cleanup / gc / tostring ────────────────────────────────────── */

void HttpResponse_Cleanup(lua_State* L, LuaHttpResponse* r) {
    r->connection = NULL;
    r->stream     = NULL;
    if (r->extra_headers) {
        free(r->extra_headers);
        r->extra_headers = NULL;
    }
}

int HttpResponse_GC(lua_State* L) {
    LuaHttpResponse* r = lua_checkhttpresponse(L, 1);
    HttpResponse_Cleanup(L, r);
    return 0;
}

int HttpResponse_ToString(lua_State* L) {
    LuaHttpResponse* r = lua_checkhttpresponse(L, 1);
    lua_pushfstring(L, "HttpResponse(%s)", r->finalized ? "finalized" : "open");
    return 1;
}
