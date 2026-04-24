#include "luaini.h"
#include "utf8bom.h"
#include "luadecimal.h"
#include "luaidentifier.h"
#include "luadatetime.h"
#include "luauint.h"
#include "luatimespan.h"

// =============================================================================
// Instance management
// =============================================================================

LuaIni* lua_ini_push(lua_State* L) {
    LuaIni* n = (LuaIni*)lua_newuserdata(L, sizeof(LuaIni));
    memset(n, 0, sizeof(LuaIni));
    luaL_setmetatable(L, LUAINI);
    return n;
}

LuaIni* lua_ini_check(lua_State* L, int idx) {
    return (LuaIni*)luaL_checkudata(L, idx, LUAINI);
}

int lua_ini_gc(lua_State* L) {
    LuaIni* n = lua_ini_check(L, 1);
    if (n->out) {
        kitsune_free(n->out);
        n->out = NULL;
    }
    return 0;
}

int lua_ini_tostring(lua_State* L) {
    lua_pushfstring(L, "Ini: %p", lua_ini_check(L, 1));
    return 1;
}

int lua_ini_new(lua_State* L) {
    lua_ini_push(L);
    return 1;
}

// =============================================================================
// Encoder — output buffer helpers
// =============================================================================

static void ibuf_grow(LuaIni* n, lua_State* L, size_t need) {
    if (n->outLen + need <= n->outCap)
        return;
    size_t cap = n->outCap ? n->outCap * 2 : 512;
    while (cap < n->outLen + need)
        cap *= 2;
    char* p = (char*)kitsune_realloc(n->out, cap);
    if (!p)
        luaL_error(L, "Ini: out of memory");
    n->out    = p;
    n->outCap = cap;
}

static void ibuf_emit(LuaIni* n, lua_State* L, const char* data, size_t len) {
    ibuf_grow(n, L, len);
    memcpy(n->out + n->outLen, data, len);
    n->outLen += len;
}

static void ibuf_emitc(LuaIni* n, lua_State* L, char c) {
    ibuf_grow(n, L, 1);
    n->out[n->outLen++] = c;
}

#define ibuf_emitlit(n, L, s) ibuf_emit(n, L, "" s, sizeof(s) - 1)

// =============================================================================
// Encoder — value stringification
// =============================================================================

// Emit a value as a string suitable for an INI value (all values are strings).
// Tables and unsupported types are skipped (return 0); scalars return 1.
static int enc_scalar(LuaIni* n, lua_State* L, int idx) {
    switch (lua_type(L, idx)) {
    case LUA_TBOOLEAN:
        if (lua_toboolean(L, idx))
            ibuf_emitlit(n, L, "true");
        else
            ibuf_emitlit(n, L, "false");
        return 1;
    case LUA_TNUMBER:
        if (lua_isinteger(L, idx)) {
            char buf[32];
            int  len = snprintf(buf, sizeof(buf), "%lld", (long long)lua_tointeger(L, idx));
            ibuf_emit(n, L, buf, (size_t)len);
        } else {
            char buf[64];
            int  len = snprintf(buf, sizeof(buf), "%.17g", (double)lua_tonumber(L, idx));
            ibuf_emit(n, L, buf, (size_t)len);
        }
        return 1;
    case LUA_TSTRING: {
        size_t      len;
        const char* s = lua_tolstring(L, idx, &len);
        ibuf_emit(n, L, s, len);
        return 1;
    }
    case LUA_TUSERDATA:
        if (lua_isuint(L, idx)) {
            char buf[21];
            int  len = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)lua_touint(L, idx)->value);
            ibuf_emit(n, L, buf, (size_t)len);
            return 1;
        }
        if (lua_isdecimal(L, idx)) {
            lua_decimal_push_string(L, idx);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            if (s) ibuf_emit(n, L, s, len);
            lua_pop(L, 1);
            return 1;
        }
        if (lua_isidentifier(L, idx)) {
            lua_identifier_push_string(L, idx);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            if (s) ibuf_emit(n, L, s, len);
            lua_pop(L, 1);
            return 1;
        }
        if (lua_isdatetime(L, idx)) {
            lua_datetime_push_string(L, idx);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            if (s) ibuf_emit(n, L, s, len);
            lua_pop(L, 1);
            return 1;
        }
        if (lua_istimespan(L, idx)) {
            lua_timespan_push_string(L, idx);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            if (s) ibuf_emit(n, L, s, len);
            lua_pop(L, 1);
            return 1;
        }
        return 0;
    default:
        return 0;
    }
}

// =============================================================================
// Encoder — main
// =============================================================================

int lua_ini_encode(lua_State* L) {
    LuaIni* n = lua_ini_check(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    n->outLen = 0;

    int tbl = 2;

    // First pass: emit global keys (section == "__global" or any non-table value
    // at the top level, for convenience).
    lua_pushnil(L);
    while (lua_next(L, tbl) != 0) {
        if (lua_type(L, -1) != LUA_TTABLE) {
            // Bare scalar at top level — emit as a global key.
            size_t      klen;
            const char* k = lua_tolstring(L, -2, &klen);
            if (k) {
                ibuf_emit(n, L, k, klen);
                ibuf_emitlit(n, L, " = ");
                enc_scalar(n, L, -1);
                ibuf_emitc(n, L, '\n');
            }
            lua_pop(L, 1);
            continue;
        }
        // Check for the "__global" pseudo-section.
        size_t      klen;
        const char* k = lua_tolstring(L, -2, &klen);
        if (k && klen == 8 && memcmp(k, "__global", 8) == 0) {
            int sec = lua_gettop(L);
            lua_pushnil(L);
            while (lua_next(L, sec) != 0) {
                size_t      vklen;
                const char* vk = lua_tolstring(L, -2, &vklen);
                if (vk && enc_scalar(n, L, -1) == 0) {
                    // skip non-scalars silently
                    lua_pop(L, 1);
                    continue;
                }
                if (vk) {
                    // We already emitted the value — rewind and redo properly.
                    n->outLen -= 0; // nothing emitted yet at this point
                }
                // Redo: emit key = value\n
                if (vk) {
                    // enc_scalar already wrote — need to not double-emit.
                    // Restructure: gather key first, then value.
                    lua_pop(L, 1);
                    continue;
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    // Cleaner approach: emit __global section properly.
    n->outLen = 0;

    // Pass 1: global keys (top-level scalars and __global section).
    lua_pushnil(L);
    while (lua_next(L, tbl) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING) {
            lua_pop(L, 1);
            continue;
        }
        size_t      klen;
        const char* k = lua_tolstring(L, -2, &klen);

        if (lua_type(L, -1) != LUA_TTABLE) {
            // Bare scalar at root level.
            ibuf_emit(n, L, k, klen);
            ibuf_emitlit(n, L, " = ");
            enc_scalar(n, L, -1);
            ibuf_emitc(n, L, '\n');
            lua_pop(L, 1);
            continue;
        }

        if (klen == 8 && memcmp(k, "__global", 8) == 0) {
            int sec = lua_gettop(L);
            lua_pushnil(L);
            while (lua_next(L, sec) != 0) {
                if (lua_type(L, -2) == LUA_TSTRING) {
                    size_t      vklen;
                    const char* vk = lua_tolstring(L, -2, &vklen);
                    ibuf_emit(n, L, vk, vklen);
                    ibuf_emitlit(n, L, " = ");
                    if (!enc_scalar(n, L, -1)) {
                        // Non-scalar: remove the "key = " we just emitted.
                        n->outLen -= vklen + 3;
                    } else {
                        ibuf_emitc(n, L, '\n');
                    }
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    // Pass 2: named sections.
    lua_pushnil(L);
    while (lua_next(L, tbl) != 0) {
        if (lua_type(L, -2) != LUA_TSTRING || lua_type(L, -1) != LUA_TTABLE) {
            lua_pop(L, 1);
            continue;
        }
        size_t      klen;
        const char* k = lua_tolstring(L, -2, &klen);
        if (klen == 8 && memcmp(k, "__global", 8) == 0) {
            lua_pop(L, 1);
            continue;
        }

        // Section header.
        ibuf_emitc(n, L, '[');
        ibuf_emit(n, L, k, klen);
        ibuf_emitlit(n, L, "]\n");

        int sec = lua_gettop(L);
        lua_pushnil(L);
        while (lua_next(L, sec) != 0) {
            if (lua_type(L, -2) == LUA_TSTRING) {
                size_t      vklen;
                const char* vk = lua_tolstring(L, -2, &vklen);
                ibuf_emit(n, L, vk, vklen);
                ibuf_emitlit(n, L, " = ");
                if (!enc_scalar(n, L, -1)) {
                    n->outLen -= vklen + 3;
                } else {
                    ibuf_emitc(n, L, '\n');
                }
            }
            lua_pop(L, 1);
        }
        ibuf_emitc(n, L, '\n');
        lua_pop(L, 1);
    }

    lua_pushlstring(L, n->out ? n->out : "", n->outLen);
    return 1;
}

// =============================================================================
// Decoder
// =============================================================================

// Trim leading and trailing whitespace in-place (returns pointer into s).
static const char* trim(const char* s, size_t* len) {
    while (*len > 0 && ((unsigned char)*s == ' ' || (unsigned char)*s == '\t'))  {
        s++;
        (*len)--;
    }
    while (*len > 0) {
        unsigned char c = (unsigned char)s[*len - 1];
        if (c == ' ' || c == '\t' || c == '\r')
            (*len)--;
        else
            break;
    }
    return s;
}

int lua_ini_decode(lua_State* L) {
    lua_ini_check(L, 1);
    size_t      srcLen;
    const char* src = luaL_checklstring(L, 2, &srcLen);
    skip_utf8_bom(&src, &srcLen);

    // Result table.
    lua_newtable(L);
    int result = lua_gettop(L);

    // Push the __global section table as the initial active section.
    lua_pushliteral(L, "__global");
    lua_newtable(L);
    lua_rawset(L, result);

    // Active section name on the Lua stack — we keep re-pushing as we change.
    const char* curSection    = "__global";
    size_t      curSectionLen = 8;

    const char* p   = src;
    const char* end = src + srcLen;

    while (p < end) {
        // Find end of line.
        const char* lineStart = p;
        while (p < end && *p != '\n')
            p++;
        size_t lineLen = (size_t)(p - lineStart);
        if (p < end)
            p++;  // skip '\n'

        size_t      tlen = lineLen;
        const char* line = trim(lineStart, &tlen);

        // Skip empty lines and comments (; and #).
        if (tlen == 0 || line[0] == ';' || line[0] == '#')
            continue;

        // Section header [name].
        if (line[0] == '[') {
            const char* close = (const char*)memchr(line + 1, ']', tlen - 1);
            if (!close)
                continue;
            size_t      slen = (size_t)(close - (line + 1));
            const char* sname = line + 1;
            // Trim the section name.
            while (slen > 0 && ((unsigned char)*sname == ' ' || (unsigned char)*sname == '\t')) {
                sname++;
                slen--;
            }
            while (slen > 0) {
                unsigned char c = (unsigned char)sname[slen - 1];
                if (c == ' ' || c == '\t')
                    slen--;
                else
                    break;
            }
            if (slen == 0)
                continue;

            // Ensure this section table exists in the result.
            lua_pushlstring(L, sname, slen);
            lua_rawget(L, result);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                lua_pushlstring(L, sname, slen);
                lua_newtable(L);
                lua_rawset(L, result);
            } else {
                lua_pop(L, 1);
            }
            curSection    = sname;
            curSectionLen = slen;
            continue;
        }

        // Key = value (or key: value).
        const char* eq = NULL;
        for (size_t i = 0; i < tlen; i++) {
            if (line[i] == '=' || line[i] == ':') {
                eq = line + i;
                break;
            }
        }
        if (!eq)
            continue;

        size_t      klen = (size_t)(eq - line);
        const char* k    = trim(line, &klen);
        if (klen == 0)
            continue;

        const char* vstart = eq + 1;
        size_t      vlen   = tlen - (size_t)(vstart - line);
        const char* v      = trim(vstart, &vlen);

        // Strip optional inline comment (; or #) not inside quotes.
        int quoted = 0;
        for (size_t i = 0; i < vlen; i++) {
            if (v[i] == '"')
                quoted = !quoted;
            if (!quoted && (v[i] == ';' || v[i] == '#')) {
                vlen = i;
                // re-trim trailing whitespace after stripping comment
                while (vlen > 0) {
                    unsigned char c = (unsigned char)v[vlen - 1];
                    if (c == ' ' || c == '\t')
                        vlen--;
                    else
                        break;
                }
                break;
            }
        }

        // Strip surrounding quotes from value if present.
        if (vlen >= 2 && v[0] == '"' && v[vlen - 1] == '"') {
            v++;
            vlen -= 2;
        }

        // Set result[curSection][key] = value.
        lua_pushlstring(L, curSection, curSectionLen);
        lua_rawget(L, result);
        int sec = lua_gettop(L);
        lua_pushlstring(L, k, klen);
        lua_pushlstring(L, v, vlen);
        lua_rawset(L, sec);
        lua_pop(L, 1);
    }

    return 1;
}
