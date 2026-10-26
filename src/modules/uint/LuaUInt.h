#pragma once
#include "LuaMainInclude.h"
#include "Platform.h"
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
void lua_pushuintstring(lua_State* L, int index);

// Constructors
int UInt_FromString(lua_State* L);   // FromString(s)
int UInt_FromNumber(lua_State* L);   // FromNumber(n)
int UInt_FromUnsigned(lua_State* L); // FromUnsigned(i)  — reinterpret int64 bits as uint64
int UInt_Zero(lua_State* L);         // Zero()

// Inspection
int UInt_ToString(lua_State* L);   // __tostring / ToString
int UInt_ToNumber(lua_State* L);   // ToNumber   (lossy lua_Number / double)
int UInt_ToInteger(lua_State* L);  // ToInteger  (truncates to signed int64)
int UInt_ToUnsigned(lua_State* L); // ToUnsigned — reinterpret uint64 bits as int64
int UInt_IsZero(lua_State* L);     // IsZero()

// Arithmetic metamethods
int UInt_Add(lua_State* L);  // __add
int UInt_Sub(lua_State* L);  // __sub
int UInt_Mul(lua_State* L);  // __mul
int UInt_Div(lua_State* L);  // __div
int UInt_Mod(lua_State* L);  // __mod

// Bitwise metamethods
int UInt_BAnd(lua_State* L);  // __band
int UInt_BOr(lua_State* L);   // __bor
int UInt_BXor(lua_State* L);  // __bxor
int UInt_BNot(lua_State* L);  // __bnot
int UInt_Shl(lua_State* L);   // __shl
int UInt_Shr(lua_State* L);   // __shr

// Comparison metamethods
int UInt_Eq(lua_State* L);  // __eq
int UInt_Lt(lua_State* L);  // __lt
int UInt_Le(lua_State* L);  // __le
