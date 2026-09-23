#include "luatext.h"
#include "platform.h"
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <wctype.h>
#ifndef _WIN32
#include <iconv.h>
#include <locale.h>
#endif

#define REPLACEMENT_CHAR 0xFFFD

// -- UTF-8 / UTF-16 primitives -------------------------------------------------

// Decodes one UTF-8 sequence starting at s[*i] and advances *i past it.
// Malformed, truncated, overlong and surrogate encodings return U+FFFD and consume one byte.
static uint32_t utf8_next(const unsigned char* s, size_t len, size_t* i) {

	unsigned char c = s[*i];
	if (c < 0x80) {
		(*i)++;
		return c;
	}

	int extra;
	uint32_t cp;
	uint32_t min;
	if ((c & 0xE0) == 0xC0) {
		extra = 1;
		cp = c & 0x1F;
		min = 0x80;
	}
	else if ((c & 0xF0) == 0xE0) {
		extra = 2;
		cp = c & 0x0F;
		min = 0x800;
	}
	else if ((c & 0xF8) == 0xF0) {
		extra = 3;
		cp = c & 0x07;
		min = 0x10000;
	}
	else {
		(*i)++;
		return REPLACEMENT_CHAR;
	}

	if (*i + (size_t)extra >= len) {
		(*i)++;
		return REPLACEMENT_CHAR;
	}

	for (int k = 1; k <= extra; k++) {
		unsigned char b = s[*i + (size_t)k];
		if ((b & 0xC0) != 0x80) {
			(*i)++;
			return REPLACEMENT_CHAR;
		}
		cp = (cp << 6) | (b & 0x3F);
	}

	*i += (size_t)extra + 1;
	if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
		return REPLACEMENT_CHAR;
	return cp;
}

uint32_t kitsune_utf8_next(const char* s, size_t len, size_t* i) {
	return utf8_next((const unsigned char*)s, len, i);
}

bool kitsune_utf8_valid(const char* s, size_t len) {

	const unsigned char* u = (const unsigned char*)s;
	for (size_t i = 0; i < len;) {
		if (u[i] < 0x80) {
			i++;
			continue;
		}
		size_t start = i;
		// A genuine U+FFFD is EF BF BD; anything else decoding to U+FFFD was malformed.
		if (utf8_next(u, len, &i) == REPLACEMENT_CHAR && !(i - start == 3 && u[start] == 0xEF && u[start + 1] == 0xBF && u[start + 2] == 0xBD))
			return false;
	}
	return true;
}

// Declared in luatext.h.
size_t kitsune_utf8_encode(char* out, uint32_t cp) {

	if (cp < 0x80) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (cp >> 18));
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

// Decodes the code point starting at UTF-16 unit u0 (u1 is the following unit, valid only
// when hasNext). Sets *used to the units consumed. Unpaired surrogates return U+FFFD.
static uint32_t utf16_decode(uint32_t u0, bool hasNext, uint32_t u1, int* used) {

	*used = 1;
	if (u0 >= 0xD800 && u0 <= 0xDBFF) {
		if (hasNext && u1 >= 0xDC00 && u1 <= 0xDFFF) {
			*used = 2;
			return 0x10000 + ((u0 - 0xD800) << 10) + (u1 - 0xDC00);
		}
		return REPLACEMENT_CHAR;
	}
	if (u0 >= 0xDC00 && u0 <= 0xDFFF)
		return REPLACEMENT_CHAR;
	return u0;
}

// Writes the UTF-16LE encoding of cp to out (room for 4 bytes); returns the byte count.
static size_t utf16le_put(char* out, uint32_t cp) {

	if (cp >= 0x10000) {
		uint32_t v = cp - 0x10000;
		uint32_t hi = 0xD800 + (v >> 10);
		uint32_t lo = 0xDC00 + (v & 0x3FF);
		out[0] = (char)(hi & 0xFF);
		out[1] = (char)(hi >> 8);
		out[2] = (char)(lo & 0xFF);
		out[3] = (char)(lo >> 8);
		return 4;
	}
	out[0] = (char)(cp & 0xFF);
	out[1] = (char)(cp >> 8);
	return 2;
}

// -- Shared helpers --------------------------------------------------------------

void lua_pushwideasutf8(lua_State* L, const wchar_t* str) {
	lua_pushwideasutf8(L, str, str ? wcslen(str) : 0);
}

void lua_pushwideasutf8(lua_State* L, const wchar_t* str, size_t len) {

	if (!str || len == 0) {
		lua_pushliteral(L, "");
		return;
	}

	// A UTF-16 unit produces at most 3 UTF-8 bytes (a surrogate pair: 2 units, 4 bytes);
	// a UTF-32 unit at most 4.
	luaL_Buffer b;
	char* out = luaL_buffinitsize(L, &b, len * (WCHAR_MAX <= 0xFFFF ? 3 : 4));
	size_t n = 0;

	for (size_t i = 0; i < len;) {
#if WCHAR_MAX <= 0xFFFF
		int used;
		uint32_t next = (i + 1 < len) ? (uint32_t)str[i + 1] : 0;
		uint32_t cp = utf16_decode((uint32_t)str[i], i + 1 < len, next, &used);
		i += (size_t)used;
#else
		uint32_t cp = (uint32_t)str[i];
		if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
			cp = REPLACEMENT_CHAR;
		i++;
#endif
		n += kitsune_utf8_encode(out + n, cp);
	}

	luaL_pushresultsize(&b, n);
}

size_t kitsune_utf8_to_wide(const char* src, size_t len, wchar_t* dst) {

	const unsigned char* s = (const unsigned char*)src;
	size_t n = 0;

	for (size_t i = 0; i < len;) {
		uint32_t cp = utf8_next(s, len, &i);
#if WCHAR_MAX <= 0xFFFF
		if (cp >= 0x10000) {
			uint32_t v = cp - 0x10000;
			dst[n++] = (wchar_t)(0xD800 + (v >> 10));
			dst[n++] = (wchar_t)(0xDC00 + (v & 0x3FF));
			continue;
		}
#endif
		dst[n++] = (wchar_t)cp;
	}

	dst[n] = L'\0';
	return n;
}

size_t kitsune_utf8_wide_len(const char* src, size_t len) {

	const unsigned char* s = (const unsigned char*)src;
	size_t n = 0;
	for (size_t i = 0; i < len;) {
		uint32_t cp = utf8_next(s, len, &i);
		n += (WCHAR_MAX <= 0xFFFF && cp >= 0x10000) ? 2 : 1;
	}
	return n;
}

wchar_t* kitsune_utf8_to_wide_alloc(const char* src) {

	if (!src)
		return NULL;
	size_t len = strlen(src);
	wchar_t* w = (wchar_t*)kitsune_malloc((len + 1) * sizeof(wchar_t));
	if (w)
		kitsune_utf8_to_wide(src, len, w);
	return w;
}

// Total length of the UTF-8 sequence introduced by lead byte c (1 for ASCII and for
// bytes that cannot start a sequence).
static size_t utf8_seq_len(unsigned char c) {

	if ((c & 0xE0) == 0xC0)
		return 2;
	if ((c & 0xF0) == 0xE0)
		return 3;
	if ((c & 0xF8) == 0xF0)
		return 4;
	return 1;
}

// Length of an incomplete sequence at the end of s: a lead byte followed by fewer
// continuation bytes than it announces. 0 when s ends on a complete (or invalid) sequence.
static size_t utf8_incomplete_tail(const unsigned char* s, size_t len) {

	size_t back = 0;
	while (back < 3 && back < len && (s[len - 1 - back] & 0xC0) == 0x80)
		back++;
	if (back == len)
		return 0;

	size_t have = back + 1;
	size_t need = utf8_seq_len(s[len - have]);
	return (need > 1 && have < need) ? have : 0;
}

size_t kitsune_utf8_to_wide_chunk(KitsuneUtf8Carry* carry, const char* src, size_t len, wchar_t* dst) {

	const unsigned char* s = (const unsigned char*)src;
	size_t n = 0;
	size_t start = 0;

	if (carry->len > 0) {
		if (src) {
			// Complete the carried sequence with continuation bytes from this chunk.
			size_t need = utf8_seq_len(carry->bytes[0]);
			while (carry->len < need && start < len && (s[start] & 0xC0) == 0x80)
				carry->bytes[carry->len++] = s[start++];
			if (carry->len < need && start == len) {
				dst[0] = L'\0';
				return 0;
			}
		}
		// Complete, interrupted by a non-continuation byte, or flushed at end of input;
		// the latter two decode to U+FFFD.
		n = kitsune_utf8_to_wide((const char*)carry->bytes, carry->len, dst);
		carry->len = 0;
	}

	if (!src) {
		dst[n] = L'\0';
		return n;
	}

	size_t tail = utf8_incomplete_tail(s + start, len - start);
	n += kitsune_utf8_to_wide(src + start, len - start - tail, dst + n);
	memcpy(carry->bytes, s + len - tail, tail);
	carry->len = (unsigned char)tail;
	return n;
}

void lua_pushutf16lefromutf8(lua_State* L, const char* src, size_t len) {

	if (len == 0) {
		lua_pushliteral(L, "");
		return;
	}

	// Each input byte yields at most 2 output bytes (a 4-byte sequence yields 4).
	const unsigned char* s = (const unsigned char*)src;
	luaL_Buffer b;
	char* out = luaL_buffinitsize(L, &b, len * 2);
	size_t n = 0;

	for (size_t i = 0; i < len;)
		n += utf16le_put(out + n, utf8_next(s, len, &i));

	luaL_pushresultsize(&b, n);
}

void lua_pushutf8fromutf16le(lua_State* L, const char* src, size_t len) {

	size_t units = len / 2;
	if (units == 0) {
		lua_pushliteral(L, "");
		return;
	}

	// Each unit yields at most 3 UTF-8 bytes (a surrogate pair: 2 units, 4 bytes).
	const unsigned char* s = (const unsigned char*)src;
	luaL_Buffer b;
	char* out = luaL_buffinitsize(L, &b, units * 3);
	size_t n = 0;

	for (size_t i = 0; i < units;) {
		uint32_t u0 = (uint32_t)s[i * 2] | ((uint32_t)s[i * 2 + 1] << 8);
		uint32_t u1 = 0;
		if (i + 1 < units)
			u1 = (uint32_t)s[i * 2 + 2] | ((uint32_t)s[i * 2 + 3] << 8);
		int used;
		uint32_t cp = utf16_decode(u0, i + 1 < units, u1, &used);
		i += (size_t)used;
		n += kitsune_utf8_encode(out + n, cp);
	}

	luaL_pushresultsize(&b, n);
}

// -- Text module -----------------------------------------------------------------

// Text.ToUtf16(str) -> UTF-16LE bytes as a Lua string (no BOM).
int TextToUtf16(lua_State* L) {

	size_t len;
	const char* s = luaL_checklstring(L, 1, &len);
	lua_pushutf16lefromutf8(L, s, len);
	return 1;
}

// Text.FromUtf16(bytes) -> UTF-8 string. A leading BOM is kept as U+FEFF.
int TextFromUtf16(lua_State* L) {

	size_t len;
	const char* s = luaL_checklstring(L, 1, &len);
	lua_pushutf8fromutf16le(L, s, len);
	return 1;
}

#ifdef _WIN32
// Code page 0 means the system's ANSI code page. That is looked up from the system locale
// rather than CP_ACP, which a process manifest (kitsune.exe's activeCodePage) can switch to
// UTF-8 and would make legacy text convert differently depending on the host process.
static UINT resolve_codepage(lua_Integer cp) {

	if (cp != 0)
		return (UINT)cp;
	DWORD acp = 0;
	if (GetLocaleInfoEx(LOCALE_NAME_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
		(LPWSTR)&acp, sizeof(acp) / sizeof(WCHAR)) && acp != 0)
		return (UINT)acp;
	return CP_ACP;  // Unicode-only system locales have no ANSI code page
}
#else
// Maps a Windows code page number to an iconv charset name; 0 means the locale's charset.
static void iconv_charset(lua_Integer cp, char* out, size_t cap) {

	if (cp == 0)
		snprintf(out, cap, "%s", "");
	else if (cp == 65001)
		snprintf(out, cap, "%s", "UTF-8");
	else if (cp == 20127)
		snprintf(out, cap, "%s", "ASCII");
	else if (cp >= 28591 && cp <= 28606)
		snprintf(out, cap, "ISO-8859-%d", (int)(cp - 28590));
	else
		snprintf(out, cap, "CP%d", (int)cp);
}

// Runs src through cd and pushes the result. An invalid or unmappable input sequence
// appends 'bad' and skips one input character (one UTF-8 sequence when utf8Input).
static void iconv_push(lua_State* L, iconv_t cd, const char* src, size_t len, const char* bad, bool utf8Input) {

	luaL_Buffer b;
	luaL_buffinit(L, &b);
	char* in = (char*)src;
	size_t inLeft = len;
	char chunk[1024];

	while (inLeft > 0) {
		char* out = chunk;
		size_t outLeft = sizeof(chunk);
		size_t r = iconv(cd, &in, &inLeft, &out, &outLeft);
		luaL_addlstring(&b, chunk, sizeof(chunk) - outLeft);
		if (r != (size_t)-1 || errno == E2BIG)
			continue;

		luaL_addstring(&b, bad);
		size_t skip = 1;
		if (utf8Input) {
			skip = 0;
			utf8_next((const unsigned char*)in, inLeft, &skip);
		}
		in += skip;
		inLeft -= skip;
		iconv(cd, NULL, NULL, NULL, NULL);
	}

	char* out = chunk;
	size_t outLeft = sizeof(chunk);
	iconv(cd, NULL, NULL, &out, &outLeft);
	luaL_addlstring(&b, chunk, sizeof(chunk) - outLeft);
	luaL_pushresult(&b);
}
#endif

// Text.FromCodepage(bytes, opt codepage) -> UTF-8 string.
// codepage is a Windows code page number (1252, 437, 932, 28591, ...); omitted or 0 means the
// system ANSI code page on Windows and the locale's charset elsewhere. Undecodable bytes become U+FFFD.
int TextFromCodepage(lua_State* L) {

	size_t len;
	const char* s = luaL_checklstring(L, 1, &len);
	lua_Integer cp = luaL_optinteger(L, 2, 0);

	if (len == 0) {
		lua_pushliteral(L, "");
		return 1;
	}

#ifdef _WIN32
	UINT wcp = resolve_codepage(cp);
	int wlen = MultiByteToWideChar(wcp, 0, s, (int)len, NULL, 0);
	if (wlen <= 0)
		return luaL_error(L, "Text.FromCodepage: unsupported code page %d", (int)cp);
	wchar_t* wide = (wchar_t*)lua_newuserdatauv(L, (size_t)wlen * sizeof(wchar_t), 0);
	MultiByteToWideChar(wcp, 0, s, (int)len, wide, wlen);
	lua_pushwideasutf8(L, wide, (size_t)wlen);
#else
	char charset[32];
	iconv_charset(cp, charset, sizeof(charset));
	iconv_t cd = iconv_open("UTF-8", charset);
	if (cd == (iconv_t)-1)
		return luaL_error(L, "Text.FromCodepage: unsupported code page %d", (int)cp);
	iconv_push(L, cd, s, len, "\xEF\xBF\xBD", false);
	iconv_close(cd);
#endif
	return 1;
}

// Text.ToCodepage(str, opt codepage) -> bytes in that code page.
// Same codepage rules as FromCodepage. Characters the code page cannot represent become '?'.
int TextToCodepage(lua_State* L) {

	size_t len;
	const char* s = luaL_checklstring(L, 1, &len);
	lua_Integer cp = luaL_optinteger(L, 2, 0);

	if (len == 0) {
		lua_pushliteral(L, "");
		return 1;
	}

#ifdef _WIN32
	wchar_t* wide = (wchar_t*)lua_newuserdatauv(L, (len + 1) * sizeof(wchar_t), 0);
	size_t wlen = kitsune_utf8_to_wide(s, len, wide);
	UINT wcp = resolve_codepage(cp);
	int blen = WideCharToMultiByte(wcp, 0, wide, (int)wlen, NULL, 0, NULL, NULL);
	if (blen <= 0)
		return luaL_error(L, "Text.ToCodepage: unsupported code page %d", (int)cp);
	luaL_Buffer b;
	char* out = luaL_buffinitsize(L, &b, (size_t)blen);
	WideCharToMultiByte(wcp, 0, wide, (int)wlen, out, blen, NULL, NULL);
	luaL_pushresultsize(&b, (size_t)blen);
#else
	char charset[32];
	iconv_charset(cp, charset, sizeof(charset));
	iconv_t cd = iconv_open(charset, "UTF-8");
	if (cd == (iconv_t)-1)
		return luaL_error(L, "Text.ToCodepage: unsupported code page %d", (int)cp);
	iconv_push(L, cd, s, len, "?", true);
	iconv_close(cd);
#endif
	return 1;
}

#ifndef _WIN32
// A UTF-8 LC_CTYPE locale for Unicode case mapping, independent of the process locale
// (towlower in the default "C" locale only maps ASCII). NULL if none is installed.
static locale_t utf8_ctype_locale() {

	static locale_t loc = []() {
		locale_t l = newlocale(LC_CTYPE_MASK, "C.UTF-8", (locale_t)0);
		if (!l)
			l = newlocale(LC_CTYPE_MASK, "en_US.UTF-8", (locale_t)0);
		return l;
	}();
	return loc;
}
#endif

// Unicode-aware case conversion of a UTF-8 string (simple, locale-independent mappings).
static int change_case(lua_State* L, bool upper) {

	size_t len;
	const char* s = luaL_checklstring(L, 1, &len);

	if (len == 0) {
		lua_pushliteral(L, "");
		return 1;
	}

#ifdef _WIN32
	wchar_t* wide = (wchar_t*)lua_newuserdatauv(L, (len + 1) * sizeof(wchar_t), 0);
	size_t wlen = kitsune_utf8_to_wide(s, len, wide);
	DWORD flags = upper ? LCMAP_UPPERCASE : LCMAP_LOWERCASE;
	int mlen = LCMapStringEx(LOCALE_NAME_INVARIANT, flags, wide, (int)wlen, NULL, 0, NULL, NULL, 0);
	if (mlen <= 0)
		return luaL_error(L, "Text.%s: case mapping failed", upper ? "Upper" : "Lower");
	wchar_t* mapped = (wchar_t*)lua_newuserdatauv(L, (size_t)mlen * sizeof(wchar_t), 0);
	LCMapStringEx(LOCALE_NAME_INVARIANT, flags, wide, (int)wlen, mapped, mlen, NULL, NULL, 0);
	lua_pushwideasutf8(L, mapped, (size_t)mlen);
#else
	locale_t loc = utf8_ctype_locale();
	const unsigned char* src = (const unsigned char*)s;
	luaL_Buffer b;
	luaL_buffinit(L, &b);

	for (size_t i = 0; i < len;) {
		wint_t cp = (wint_t)utf8_next(src, len, &i);
		wint_t m;
		if (loc)
			m = upper ? towupper_l(cp, loc) : towlower_l(cp, loc);
		else
			m = upper ? towupper(cp) : towlower(cp);
		char tmp[4];
		luaL_addlstring(&b, tmp, kitsune_utf8_encode(tmp, (uint32_t)m));
	}

	luaL_pushresult(&b);
#endif
	return 1;
}

// Text.Lower(str) -> str with every letter lower-cased (all scripts, not only ASCII).
int TextLower(lua_State* L) {
	return change_case(L, false);
}

// Text.Upper(str) -> str with every letter upper-cased (all scripts, not only ASCII).
int TextUpper(lua_State* L) {
	return change_case(L, true);
}
