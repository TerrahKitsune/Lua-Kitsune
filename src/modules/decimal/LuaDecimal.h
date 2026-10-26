#pragma once
#include "LuaMainInclude.h"
#include "Platform.h"
#include <stdint.h>

static const char* LUADECIMAL = "DECIMAL";

// 128-bit decimal with up to 34 significant digits.
// Coefficient is an unsigned 128-bit integer stored as two uint64_t (lo = low 64 bits,
// hi = high 64 bits). scale = number of decimal digits after the decimal point.
// negative = 1 means the value is negative.
// Special states: both lo and hi zero with scale 0 represents zero.
typedef struct LuaDecimal {
	uint64_t lo;
	uint64_t hi;
	int16_t  scale;
	uint8_t  negative;
	uint8_t  pad;
} LuaDecimal;

LuaDecimal* lua_pushdecimal(lua_State* L);
LuaDecimal* lua_todecimal(lua_State* L, int index);

int lua_isdecimal(lua_State* L, int index);

// Pushes the canonical decimal string representation of the value at index.
void lua_pushdecimalstring(lua_State* L, int index);

// Parses a decimal string (e.g. "123.456", "-0.001", "9999999999999999999999999999999999")
// into *out.  Returns 1 on success, 0 on failure. Does not touch the Lua stack.
int DecimalParseC(const char* s, size_t len, LuaDecimal* out);

// Constructors
int Decimal_FromString(lua_State* L);
int Decimal_FromNumber(lua_State* L);
int Decimal_Zero(lua_State* L);

// Inspection
int Decimal_ToString(lua_State* L);    // __tostring / ToString
int Decimal_ToNumber(lua_State* L);    // ToNumber  (lossy lua_Number)
int Decimal_Scale(lua_State* L);       // Scale()   decimal digits after point
int Decimal_Precision(lua_State* L);   // Precision() total significant digits
int Decimal_IsEmpty(lua_State* L);     // IsEmpty() true when value is zero
int Decimal_IsNegative(lua_State* L);  // IsNegative()
int Decimal_Abs(lua_State* L);         // Abs()

// Arithmetic
int Decimal_Add(lua_State* L);   // __add
int Decimal_Sub(lua_State* L);   // __sub
int Decimal_Mul(lua_State* L);   // __mul
int Decimal_Div(lua_State* L);   // __div
int Decimal_Unm(lua_State* L);   // __unm (unary minus)
int Decimal_Mod(lua_State* L);   // __mod

// Comparison metamethods
int Decimal_Eq(lua_State* L);    // __eq
int Decimal_Lt(lua_State* L);    // __lt
int Decimal_Le(lua_State* L);    // __le

// Rounding
int Decimal_Round(lua_State* L);    // Round(scale)
int Decimal_Truncate(lua_State* L); // Truncate(scale)
