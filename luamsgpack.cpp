#include "luamsgpack.h"
#include "stream.h"
#include "luawchar.h"
#include "luaidentifier.h"
#include "luadatetime.h"
#include "luadecimal.h"
#include "luauint.h"
#include "luatimespan.h"

// =============================================================================
// Instance management
// =============================================================================

LuaMsgPack* lua_msgpack_push(lua_State* L) {
    LuaMsgPack* m = (LuaMsgPack*)lua_newuserdata(L, sizeof(LuaMsgPack));
    memset(m, 0, sizeof(LuaMsgPack));
    msgpack_sbuffer_init(&m->sbuf);
    luaL_setmetatable(L, LUAMSGPACK);
    return m;
}

LuaMsgPack* lua_msgpack_check(lua_State* L, int idx) {
    return (LuaMsgPack*)luaL_checkudata(L, idx, LUAMSGPACK);
}

int lua_msgpack_gc(lua_State* L) {
    LuaMsgPack* m = lua_msgpack_check(L, 1);
    msgpack_sbuffer_destroy(&m->sbuf);
    if (m->rec) {
        kitsune_free(m->rec);
        m->rec = NULL;
    }
    return 0;
}

int lua_msgpack_tostring(lua_State* L) {
    lua_pushfstring(L, "MsgPack: %p", lua_msgpack_check(L, 1));
    return 1;
}

int lua_msgpack_new(lua_State* L) {
    lua_msgpack_push(L);
    return 1;
}

// =============================================================================
// Anti-recursion
// =============================================================================

static void rec_push(LuaMsgPack* m, lua_State* L, uintptr_t addr) {
    for (size_t i = 0; i < m->recLen; i++) {
        if (m->rec[i] == addr)
            luaL_error(L, "MsgPack: recursion detected");
    }
    if (m->recLen == m->recCap) {
        size_t     cap = m->recCap ? m->recCap * 2 : 8;
        uintptr_t* p   = (uintptr_t*)kitsune_realloc(m->rec, cap * sizeof(uintptr_t));
        if (!p)
            luaL_error(L, "MsgPack: out of memory");
        m->rec    = p;
        m->recCap = cap;
    }
    m->rec[m->recLen++] = addr;
}

static void rec_pop(LuaMsgPack* m) {
    if (m->recLen > 0)
        m->recLen--;
}

// =============================================================================
// Encoder
// =============================================================================

static void enc_value(LuaMsgPack* m, msgpack_packer* pk, lua_State* L);

static void enc_string_value(msgpack_packer* pk, const char* s, size_t len) {
    msgpack_pack_str(pk, len);
    if (len > 0)
        msgpack_pack_str_body(pk, s, len);
}

static void enc_table(LuaMsgPack* m, msgpack_packer* pk, lua_State* L) {
    int tbl = lua_gettop(L);
    rec_push(m, L, (uintptr_t)lua_topointer(L, tbl));

    // Classify: detect array (sequential integer keys 1..n) vs map
    lua_Integer n     = (lua_Integer)lua_rawlen(L, tbl);
    lua_Integer count = 0;
    bool        seq   = true;

    lua_pushnil(L);
    while (lua_next(L, tbl) != 0) {
        lua_pop(L, 1);
        count++;
        if (seq) {
            if (!lua_isinteger(L, -1)) {
                seq = false;
            } else {
                lua_Integer k = lua_tointeger(L, -1);
                if (k < 1 || k > n)
                    seq = false;
            }
            if (!seq) {
                lua_pop(L, 1);
                break;
            }
        }
    }
    seq = seq && (count == n);

    if (seq) {
        msgpack_pack_array(pk, (uint32_t)n);
        for (lua_Integer i = 1; i <= n; i++) {
            lua_rawgeti(L, tbl, i);
            enc_value(m, pk, L);
            lua_pop(L, 1);
        }
    } else {
        // Count total pairs for the map header
        lua_Integer pairs = 0;
        lua_pushnil(L);
        while (lua_next(L, tbl) != 0) {
            lua_pop(L, 1);
            pairs++;
        }
        msgpack_pack_map(pk, (uint32_t)pairs);
        lua_pushnil(L);
        while (lua_next(L, tbl) != 0) {
            // key at -2, value at -1 — push a copy of the key for enc_value
            lua_pushvalue(L, -2);
            enc_value(m, pk, L);
            lua_pop(L, 1);
            enc_value(m, pk, L);
            lua_pop(L, 1);
        }
    }

    rec_pop(m);
}

static void enc_value(LuaMsgPack* m, msgpack_packer* pk, lua_State* L) {
    switch (lua_type(L, -1)) {
    case LUA_TNIL:
        msgpack_pack_nil(pk);
        break;
    case LUA_TBOOLEAN:
        if (lua_toboolean(L, -1))
            msgpack_pack_true(pk);
        else
            msgpack_pack_false(pk);
        break;
    case LUA_TNUMBER:
        if (lua_isinteger(L, -1)) {
            msgpack_pack_int64(pk, (int64_t)lua_tointeger(L, -1));
        } else {
            msgpack_pack_double(pk, (double)lua_tonumber(L, -1));
        }
        break;
    case LUA_TSTRING: {
        size_t      len;
        const char* s = lua_tolstring(L, -1, &len);
        enc_string_value(pk, s, len);
        break;
    }
    case LUA_TTABLE:
        enc_table(m, pk, L);
        break;
    case LUA_TUSERDATA:
        if (lua_isuint(L, -1)) {
            msgpack_pack_uint64(pk, lua_touint(L, -1)->value);
            break;
        }
        if (lua_istimespan(L, -1)) {
            // Encode as a string so the receiver can reconstruct the duration from the canonical form.
            lua_timespan_push_string(L, -1);
            size_t len;
            const char* s = lua_tolstring(L, -1, &len);
            if (s) msgpack_pack_str_with_body(pk, s, len);
            lua_pop(L, 1);
            break;
        }
        if (lua_iswchar(L, -1)) {
            ToUtf8(L);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            enc_string_value(pk, s, len);
            lua_pop(L, 1);
            break;
        }
        if (lua_isidentifier(L, -1)) {
            lua_identifier_push_string(L, -1);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            enc_string_value(pk, s, len);
            lua_pop(L, 1);
            break;
        }
        if (lua_isdatetime(L, -1)) {
            lua_datetime_push_string(L, -1);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            enc_string_value(pk, s, len);
            lua_pop(L, 1);
            break;
        }
        if (lua_isdecimal(L, -1)) {
            lua_decimal_push_string(L, -1);
            size_t      len;
            const char* s = lua_tolstring(L, -1, &len);
            enc_string_value(pk, s, len);
            lua_pop(L, 1);
            break;
        }
        if (lua_isstream(L, -1)) {
            LuaStream* s = lua_toluastream(L, -1);
            if (s && (s->Caps & STREAM_CAP_READ) && (s->Caps & STREAM_CAP_SEEK)) {
                lua_Integer saved = lua_stream_curpos(L, s);
                lua_stream_setpos(L, s, 0);
                lua_stream_read_chunk(L, s, 0);
                if (lua_type(L, -1) == LUA_TSTRING) {
                    size_t      len;
                    const char* data = lua_tolstring(L, -1, &len);
                    msgpack_pack_bin(pk, len);
                    if (len > 0)
                        msgpack_pack_bin_body(pk, data, len);
                } else {
                    msgpack_pack_nil(pk);
                }
                lua_pop(L, 1);
                lua_stream_setpos(L, s, saved);
                break;
            }
        }
        msgpack_pack_nil(pk);
        break;
    default:
        // Functions, threads, light userdata — not representable, encode as nil
        msgpack_pack_nil(pk);
        break;
    }
}

// =============================================================================
// Decoder
// =============================================================================

static void dec_object(lua_State* L, const msgpack_object* obj) {
    switch (obj->type) {
    case MSGPACK_OBJECT_NIL:
        lua_pushnil(L);
        break;
    case MSGPACK_OBJECT_BOOLEAN:
        lua_pushboolean(L, obj->via.boolean ? 1 : 0);
        break;
    case MSGPACK_OBJECT_POSITIVE_INTEGER:
        // Fit into lua_Integer if possible; otherwise push a LuaUInt to preserve the full uint64 range.
        if (obj->via.u64 <= (uint64_t)LUA_MAXINTEGER)
            lua_pushinteger(L, (lua_Integer)obj->via.u64);
        else
            lua_pushuint(L)->value = obj->via.u64;
        break;
    case MSGPACK_OBJECT_NEGATIVE_INTEGER:
        lua_pushinteger(L, (lua_Integer)obj->via.i64);
        break;
    case MSGPACK_OBJECT_FLOAT32:
    case MSGPACK_OBJECT_FLOAT64:
        lua_pushnumber(L, (lua_Number)obj->via.f64);
        break;
    case MSGPACK_OBJECT_STR:
        lua_pushlstring(L, obj->via.str.ptr ? obj->via.str.ptr : "", obj->via.str.size);
        break;
    case MSGPACK_OBJECT_BIN:
        // bin → in-memory Stream pre-loaded with the bytes, seeked to 0
        lua_pushluastream(L, (const BYTE*)obj->via.bin.ptr, obj->via.bin.size);
        break;
    case MSGPACK_OBJECT_ARRAY: {
        lua_createtable(L, (int)obj->via.array.size, 0);
        for (uint32_t i = 0; i < obj->via.array.size; i++) {
            dec_object(L, &obj->via.array.ptr[i]);
            lua_rawseti(L, -2, (lua_Integer)(i + 1));
        }
        break;
    }
    case MSGPACK_OBJECT_MAP: {
        lua_createtable(L, 0, (int)obj->via.map.size);
        for (uint32_t i = 0; i < obj->via.map.size; i++) {
            dec_object(L, &obj->via.map.ptr[i].key);
            dec_object(L, &obj->via.map.ptr[i].val);
            lua_rawset(L, -3);
        }
        break;
    }
    case MSGPACK_OBJECT_EXT:
        // ext types are not supported — push nil
        lua_pushnil(L);
        break;
    default:
        lua_pushnil(L);
        break;
    }
}

// =============================================================================
// Lua API
// =============================================================================

int lua_msgpack_encode(lua_State* L) {
    LuaMsgPack* m = lua_msgpack_check(L, 1);
    luaL_checkany(L, 2);

    msgpack_sbuffer_clear(&m->sbuf);
    m->recLen = 0;

    msgpack_packer pk;
    msgpack_packer_init(&pk, &m->sbuf, msgpack_sbuffer_write);

    lua_pushvalue(L, 2);
    enc_value(m, &pk, L);
    lua_pop(L, 1);

    lua_pushlstring(L, m->sbuf.data ? m->sbuf.data : "", m->sbuf.size);
    return 1;
}

int lua_msgpack_decode(lua_State* L) {
    lua_msgpack_check(L, 1);

    if (lua_isstream(L, 2))
        return lua_msgpack_decode_from_stream(L);

    size_t      len;
    const char* data = luaL_checklstring(L, 2, &len);

    msgpack_unpacked result;
    msgpack_unpacked_init(&result);

    size_t                off = 0;
    msgpack_unpack_return ret = msgpack_unpack_next(&result, data, len, &off);

    if (ret == MSGPACK_UNPACK_SUCCESS || ret == MSGPACK_UNPACK_EXTRA_BYTES) {
        dec_object(L, &result.data);
        msgpack_unpacked_destroy(&result);
        return 1;
    }

    msgpack_unpacked_destroy(&result);
    lua_pushnil(L);
    if (ret == MSGPACK_UNPACK_PARSE_ERROR)
        lua_pushstring(L, "MsgPack: parse error");
    else if (ret == MSGPACK_UNPACK_CONTINUE)
        lua_pushstring(L, "MsgPack: incomplete data");
    else
        lua_pushstring(L, "MsgPack: unpack error");
    return 2;
}

// Context struct for the stream-backed packer write callback
typedef struct {
    lua_State* L;
    LuaStream* st;
    int        failed;
} MsgPackStreamCtx;

static int msgpack_stream_write_cb(void* data, const char* buf, size_t len) {
    MsgPackStreamCtx* ctx = (MsgPackStreamCtx*)data;
    if (ctx->failed)
        return -1;
    lua_stream_write_bytes(ctx->L, ctx->st, buf, len);
    lua_pop(ctx->L, 1);
    return 0;
}

int lua_msgpack_encode_into_stream(lua_State* L) {
    LuaMsgPack* m = lua_msgpack_check(L, 1);

    if (!lua_isstream(L, 2))
        return luaL_argerror(L, 2, "stream expected");

    LuaStream* st = lua_toluastream(L, 2);
    if (!(st->Caps & STREAM_CAP_WRITE)) {
        lua_pushboolean(L, false);
        lua_pushstring(L, "stream is not writable");
        return 2;
    }

    luaL_checkany(L, 3);
    m->recLen = 0;

    MsgPackStreamCtx ctx;
    ctx.L      = L;
    ctx.st     = st;
    ctx.failed = 0;

    msgpack_packer pk;
    msgpack_packer_init(&pk, &ctx, msgpack_stream_write_cb);

    lua_pushvalue(L, 3);
    enc_value(m, &pk, L);
    lua_pop(L, 1);

    if (ctx.failed) {
        lua_pushboolean(L, false);
        lua_pushstring(L, "MsgPack: stream write failed");
        return 2;
    }
    lua_pushboolean(L, true);
    return 1;
}

int lua_msgpack_decode_from_stream(lua_State* L) {
    lua_msgpack_check(L, 1);

    if (!lua_isstream(L, 2))
        return luaL_argerror(L, 2, "stream expected");

    LuaStream* st = lua_toluastream(L, 2);
    if (!(st->Caps & STREAM_CAP_READ)) {
        lua_pushnil(L);
        lua_pushstring(L, "stream is not readable");
        return 2;
    }

    // Read all available bytes from the stream into a Lua string
    lua_stream_read_chunk(L, st, 0);
    if (lua_type(L, -1) != LUA_TSTRING) {
        lua_pop(L, 1);
        lua_pushnil(L);
        lua_pushstring(L, "MsgPack: could not read from stream");
        return 2;
    }

    size_t      len;
    const char* data = lua_tolstring(L, -1, &len);

    msgpack_unpacked result;
    msgpack_unpacked_init(&result);

    size_t                off = 0;
    msgpack_unpack_return ret = msgpack_unpack_next(&result, data, len, &off);

    // Seek back any bytes that belonged to the next message
    if (st->Caps & STREAM_CAP_SEEK) {
        size_t unconsumed = len - off;
        if (unconsumed > 0) {
            lua_Integer curPos = lua_stream_curpos(L, st);
            lua_stream_setpos(L, st, curPos - (lua_Integer)unconsumed);
        }
    }

    lua_pop(L, 1);  // pop the string

    if (ret == MSGPACK_UNPACK_SUCCESS || ret == MSGPACK_UNPACK_EXTRA_BYTES) {
        dec_object(L, &result.data);
        msgpack_unpacked_destroy(&result);
        return 1;
    }

    msgpack_unpacked_destroy(&result);
    lua_pushnil(L);
    if (ret == MSGPACK_UNPACK_PARSE_ERROR)
        lua_pushstring(L, "MsgPack: parse error");
    else if (ret == MSGPACK_UNPACK_CONTINUE)
        lua_pushstring(L, "MsgPack: incomplete data");
    else
        lua_pushstring(L, "MsgPack: unpack error");
    return 2;
}
