#include "uint/LuaUInt.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "Mem.h"

// Userdata helpers

LuaUInt* lua_pushuint(lua_State* L) {
    LuaUInt* u = (LuaUInt*)lua_newuserdata(L, sizeof(LuaUInt));
    memset(u, 0, sizeof(LuaUInt));
    luaL_setmetatable(L, LUAUINT);
    return u;
}

LuaUInt* lua_touint(lua_State* L, int index) {
    return (LuaUInt*)luaL_checkudata(L, index, LUAUINT);
}

int lua_isuint(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TUSERDATA) return 0;
    if (!lua_getmetatable(L, index)) return 0;
    luaL_getmetatable(L, LUAUINT);
    int result = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    return result;
}

// String helpers

// Format uint64 as a decimal string into buf (must be at least 21 bytes).
static int UIntFormat(uint64_t v, char* buf, size_t bufsz) {
    if (v == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }
    // Build digits in reverse.
    char tmp[21];
    int n = 0;
    while (v > 0) {
        tmp[n++] = (char)('0' + (int)(v % 10));
        v /= 10;
    }
    if (n >= (int)bufsz)
        n = (int)bufsz - 1;
    for (int i = 0; i < n; i++)
        buf[i] = tmp[n - 1 - i];
    buf[n] = '\0';
    return n;
}

void lua_pushuintstring(lua_State* L, int index) {
    LuaUInt* u = (LuaUInt*)lua_touserdata(L, index);
    char buf[21];
    UIntFormat(u->value, buf, sizeof(buf));
    lua_pushstring(L, buf);
}

// Parse an unsigned decimal string (no sign allowed, no fractions).
// Returns 1 on success, 0 on failure. Does not touch the Lua stack.
static int UIntParseC(const char* s, size_t len, uint64_t* out) {
    if (!s || len == 0)
        return 0;
    size_t i = 0;
    // Allow an optional leading '+'.
    if (s[i] == '+')
        i++;
    if (i >= len)
        return 0;
    uint64_t v = 0;
    int hasDigit = 0;
    for (; i < len; i++) {
        char c = s[i];
        if (!isdigit((unsigned char)c))
            return 0;
        hasDigit = 1;
        uint64_t digit = (uint64_t)(c - '0');
        // Check for overflow: v * 10 + digit > UINT64_MAX
        if (v > (UINT64_MAX - digit) / 10)
            return 0;
        v = v * 10 + digit;
    }
    if (!hasDigit)
        return 0;
    *out = v;
    return 1;
}

// Coerce argument at stack index to a uint64_t value.
// Accepts: LuaUInt, Lua integer (reinterpret bits), Lua number (truncate), string.
// Returns 1 on success, 0 on failure (does not error — caller decides).
static int CoerceUInt(lua_State* L, int index, uint64_t* out) {
    if (lua_isuint(L, index)) {
        *out = lua_touint(L, index)->value;
        return 1;
    }
    if (lua_isinteger(L, index)) {
        // Reinterpret the bit pattern — negative int64 maps to large uint64.
        *out = (uint64_t)lua_tointeger(L, index);
        return 1;
    }
    if (lua_type(L, index) == LUA_TNUMBER) {
        double d = (double)lua_tonumber(L, index);
        if (d < 0.0 || d > (double)UINT64_MAX)
            return 0;
        *out = (uint64_t)d;
        return 1;
    }
    if (lua_type(L, index) == LUA_TSTRING) {
        size_t len;
        const char* s = lua_tolstring(L, index, &len);
        return UIntParseC(s, len, out);
    }
    return 0;
}

// Constructors

int UInt_FromString(lua_State* L) {
    size_t len;
    const char* s = luaL_checklstring(L, 1, &len);
    uint64_t v = 0;
    if (!UIntParseC(s, len, &v)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushuint(L)->value = v;
    return 1;
}

int UInt_FromNumber(lua_State* L) {
    double d = luaL_checknumber(L, 1);
    if (d < 0.0 || d > (double)UINT64_MAX) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushuint(L)->value = (uint64_t)d;
    return 1;
}

int UInt_FromUnsigned(lua_State* L) {
    lua_Integer i = luaL_checkinteger(L, 1);
    // Reinterpret the signed int64 bit pattern as uint64.
    lua_pushuint(L)->value = (uint64_t)i;
    return 1;
}

int UInt_Zero(lua_State* L) {
    lua_pushuint(L); // already zeroed
    return 1;
}

// Inspection

int UInt_ToString(lua_State* L) {
    LuaUInt* u = lua_touint(L, 1);
    char buf[21];
    UIntFormat(u->value, buf, sizeof(buf));
    lua_pushstring(L, buf);
    return 1;
}

int UInt_ToNumber(lua_State* L) {
    LuaUInt* u = lua_touint(L, 1);
    lua_pushnumber(L, (lua_Number)u->value);
    return 1;
}

int UInt_ToInteger(lua_State* L) {
    LuaUInt* u = lua_touint(L, 1);
    // Truncate to the lower 63 bits (signed int64 range).
    lua_pushinteger(L, (lua_Integer)(u->value & (uint64_t)LUA_MAXINTEGER));
    return 1;
}

int UInt_ToUnsigned(lua_State* L) {
    LuaUInt* u = lua_touint(L, 1);
    // Reinterpret the uint64 bit pattern as a signed int64.
    lua_pushinteger(L, (lua_Integer)u->value);
    return 1;
}

int UInt_IsZero(lua_State* L) {
    LuaUInt* u = lua_touint(L, 1);
    lua_pushboolean(L, u->value == 0 ? 1 : 0);
    return 1;
}

// Arithmetic

int UInt_Add(lua_State* L) {
    uint64_t a, b;
    if (!CoerceUInt(L, 1, &a) || !CoerceUInt(L, 2, &b))
        return luaL_error(L, "UInt: cannot add incompatible types");
    lua_pushuint(L)->value = a + b; // wraps on overflow, matching unsigned semantics
    return 1;
}

int UInt_Sub(lua_State* L) {
    uint64_t a, b;
    if (!CoerceUInt(L, 1, &a) || !CoerceUInt(L, 2, &b))
        return luaL_error(L, "UInt: cannot subtract incompatible types");
    lua_pushuint(L)->value = a - b; // wraps on underflow, matching unsigned semantics
    return 1;
}

int UInt_Mul(lua_State* L) {
    uint64_t a, b;
    if (!CoerceUInt(L, 1, &a) || !CoerceUInt(L, 2, &b))
        return luaL_error(L, "UInt: cannot multiply incompatible types");
    lua_pushuint(L)->value = a * b;
    return 1;
}

int UInt_Div(lua_State* L) {
    uint64_t a, b;
    if (!CoerceUInt(L, 1, &a) || !CoerceUInt(L, 2, &b))
        return luaL_error(L, "UInt: cannot divide incompatible types");
    if (b == 0)
        return luaL_error(L, "UInt: division by zero");
    lua_pushuint(L)->value = a / b;
    return 1;
}

int UInt_Mod(lua_State* L) {
    uint64_t a, b;
    if (!CoerceUInt(L, 1, &a) || !CoerceUInt(L, 2, &b))
        return luaL_error(L, "UInt: cannot mod incompatible types");
    if (b == 0)
        return luaL_error(L, "UInt: modulo by zero");
    lua_pushuint(L)->value = a % b;
    return 1;
}

// Bitwise

int UInt_BAnd(lua_State* L) {
    uint64_t a, b;
    if (!CoerceUInt(L, 1, &a) || !CoerceUInt(L, 2, &b))
        return luaL_error(L, "UInt: cannot band incompatible types");
    lua_pushuint(L)->value = a & b;
    return 1;
}

int UInt_BOr(lua_State* L) {
    uint64_t a, b;
    if (!CoerceUInt(L, 1, &a) || !CoerceUInt(L, 2, &b))
        return luaL_error(L, "UInt: cannot bor incompatible types");
    lua_pushuint(L)->value = a | b;
    return 1;
}

int UInt_BXor(lua_State* L) {
    uint64_t a, b;
    if (!CoerceUInt(L, 1, &a) || !CoerceUInt(L, 2, &b))
        return luaL_error(L, "UInt: cannot bxor incompatible types");
    lua_pushuint(L)->value = a ^ b;
    return 1;
}

int UInt_BNot(lua_State* L) {
    LuaUInt* u = lua_touint(L, 1);
    lua_pushuint(L)->value = ~u->value;
    return 1;
}

int UInt_Shl(lua_State* L) {
    uint64_t a, b;
    if (!CoerceUInt(L, 1, &a) || !CoerceUInt(L, 2, &b))
        return luaL_error(L, "UInt: cannot shl incompatible types");
    lua_pushuint(L)->value = (b >= 64) ? 0 : (a << b);
    return 1;
}

int UInt_Shr(lua_State* L) {
    uint64_t a, b;
    if (!CoerceUInt(L, 1, &a) || !CoerceUInt(L, 2, &b))
        return luaL_error(L, "UInt: cannot shr incompatible types");
    lua_pushuint(L)->value = (b >= 64) ? 0 : (a >> b);
    return 1;
}

// Comparison

int UInt_Eq(lua_State* L) {
    uint64_t a, b;
    if (!CoerceUInt(L, 1, &a) || !CoerceUInt(L, 2, &b)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, a == b ? 1 : 0);
    return 1;
}

int UInt_Lt(lua_State* L) {
    uint64_t a, b;
    if (!CoerceUInt(L, 1, &a) || !CoerceUInt(L, 2, &b))
        return luaL_error(L, "UInt: cannot compare incompatible types");
    lua_pushboolean(L, a < b ? 1 : 0);
    return 1;
}

int UInt_Le(lua_State* L) {
    uint64_t a, b;
    if (!CoerceUInt(L, 1, &a) || !CoerceUInt(L, 2, &b))
        return luaL_error(L, "UInt: cannot compare incompatible types");
    lua_pushboolean(L, a <= b ? 1 : 0);
    return 1;
}
