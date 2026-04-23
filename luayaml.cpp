#include "luayaml.h"
#include "luawchar.h"
#include "luaidentifier.h"
#include "luadatetime.h"
#include "luadecimal.h"
#include "luauint.h"
#include "luatimespan.h"

// =============================================================================
// Instance management
// =============================================================================

LuaYaml* lua_yaml_push(lua_State* L) {
    LuaYaml* y = (LuaYaml*)lua_newuserdata(L, sizeof(LuaYaml));
    memset(y, 0, sizeof(LuaYaml));
    luaL_setmetatable(L, LUAYAML);
    return y;
}

LuaYaml* lua_yaml_check(lua_State* L, int idx) {
    return (LuaYaml*)luaL_checkudata(L, idx, LUAYAML);
}

int lua_yaml_gc(lua_State* L) {
    LuaYaml* y = lua_yaml_check(L, 1);
    if (y->out) {
        kitsune_free(y->out);
        y->out = NULL;
    }
    if (y->rec) {
        kitsune_free(y->rec);
        y->rec = NULL;
    }
    return 0;
}

int lua_yaml_tostring(lua_State* L) {
    lua_pushfstring(L, "Yaml: %p", lua_yaml_check(L, 1));
    return 1;
}

int lua_yaml_new(lua_State* L) {
    int      pretty = lua_isboolean(L, 1) ? lua_toboolean(L, 1) : 0;
    LuaYaml* y      = lua_yaml_push(L);
    y->pretty       = pretty;
    return 1;
}

// =============================================================================
// Anti-recursion
// =============================================================================

static void rec_push(LuaYaml* y, lua_State* L, uintptr_t addr) {
    for (size_t i = 0; i < y->recLen; i++) {
        if (y->rec[i] == addr)
            luaL_error(L, "Yaml: recursion detected");
    }
    if (y->recLen == y->recCap) {
        size_t     cap = y->recCap ? y->recCap * 2 : 8;
        uintptr_t* p   = (uintptr_t*)kitsune_realloc(y->rec, cap * sizeof(uintptr_t));
        if (!p)
            luaL_error(L, "Yaml: out of memory");
        y->rec    = p;
        y->recCap = cap;
    }
    y->rec[y->recLen++] = addr;
}

static void rec_pop(LuaYaml* y) {
    if (y->recLen > 0)
        y->recLen--;
}

// =============================================================================
// Encoder — output buffer helpers
// =============================================================================

static int yaml_write_cb(void* data, unsigned char* buf, size_t size) {
    LuaYaml* y = (LuaYaml*)data;
    size_t needed = y->outLen + size;
    if (needed > y->outCap) {
        size_t cap = y->outCap ? y->outCap * 2 : 512;
        while (cap < needed)
            cap *= 2;
        char* p = (char*)kitsune_realloc(y->out, cap);
        if (!p)
            return 0;
        y->out    = p;
        y->outCap = cap;
    }
    memcpy(y->out + y->outLen, buf, size);
    y->outLen += size;
    return 1;
}

// =============================================================================
// Encoder — push Lua value as YAML events
// =============================================================================

static void enc_value(LuaYaml* y, yaml_emitter_t* em, lua_State* L);

static void enc_string_scalar(yaml_emitter_t* em, lua_State* L,
    const char* str, size_t len, int style) {
    yaml_event_t ev;
    yaml_scalar_event_initialize(
        &ev, NULL, NULL,
        (yaml_char_t*)str, (int)len,
        1, 1,
        (yaml_scalar_style_t)style);
    if (!yaml_emitter_emit(em, &ev))
        luaL_error(L, "Yaml: emitter error during scalar");
}

static void enc_table(LuaYaml* y, yaml_emitter_t* em, lua_State* L) {
    int tbl = lua_gettop(L);
    rec_push(y, L, (uintptr_t)lua_topointer(L, tbl));

    // Classify: sequential integer keys 1..n => sequence, else mapping
    lua_Integer n     = (lua_Integer)lua_rawlen(L, tbl);
    lua_Integer count = 0;
    int         seq   = 1;

    lua_pushnil(L);
    while (lua_next(L, tbl) != 0) {
        lua_pop(L, 1);
        count++;
        if (seq) {
            if (!lua_isinteger(L, -1)) {
                seq = 0;
            } else {
                lua_Integer k = lua_tointeger(L, -1);
                if (k < 1 || k > n)
                    seq = 0;
            }
        }
    }
    seq = seq && (count == n);

    yaml_event_t ev;

    if (seq) {
        yaml_sequence_start_event_initialize(
            &ev, NULL, NULL, 1,
            y->pretty ? YAML_BLOCK_SEQUENCE_STYLE : YAML_FLOW_SEQUENCE_STYLE);
        if (!yaml_emitter_emit(em, &ev))
            luaL_error(L, "Yaml: emitter error during sequence start");

        for (lua_Integer i = 1; i <= n; i++) {
            lua_rawgeti(L, tbl, i);
            enc_value(y, em, L);
            lua_pop(L, 1);
        }

        yaml_sequence_end_event_initialize(&ev);
        if (!yaml_emitter_emit(em, &ev))
            luaL_error(L, "Yaml: emitter error during sequence end");
    } else {
        yaml_mapping_start_event_initialize(
            &ev, NULL, NULL, 1,
            y->pretty ? YAML_BLOCK_MAPPING_STYLE : YAML_FLOW_MAPPING_STYLE);
        if (!yaml_emitter_emit(em, &ev))
            luaL_error(L, "Yaml: emitter error during mapping start");

        lua_pushnil(L);
        while (lua_next(L, tbl) != 0) {
            // key at -2, value at -1
            lua_pushvalue(L, -2);
            enc_value(y, em, L);  // key
            lua_pop(L, 1);
            enc_value(y, em, L);  // value
            lua_pop(L, 1);
        }

        yaml_mapping_end_event_initialize(&ev);
        if (!yaml_emitter_emit(em, &ev))
            luaL_error(L, "Yaml: emitter error during mapping end");
    }

    rec_pop(y);
}

static void enc_value(LuaYaml* y, yaml_emitter_t* em, lua_State* L) {
    int style = YAML_ANY_SCALAR_STYLE;
    switch (lua_type(L, -1)) {
    case LUA_TNIL:
        enc_string_scalar(em, L, "null", 4, style);
        break;
    case LUA_TBOOLEAN:
        if (lua_toboolean(L, -1))
            enc_string_scalar(em, L, "true", 4, style);
        else
            enc_string_scalar(em, L, "false", 5, style);
        break;
    case LUA_TNUMBER: {
        char buf[64];
        int  n;
        if (lua_isinteger(L, -1))
            n = snprintf(buf, sizeof(buf), "%lld", (long long)lua_tointeger(L, -1));
        else
            n = snprintf(buf, sizeof(buf), "%.17g", (double)lua_tonumber(L, -1));
        enc_string_scalar(em, L, buf, (size_t)n, style);
        break;
    }
    case LUA_TSTRING: {
        size_t      len;
        const char* s = lua_tolstring(L, -1, &len);
        enc_string_scalar(em, L, s, len, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
        break;
    }
    case LUA_TTABLE:
        enc_table(y, em, L);
        break;
    case LUA_TUSERDATA:
        if (lua_iswchar(L, -1)) {
            ToUtf8(L);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            enc_string_scalar(em, L, s, len, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
            lua_pop(L, 1);
            break;
        }
        if (lua_isidentifier(L, -1)) {
            lua_identifier_push_string(L, -1);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            enc_string_scalar(em, L, s, len, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
            lua_pop(L, 1);
            break;
        }
        if (lua_isdatetime(L, -1)) {
            lua_datetime_push_string(L, -1);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            enc_string_scalar(em, L, s, len, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
            lua_pop(L, 1);
            break;
        }
        if (lua_isdecimal(L, -1)) {
            lua_decimal_push_string(L, -1);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            enc_string_scalar(em, L, s, len, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
            lua_pop(L, 1);
            break;
        }
        if (lua_isuint(L, -1)) {
            char buf[21];
            int  n = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)lua_touint(L, -1)->value);
            enc_string_scalar(em, L, buf, (size_t)n, YAML_PLAIN_SCALAR_STYLE);
            break;
        }
        if (lua_istimespan(L, -1)) {
            lua_timespan_push_string(L, -1);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            enc_string_scalar(em, L, s, len, YAML_DOUBLE_QUOTED_SCALAR_STYLE);
            lua_pop(L, 1);
            break;
        }
        enc_string_scalar(em, L, "null", 4, style);
        break;
    default:
        // Functions, threads, light userdata — not representable
        enc_string_scalar(em, L, "null", 4, style);
        break;
    }
}

// =============================================================================
// Decoder — walk libyaml events and push Lua values
// =============================================================================

// Forward declaration for recursive mapping/sequence handling.
static void dec_value(lua_State* L, yaml_parser_t* parser);

// Decodes one scalar value from a YAML_SCALAR_EVENT.
// Applies YAML 1.1 type coercion: null, bool, integer, float, else string.
static void dec_scalar(lua_State* L, yaml_event_t* ev) {
    const char* v     = (const char*)ev->data.scalar.value;
    size_t      len   = ev->data.scalar.length;
    int         plain = (ev->data.scalar.style == YAML_PLAIN_SCALAR_STYLE);

    if (!plain) {
        lua_pushlstring(L, v, len);
        return;
    }

    // null
    if (len == 0 || strcmp(v, "null") == 0 || strcmp(v, "~") == 0 ||
        strcmp(v, "Null") == 0 || strcmp(v, "NULL") == 0) {
        lua_pushnil(L);
        return;
    }

    // bool
    if (strcmp(v, "true") == 0 || strcmp(v, "True") == 0 || strcmp(v, "TRUE") == 0 ||
        strcmp(v, "yes") == 0 || strcmp(v, "Yes") == 0 || strcmp(v, "YES") == 0 ||
        strcmp(v, "on") == 0 || strcmp(v, "On") == 0 || strcmp(v, "ON") == 0) {
        lua_pushboolean(L, 1);
        return;
    }
    if (strcmp(v, "false") == 0 || strcmp(v, "False") == 0 || strcmp(v, "FALSE") == 0 ||
        strcmp(v, "no") == 0 || strcmp(v, "No") == 0 || strcmp(v, "NO") == 0 ||
        strcmp(v, "off") == 0 || strcmp(v, "Off") == 0 || strcmp(v, "OFF") == 0) {
        lua_pushboolean(L, 0);
        return;
    }

    // integer
    {
        char*    end = NULL;
        long long iv = strtoll(v, &end, 0);
        if (end && end != v && *end == '\0') {
            lua_pushinteger(L, (lua_Integer)iv);
            return;
        }
    }

    // float
    {
        char*  end = NULL;
        double dv  = strtod(v, &end);
        if (end && end != v && *end == '\0') {
            lua_pushnumber(L, (lua_Number)dv);
            return;
        }
    }

    lua_pushlstring(L, v, len);
}

static void dec_sequence(lua_State* L, yaml_parser_t* parser) {
    lua_newtable(L);
    lua_Integer idx = 1;
    for (;;) {
        yaml_event_t ev;
        if (!yaml_parser_parse(parser, &ev))
            luaL_error(L, "Yaml: parse error in sequence");
        if (ev.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&ev);
            break;
        }
        // Push the event back by processing it directly here.
        yaml_event_type_t type = ev.type;
        if (type == YAML_SCALAR_EVENT) {
            dec_scalar(L, &ev);
            yaml_event_delete(&ev);
        } else if (type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            dec_value(L, parser);
        } else if (type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            dec_value(L, parser);
        } else if (type == YAML_ALIAS_EVENT) {
            // Aliases resolved by libyaml — treat value as nil fallback.
            yaml_event_delete(&ev);
            lua_pushnil(L);
        } else {
            yaml_event_delete(&ev);
            continue;
        }
        lua_rawseti(L, -2, idx++);
    }
}

static void dec_mapping_inner(lua_State* L, yaml_parser_t* parser) {
    lua_newtable(L);
    for (;;) {
        // Parse key
        yaml_event_t kev;
        if (!yaml_parser_parse(parser, &kev))
            luaL_error(L, "Yaml: parse error in mapping key");
        if (kev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&kev);
            break;
        }
        if (kev.type == YAML_SCALAR_EVENT) {
            dec_scalar(L, &kev);
            yaml_event_delete(&kev);
        } else {
            yaml_event_delete(&kev);
            lua_pushnil(L);  // non-scalar key — push nil key (will overwrite)
        }

        // Parse value
        dec_value(L, parser);

        // key at -2, value at -1
        lua_rawset(L, -3);
    }
}

// Consumes one complete YAML value from the parser, pushing it onto the Lua stack.
// Caller must have already consumed the START event (MAPPING_START, SEQUENCE_START)
// or be about to receive SCALAR.  This function handles all three cases by
// consuming the NEXT event from the parser.
static void dec_value(lua_State* L, yaml_parser_t* parser) {
    yaml_event_t ev;
    for (;;) {
        if (!yaml_parser_parse(parser, &ev))
            luaL_error(L, "Yaml: parse error");
        yaml_event_type_t type = ev.type;
        if (type == YAML_SCALAR_EVENT) {
            dec_scalar(L, &ev);
            yaml_event_delete(&ev);
            return;
        }
        if (type == YAML_MAPPING_START_EVENT) {
            yaml_event_delete(&ev);
            dec_mapping_inner(L, parser);
            return;
        }
        if (type == YAML_SEQUENCE_START_EVENT) {
            yaml_event_delete(&ev);
            dec_sequence(L, parser);
            return;
        }
        if (type == YAML_ALIAS_EVENT) {
            // libyaml resolves anchors/aliases internally; aliases appear as scalars.
            // If we see a raw alias event, treat it as nil.
            yaml_event_delete(&ev);
            lua_pushnil(L);
            return;
        }
        if (type == YAML_DOCUMENT_END_EVENT || type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&ev);
            lua_pushnil(L);
            return;
        }
        yaml_event_delete(&ev);
        // Skip STREAM_START, DOCUMENT_START, etc. and loop.
    }
}

// =============================================================================
// Lua API entry points
// =============================================================================

int lua_yaml_encode(lua_State* L) {
    LuaYaml* y = lua_yaml_check(L, 1);
    luaL_checkany(L, 2);

    // Reset output buffer but keep its allocation.
    y->outLen = 0;
    y->recLen = 0;

    yaml_emitter_t em;
    yaml_emitter_initialize(&em);
    yaml_emitter_set_output(&em, yaml_write_cb, y);
    yaml_emitter_set_unicode(&em, 1);

    yaml_event_t ev;

    yaml_stream_start_event_initialize(&ev, YAML_UTF8_ENCODING);
    if (!yaml_emitter_emit(&em, &ev)) {
        yaml_emitter_delete(&em);
        luaL_error(L, "Yaml: emitter error (stream start)");
    }

    yaml_document_start_event_initialize(&ev, NULL, NULL, NULL, 1);
    if (!yaml_emitter_emit(&em, &ev)) {
        yaml_emitter_delete(&em);
        luaL_error(L, "Yaml: emitter error (document start)");
    }

    lua_pushvalue(L, 2);
    enc_value(y, &em, L);
    lua_pop(L, 1);

    yaml_document_end_event_initialize(&ev, 1);
    if (!yaml_emitter_emit(&em, &ev)) {
        yaml_emitter_delete(&em);
        luaL_error(L, "Yaml: emitter error (document end)");
    }

    yaml_stream_end_event_initialize(&ev);
    if (!yaml_emitter_emit(&em, &ev)) {
        yaml_emitter_delete(&em);
        luaL_error(L, "Yaml: emitter error (stream end)");
    }

    yaml_emitter_delete(&em);

    lua_pushlstring(L, y->out ? y->out : "", y->outLen);
    return 1;
}

int lua_yaml_decode(lua_State* L) {
    lua_yaml_check(L, 1);

    size_t      len;
    const char* src = luaL_checklstring(L, 2, &len);

    yaml_parser_t parser;
    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, (const unsigned char*)src, len);

    dec_value(L, &parser);

    yaml_parser_delete(&parser);
    return 1;
}
