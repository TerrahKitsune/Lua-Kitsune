#pragma once
#include "lua_main_incl.h"
#include "platform.h"
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
	uint8_t  _pad;
} LuaDecimal;

LuaDecimal* lua_pushdecimal(lua_State* L);
LuaDecimal* lua_todecimal(lua_State* L, int index);

int lua_isdecimal(lua_State* L, int index);

// Pushes the canonical decimal string representation of the value at index.
void lua_decimal_push_string(lua_State* L, int index);

// Parses a decimal string (e.g. "123.456", "-0.001", "9999999999999999999999999999999999")
// into *out.  Returns 1 on success, 0 on failure. Does not touch the Lua stack.
int decimal_parse_c(const char* s, size_t len, LuaDecimal* out);

// Constructors
int decimal_fromstring(lua_State* L);
int decimal_fromnumber(lua_State* L);
int decimal_zero(lua_State* L);

// Inspection
int decimal_tostring(lua_State* L);    // __tostring / ToString
int decimal_tonumber(lua_State* L);    // ToNumber  (lossy lua_Number)
int decimal_scale(lua_State* L);       // Scale()   decimal digits after point
int decimal_precision(lua_State* L);   // Precision() total significant digits
int decimal_isempty(lua_State* L);     // IsEmpty() true when value is zero
int decimal_isnegative(lua_State* L);  // IsNegative()
int decimal_abs(lua_State* L);         // Abs()

// Arithmetic
int decimal_add(lua_State* L);   // __add
int decimal_sub(lua_State* L);   // __sub
int decimal_mul(lua_State* L);   // __mul
int decimal_div(lua_State* L);   // __div
int decimal_unm(lua_State* L);   // __unm (unary minus)
int decimal_mod(lua_State* L);   // __mod

// Comparison metamethods
int decimal_eq(lua_State* L);    // __eq
int decimal_lt(lua_State* L);    // __lt
int decimal_le(lua_State* L);    // __le

// Rounding
int decimal_round(lua_State* L);    // Round(scale)
int decimal_truncate(lua_State* L); // Truncate(scale)
