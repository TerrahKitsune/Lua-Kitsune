#include "decimal/LuaDecimal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "Mem.h"

// ── Userdata helpers ──────────────────────────────────────────────────────────

LuaDecimal* lua_pushdecimal(lua_State* L) {
	LuaDecimal* d = (LuaDecimal*)lua_newuserdata(L, sizeof(LuaDecimal));
	memset(d, 0, sizeof(LuaDecimal));
	luaL_setmetatable(L, LUADECIMAL);
	return d;
}

LuaDecimal* lua_todecimal(lua_State* L, int index) {
	return (LuaDecimal*)luaL_checkudata(L, index, LUADECIMAL);
}

int lua_isdecimal(lua_State* L, int index) {
	if (lua_type(L, index) != LUA_TUSERDATA) return 0;
	if (!lua_getmetatable(L, index)) return 0;
	luaL_getmetatable(L, LUADECIMAL);
	int result = lua_rawequal(L, -1, -2);
	lua_pop(L, 2);
	return result;
}

// ── 128-bit unsigned integer arithmetic ──────────────────────────────────────
// We represent 128-bit unsigned integers as { uint64_t lo, uint64_t hi }.

static int U128IsZero(uint64_t lo, uint64_t hi) {
	return lo == 0 && hi == 0;
}

// a *= 10
static void U128Mul10(uint64_t* lo, uint64_t* hi) {
	// Multiply lo by 10 using split halves to avoid overflow.
	uint64_t lo_lo = (*lo & 0xFFFFFFFFULL) * 10;
	uint64_t lo_hi = (*lo >> 32) * 10;
	uint64_t newLo = lo_lo + (lo_hi << 32);
	uint64_t carry  = (lo_hi >> 32) + (newLo < lo_lo ? 1ULL : 0ULL);
	// Multiply hi by 10 the same way to avoid overflow.
	uint64_t hi_lo = (*hi & 0xFFFFFFFFULL) * 10;
	uint64_t hi_hi = (*hi >> 32) * 10;
	uint64_t newHi = hi_lo + (hi_hi << 32) + carry;
	*lo = newLo;
	*hi = newHi;
}

// a += digit (0-9)
static void U128AddDigit(uint64_t* lo, uint64_t* hi, uint64_t digit) {
	uint64_t old = *lo;
	*lo += digit;
	if (*lo < old)
		(*hi)++;
}

// compare: returns -1, 0, +1
static int U128Cmp(uint64_t alo, uint64_t ahi, uint64_t blo, uint64_t bhi) {
	if (ahi != bhi) return ahi < bhi ? -1 : 1;
	if (alo != blo) return alo < blo ? -1 : 1;
	return 0;
}

// a -= b (assumes a >= b)
static void U128Sub(uint64_t* alo, uint64_t* ahi, uint64_t blo, uint64_t bhi) {
	if (*alo < blo) (*ahi)--;
	*alo -= blo;
	*ahi -= bhi;
}

// a += b
static void U128Add(uint64_t* alo, uint64_t* ahi, uint64_t blo, uint64_t bhi) {
	uint64_t old = *alo;
	*alo += blo;
	if (*alo < old) (*ahi)++;
	*ahi += bhi;
}

// a *= b (b is a small uint32)
static void U128Mul32(uint64_t* lo, uint64_t* hi, uint32_t b) {
	uint64_t b64 = (uint64_t)b;
	// Multiply each 32-bit limb by b64 and accumulate into 128-bit result.
	uint64_t lo0 = (*lo & 0xFFFFFFFFULL) * b64;  // bits 0-63
	uint64_t lo1 = (*lo >> 32)           * b64;  // bits 32-95
	uint64_t hi0 = (*hi & 0xFFFFFFFFULL) * b64;  // bits 64-127
	uint64_t hi1 = (*hi >> 32)           * b64;  // bits 96-159 (upper half lost)
	// Assemble low 64: lo0 + (lo1 << 32)
	uint64_t newLo = lo0 + (lo1 << 32);
	uint64_t c1 = (newLo < lo0) ? 1ULL : 0ULL;  // carry from lo
	// Assemble high 64 with full carry chain.
	uint64_t h = (lo1 >> 32) + c1;
	h += hi0; uint64_t c2 = (h < hi0) ? 1ULL : 0ULL;
	h += (hi1 << 32); c2 += (h < (hi1 << 32)) ? 1ULL : 0ULL;
	(void)c2;  // c2 would carry into bits 128+ which we discard
	*lo = newLo;
	*hi = h;
}

// Full 128x128 → 128 multiply (truncates to low 128 bits; upper bits beyond 128 are discarded)
static void U128Mul128(uint64_t* rlo, uint64_t* rhi,
	uint64_t alo, uint64_t ahi,
	uint64_t blo, uint64_t bhi) {
	uint64_t a0 = alo & 0xFFFFFFFFULL, a1 = alo >> 32;
	uint64_t a2 = ahi & 0xFFFFFFFFULL, a3 = ahi >> 32;
	uint64_t b0 = blo & 0xFFFFFFFFULL, b1 = blo >> 32;
	uint64_t b2 = bhi & 0xFFFFFFFFULL, b3 = bhi >> 32;
	// Partial products at each 32-bit column.
	uint64_t p00 = a0 * b0;  // col 0
	uint64_t p01 = a0 * b1;  // col 1
	uint64_t p10 = a1 * b0;  // col 1
	uint64_t p02 = a0 * b2;  // col 2
	uint64_t p11 = a1 * b1;  // col 2
	uint64_t p20 = a2 * b0;  // col 2
	uint64_t p03 = a0 * b3;  // col 3
	uint64_t p12 = a1 * b2;  // col 3
	uint64_t p21 = a2 * b1;  // col 3
	uint64_t p30 = a3 * b0;  // col 3
	// Col 1 (bits 32-63): sum p01+p10, carry into col 2.
	uint64_t col1   = p01 + p10;
	uint64_t col1C = (col1 < p01) ? 1ULL : 0ULL;
	// Low 64: p00 + (col1 << 32)
	uint64_t rlo2 = p00 + (col1 << 32);
	uint64_t loC = (rlo2 < p00) ? 1ULL : 0ULL;
	// Col 2 (bits 64-95): p02+p11+p20 + (col1>>32) + col1C<<32 + loC
	// Accumulate with carry tracking.
	uint64_t c2 = (col1 >> 32) + (col1C << 32) + loC;
	uint64_t s2 = p02; uint64_t ca;
	s2 += p11; ca  = (s2 < p02)  ? 1ULL : 0ULL;
	s2 += p20; ca += (s2 < p20)  ? 1ULL : 0ULL;
	s2 += c2;  ca += (s2 < c2)   ? 1ULL : 0ULL;
	// Col 3 (bits 96-127): p03+p12+p21+p30+ca; only the low 32 bits land in rhi.
	// Track carries so overflow within s3 doesn’t lose bits still in our 128-bit window.
	uint64_t s3 = p03;
	s3 += p12; uint64_t c3 = (s3 < p12) ? 1ULL : 0ULL;
	s3 += p21; c3 += (s3 < p21) ? 1ULL : 0ULL;
	s3 += p30; c3 += (s3 < p30) ? 1ULL : 0ULL;
	s3 += ca;  c3 += (s3 < ca)  ? 1ULL : 0ULL;
	(void)c3;  // c3 is in bits 128+ — discard
	uint64_t rhi2 = s2 + (s3 << 32);
	*rlo = rlo2;
	*rhi = rhi2;
}

// Divide 128-bit by 10; returns remainder
static uint64_t U128Div10(uint64_t* lo, uint64_t* hi) {
	// Divide high 64 bits first, carry remainder into low 64 bits.
	uint64_t rhi = *hi / 10;
	uint64_t remHi = *hi % 10;
	// Combine remainder from high with low: (remHi * 2^64 + lo) / 10
	// Use the identity: remHi * 2^64 = remHi * (10 * 1844674407370955161 + 6)
	// Simpler: do it in two 64-bit halves.
	uint64_t loVal = *lo;
	// high 32 bits of lo
	uint64_t loHiWord = (remHi << 32) | (loVal >> 32);
	uint64_t q1 = loHiWord / 10;
	uint64_t r1 = loHiWord % 10;
	// low 32 bits of lo
	uint64_t loLoWord = (r1 << 32) | (loVal & 0xFFFFFFFFULL);
	uint64_t q0 = loLoWord / 10;
	uint64_t r0 = loLoWord % 10;
	*hi = rhi;
	*lo = (q1 << 32) | q0;
	return r0;
}

// Number of decimal digits in 128-bit value
static int U128Digits(uint64_t lo, uint64_t hi) {
	if (U128IsZero(lo, hi)) return 1;
	int count = 0;
	while (!U128IsZero(lo, hi)) {
		U128Div10(&lo, &hi);
		count++;
	}
	return count;
}

// Scale a 128-bit value up by 10^n
static void U128ScaleUp(uint64_t* lo, uint64_t* hi, int n) {
	for (int i = 0; i < n; i++)
		U128Mul10(lo, hi);
}

// ── Parse / format ────────────────────────────────────────────────────────────

int DecimalParseC(const char* s, size_t len, LuaDecimal* out) {
	if (!s || len == 0) return 0;
	memset(out, 0, sizeof(LuaDecimal));

	size_t i = 0;
	if (s[i] == '-') { out->negative = 1; i++; }
	else if (s[i] == '+') { i++; }

	int hasDot = 0;
	int scale = 0;  // use plain int to detect overflow before clamping to int16_t
	int hasDigit = 0;

	// Skip leading zeros (but track them for scale if after dot)
	for (; i < len; i++) {
		char c = s[i];
		if (c == '.') {
			if (hasDot) return 0;
			hasDot = 1;
			continue;
		}
		if (!isdigit((unsigned char)c)) break;
		hasDigit = 1;
		if (hasDot) {
			if (scale >= 32767) return 0;  // too many fractional digits for int16_t scale
			scale++;
		}
		U128Mul10(&out->lo, &out->hi);
		U128AddDigit(&out->lo, &out->hi, (uint64_t)(c - '0'));
	}

	// Allow optional exponent: e.g. "1.23E+5" from Postgres scientific notation
	if (i < len && (s[i] == 'e' || s[i] == 'E')) {
		i++;
		int expNeg = 0;
		if (i < len && s[i] == '-') { expNeg = 1; i++; }
		else if (i < len && s[i] == '+') { i++; }
		int exp = 0;
		for (; i < len && isdigit((unsigned char)s[i]); i++) {
			exp = exp * 10 + (s[i] - '0');
			if (exp > 6200) return 0;  // exceeds int16_t range for scale
		}
		if (expNeg)
			scale = scale + exp;
		else
			scale = scale - exp;
	}

	if (!hasDigit) return 0;
	if (scale < 0) {
		// Positive exponent shifted scale below zero: scale up the coefficient.
		if (-scale > 6200) return 0;
		U128ScaleUp(&out->lo, &out->hi, -scale);
		scale = 0;
	}
	if (scale > 32767) return 0;
	out->scale = (int16_t)scale;
	if (U128IsZero(out->lo, out->hi)) out->negative = 0;
	return 1;
}

// Formats to buf (must be at least 64 bytes). Returns chars written.
static int DecimalFormat(const LuaDecimal* d, char* buf, size_t bufsz) {
	if (U128IsZero(d->lo, d->hi)) {
		if (d->scale > 0) {
			// Clamp scale so we never write beyond bufsz.
			// "0." + scale zeros + NUL; bufsz is 64, so max digits = bufsz - 3.
			int effectiveScale = d->scale;
			if (effectiveScale > (int)bufsz - 3)
				effectiveScale = (int)bufsz - 3;
			int n = snprintf(buf, bufsz, "0.%0*d", effectiveScale, 0);
			return n;
		}
		return snprintf(buf, bufsz, "0");
	}

	// Extract digits by repeated division
	char digits[44];  // 2^127 has 39 decimal digits; 44 gives a safe margin
	int ndigits = 0;
	uint64_t lo = d->lo, hi = d->hi;
	while (!U128IsZero(lo, hi) && ndigits < 43) {
		uint64_t rem = U128Div10(&lo, &hi);
		digits[ndigits++] = (char)('0' + rem);
	}
	// digits[] is in reverse order

	int dotFromRight = (int)d->scale;
	int totalIntDigits = ndigits - dotFromRight;

	char* p = buf;
	char* end = buf + bufsz - 1;

	if (d->negative) *p++ = '-';

	if (totalIntDigits <= 0) {
		// Pure fractional: "0.000123"
		*p++ = '0';
		*p++ = '.';
		for (int z = 0; z < -totalIntDigits && p < end; z++) *p++ = '0';
		for (int j = ndigits - 1; j >= 0 && p < end; j--) *p++ = digits[j];
	}
	else {
		// Has integer part
		for (int j = ndigits - 1; j >= 0 && p < end; j--) {
			*p++ = digits[j];
			if (dotFromRight > 0 && j == dotFromRight && p < end)
				*p++ = '.';
		}
	}
	*p = '\0';
	return (int)(p - buf);
}

void lua_pushdecimalstring(lua_State* L, int index) {
	LuaDecimal* d = (LuaDecimal*)lua_touserdata(L, index);
	char buf[64];
	DecimalFormat(d, buf, sizeof(buf));
	lua_pushstring(L, buf);
}

// ── Direct numeric conversion (no Lua string heap roundtrip) ─────────────────

// Convert a C int64 directly into a LuaDecimal (exact, scale=0).
static void DecimalFromInteger(int64_t v, LuaDecimal* out) {
	memset(out, 0, sizeof(LuaDecimal));
	if (v < 0) {
		out->negative = 1;
		// Handle INT64_MIN safely: negate as uint64.
		uint64_t uv = (v == INT64_MIN)
			? ((uint64_t)INT64_MAX + 1ULL)
			: (uint64_t)(-v);
		out->lo = uv;
	}
	else {
		out->lo = (uint64_t)v;
	}
}

// Convert a C double into a LuaDecimal via snprintf — the stack-allocated buf
// avoids any Lua string allocation.  Uses %.17g for the shortest round-trip
// exact decimal representation of the double.
static int DecimalFromDouble(double v, LuaDecimal* out) {
	char buf[48];
	int n = snprintf(buf, sizeof(buf), "%.17g", v);
	if (n <= 0 || n >= (int)sizeof(buf))
		return 0;
	return DecimalParseC(buf, (size_t)n, out);
}

// Coerce argument at stack index to LuaDecimal, handling plain number/string too.
// Returns NULL if coercion fails (does not error — caller decides).
static LuaDecimal* CoerceDecimal(lua_State* L, int index, LuaDecimal* tmp) {
	if (lua_isdecimal(L, index))
		return lua_todecimal(L, index);
	if (lua_isinteger(L, index)) {
		DecimalFromInteger((int64_t)lua_tointeger(L, index), tmp);
		return tmp;
	}
	if (lua_type(L, index) == LUA_TNUMBER) {
		if (DecimalFromDouble((double)lua_tonumber(L, index), tmp))
			return tmp;
		return NULL;
	}
	if (lua_type(L, index) == LUA_TSTRING) {
		size_t len;
		const char* s = lua_tolstring(L, index, &len);
		if (s && DecimalParseC(s, len, tmp))
			return tmp;
	}
	return NULL;
}

// ── Constructors ──────────────────────────────────────────────────────────────

int Decimal_FromString(lua_State* L) {
	size_t len;
	const char* s = luaL_checklstring(L, 1, &len);
	LuaDecimal* d = lua_pushdecimal(L);
	if (!DecimalParseC(s, len, d)) {
		lua_pop(L, 1);
		lua_pushnil(L);
	}
	return 1;
}

int Decimal_FromNumber(lua_State* L) {
	LuaDecimal* d = lua_pushdecimal(L);
	if (lua_isinteger(L, 1)) {
		DecimalFromInteger((int64_t)lua_tointeger(L, 1), d);
	}
	else {
		luaL_checknumber(L, 1);  // raise error if not a number
		if (!DecimalFromDouble((double)lua_tonumber(L, 1), d)) {
			lua_pop(L, 1);
			lua_pushnil(L);
		}
	}
	return 1;
}

int Decimal_Zero(lua_State* L) {
	lua_pushdecimal(L);  // already zeroed by lua_pushdecimal
	return 1;
}

// ── Inspection ───────────────────────────────────────────────────────────────

int Decimal_ToString(lua_State* L) {
	LuaDecimal* d = lua_todecimal(L, 1);
	char buf[64];
	DecimalFormat(d, buf, sizeof(buf));
	lua_pushstring(L, buf);
	return 1;
}

int Decimal_ToNumber(lua_State* L) {
	LuaDecimal* d = lua_todecimal(L, 1);
	char buf[64];
	DecimalFormat(d, buf, sizeof(buf));
	lua_pushnumber(L, (lua_Number)strtod(buf, NULL));
	return 1;
}

int Decimal_Scale(lua_State* L) {
	LuaDecimal* d = lua_todecimal(L, 1);
	lua_pushinteger(L, d->scale);
	return 1;
}

int Decimal_Precision(lua_State* L) {
	LuaDecimal* d = lua_todecimal(L, 1);
	lua_pushinteger(L, U128Digits(d->lo, d->hi));
	return 1;
}

int Decimal_IsEmpty(lua_State* L) {
	LuaDecimal* d = lua_todecimal(L, 1);
	lua_pushboolean(L, U128IsZero(d->lo, d->hi) ? 1 : 0);
	return 1;
}

int Decimal_IsNegative(lua_State* L) {
	LuaDecimal* d = lua_todecimal(L, 1);
	lua_pushboolean(L, d->negative && !U128IsZero(d->lo, d->hi) ? 1 : 0);
	return 1;
}

int Decimal_Abs(lua_State* L) {
	LuaDecimal* src = lua_todecimal(L, 1);
	LuaDecimal* dst = lua_pushdecimal(L);
	*dst = *src;
	dst->negative = 0;
	return 1;
}

// ── Arithmetic helpers ────────────────────────────────────────────────────────

// Align two decimals to the same scale, modifying copies.
// The one with smaller scale gets its coefficient scaled up.
static void AlignScales(LuaDecimal* a, LuaDecimal* b) {
	int diff = (int)a->scale - (int)b->scale;
	if (diff > 0)
		U128ScaleUp(&b->lo, &b->hi, diff);
	else if (diff < 0)
		U128ScaleUp(&a->lo, &a->hi, -diff);
	int16_t common = a->scale > b->scale ? a->scale : b->scale;
	a->scale = common;
	b->scale = common;
}

// SignedAdd: result = a + b (signed), stored in *r
static void SignedAdd(const LuaDecimal* a, const LuaDecimal* b, LuaDecimal* r) {
	LuaDecimal ta = *a, tb = *b;
	AlignScales(&ta, &tb);
	r->scale = ta.scale;

	if (ta.negative == tb.negative) {
		// Same sign: add magnitudes
		r->lo = ta.lo; r->hi = ta.hi;
		U128Add(&r->lo, &r->hi, tb.lo, tb.hi);
		r->negative = ta.negative;
	}
	else {
		// Different signs: subtract smaller from larger
		int cmp = U128Cmp(ta.lo, ta.hi, tb.lo, tb.hi);
		if (cmp == 0) {
			r->lo = 0; r->hi = 0; r->negative = 0;
		}
		else if (cmp > 0) {
			r->lo = ta.lo; r->hi = ta.hi;
			U128Sub(&r->lo, &r->hi, tb.lo, tb.hi);
			r->negative = ta.negative;
		}
		else {
			r->lo = tb.lo; r->hi = tb.hi;
			U128Sub(&r->lo, &r->hi, ta.lo, ta.hi);
			r->negative = tb.negative;
		}
	}
	if (U128IsZero(r->lo, r->hi)) r->negative = 0;
}

// ── Arithmetic operations ─────────────────────────────────────────────────────

// U128Divmod: quotient *q = n / d, remainder *rem = n % d.  d must be non-zero.
static void U128Divmod(
	uint64_t nlo,  uint64_t nhi,
	uint64_t dlo,  uint64_t dhi,
	uint64_t* qlo, uint64_t* qhi,
	uint64_t* remlo, uint64_t* remhi)
{
	*qlo = *qhi = *remlo = *remhi = 0;

	// Find the position of the highest set bit of n (0-based from LSB).
	// We count from bit 127 downward rather than shifting n to avoid UB.
	int nbits = 0;
	if (nhi != 0) {
		for (int b = 63; b >= 0; b--) {
			if ((nhi >> b) & 1ULL) { nbits = b + 65; break; }
		}
	} else if (nlo != 0) {
		for (int b = 63; b >= 0; b--) {
			if ((nlo >> b) & 1ULL) { nbits = b + 1; break; }
		}
	}
	if (nbits == 0) return;  // n == 0

	// Classic binary restoring division: process MSB to LSB of n.
	for (int i = nbits - 1; i >= 0; i--) {
		// Shift remainder left 1 and bring in bit i of n.
		*remhi = (*remhi << 1) | (*remlo >> 63);
		uint64_t bit;
		if (i < 64)
			bit = (nlo >> i) & 1ULL;
		else
			bit = (nhi >> (i - 64)) & 1ULL;
		*remlo = (*remlo << 1) | bit;

		if (U128Cmp(*remlo, *remhi, dlo, dhi) >= 0) {
			U128Sub(remlo, remhi, dlo, dhi);
			if (i < 64) *qlo |= (1ULL << i);
			else        *qhi |= (1ULL << (i - 64));
		}
	}
}

int Decimal_Add(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = CoerceDecimal(L, 1, &ta);
	LuaDecimal* b = CoerceDecimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for addition");
	LuaDecimal* r = lua_pushdecimal(L);
	SignedAdd(a, b, r);
	return 1;
}

int Decimal_Sub(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = CoerceDecimal(L, 1, &ta);
	LuaDecimal* b = CoerceDecimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for subtraction");
	// negate b then add
	LuaDecimal nb = *b;
	if (!U128IsZero(nb.lo, nb.hi)) nb.negative = nb.negative ^ 1;
	LuaDecimal* r = lua_pushdecimal(L);
	SignedAdd(a, &nb, r);
	return 1;
}

int Decimal_Mul(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = CoerceDecimal(L, 1, &ta);
	LuaDecimal* b = CoerceDecimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for multiplication");
	LuaDecimal* r = lua_pushdecimal(L);
	U128Mul128(&r->lo, &r->hi, a->lo, a->hi, b->lo, b->hi);
	int combinedScale = (int)a->scale + (int)b->scale;
	if (combinedScale > 32767) combinedScale = 32767;  // saturate; high scales lose trailing zeros which is harmless
	r->scale = (int16_t)combinedScale;
	r->negative = (a->negative != b->negative) ? 1 : 0;
	if (U128IsZero(r->lo, r->hi)) r->negative = 0;
	return 1;
}

int Decimal_Div(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = CoerceDecimal(L, 1, &ta);
	LuaDecimal* b = CoerceDecimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for division");
	if (U128IsZero(b->lo, b->hi)) return luaL_error(L, "Decimal division by zero");

	LuaDecimal* r = lua_pushdecimal(L);
	int ts = 10 + (a->scale > b->scale ? (int)a->scale - (int)b->scale : 0);
	if (ts > 32767) ts = 32767;
	int16_t targetScale = (int16_t)ts;

	// a/b = (a_coeff * 10^-a_scale) / (b_coeff * 10^-b_scale)
	//      = (a_coeff / b_coeff) * 10^(b_scale - a_scale)
	// For the integer quotient (a_coeff * 10^S / b_coeff) to represent the answer
	// with targetScale decimal places:  S = targetScale + b_scale - a_scale
	int scaleShift = (int)targetScale + (int)b->scale - (int)a->scale;
	uint64_t nlo = a->lo, nhi = a->hi;
	if (scaleShift > 0)
		U128ScaleUp(&nlo, &nhi, scaleShift);
	else if (scaleShift < 0) {
		// a has more fractional digits than needed — truncate excess from numerator.
		for (int i = 0; i < -scaleShift; i++)
			U128Div10(&nlo, &nhi);
	}

	uint64_t qlo, qhi, remlo, remhi;
	U128Divmod(nlo, nhi, b->lo, b->hi, &qlo, &qhi, &remlo, &remhi);

	r->lo    = qlo;
	r->hi    = qhi;
	r->scale = targetScale;
	r->negative = (a->negative != b->negative) ? 1 : 0;
	if (U128IsZero(r->lo, r->hi)) r->negative = 0;
	return 1;
}

int Decimal_Unm(lua_State* L) {
	LuaDecimal* src = lua_todecimal(L, 1);
	LuaDecimal* r = lua_pushdecimal(L);
	*r = *src;
	if (!U128IsZero(r->lo, r->hi)) r->negative = r->negative ^ 1;
	return 1;
}

int Decimal_Mod(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = CoerceDecimal(L, 1, &ta);
	LuaDecimal* b = CoerceDecimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for modulo");
	if (U128IsZero(b->lo, b->hi)) return luaL_error(L, "Decimal modulo by zero");

	LuaDecimal ca = *a, cb = *b;
	AlignScales(&ca, &cb);

	uint64_t qlo, qhi, remlo, remhi;
	U128Divmod(ca.lo, ca.hi, cb.lo, cb.hi, &qlo, &qhi, &remlo, &remhi);
	(void)qlo; (void)qhi;

	LuaDecimal* r = lua_pushdecimal(L);
	r->lo    = remlo;
	r->hi    = remhi;
	r->scale = ca.scale;
	r->negative = a->negative;
	if (U128IsZero(r->lo, r->hi)) r->negative = 0;
	return 1;
}

// ── Comparison ────────────────────────────────────────────────────────────────

// Returns -1, 0, +1 comparing a and b numerically
static int DecimalCmp(const LuaDecimal* a, const LuaDecimal* b) {
	LuaDecimal ta = *a, tb = *b;
	int azero = U128IsZero(ta.lo, ta.hi);
	int bzero = U128IsZero(tb.lo, tb.hi);
	if (azero && bzero) return 0;
	if (azero) return tb.negative ? 1 : -1;
	if (bzero) return ta.negative ? -1 : 1;
	if (ta.negative != tb.negative) return ta.negative ? -1 : 1;
	AlignScales(&ta, &tb);
	int mc = U128Cmp(ta.lo, ta.hi, tb.lo, tb.hi);
	return ta.negative ? -mc : mc;
}

int Decimal_Eq(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = CoerceDecimal(L, 1, &ta);
	LuaDecimal* b = CoerceDecimal(L, 2, &tb);
	if (!a || !b) { lua_pushboolean(L, 0); return 1; }
	lua_pushboolean(L, DecimalCmp(a, b) == 0 ? 1 : 0);
	return 1;
}

int Decimal_Lt(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = CoerceDecimal(L, 1, &ta);
	LuaDecimal* b = CoerceDecimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for comparison");
	lua_pushboolean(L, DecimalCmp(a, b) < 0 ? 1 : 0);
	return 1;
}

int Decimal_Le(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = CoerceDecimal(L, 1, &ta);
	LuaDecimal* b = CoerceDecimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for comparison");
	lua_pushboolean(L, DecimalCmp(a, b) <= 0 ? 1 : 0);
	return 1;
}

// ── Rounding ─────────────────────────────────────────────────────────────────

int Decimal_Round(lua_State* L) {
	LuaDecimal* src = lua_todecimal(L, 1);
	int newScale = (int)luaL_optinteger(L, 2, 0);
	if (newScale < 0) newScale = 0;
	LuaDecimal* r = lua_pushdecimal(L);
	*r = *src;
	int diff = (int)r->scale - newScale;
	if (diff <= 0) return 1;  // already at or coarser than requested scale
	// Remove (diff-1) digits silently, then check the last removed digit for rounding.
	for (int i = 0; i < diff - 1; i++)
		U128Div10(&r->lo, &r->hi);
	uint64_t decidingRem = U128Div10(&r->lo, &r->hi);
	r->scale = (int16_t)newScale;
	if (decidingRem >= 5)
		U128AddDigit(&r->lo, &r->hi, 1);
	if (U128IsZero(r->lo, r->hi)) r->negative = 0;
	return 1;
}

int Decimal_Truncate(lua_State* L) {
	LuaDecimal* src = lua_todecimal(L, 1);
	int newScale = (int)luaL_optinteger(L, 2, 0);
	if (newScale < 0) newScale = 0;
	LuaDecimal* r = lua_pushdecimal(L);
	*r = *src;
	int diff = (int)r->scale - newScale;
	if (diff <= 0) return 1;
	for (int i = 0; i < diff; i++)
		U128Div10(&r->lo, &r->hi);
	r->scale = (int16_t)newScale;
	if (U128IsZero(r->lo, r->hi)) r->negative = 0;
	return 1;
}
