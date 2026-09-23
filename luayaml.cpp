#include "luayaml.h"
#include "utf8bom.h"
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

// Nesting limit for sequences/mappings; the decoder recurses on the C stack.
#define YAML_DECODE_MAX_DEPTH 1000

// Owns the libyaml parser for the duration of a Decode call. It lives in a
// userdata so a Lua error raised mid-decode (parse error, out of memory) still
// releases the parser when the userdata is collected.
typedef struct {
    yaml_parser_t parser;
    int           live;
} YamlDecoder;

typedef struct {
    yaml_parser_t* parser;
    int            anchors;  // absolute stack index of the anchor-name -> value table
    int            depth;
} YamlDecodeState;

// Stored in the anchor table for anchored nulls, so an alias to a null can be
// told apart from an alias to an anchor that was never defined.
static char yaml_null_anchor;

static int yaml_decoder_gc(lua_State* L) {
    YamlDecoder* d = (YamlDecoder*)lua_touserdata(L, 1);
    if (d && d->live) {
        d->live = 0;
        yaml_parser_delete(&d->parser);
    }
    return 0;
}

static void dec_next(lua_State* L, YamlDecodeState* s, yaml_event_t* ev, const char* where) {
    if (!yaml_parser_parse(s->parser, ev)) {
        yaml_parser_t* p = s->parser;
        luaL_error(L, "Yaml: parse error in %s: %s (line %d, column %d)", where,
                   p->problem ? p->problem : "unknown error",
                   (int)p->problem_mark.line + 1, (int)p->problem_mark.column + 1);
    }
}

// Stores the value on top of the stack under the anchor name at nameIdx.
static void dec_register_anchor(lua_State* L, YamlDecodeState* s, int nameIdx) {
    lua_pushvalue(L, nameIdx);
    if (lua_isnil(L, -2))
        lua_pushlightuserdata(L, &yaml_null_anchor);
    else
        lua_pushvalue(L, -2);
    lua_rawset(L, s->anchors);
}

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

static void dec_node(lua_State* L, YamlDecodeState* s, yaml_event_t* ev);

static void dec_sequence_items(lua_State* L, YamlDecodeState* s) {
    lua_Integer idx = 1;
    for (;;) {
        yaml_event_t ev;
        dec_next(L, s, &ev, "sequence");
        if (ev.type == YAML_SEQUENCE_END_EVENT) {
            yaml_event_delete(&ev);
            return;
        }
        dec_node(L, s, &ev);
        lua_rawseti(L, -2, idx++);
    }
}

// Copies the entries of the merge source on top of the stack into tbl, keeping
// keys tbl already has. Non-table sources are ignored.
static void dec_merge_from(lua_State* L, int tbl) {
    if (!lua_istable(L, -1))
        return;
    lua_pushnil(L);
    while (lua_next(L, -2)) {
        lua_pushvalue(L, -2);
        if (lua_rawget(L, tbl) == LUA_TNIL) {
            lua_pop(L, 1);
            lua_pushvalue(L, -2);
            lua_insert(L, -2);
            lua_rawset(L, tbl);
        } else {
            lua_pop(L, 2);
        }
    }
}

static void dec_mapping_items(lua_State* L, YamlDecodeState* s) {
    int         tbl     = lua_gettop(L);
    int         merges  = 0;  // stack index of pending `<<` sources, created on first use
    lua_Integer nmerges = 0;
    for (;;) {
        yaml_event_t kev;
        dec_next(L, s, &kev, "mapping key");
        if (kev.type == YAML_MAPPING_END_EVENT) {
            yaml_event_delete(&kev);
            break;
        }
        int isMerge = kev.type == YAML_SCALAR_EVENT &&
                      kev.data.scalar.style == YAML_PLAIN_SCALAR_STYLE &&
                      kev.data.scalar.length == 2 &&
                      memcmp(kev.data.scalar.value, "<<", 2) == 0;
        dec_node(L, s, &kev);

        yaml_event_t vev;
        dec_next(L, s, &vev, "mapping value");
        int valueIsList = vev.type == YAML_SEQUENCE_START_EVENT;
        dec_node(L, s, &vev);

        // YAML 1.1 merge key: `<<: *base` or `<<: [*a, *b]`. Applied after the
        // mapping is complete so explicit keys win regardless of their position,
        // and earlier sources in a list win over later ones.
        if (isMerge && lua_istable(L, -1)) {
            if (!merges) {
                lua_newtable(L);
                lua_insert(L, tbl + 1);
                merges = tbl + 1;
            }
            if (valueIsList) {
                lua_Integer n = (lua_Integer)lua_rawlen(L, -1);
                for (lua_Integer i = 1; i <= n; i++) {
                    lua_rawgeti(L, -1, i);
                    lua_rawseti(L, merges, ++nmerges);
                }
                lua_pop(L, 1);
            } else {
                lua_rawseti(L, merges, ++nmerges);
            }
            lua_pop(L, 1);  // key
            continue;
        }

        // A null or NaN key cannot be stored in a Lua table; drop the pair.
        if (lua_isnil(L, -2) ||
            (lua_type(L, -2) == LUA_TNUMBER && lua_tonumber(L, -2) != lua_tonumber(L, -2))) {
            lua_pop(L, 2);
            continue;
        }
        lua_rawset(L, tbl);
    }

    if (merges) {
        for (lua_Integer i = 1; i <= nmerges; i++) {
            lua_rawgeti(L, merges, i);
            dec_merge_from(L, tbl);
            lua_pop(L, 1);
        }
        lua_remove(L, merges);
    }
}

// Pushes the value for an event that has already been parsed, consuming any
// nested events for collections. Takes ownership of ev.
static void dec_node(lua_State* L, YamlDecodeState* s, yaml_event_t* ev) {
    if (!lua_checkstack(L, 8)) {
        yaml_event_delete(ev);
        luaL_error(L, "Yaml: nesting too deep");
    }
    switch (ev->type) {
    case YAML_SCALAR_EVENT: {
        int nameIdx = 0;
        if (ev->data.scalar.anchor) {
            lua_pushstring(L, (const char*)ev->data.scalar.anchor);
            nameIdx = lua_gettop(L);
        }
        dec_scalar(L, ev);
        yaml_event_delete(ev);
        if (nameIdx) {
            dec_register_anchor(L, s, nameIdx);
            lua_remove(L, nameIdx);
        }
        return;
    }
    case YAML_SEQUENCE_START_EVENT:
    case YAML_MAPPING_START_EVENT: {
        int isSeq = ev->type == YAML_SEQUENCE_START_EVENT;
        const yaml_char_t* anchor = isSeq ? ev->data.sequence_start.anchor
                                          : ev->data.mapping_start.anchor;
        int nameIdx = 0;
        if (anchor) {
            lua_pushstring(L, (const char*)anchor);
            nameIdx = lua_gettop(L);
        }
        yaml_event_delete(ev);
        if (++s->depth > YAML_DECODE_MAX_DEPTH)
            luaL_error(L, "Yaml: nesting deeper than %d levels", YAML_DECODE_MAX_DEPTH);
        lua_newtable(L);
        // Registered before the contents so aliases inside it (self references)
        // resolve to the same table.
        if (nameIdx)
            dec_register_anchor(L, s, nameIdx);
        if (isSeq)
            dec_sequence_items(L, s);
        else
            dec_mapping_items(L, s);
        s->depth--;
        if (nameIdx)
            lua_remove(L, nameIdx);
        return;
    }
    case YAML_ALIAS_EVENT: {
        lua_pushstring(L, (const char*)ev->data.alias.anchor);
        yaml_event_delete(ev);
        lua_pushvalue(L, -1);
        int t = lua_rawget(L, s->anchors);
        if (t == LUA_TNIL)
            luaL_error(L, "Yaml: undefined alias '*%s'", lua_tostring(L, -2));
        if (t == LUA_TLIGHTUSERDATA && lua_touserdata(L, -1) == &yaml_null_anchor) {
            lua_pop(L, 1);
            lua_pushnil(L);
        }
        lua_remove(L, -2);
        return;
    }
    default:
        yaml_event_delete(ev);
        lua_pushnil(L);
        return;
    }
}

// Decodes the first document of the stream; an empty stream decodes to nil.
static void dec_document(lua_State* L, YamlDecodeState* s) {
    for (;;) {
        yaml_event_t ev;
        dec_next(L, s, &ev, "document");
        yaml_event_type_t type = ev.type;
        if (type == YAML_STREAM_START_EVENT || type == YAML_DOCUMENT_START_EVENT) {
            yaml_event_delete(&ev);
            continue;
        }
        if (type == YAML_DOCUMENT_END_EVENT || type == YAML_STREAM_END_EVENT) {
            yaml_event_delete(&ev);
            lua_pushnil(L);
            return;
        }
        dec_node(L, s, &ev);
        return;
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
    skip_utf8_bom(&src, &len);

    YamlDecoder* d = (YamlDecoder*)lua_newuserdatauv(L, sizeof(YamlDecoder), 0);
    d->live = 0;
    if (luaL_newmetatable(L, "Kitsune.YamlDecoder")) {
        lua_pushcfunction(L, yaml_decoder_gc);
        lua_setfield(L, -2, "__gc");
    }
    lua_setmetatable(L, -2);
    if (!yaml_parser_initialize(&d->parser))
        luaL_error(L, "Yaml: out of memory");
    d->live = 1;
    yaml_parser_set_input_string(&d->parser, (const unsigned char*)src, len);

    lua_newtable(L);  // anchors
    YamlDecodeState s = { &d->parser, lua_gettop(L), 0 };
    dec_document(L, &s);

    d->live = 0;
    yaml_parser_delete(&d->parser);
    return 1;
}
