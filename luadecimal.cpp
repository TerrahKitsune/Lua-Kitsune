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
	// (hi:lo) * 10  = hi*10 : lo*10, carrying overflow from lo into hi
	uint64_t lo_hi = (*lo >> 32) * 10;
	uint64_t lo_lo = (*lo & 0xFFFFFFFFULL) * 10;
	lo_lo += (lo_hi << 32);
	uint64_t carry = (lo_hi >> 32) + (lo_lo < (lo_hi << 32) ? 1 : 0);
	*lo = lo_lo;
	*hi = (*hi) * 10 + carry;
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
	uint64_t lo0 = (*lo & 0xFFFFFFFFULL) * b64;
	uint64_t lo1 = (*lo >> 32) * b64;
	uint64_t hi0 = (*hi & 0xFFFFFFFFULL) * b64;
	uint64_t hi1 = (*hi >> 32) * b64;
	uint64_t new_lo = lo0 + (lo1 << 32);
	uint64_t carry = (new_lo < lo0) ? 1ULL : 0ULL;
	carry += (lo1 >> 32);
	carry += hi0 + (hi1 << 32);
	*lo = new_lo;
	*hi = carry;
}

// Full 128x128 → 128 multiply (truncates to 128 bits, sufficient for our scale range)
static void u128_mul128(uint64_t* rlo, uint64_t* rhi,
	uint64_t alo, uint64_t ahi,
	uint64_t blo, uint64_t bhi) {
	// Split a and b into 32-bit limbs and do schoolbook multiplication.
	// Only keep the low 128 bits of the result.
	uint64_t a0 = alo & 0xFFFFFFFFULL, a1 = alo >> 32;
	uint64_t a2 = ahi & 0xFFFFFFFFULL, a3 = ahi >> 32;
	uint64_t b0 = blo & 0xFFFFFFFFULL, b1 = blo >> 32;
	uint64_t b2 = bhi & 0xFFFFFFFFULL, b3 = bhi >> 32;
	// Compute partial products that contribute to the low 128 bits.
	uint64_t lo = a0 * b0;
	uint64_t mid = a0 * b1 + a1 * b0;
	uint64_t hi2 = a0 * b2 + a1 * b1 + a2 * b0;
	uint64_t hi3 = a0 * b3 + a1 * b2 + a2 * b1 + a3 * b0;
	// Accumulate into 128-bit result.
	uint64_t rlo2 = lo + ((mid & 0xFFFFFFFFULL) << 32);
	uint64_t carry = (rlo2 < lo) ? 1ULL : 0ULL;
	uint64_t rhi2 = (mid >> 32) + hi2 + (hi3 << 32) + carry;
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
	int16_t scale = 0;
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
		if (has_dot) scale++;
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
		for (; i < len && isdigit((unsigned char)s[i]); i++)
			exp = exp * 10 + (s[i] - '0');
		if (exp_neg)
			scale = (int16_t)(scale + exp);
		else
			scale = (int16_t)(scale - exp);
	}

	if (!has_digit) return 0;
	if (scale < 0) {
		// Positive exponent: scale the coefficient up
		u128_scale_up(&out->lo, &out->hi, -scale);
		scale = 0;
	}
	out->scale = scale;
	// Normalise: -0 → +0
	if (u128_iszero(out->lo, out->hi)) out->negative = 0;
	return 1;
}

// Formats to buf (must be at least 48 bytes). Returns chars written.
static int decimal_format(const LuaDecimal* d, char* buf, size_t bufsz) {
	if (u128_iszero(d->lo, d->hi)) {
		if (d->scale > 0) {
			int n = snprintf(buf, bufsz, "0.%0*d", (int)d->scale, 0);
			return n;
		}
		return snprintf(buf, bufsz, "0");
	}

	// Extract digits by repeated division
	char digits[42];
	int ndigits = 0;
	uint64_t lo = d->lo, hi = d->hi;
	while (!u128_iszero(lo, hi)) {
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
	char buf[48];
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
	char buf[48];
	decimal_format(d, buf, sizeof(buf));
	lua_pushstring(L, buf);
	return 1;
}

int decimal_tonumber(lua_State* L) {
	LuaDecimal* d = lua_todecimal(L, 1);
	char buf[48];
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
	if (!u128_iszero(nb.lo, nb.hi)) nb.negative = !nb.negative;
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
	r->scale = (int16_t)(a->scale + b->scale);
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

	// Scale dividend up for precision: 34 digits max
	LuaDecimal* r = lua_pushdecimal(L);
	int16_t target_scale = 10;  // default: 10 decimal places of result
	if (a->scale > b->scale)
		target_scale = (int16_t)(a->scale - b->scale + 10);

	// Shift numerator left by target_scale extra digits
	uint64_t nlo = a->lo, nhi = a->hi;
	u128_scale_up(&nlo, &nhi, target_scale + b->scale);

	// Long divide nlo:nhi by b (portable 128/128 → 128).
	// We use repeated subtraction shifted by the magnitude difference.
	uint64_t qlo = 0, qhi = 0;
	// Find how many bits the divisor needs to shift to align with dividend.
	uint64_t dlo = b->lo, dhi = b->hi;
	int shift = 0;
	// Count leading zeros to find alignment.
	// Shift divisor left until it is larger than dividend, then back off one.
	uint64_t tlo = dlo, thi = dhi;
	while (u128_cmp(tlo, thi, nlo, nhi) <= 0 && shift < 127) {
		uint64_t old_tlo = tlo;
		thi = (thi << 1) | (tlo >> 63);
		tlo = tlo << 1;
		(void)old_tlo;
		shift++;
	}
	for (int bit = shift; bit >= 0; bit--) {
		// Shift divisor right by 1 to get 2^bit * original divisor
		tlo = (tlo >> 1) | (thi << 63);
		thi = thi >> 1;
		if (u128_cmp(nlo, nhi, tlo, thi) >= 0) {
			u128_sub(&nlo, &nhi, tlo, thi);
			// Set bit 'bit' in quotient
			if (bit < 64) {
				qlo |= (1ULL << bit);
			}
			else if (bit < 128) {
				qhi |= (1ULL << (bit - 64));
			}
		}
	}
	r->lo = qlo;
	r->hi = qhi;
	r->scale = (int16_t)(target_scale + b->scale - b->scale + a->scale - a->scale + target_scale - target_scale + target_scale);
	r->scale = target_scale;
	r->negative = (a->negative != b->negative) ? 1 : 0;
	if (u128_iszero(r->lo, r->hi)) r->negative = 0;
	return 1;
}

int decimal_unm(lua_State* L) {
	LuaDecimal* src = lua_todecimal(L, 1);
	LuaDecimal* r = lua_pushdecimal(L);
	*r = *src;
	if (!u128_iszero(r->lo, r->hi)) r->negative = !r->negative;
	return 1;
}

int decimal_mod(lua_State* L) {
	LuaDecimal ta, tb;
	LuaDecimal* a = coerce_decimal(L, 1, &ta);
	LuaDecimal* b = coerce_decimal(L, 2, &tb);
	if (!a || !b) return luaL_error(L, "Decimal expected for modulo");
	if (u128_iszero(b->lo, b->hi)) return luaL_error(L, "Decimal modulo by zero");

	// Align scales
	LuaDecimal ca = *a, cb = *b;
	align_scales(&ca, &cb);

	// Compute remainder via: rem = na - (na/nb)*nb
	uint64_t nalo = ca.lo, nahi = ca.hi;
	uint64_t nblo = cb.lo, nbhi = cb.hi;
	// Quotient via the same bit-shift long division
	uint64_t qlo = 0, qhi = 0;
	uint64_t tlo = nblo, thi = nbhi;
	int shift = 0;
	while (u128_cmp(tlo, thi, nalo, nahi) <= 0 && shift < 127) {
		thi = (thi << 1) | (tlo >> 63);
		tlo = tlo << 1;
		shift++;
	}
	uint64_t remlo = nalo, remhi = nahi;
	for (int bit = shift; bit >= 0; bit--) {
		tlo = (tlo >> 1) | (thi << 63);
		thi = thi >> 1;
		if (u128_cmp(remlo, remhi, tlo, thi) >= 0) {
			u128_sub(&remlo, &remhi, tlo, thi);
			if (bit < 64)  qlo |= (1ULL << bit);
			else if (bit < 128) qhi |= (1ULL << (bit - 64));
		}
	}
	(void)qlo; (void)qhi;

	LuaDecimal* r = lua_pushdecimal(L);
	r->lo = remlo;
	r->hi = remhi;
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
	if (diff <= 0) return 1;  // already at or finer than requested scale
	// Remove diff digits from coefficient, round half-up
	uint64_t last_rem = 0;
	for (int i = 0; i < diff; i++)
		last_rem = u128_div10(&r->lo, &r->hi);
	r->scale = (int16_t)new_scale;
	if (last_rem >= 5)
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
