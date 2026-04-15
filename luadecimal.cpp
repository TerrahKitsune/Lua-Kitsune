#include "luadecimal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include "mem.h"

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
	return luaL_testudata(L, index, LUADECIMAL) != NULL;
}

// ── 128-bit unsigned integer arithmetic ──────────────────────────────────────
// We represent 128-bit unsigned integers as { uint64_t lo, uint64_t hi }.

static int u128_iszero(uint64_t lo, uint64_t hi) {
	return lo == 0 && hi == 0;
}

// a *= 10
static void u128_mul10(uint64_t* lo, uint64_t* hi) {
	// Multiply lo by 10 using split halves to avoid overflow.
	uint64_t lo_lo = (*lo & 0xFFFFFFFFULL) * 10;
	uint64_t lo_hi = (*lo >> 32) * 10;
	uint64_t new_lo = lo_lo + (lo_hi << 32);
	uint64_t carry  = (lo_hi >> 32) + (new_lo < lo_lo ? 1ULL : 0ULL);
	// Multiply hi by 10 the same way to avoid overflow.
	uint64_t hi_lo = (*hi & 0xFFFFFFFFULL) * 10;
	uint64_t hi_hi = (*hi >> 32) * 10;
	uint64_t new_hi = hi_lo + (hi_hi << 32) + carry;
	*lo = new_lo;
	*hi = new_hi;
}

// a += digit (0-9)
static void u128_add_digit(uint64_t* lo, uint64_t* hi, uint64_t digit) {
	uint64_t old = *lo;
	*lo += digit;
	if (*lo < old)
		(*hi)++;
}

// compare: returns -1, 0, +1
static int u128_cmp(uint64_t alo, uint64_t ahi, uint64_t blo, uint64_t bhi) {
	if (ahi != bhi) return ahi < bhi ? -1 : 1;
	if (alo != blo) return alo < blo ? -1 : 1;
	return 0;
}

// a -= b (assumes a >= b)
static void u128_sub(uint64_t* alo, uint64_t* ahi, uint64_t blo, uint64_t bhi) {
	if (*alo < blo) (*ahi)--;
	*alo -= blo;
	*ahi -= bhi;
}

// a += b
static void u128_add(uint64_t* alo, uint64_t* ahi, uint64_t blo, uint64_t bhi) {
	uint64_t old = *alo;
	*alo += blo;
	if (*alo < old) (*ahi)++;
	*ahi += bhi;
}

// a *= b (b is a small uint32)
static void u128_mul32(uint64_t* lo, uint64_t* hi, uint32_t b) {
	uint64_t b64 = (uint64_t)b;
	// Multiply each 32-bit limb by b64 and accumulate into 128-bit result.
	uint64_t lo0 = (*lo & 0xFFFFFFFFULL) * b64;  // bits 0-63
	uint64_t lo1 = (*lo >> 32)           * b64;  // bits 32-95
	uint64_t hi0 = (*hi & 0xFFFFFFFFULL) * b64;  // bits 64-127
	uint64_t hi1 = (*hi >> 32)           * b64;  // bits 96-159 (upper half lost)
	// Assemble low 64: lo0 + (lo1 << 32)
	uint64_t new_lo = lo0 + (lo1 << 32);
	uint64_t c1 = (new_lo < lo0) ? 1ULL : 0ULL;  // carry from lo
	// Assemble high 64 with full carry chain.
	uint64_t h = (lo1 >> 32) + c1;
	h += hi0; uint64_t c2 = (h < hi0) ? 1ULL : 0ULL;
	h += (hi1 << 32); c2 += (h < (hi1 << 32)) ? 1ULL : 0ULL;
	(void)c2;  // c2 would carry into bits 128+ which we discard
	*lo = new_lo;
	*hi = h;
}

// Full 128x128 → 128 multiply (truncates to low 128 bits; upper bits beyond 128 are discarded)
static void u128_mul128(uint64_t* rlo, uint64_t* rhi,
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
	uint64_t col1_c = (col1 < p01) ? 1ULL : 0ULL;
	// Low 64: p00 + (col1 << 32)
	uint64_t rlo2 = p00 + (col1 << 32);
	uint64_t lo_c = (rlo2 < p00) ? 1ULL : 0ULL;
	// Col 2 (bits 64-95): p02+p11+p20 + (col1>>32) + col1_c<<32 + lo_c
	// Accumulate with carry tracking.
	uint64_t c2 = (col1 >> 32) + (col1_c << 32) + lo_c;
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
static uint64_t u128_div10(uint64_t* lo, uint64_t* hi) {
	// Divide high 64 bits first, carry remainder into low 64 bits.
	uint64_t rhi = *hi / 10;
	uint64_t rem_hi = *hi % 10;
	// Combine remainder from high with low: (rem_hi * 2^64 + lo) / 10
	// Use the identity: rem_hi * 2^64 = rem_hi * (10 * 1844674407370955161 + 6)
	// Simpler: do it in two 64-bit halves.
	uint64_t lo_val = *lo;
	// high 32 bits of lo
	uint64_t lo_hi_word = (rem_hi << 32) | (lo_val >> 32);
	uint64_t q1 = lo_hi_word / 10;
	uint64_t r1 = lo_hi_word % 10;
	// low 32 bits of lo
	uint64_t lo_lo_word = (r1 << 32) | (lo_val & 0xFFFFFFFFULL);
	uint64_t q0 = lo_lo_word / 10;
	uint64_t r0 = lo_lo_word % 10;
	*hi = rhi;
	*lo = (q1 << 32) | q0;
	return r0;
}

// Number of decimal digits in 128-bit value
static int u128_digits(uint64_t lo, uint64_t hi) {
	if (u128_iszero(lo, hi)) return 1;
	int count = 0;
	while (!u128_iszero(lo, hi)) {
		u128_div10(&lo, &hi);
		count++;
	}
	return count;
}

// Scale a 128-bit value up by 10^n
static void u128_scale_up(uint64_t* lo, uint64_t* hi, int n) {
	for (int i = 0; i < n; i++)
		u128_mul10(lo, hi);
}

// ── Parse / format ────────────────────────────────────────────────────────────

int decimal_parse_c(const char* s, size_t len, LuaDecimal* out) {
	if (!s || len == 0) return 0;
	memset(out, 0, sizeof(LuaDecimal));

	size_t i = 0;
	if (s[i] == '-') { out->negative = 1; i++; }
	else if (s[i] == '+') { i++; }

	int has_dot = 0;
	int scale = 0;  // use plain int to detect overflow before clamping to int16_t
	int has_digit = 0;

	// Skip leading zeros (but track them for scale if after dot)
	for (; i < len; i++) {
		char c = s[i];
		if (c == '.') {
			if (has_dot) return 0;
			has_dot = 1;
			continue;
		}
		if (!isdigit((unsigned char)c)) break;
		has_digit = 1;
		if (has_dot) {
			if (scale >= 32767) return 0;  // too many fractional digits for int16_t scale
			scale++;
		}
		u128_mul10(&out->lo, &out->hi);
		u128_add_digit(&out->lo, &out->hi, (uint64_t)(c - '0'));
	}

	// Allow optional exponent: e.g. "1.23E+5" from Postgres scientific notation
	if (i < len && (s[i] == 'e' || s[i] == 'E')) {
		i++;
		int exp_neg = 0;
		if (i < len && s[i] == '-') { exp_neg = 1; i++; }
		else if (i < len && s[i] == '+') { i++; }
		int exp = 0;
		for (; i < len && isdigit((unsigned char)s[i]); i++) {
			exp = exp * 10 + (s[i] - '0');
			if (exp > 6200) return 0;  // exceeds int16_t range for scale
		}
		if (exp_neg)
			scale = scale + exp;
		else
			scale = scale - exp;
	}

	if (!has_digit) return 0;
	if (scale < 0) {
		// Positive exponent shifted scale below zero: scale up the coefficient.
		if (-scale > 6200) return 0;
		u128_scale_up(&out->lo, &out->hi, -scale);
		scale = 0;
	}
	if (scale > 32767) return 0;
	out->scale = (int16_t)scale;
	if (u128_iszero(out->lo, out->hi)) out->negative = 0;
	return 1;
}

// Formats to buf (must be at least 64 bytes). Returns chars written.
static int decimal_format(const LuaDecimal* d, char* buf, size_t bufsz) {
	if (u128_iszero(d->lo, d->hi)) {
		if (d->scale > 0) {
			// Clamp scale so we never write beyond bufsz.
			// "0." + scale zeros + NUL; bufsz is 64, so max digits = bufsz - 3.
			int effective_scale = d->scale;
			if (effective_scale > (int)bufsz - 3)
				effective_scale = (int)bufsz - 3;
			int n = snprintf(buf, bufsz, "0.%0*d", effective_scale, 0);
			return n;
		}
		return snprintf(buf, bufsz, "0");
	}

	// Extract digits by repeated division
	char digits[44];  // 2^127 has 39 decimal digits; 44 gives a safe margin
	int ndigits = 0;
	uint64_t lo = d->lo, hi = d->hi;
	while (!u128_iszero(lo, hi) && ndigits < 43) {
		uint64_t rem = u128_div10(&lo, &hi);
		digits[ndigits++] = (char)('0' + rem);
	}
	// digits[] is in reverse order

	int dot_from_right = (int)d->scale;
	int total_int_digits = ndigits - dot_from_right;

	char* p = buf;
	char* end = buf + bufsz - 1;

	if (d->negative) *p++ = '-';

	if (total_int_digits <= 0) {
		// Pure fractional: "0.000123"
		*p++ = '0';
		*p++ = '.';
		for (int z = 0; z < -total_int_digits && p < end; z++) *p++ = '0';
		for (int j = ndigits - 1; j >= 0 && p < end; j--) *p++ = digits[j];
	}
	else {
		// Has integer part
		for (int j = ndigits - 1; j >= 0 && p < end; j--) {
			*p++ = digits[j];
			if (dot_from_right > 0 && j == dot_from_right && p < end)
				*p++ = '.';
		}
	}
	*p = '\0';
	return (int)(p - buf);
}

void lua_decimal_push_string(lua_State* L, int index) {
	LuaDecimal* d = (LuaDecimal*)lua_touserdata(L, index);
	char buf[64];
	decimal_format(d, buf, sizeof(buf));
	lua_pushstring(L, buf);
}

// ── Direct numeric conversion (no Lua string heap roundtrip) ─────────────────

// Convert a C int64 directly into a LuaDecimal (exact, scale=0).
static void decimal_from_integer(int64_t v, LuaDecimal* out) {
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
static int decimal_from_double(double v, LuaDecimal* out) {
	char buf[48];
	int n = snprintf(buf, sizeof(buf), "%.17g", v);
	if (n <= 0 || n >= (int)sizeof(buf))
		return 0;
	return decimal_parse_c(buf, (size_t)n, out);
}

// Coerce argument at stack index to LuaDecimal, handling plain number/string too.
// Returns NULL if coercion fails (does not error — caller decides).
static LuaDecimal* coerce_decimal(lua_State* L, int index, LuaDecimal* tmp) {
	if (lua_isdecimal(L, index))
		return lua_todecimal(L, index);
	if (lua_isinteger(L, index)) {
		decimal_from_integer((int64_t)lua_tointeger(L, index), tmp);
		return tmp;
	}
	if (lua_type(L, index) == LUA_TNUMBER) {
		if (decimal_from_double((double)lua_tonumber(L, index), tmp))
			return tmp;
		return NULL;
	}
	if (lua_type(L, index) == LUA_TSTRING) {
		size_t len;
		const char* s = lua_tolstring(L, index, &len);
		if (s && decimal_parse_c(s, len, tmp))
			return tmp;
	}
	return NULL;
}

// ── Constructors ──────────────────────────────────────────────────────────────

int decimal_fromstring(lua_State* L) {
	size_t len;
	const char* s = luaL_checklstring(L, 1, &len);
	LuaDecimal* d = lua_pushdecimal(L);
	if (!decimal_parse_c(s, len, d)) {
		lua_pop(L, 1);
		lua_pushnil(L);
	}
	return 1;
}

int decimal_fromnumber(lua_State* L) {
	LuaDecimal* d = lua_pushdecimal(L);
	if (lua_isinteger(L, 1)) {
		decimal_from_integer((int64_t)lua_tointeger(L, 1), d);
	}
	else {
		luaL_checknumber(L, 1);  // raise error if not a number
		if (!decimal_from_double((double)lua_tonumber(L, 1), d)) {
			lua_pop(L, 1);
			lua_pushnil(L);
		}
	}
	return 1;
}

int decimal_zero(lua_State* L) {
	lua_pushdecimal(L);  // already zeroed by lua_pushdecimal
	return 1;
}

// ── Inspection ───────────────────────────────────────────────────────────────

int decimal_tostring(lua_State* L) {
	LuaDecimal* d = lua_todecimal(L, 1);
	char buf[64];
	decimal_format(d, buf, sizeof(buf));
	lua_pushstring(L, buf);
	return 1;
}

int decimal_tonumber(lua_State* L) {
	LuaDecimal* d = lua_todecimal(L, 1);
	char buf[64];
	decimal_format(d, buf, sizeof(buf));
	lua_pushnumber(L, (lua_Number)strtod(buf, NULL));
	return 1;
}

int decimal_scale(lua_State* L) {
	LuaDecimal* d = lua_todecimal(L, 1);
	lua_pushinteger(L, d->scale);
	return 1;
}

int decimal_precision(lua_State* L) {
	LuaDecimal* d = lua_todecimal(L, 1);
	lua_pushinteger(L, u128_digits(d->lo, d->hi));
	return 1;
}

int decimal_isempty(lua_State* L) {
	LuaDecimal* d = lua_todecimal(L, 1);
	lua_pushboolean(L, u128_iszero(d->lo, d->hi) ? 1 : 0);
	return 1;
}

int decimal_isnegative(lua_State* L) {
	LuaDecimal* d = lua_todecimal(L, 1);
	lua_pushboolean(L, d->negative && !u128_iszero(d->lo, d->hi) ? 1 : 0);
	return 1;
}

int decimal_abs(lua_State* L) {
	LuaDecimal* src = lua_todecimal(L, 1);
	LuaDecimal* dst = lua_pushdecimal(L);
	*dst = *src;
	dst->negative = 0;
	return 1;
}

// ── Arithmetic helpers ────────────────────────────────────────────────────────

// Align two decimals to the same scale, modifying copies.
// The one with smaller scale gets its coefficient scaled up.
static void align_scales(LuaDecimal* a, LuaDecimal* b) {
	int diff = (int)a->scale - (int)b->scale;
	if (diff > 0)
		u128_scale_up(&b->lo, &b->hi, diff);
	else if (diff < 0)
		u128_scale_up(&a->lo, &a->hi, -diff);
	int16_t common = a->scale > b->scale ? a->scale : b->scale;
	a->scale = common;
	b->scale = common;
}

// signed_add: result = a + b (signed), stored in *r
static void signed_add(const LuaDecimal* a, const LuaDecimal* b, LuaDecimal* r) {
	LuaDecimal ta = *a, tb = *b;
	align_scales(&ta, &tb);
	r->scale = ta.scale;

	if (ta.negative == tb.negative) {
		// Same sign: add magnitudes
		r->lo = ta.lo; r->hi = ta.hi;
		u128_add(&r->lo, &r->hi, tb.lo, tb.hi);
		r->negative = ta.negative;
	}
	else {
		// Different signs: subtract smaller from larger
		int cmp = u128_cmp(ta.lo, ta.hi, tb.lo, tb.hi);
		if (cmp == 0) {
			r->lo = 0; r->hi = 0; r->negative = 0;
		}
		else if (cmp > 0) {
			r->lo = ta.lo; r->hi = ta.hi;
			u128_sub(&r->lo, &r->hi, tb.lo, tb.hi);
			r->negative = ta.negative;
		}
		else {
			r->lo = tb.lo; r->hi = tb.hi;
			u128_sub(&r->lo, &r->hi, ta.lo, ta.hi);
			r->negative = tb.negative;
		}
	}
	if (u128_iszero(r->lo, r->hi)) r->negative = 0;
}

// ── Arithmetic operations ─────────────────────────────────────────────────────

// u128_divmod: quotient *q = n / d, remainder *rem = n % d.  d must be non-zero.
static void u128_divmod(
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

		if (u128_cmp(*remlo, *remhi, dlo, dhi) >= 0) {
			u128_sub(remlo, remhi, dlo, dhi);
			if (i < 64) *qlo |= (1ULL << i);
			else        *qhi |= (1ULL << (i - 64));
		}
	}
}

int decimal_add(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = coerce_decimal(L, 1, &ta);
	LuaDecimal* b = coerce_decimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for addition");
	LuaDecimal* r = lua_pushdecimal(L);
	signed_add(a, b, r);
	return 1;
}

int decimal_sub(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = coerce_decimal(L, 1, &ta);
	LuaDecimal* b = coerce_decimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for subtraction");
	// negate b then add
	LuaDecimal nb = *b;
	if (!u128_iszero(nb.lo, nb.hi)) nb.negative = nb.negative ^ 1;
	LuaDecimal* r = lua_pushdecimal(L);
	signed_add(a, &nb, r);
	return 1;
}

int decimal_mul(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = coerce_decimal(L, 1, &ta);
	LuaDecimal* b = coerce_decimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for multiplication");
	LuaDecimal* r = lua_pushdecimal(L);
	u128_mul128(&r->lo, &r->hi, a->lo, a->hi, b->lo, b->hi);
	int combined_scale = (int)a->scale + (int)b->scale;
	if (combined_scale > 32767) combined_scale = 32767;  // saturate; high scales lose trailing zeros which is harmless
	r->scale = (int16_t)combined_scale;
	r->negative = (a->negative != b->negative) ? 1 : 0;
	if (u128_iszero(r->lo, r->hi)) r->negative = 0;
	return 1;
}

int decimal_div(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = coerce_decimal(L, 1, &ta);
	LuaDecimal* b = coerce_decimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for division");
	if (u128_iszero(b->lo, b->hi)) return luaL_error(L, "Decimal division by zero");

	LuaDecimal* r = lua_pushdecimal(L);
	int ts = 10 + (a->scale > b->scale ? (int)a->scale - (int)b->scale : 0);
	if (ts > 32767) ts = 32767;
	int16_t target_scale = (int16_t)ts;

	// a/b = (a_coeff * 10^-a_scale) / (b_coeff * 10^-b_scale)
	//      = (a_coeff / b_coeff) * 10^(b_scale - a_scale)
	// For the integer quotient (a_coeff * 10^S / b_coeff) to represent the answer
	// with target_scale decimal places:  S = target_scale + b_scale - a_scale
	int scale_shift = (int)target_scale + (int)b->scale - (int)a->scale;
	uint64_t nlo = a->lo, nhi = a->hi;
	if (scale_shift > 0)
		u128_scale_up(&nlo, &nhi, scale_shift);
	else if (scale_shift < 0) {
		// a has more fractional digits than needed — truncate excess from numerator.
		for (int i = 0; i < -scale_shift; i++)
			u128_div10(&nlo, &nhi);
	}

	uint64_t qlo, qhi, remlo, remhi;
	u128_divmod(nlo, nhi, b->lo, b->hi, &qlo, &qhi, &remlo, &remhi);

	r->lo    = qlo;
	r->hi    = qhi;
	r->scale = target_scale;
	r->negative = (a->negative != b->negative) ? 1 : 0;
	if (u128_iszero(r->lo, r->hi)) r->negative = 0;
	return 1;
}

int decimal_unm(lua_State* L) {
	LuaDecimal* src = lua_todecimal(L, 1);
	LuaDecimal* r = lua_pushdecimal(L);
	*r = *src;
	if (!u128_iszero(r->lo, r->hi)) r->negative = r->negative ^ 1;
	return 1;
}

int decimal_mod(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = coerce_decimal(L, 1, &ta);
	LuaDecimal* b = coerce_decimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for modulo");
	if (u128_iszero(b->lo, b->hi)) return luaL_error(L, "Decimal modulo by zero");

	LuaDecimal ca = *a, cb = *b;
	align_scales(&ca, &cb);

	uint64_t qlo, qhi, remlo, remhi;
	u128_divmod(ca.lo, ca.hi, cb.lo, cb.hi, &qlo, &qhi, &remlo, &remhi);
	(void)qlo; (void)qhi;

	LuaDecimal* r = lua_pushdecimal(L);
	r->lo    = remlo;
	r->hi    = remhi;
	r->scale = ca.scale;
	r->negative = a->negative;
	if (u128_iszero(r->lo, r->hi)) r->negative = 0;
	return 1;
}

// ── Comparison ────────────────────────────────────────────────────────────────

// Returns -1, 0, +1 comparing a and b numerically
static int decimal_cmp(const LuaDecimal* a, const LuaDecimal* b) {
	LuaDecimal ta = *a, tb = *b;
	int azero = u128_iszero(ta.lo, ta.hi);
	int bzero = u128_iszero(tb.lo, tb.hi);
	if (azero && bzero) return 0;
	if (azero) return tb.negative ? 1 : -1;
	if (bzero) return ta.negative ? -1 : 1;
	if (ta.negative != tb.negative) return ta.negative ? -1 : 1;
	align_scales(&ta, &tb);
	int mc = u128_cmp(ta.lo, ta.hi, tb.lo, tb.hi);
	return ta.negative ? -mc : mc;
}

int decimal_eq(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = coerce_decimal(L, 1, &ta);
	LuaDecimal* b = coerce_decimal(L, 2, &tb);
	if (!a || !b) { lua_pushboolean(L, 0); return 1; }
	lua_pushboolean(L, decimal_cmp(a, b) == 0 ? 1 : 0);
	return 1;
}

int decimal_lt(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = coerce_decimal(L, 1, &ta);
	LuaDecimal* b = coerce_decimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for comparison");
	lua_pushboolean(L, decimal_cmp(a, b) < 0 ? 1 : 0);
	return 1;
}

int decimal_le(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = coerce_decimal(L, 1, &ta);
	LuaDecimal* b = coerce_decimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for comparison");
	lua_pushboolean(L, decimal_cmp(a, b) <= 0 ? 1 : 0);
	return 1;
}

// ── Rounding ─────────────────────────────────────────────────────────────────

int decimal_round(lua_State* L) {
	LuaDecimal* src = lua_todecimal(L, 1);
	int new_scale = (int)luaL_optinteger(L, 2, 0);
	if (new_scale < 0) new_scale = 0;
	LuaDecimal* r = lua_pushdecimal(L);
	*r = *src;
	int diff = (int)r->scale - new_scale;
	if (diff <= 0) return 1;  // already at or coarser than requested scale
	// Remove (diff-1) digits silently, then check the last removed digit for rounding.
	for (int i = 0; i < diff - 1; i++)
		u128_div10(&r->lo, &r->hi);
	uint64_t deciding_rem = u128_div10(&r->lo, &r->hi);
	r->scale = (int16_t)new_scale;
	if (deciding_rem >= 5)
		u128_add_digit(&r->lo, &r->hi, 1);
	if (u128_iszero(r->lo, r->hi)) r->negative = 0;
	return 1;
}

int decimal_truncate(lua_State* L) {
	LuaDecimal* src = lua_todecimal(L, 1);
	int new_scale = (int)luaL_optinteger(L, 2, 0);
	if (new_scale < 0) new_scale = 0;
	LuaDecimal* r = lua_pushdecimal(L);
	*r = *src;
	int diff = (int)r->scale - new_scale;
	if (diff <= 0) return 1;
	for (int i = 0; i < diff; i++)
		u128_div10(&r->lo, &r->hi);
	r->scale = (int16_t)new_scale;
	if (u128_iszero(r->lo, r->hi)) r->negative = 0;
	return 1;
}
