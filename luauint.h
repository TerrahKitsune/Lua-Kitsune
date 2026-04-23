#pragma once
#include "lua_main_incl.h"
#include "platform.h"
#include <stdint.h>

static const char* LUAUINT = "UINT";

// Unsigned 64-bit integer userdata.
// Lua integers are signed int64, so values above INT64_MAX cannot be represented
// natively. LuaUInt wraps a uint64_t to preserve the full unsigned range and
// to allow faithful msgpack uint64 wire-type round-tripping.
typedef struct LuaUInt {
    uint64_t value;
} LuaUInt;

LuaUInt* lua_pushuint(lua_State* L);
LuaUInt* lua_touint(lua_State* L, int index);

int lua_isuint(lua_State* L, int index);

// Pushes the canonical decimal string representation of the UInt at index.
void lua_uint_push_string(lua_State* L, int index);

// Constructors
int uint_fromstring(lua_State* L);   // FromString(s)
int uint_fromnumber(lua_State* L);   // FromNumber(n)
int uint_fromunsigned(lua_State* L); // FromUnsigned(i)  — reinterpret int64 bits as uint64
int uint_zero(lua_State* L);         // Zero()

// Inspection
int uint_tostring(lua_State* L);   // __tostring / ToString
int uint_tonumber(lua_State* L);   // ToNumber   (lossy lua_Number / double)
int uint_tointeger(lua_State* L);  // ToInteger  (truncates to signed int64)
int uint_tounsigned(lua_State* L); // ToUnsigned — reinterpret uint64 bits as int64
int uint_iszero(lua_State* L);     // IsZero()

// Arithmetic metamethods
int uint_add(lua_State* L);  // __add
int uint_sub(lua_State* L);  // __sub
int uint_mul(lua_State* L);  // __mul
int uint_div(lua_State* L);  // __div
int uint_mod(lua_State* L);  // __mod

// Bitwise metamethods
int uint_band(lua_State* L);  // __band
int uint_bor(lua_State* L);   // __bor
int uint_bxor(lua_State* L);  // __bxor
int uint_bnot(lua_State* L);  // __bnot
int uint_shl(lua_State* L);   // __shl
int uint_shr(lua_State* L);   // __shr

// Comparison metamethods
int uint_eq(lua_State* L);  // __eq
int uint_lt(lua_State* L);  // __lt
int uint_le(lua_State* L);  // __le
