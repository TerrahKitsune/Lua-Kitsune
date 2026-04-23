#include "luaidentifier.h"
#include <string.h>
#include <stdio.h>
#include <atomic>
#include "mem.h"
#ifdef _WIN32
#include <objbase.h>
#include <stdlib.h>
#else
#include <sys/random.h>
#include <time.h>
#endif

// ── Random byte generators ────────────────────────────────────────────────────

#ifdef _WIN32
static int generate_uuid_bytes(uint8_t* out) {
    GUID guid;
    if (CoCreateGuid(&guid) != S_OK)
        return 0;
    unsigned long d1 = _byteswap_ulong(guid.Data1);
    unsigned short d2 = _byteswap_ushort(guid.Data2);
    unsigned short d3 = _byteswap_ushort(guid.Data3);
    memcpy(&out[0], &d1, 4);
    memcpy(&out[4], &d2, 2);
    memcpy(&out[6], &d3, 2);
    memcpy(&out[8], guid.Data4, 8);
    return 1;
}

static int generate_random_bytes(uint8_t* out, int n) {
    GUID g;
    if (CoCreateGuid(&g) != S_OK)
        return 0;
    memcpy(out, &g, n);
    return 1;
}
#else
static int generate_uuid_bytes(uint8_t* out) {
    if (getrandom(out, 16, 0) != (ssize_t)16)
        return 0;
    out[6] = (out[6] & 0x0F) | 0x40;
    out[8] = (out[8] & 0x3F) | 0x80;
    return 1;
}

static int generate_random_bytes(uint8_t* out, int n) {
    return getrandom(out, (size_t)n, 0) == (ssize_t)n;
}
#endif

// ── OID counter (MongoDB ObjectID: 4-byte timestamp + 5-byte random + 3-byte counter) ──

static std::atomic<uint32_t> g_oid_counter(0);

static int generate_oid_bytes(uint8_t* out) {
    uint32_t ts = (uint32_t)time(NULL);
    uint8_t rand5[5];
    if (!generate_random_bytes(rand5, 5))
        return 0;
    uint32_t cnt = (g_oid_counter.fetch_add(1) + 1) & 0xFFFFFF;

    out[0] = (ts >> 24) & 0xFF;
    out[1] = (ts >> 16) & 0xFF;
    out[2] = (ts >> 8)  & 0xFF;
    out[3] =  ts        & 0xFF;
    memcpy(&out[4], rand5, 5);
    out[9]  = (cnt >> 16) & 0xFF;
    out[10] = (cnt >> 8)  & 0xFF;
    out[11] =  cnt        & 0xFF;
    return 1;
}

// ── String formatters ─────────────────────────────────────────────────────────

static void uuid_bytes_to_string(const uint8_t* b, char* out) {
    sprintf(out,
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        b[0],  b[1],  b[2],  b[3],
        b[4],  b[5],
        b[6],  b[7],
        b[8],  b[9],
        b[10], b[11], b[12], b[13], b[14], b[15]);
}

static void oid_bytes_to_string(const uint8_t* b, char* out) {
    for (int i = 0; i < 12; i++)
        sprintf(&out[i * 2], "%02x", b[i]);
    out[24] = '\0';
}

// ── push / to helpers ─────────────────────────────────────────────────────────

LuaIdentifier* lua_pushidentifier(lua_State* L) {
    LuaIdentifier* id = (LuaIdentifier*)lua_newuserdata(L, sizeof(LuaIdentifier));
    if (!id) {
        luaL_error(L, "out of memory");
        return NULL;
    }
    memset(id, 0, sizeof(LuaIdentifier));
    luaL_getmetatable(L, LUAIDENTIFIER);
    lua_setmetatable(L, -2);
    return id;
}

LuaIdentifier* lua_toidentifier(lua_State* L, int index) {
    LuaIdentifier* id = (LuaIdentifier*)luaL_checkudata(L, index, LUAIDENTIFIER);
    if (!id)
        luaL_error(L, "parameter is not an %s", LUAIDENTIFIER);
    return id;
}

// ── Constructor functions ─────────────────────────────────────────────────────

int identifier_newuuid(lua_State* L) {
    LuaIdentifier* id = lua_pushidentifier(L);
    if (!generate_uuid_bytes(id->bytes)) {
        lua_pushnil(L);
        return 1;
    }
    id->type = IDENTIFIER_UUID;
    id->len  = 16;
    return 1;
}

int identifier_newoid(lua_State* L) {
    LuaIdentifier* id = lua_pushidentifier(L);
    if (!generate_oid_bytes(id->bytes)) {
        lua_pushnil(L);
        return 1;
    }
    id->type = IDENTIFIER_OID;
    id->len  = 12;
    return 1;
}

LuaIdentifier* lua_pushidentifier_fromstring(lua_State* L, const char* str, size_t len) {
    if (len == 36 && str[8] == '-' && str[13] == '-' && str[18] == '-' && str[23] == '-') {
        LuaIdentifier* id = lua_pushidentifier(L);
        char hex[33];
        int j = 0;
        for (int i = 0; i < 36; i++) {
            if (str[i] != '-')
                hex[j++] = str[i];
        }
        hex[32] = '\0';
        for (int i = 0; i < 16; i++) {
            unsigned int byte = 0;
            sscanf(&hex[i * 2], "%02x", &byte);
            id->bytes[i] = (uint8_t)byte;
        }
        id->type = IDENTIFIER_UUID;
        id->len  = 16;
        return id;
    }
    lua_pushnil(L);
    return NULL;
}

int identifier_fromstring(lua_State* L) {
    size_t len;
    const char* str = luaL_checklstring(L, 1, &len);

    if (lua_pushidentifier_fromstring(L, str, len))
        return 1;

    if (len == 24) {
        LuaIdentifier* id = lua_pushidentifier(L);
        for (int i = 0; i < 12; i++) {
            unsigned int byte = 0;
            if (sscanf(&str[i * 2], "%02x", &byte) != 1) {
                lua_pop(L, 1);
                lua_pushnil(L);
                return 1;
            }
            id->bytes[i] = (uint8_t)byte;
        }
        id->type = IDENTIFIER_OID;
        id->len  = 12;
        return 1;
    }

    // nil was already pushed by lua_pushidentifier_fromstring on the non-UUID path
    return 1;
}

int identifier_frombytes(lua_State* L) {
    size_t len;
    const char* bytes = luaL_checklstring(L, 1, &len);
    LuaIdentifier* id = lua_pushidentifier(L);

    if (len == 16) {
        memcpy(id->bytes, bytes, 16);
        id->type = IDENTIFIER_UUID;
        id->len  = 16;
    } else if (len == 12) {
        memcpy(id->bytes, bytes, 12);
        id->type = IDENTIFIER_OID;
        id->len  = 12;
    } else {
        lua_pop(L, 1);
        lua_pushnil(L);
        return 1;
    }
    return 1;
}

// ── Instance methods ──────────────────────────────────────────────────────────

int identifier_gettype(lua_State* L) {
    LuaIdentifier* id = lua_toidentifier(L, 1);
    switch (id->type) {
    case IDENTIFIER_UUID:
        lua_pushliteral(L, "UUID");
        break;
    case IDENTIFIER_OID:
        lua_pushliteral(L, "OID");
        break;
    default:
        lua_pushliteral(L, "unknown");
        break;
    }
    return 1;
}

int identifier_asbytes(lua_State* L) {
    LuaIdentifier* id = lua_toidentifier(L, 1);
    lua_pushlstring(L, (const char*)id->bytes, (size_t)id->len);
    return 1;
}

int identifier_isempty(lua_State* L) {
    LuaIdentifier* id = lua_toidentifier(L, 1);
    for (int i = 0; i < id->len; i++) {
        if (id->bytes[i] != 0) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    lua_pushboolean(L, 1);
    return 1;
}

int identifier_asstring(lua_State* L) {
    return identifier_tostring(L);
}

// ── Metamethods ───────────────────────────────────────────────────────────────

int identifier_eq(lua_State* L) {
    LuaIdentifier* a = lua_toidentifier(L, 1);
    LuaIdentifier* b = lua_toidentifier(L, 2);
    lua_pushboolean(L,
        a->type == b->type &&
        a->len  == b->len  &&
        memcmp(a->bytes, b->bytes, (size_t)a->len) == 0);
    return 1;
}

int identifier_tostring(lua_State* L) {
    LuaIdentifier* id = lua_toidentifier(L, 1);
    char buf[37];
    if (id->type == IDENTIFIER_UUID) {
        uuid_bytes_to_string(id->bytes, buf);
        lua_pushstring(L, buf);
    } else {
        oid_bytes_to_string(id->bytes, buf);
        lua_pushstring(L, buf);
    }
    return 1;
}

int lua_isidentifier(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TUSERDATA) return 0;
    if (!lua_getmetatable(L, index)) return 0;
    luaL_getmetatable(L, LUAIDENTIFIER);
    int result = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    return result;
}

void lua_identifier_push_string(lua_State* L, int index) {
    LuaIdentifier* id = (LuaIdentifier*)lua_touserdata(L, index);
    char buf[37];
    if (id->type == IDENTIFIER_UUID)
        uuid_bytes_to_string(id->bytes, buf);
    else
        oid_bytes_to_string(id->bytes, buf);
    lua_pushstring(L, buf);
}
