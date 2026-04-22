#include "luatoml.h"
#include "luawchar.h"
#include "luaidentifier.h"
#include "luadatetime.h"
#include "luadecimal.h"

// =============================================================================
// Instance management
// =============================================================================

LuaToml* lua_toml_push(lua_State* L) {
    LuaToml* t = (LuaToml*)lua_newuserdata(L, sizeof(LuaToml));
    memset(t, 0, sizeof(LuaToml));
    luaL_setmetatable(L, LUATOML);
    return t;
}

LuaToml* lua_toml_check(lua_State* L, int idx) {
    return (LuaToml*)luaL_checkudata(L, idx, LUATOML);
}

int lua_toml_gc(lua_State* L) {
    LuaToml* t = lua_toml_check(L, 1);
    if (t->out) {
        kitsune_free(t->out);
        t->out = NULL;
    }
    if (t->rec) {
        kitsune_free(t->rec);
        t->rec = NULL;
    }
    return 0;
}

int lua_toml_tostring(lua_State* L) {
    lua_pushfstring(L, "Toml: %p", lua_toml_check(L, 1));
    return 1;
}

int lua_toml_new(lua_State* L) {
    int      pretty = lua_isboolean(L, 1) ? lua_toboolean(L, 1) : 0;
    LuaToml* t      = lua_toml_push(L);
    t->pretty       = pretty;
    return 1;
}

// =============================================================================
// Anti-recursion
// =============================================================================

static void rec_push(LuaToml* t, lua_State* L, uintptr_t addr) {
    for (size_t i = 0; i < t->recLen; i++) {
        if (t->rec[i] == addr)
            luaL_error(L, "Toml: recursion detected");
    }
    if (t->recLen == t->recCap) {
        size_t     cap = t->recCap ? t->recCap * 2 : 8;
        uintptr_t* p   = (uintptr_t*)kitsune_realloc(t->rec, cap * sizeof(uintptr_t));
        if (!p)
            luaL_error(L, "Toml: out of memory");
        t->rec    = p;
        t->recCap = cap;
    }
    t->rec[t->recLen++] = addr;
}

static void rec_pop(LuaToml* t) {
    if (t->recLen > 0)
        t->recLen--;
}

// =============================================================================
// Encoder — output buffer helpers
// =============================================================================

static void tbuf_grow(LuaToml* t, lua_State* L, size_t need) {
    if (t->outLen + need <= t->outCap)
        return;
    size_t cap = t->outCap ? t->outCap * 2 : 512;
    while (cap < t->outLen + need)
        cap *= 2;
    char* p = (char*)kitsune_realloc(t->out, cap);
    if (!p)
        luaL_error(L, "Toml: out of memory");
    t->out    = p;
    t->outCap = cap;
}

static void tbuf_emit(LuaToml* t, lua_State* L, const char* data, size_t len) {
    tbuf_grow(t, L, len);
    memcpy(t->out + t->outLen, data, len);
    t->outLen += len;
}

static void tbuf_emitc(LuaToml* t, lua_State* L, char c) {
    tbuf_grow(t, L, 1);
    t->out[t->outLen++] = c;
}

#define tbuf_emitlit(t, L, s) tbuf_emit(t, L, "" s, sizeof(s) - 1)

// =============================================================================
// Encoder — scalar helpers
// =============================================================================

// Emit a TOML basic string (double-quoted, with escapes).
static void enc_string(LuaToml* t, lua_State* L, const char* s, size_t len) {
    tbuf_emitc(t, L, '"');
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  tbuf_emitlit(t, L, "\\\""); break;
        case '\\': tbuf_emitlit(t, L, "\\\\"); break;
        case '\n': tbuf_emitlit(t, L, "\\n");  break;
        case '\r': tbuf_emitlit(t, L, "\\r");  break;
        case '\t': tbuf_emitlit(t, L, "\\t");  break;
        case '\b': tbuf_emitlit(t, L, "\\b");  break;
        case '\f': tbuf_emitlit(t, L, "\\f");  break;
        default:
            if (c < 0x20) {
                char esc[7];
                int  n = snprintf(esc, sizeof(esc), "\\u%04x", c);
                tbuf_emit(t, L, esc, (size_t)n);
            } else {
                tbuf_emitc(t, L, (char)c);
            }
        }
    }
    tbuf_emitc(t, L, '"');
}

// Returns 1 if the string is a valid bare TOML key (A-Z a-z 0-9 - _).
static int is_bare_key(const char* s, size_t len) {
    if (len == 0)
        return 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_'))
            return 0;
    }
    return 1;
}

// Emit a TOML key — bare if possible, otherwise quoted.
static void enc_key(LuaToml* t, lua_State* L, const char* s, size_t len) {
    if (is_bare_key(s, len))
        tbuf_emit(t, L, s, len);
    else
        enc_string(t, L, s, len);
}

// =============================================================================
// Encoder — forward declarations
// =============================================================================

static void enc_value(LuaToml* t, lua_State* L, int depth, int is_inline);
static void enc_table(LuaToml* t, lua_State* L, int depth, const char* path, size_t pathLen);

// =============================================================================
// Encoder — classify a Lua table
// =============================================================================

// Returns 1 if the table on top of the stack is a pure array (1..n integer keys).
static int is_array(lua_State* L) {
    int     tbl = lua_gettop(L);
    lua_Integer n   = (lua_Integer)lua_rawlen(L, tbl);
    lua_Integer cnt = 0;
    lua_pushnil(L);
    while (lua_next(L, tbl) != 0) {
        lua_pop(L, 1);
        cnt++;
        if (!lua_isinteger(L, -1)) {
            lua_pop(L, 1);
            return 0;
        }
        lua_Integer k = lua_tointeger(L, -1);
        if (k < 1 || k > n) {
            lua_pop(L, 1);
            return 0;
        }
    }
    return cnt == n;
}

// Returns 1 if a table contains any sub-table or array-of-tables children
// (meaning it must be emitted as a TOML section header, not inline).
static int has_table_children(lua_State* L) {
    int tbl = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, tbl) != 0) {
        if (lua_type(L, -1) == LUA_TTABLE) {
            lua_pop(L, 2);
            return 1;
        }
        lua_pop(L, 1);
    }
    return 0;
}

// =============================================================================
// Encoder — inline value emitter (used for array elements and inline tables)
// =============================================================================

static void enc_inline_array(LuaToml* t, lua_State* L, int depth);
static void enc_inline_table(LuaToml* t, lua_State* L, int depth);

static void enc_value(LuaToml* t, lua_State* L, int depth, int is_inline) {
    switch (lua_type(L, -1)) {
    case LUA_TNIL:
        // TOML has no null — emit empty string as closest approximation
        tbuf_emitlit(t, L, "\"\"");
        break;
    case LUA_TBOOLEAN:
        if (lua_toboolean(L, -1))
            tbuf_emitlit(t, L, "true");
        else
            tbuf_emitlit(t, L, "false");
        break;
    case LUA_TNUMBER:
        if (lua_isinteger(L, -1)) {
            char buf[32];
            int  n = snprintf(buf, sizeof(buf), "%lld", (long long)lua_tointeger(L, -1));
            tbuf_emit(t, L, buf, (size_t)n);
        } else {
            double d = lua_tonumber(L, -1);
            if (d != d) {
                tbuf_emitlit(t, L, "nan");
            } else if (d == HUGE_VAL) {
                tbuf_emitlit(t, L, "inf");
            } else if (d == -HUGE_VAL) {
                tbuf_emitlit(t, L, "-inf");
            } else {
                char buf[64];
                int  n = snprintf(buf, sizeof(buf), "%.17g", d);
                // Ensure a decimal point so TOML recognises it as float.
                int has_dot = 0;
                for (int i = 0; i < n; i++) {
                    if (buf[i] == '.' || buf[i] == 'e' || buf[i] == 'E' || buf[i] == 'n' || buf[i] == 'i') {
                        has_dot = 1;
                        break;
                    }
                }
                tbuf_emit(t, L, buf, (size_t)n);
                if (!has_dot)
                    tbuf_emitlit(t, L, ".0");
            }
        }
        break;
    case LUA_TSTRING: {
        size_t      len;
        const char* s = lua_tolstring(L, -1, &len);
        enc_string(t, L, s, len);
        break;
    }
    case LUA_TTABLE:
        if (is_array(L))
            enc_inline_array(t, L, depth);
        else
            enc_inline_table(t, L, depth);
        break;
    case LUA_TUSERDATA:
        if (lua_iswchar(L, -1)) {
            ToUtf8(L);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            enc_string(t, L, s, len);
            lua_pop(L, 1);
            break;
        }
        if (lua_isidentifier(L, -1)) {
            lua_identifier_push_string(L, -1);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            enc_string(t, L, s, len);
            lua_pop(L, 1);
            break;
        }
        if (lua_isdatetime(L, -1)) {
            lua_datetime_push_string(L, -1);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            // Emit datetime without quotes — TOML has a native datetime type.
            tbuf_emit(t, L, s, len);
            lua_pop(L, 1);
            break;
        }
        if (lua_isdecimal(L, -1)) {
            lua_decimal_push_string(L, -1);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            enc_string(t, L, s, len);
            lua_pop(L, 1);
            break;
        }
        tbuf_emitlit(t, L, "\"\"");
        break;
    default:
        tbuf_emitlit(t, L, "\"\"");
        break;
    }
}

static void enc_inline_array(LuaToml* t, lua_State* L, int depth) {
    int        tbl = lua_gettop(L);
    lua_Integer n   = (lua_Integer)lua_rawlen(L, tbl);
    rec_push(t, L, (uintptr_t)lua_topointer(L, tbl));
    tbuf_emitc(t, L, '[');
    for (lua_Integer i = 1; i <= n; i++) {
        if (i > 1)
            tbuf_emitlit(t, L, ", ");
        lua_rawgeti(L, tbl, i);
        enc_value(t, L, depth + 1, 1);
        lua_pop(L, 1);
    }
    tbuf_emitc(t, L, ']');
    rec_pop(t);
}

static void enc_inline_table(LuaToml* t, lua_State* L, int depth) {
    int tbl   = lua_gettop(L);
    int first = 1;
    rec_push(t, L, (uintptr_t)lua_topointer(L, tbl));
    tbuf_emitlit(t, L, "{");
    lua_pushnil(L);
    while (lua_next(L, tbl) != 0) {
        if (!first)
            tbuf_emitlit(t, L, ", ");
        first = 0;
        // key
        lua_pushvalue(L, -2);
        size_t      klen;
        const char* k = lua_tolstring(L, -1, &klen);
        enc_key(t, L, k, klen);
        lua_pop(L, 1);
        tbuf_emitlit(t, L, " = ");
        enc_value(t, L, depth + 1, 1);
        lua_pop(L, 1);
    }
    tbuf_emitlit(t, L, "}");
    rec_pop(t);
}

// =============================================================================
// Encoder — top-level section emitter
// =============================================================================

// Emit indent (2 spaces per level) when pretty mode is on.
static void enc_indent(LuaToml* t, lua_State* L, int depth) {
    if (!t->pretty)
        return;
    for (int i = 0; i < depth; i++)
        tbuf_emitlit(t, L, "  ");
}

// Forward: emit key = value lines for all scalar/array keys in a table,
// then recurse into sub-tables as [section] blocks.
static void enc_table_body(LuaToml* t, lua_State* L, int depth,
    const char* path, size_t pathLen);

static void enc_table_body(LuaToml* t, lua_State* L, int depth,
    const char* path, size_t pathLen) {
    int tbl = lua_gettop(L);
    rec_push(t, L, (uintptr_t)lua_topointer(L, tbl));

    // First pass: emit all scalar and array-of-scalars key=value pairs.
    lua_pushnil(L);
    while (lua_next(L, tbl) != 0) {
        int vtype = lua_type(L, -1);
        if (vtype == LUA_TTABLE) {
            // Defer sub-tables and arrays of tables to second pass.
            lua_pop(L, 1);
            continue;
        }
        // key
        lua_pushvalue(L, -2);
        size_t      klen;
        const char* k = lua_tolstring(L, -1, &klen);
        enc_indent(t, L, depth);
        enc_key(t, L, k, klen);
        lua_pop(L, 1);
        tbuf_emitlit(t, L, " = ");
        enc_value(t, L, depth, 0);
        tbuf_emitc(t, L, '\n');
        lua_pop(L, 1);
    }

    // Second pass: emit sub-tables as [section] or [[array]] headers.
    lua_pushnil(L);
    while (lua_next(L, tbl) != 0) {
        if (lua_type(L, -1) != LUA_TTABLE) {
            lua_pop(L, 1);
            continue;
        }

        lua_pushvalue(L, -2);
        size_t      klen;
        const char* k = lua_tolstring(L, -1, &klen);
        lua_pop(L, 1);

        // Build the dotted path for this sub-key.
        size_t newPathLen = pathLen + (pathLen > 0 ? 1 : 0) + klen;
        char*  newPath    = (char*)kitsune_malloc(newPathLen + 1);
        if (!newPath)
            luaL_error(L, "Toml: out of memory");
        if (pathLen > 0) {
            memcpy(newPath, path, pathLen);
            newPath[pathLen] = '.';
            memcpy(newPath + pathLen + 1, k, klen);
        } else {
            memcpy(newPath, k, klen);
        }
        newPath[newPathLen] = '\0';

        // Determine: array-of-tables or plain sub-table?
        if (is_array(L)) {
            // Array of tables: emit [[path]] for each element.
            int        arr  = lua_gettop(L);
            lua_Integer n   = (lua_Integer)lua_rawlen(L, arr);
            for (lua_Integer i = 1; i <= n; i++) {
                lua_rawgeti(L, arr, i);
                if (lua_type(L, -1) == LUA_TTABLE) {
                    if (t->pretty && depth > 0)
                        tbuf_emitc(t, L, '\n');
                    tbuf_emitlit(t, L, "[[");
                    enc_key(t, L, newPath, newPathLen);
                    tbuf_emitlit(t, L, "]]\n");
                    enc_table_body(t, L, 0, newPath, newPathLen);
                } else {
                    // Array of non-tables — emit as inline array value instead.
                    enc_indent(t, L, depth);
                    enc_key(t, L, k, klen);
                    tbuf_emitlit(t, L, " = ");
                    lua_pushvalue(L, arr);
                    enc_inline_array(t, L, depth);
                    lua_pop(L, 1);
                    tbuf_emitc(t, L, '\n');
                    lua_pop(L, 1);
                    break;
                }
                lua_pop(L, 1);
            }
        } else {
            // Plain sub-table: emit [path] header.
            if (t->pretty && depth > 0)
                tbuf_emitc(t, L, '\n');
            tbuf_emitlit(t, L, "[");
            enc_key(t, L, newPath, newPathLen);
            tbuf_emitlit(t, L, "]\n");
            enc_table_body(t, L, 0, newPath, newPathLen);
        }

        kitsune_free(newPath);
        lua_pop(L, 1);
    }

    rec_pop(t);
}

// =============================================================================
// Decoder — tomlc99 DOM -> Lua tables
// =============================================================================

static void dec_table(lua_State* L, toml_table_t* tab);
static void dec_array(lua_State* L, toml_array_t* arr);

static void dec_array(lua_State* L, toml_array_t* arr) {
    char kind = toml_array_kind(arr);
    int  n    = toml_array_nelem(arr);
    lua_createtable(L, n, 0);

    for (int i = 0; i < n; i++) {
        if (kind == 't') {
            toml_table_t* sub = toml_table_at(arr, i);
            dec_table(L, sub);
        } else if (kind == 'a') {
            toml_array_t* sub = toml_array_at(arr, i);
            dec_array(L, sub);
        } else {
            // 'v' (value) or 'm' (mixed) — try each type in order.
            toml_datum_t d;
            d = toml_string_at(arr, i);
            if (d.ok) {
                lua_pushstring(L, d.u.s);
                free(d.u.s);
                goto pushed;
            }
            d = toml_int_at(arr, i);
            if (d.ok) {
                lua_pushinteger(L, (lua_Integer)d.u.i);
                goto pushed;
            }
            d = toml_double_at(arr, i);
            if (d.ok) {
                lua_pushnumber(L, (lua_Number)d.u.d);
                goto pushed;
            }
            d = toml_bool_at(arr, i);
            if (d.ok) {
                lua_pushboolean(L, d.u.b);
                goto pushed;
            }
            d = toml_timestamp_at(arr, i);
            if (d.ok) {
                // Push timestamp as an ISO 8601 string.
                char buf[64];
                toml_timestamp_t* ts = d.u.ts;
                if (ts->year && ts->hour) {
                    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d%s",
                             *ts->year, *ts->month, *ts->day,
                             *ts->hour, *ts->minute, *ts->second,
                             ts->z ? ts->z : "");
                } else if (ts->year) {
                    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                             *ts->year, *ts->month, *ts->day);
                } else {
                    snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                             *ts->hour, *ts->minute, *ts->second);
                }
                free(ts);
                lua_pushstring(L, buf);
                goto pushed;
            }
            lua_pushnil(L);
            pushed:;
        }
        lua_rawseti(L, -2, i + 1);
    }
}

static void dec_table(lua_State* L, toml_table_t* tab) {
    lua_newtable(L);
    int idx = 0;
    const char* key;
    while ((key = toml_key_in(tab, idx++)) != NULL) {
        lua_pushstring(L, key);

        // Try sub-table first.
        toml_table_t* sub = toml_table_in(tab, key);
        if (sub) {
            dec_table(L, sub);
            lua_rawset(L, -3);
            continue;
        }

        // Try array.
        toml_array_t* arr = toml_array_in(tab, key);
        if (arr) {
            dec_array(L, arr);
            lua_rawset(L, -3);
            continue;
        }

        // Try scalar values.
        toml_datum_t d;
        d = toml_string_in(tab, key);
        if (d.ok) {
            lua_pushstring(L, d.u.s);
            free(d.u.s);
            lua_rawset(L, -3);
            continue;
        }
        d = toml_int_in(tab, key);
        if (d.ok) {
            lua_pushinteger(L, (lua_Integer)d.u.i);
            lua_rawset(L, -3);
            continue;
        }
        d = toml_double_in(tab, key);
        if (d.ok) {
            lua_pushnumber(L, (lua_Number)d.u.d);
            lua_rawset(L, -3);
            continue;
        }
        d = toml_bool_in(tab, key);
        if (d.ok) {
            lua_pushboolean(L, d.u.b);
            lua_rawset(L, -3);
            continue;
        }
        d = toml_timestamp_in(tab, key);
        if (d.ok) {
            char buf[64];
            toml_timestamp_t* ts = d.u.ts;
            if (ts->year && ts->hour) {
                snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d%s",
                         *ts->year, *ts->month, *ts->day,
                         *ts->hour, *ts->minute, *ts->second,
                         ts->z ? ts->z : "");
            } else if (ts->year) {
                snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
                         *ts->year, *ts->month, *ts->day);
            } else {
                snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                         *ts->hour, *ts->minute, *ts->second);
            }
            free(ts);
            lua_pushstring(L, buf);
            lua_rawset(L, -3);
            continue;
        }

        // Unknown — push nil value, pop the key.
        lua_pop(L, 1);
    }
}

// =============================================================================
// Lua API entry points
// =============================================================================

int lua_toml_encode(lua_State* L) {
    LuaToml* t = lua_toml_check(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);

    t->outLen = 0;
    t->recLen = 0;

    lua_pushvalue(L, 2);
    enc_table_body(t, L, 0, "", 0);
    lua_pop(L, 1);

    lua_pushlstring(L, t->out ? t->out : "", t->outLen);
    return 1;
}

int lua_toml_decode(lua_State* L) {
    lua_toml_check(L, 1);
    size_t      len;
    const char* src = luaL_checklstring(L, 2, &len);

    // toml_parse requires a NUL-terminated, mutable string.
    char* buf = (char*)kitsune_malloc(len + 1);
    if (!buf)
        luaL_error(L, "Toml: out of memory");
    memcpy(buf, src, len);
    buf[len] = '\0';

    char errbuf[256];
    toml_table_t* tab = toml_parse(buf, errbuf, sizeof(errbuf));
    kitsune_free(buf);

    if (!tab) {
        lua_pushnil(L);
        lua_pushstring(L, errbuf);
        return 2;
    }

    dec_table(L, tab);
    toml_free(tab);
    return 1;
}
