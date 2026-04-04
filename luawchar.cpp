#include "luawchar.h"
#include <algorithm>
#include <wchar.h>
#include <wctype.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>
#include "platform.h"
#include <locale.h>
#include <cstdarg>
#ifndef _WIN32
#include <iconv.h>
#endif

#ifndef _WIN32
// Portable UTF-8 <-> wchar_t conversion helpers using POSIX iconv.
static size_t utf8_to_wchar(const char* src, size_t srcLen, wchar_t* dst, size_t dstCap) {
	if (srcLen == 0 || !dst || dstCap == 0) return 0;
	iconv_t cd = iconv_open("WCHAR_T", "UTF-8");
	if (cd == (iconv_t)-1) return 0;
	char* in = (char*)src;
	size_t inLeft = srcLen;
	char* out = (char*)dst;
	size_t outLeft = dstCap * sizeof(wchar_t);
	iconv(cd, &in, &inLeft, &out, &outLeft);
	iconv_close(cd);
	size_t written = (dstCap * sizeof(wchar_t) - outLeft) / sizeof(wchar_t);
	if (written < dstCap) dst[written] = L'\0';
	return written;
}
static size_t wchar_to_utf8(const wchar_t* src, size_t srcLen, char* dst, size_t dstCap) {
	if (srcLen == 0 || !dst || dstCap == 0) return 0;
	iconv_t cd = iconv_open("UTF-8", "WCHAR_T");
	if (cd == (iconv_t)-1) return 0;
	char* in = (char*)src;
	size_t inLeft = srcLen * sizeof(wchar_t);
	char* out = dst;
	size_t outLeft = dstCap;
	iconv(cd, &in, &inLeft, &out, &outLeft);
	iconv_close(cd);
	size_t written = dstCap - outLeft;
	if (written < dstCap) dst[written] = '\0';
	return written;
}
#endif

LuaWChar* lua_pushwchar(lua_State* L, const wchar_t* str) {
	return lua_pushwchar(L, str, wcslen(str));
}

LuaWChar* lua_pushwchar(lua_State* L, const wchar_t* str, size_t len) {

	LuaWChar* wchar = lua_pushwchar(L);

	wchar->str = (wchar_t*)gff_calloc(len + 1, sizeof(wchar_t));

	if (!wchar->str) {
		luaL_error(L, "out of memory");
		return 0;
	}

	if (str && len > 0)
		memcpy(wchar->str, str, len * sizeof(wchar_t));

	wchar->len = len;

	return wchar;
}

int GetWCharCountForCodePoint(int codePoint) {
#ifdef _WIN32
	// On Windows wchar_t is UTF-16; supplementary code points need a surrogate pair.
	if (codePoint >= 0 && codePoint <= 0x10FFFF) {
		if (codePoint <= 0xFFFF) {
			return 1;
		}
		else {
			return 2;
		}
	}
	return 0;
#else
	// On Linux wchar_t is UTF-32: every valid code point fits in one wchar_t.
	return (codePoint >= 0 && codePoint <= 0x10FFFF) ? 1 : 0;
#endif
}

bool FillLuaWCharWithCodePoint(LuaWChar* luaStr, int codePoint) {

	int wcharCount = GetWCharCountForCodePoint(codePoint);

	if (wcharCount > 0) {

		luaStr->str = (wchar_t*)gff_calloc(wcharCount + 1, sizeof(wchar_t));

		if (!luaStr->str) {
			return false;
		}

		luaStr->len = wcharCount;

#ifdef _WIN32
		if (wcharCount == 1) {
			luaStr->str[0] = (wchar_t)codePoint;
		}
		else {
			luaStr->str[0] = (wchar_t)(((codePoint - 0x10000) >> 10) + 0xD800);
			luaStr->str[1] = (wchar_t)(((codePoint - 0x10000) & 0x3FF) + 0xDC00);
		}
#else
		// On Linux wchar_t is UTF-32: store the full code point in one element.
		luaStr->str[0] = (wchar_t)codePoint;
#endif

		return true;
	}

	return false;
}

int FromBytes(lua_State* L) {

	size_t len;
	LuaWChar* wchar;

	if (lua_type(L, 1) == LUA_TSTRING) {

		const char* raw = lua_tolstring(L, 1, &len);

		if ((len % sizeof(char16_t)) != 0) {
			luaL_error(L, "byte buffer length is not a multiple of 2 (expected char16_t / UTF-16 LE)");
			return 0;
		}

		const char16_t* chars = (const char16_t*)raw;
		size_t charCount = len / sizeof(char16_t);

		LuaWChar* wchar = lua_pushwchar(L);
		size_t wcharLen = 0;
		wchar->str = char16_alloc_as_wchar(chars, charCount, &wcharLen);
		wchar->len = wcharLen;

		if (!wchar->str) {
			luaL_error(L, "out of memory");
			return 0;
		}

		return 1;
	}
	else if (lua_type(L, 1) == LUA_TNUMBER) {

		int byte = (int)lua_tointeger(L, 1);

		LuaWChar* wchar = lua_pushwchar(L);
		FillLuaWCharWithCodePoint(wchar, byte);

		return 1;
	}

	luaL_checktype(L, 1, LUA_TTABLE);
	len = lua_rawlen(L, 1);
	wchar = lua_pushwchar(L);

	// Collect table values as char16_t code units, then convert to wchar_t.
	char16_t* char16buf = (char16_t*)gff_malloc((len + 1) * sizeof(char16_t));
	if (!char16buf) {
		luaL_error(L, "out of memory");
		return 0;
	}

	lua_pushvalue(L, 1);
	for (size_t i = 0; i < len; i++) {
		lua_pushinteger(L, (lua_Integer)(i + 1));
		lua_gettable(L, -2);
		char16buf[i] = (char16_t)lua_tointeger(L, -1);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);

	size_t wcharLen = 0;
	wchar->str = char16_alloc_as_wchar(char16buf, len, &wcharLen);
	gff_free(char16buf);
	wchar->len = wcharLen;

	if (!wchar->str) {
		luaL_error(L, "out of memory");
		return 0;
	}

	return 1;
}

int ToBytes(lua_State* L) {

	LuaWChar* wchar = lua_towchar(L, 1);

	size_t char16Len = 0;
	char16_t* buf = wchar_alloc_as_char16(wchar->str, wchar->len, &char16Len);

	lua_createtable(L, (int)char16Len, 0);

	if (buf) {
		for (size_t i = 0; i < char16Len; i++) {
			lua_pushinteger(L, (lua_Integer)buf[i]);
			lua_rawseti(L, -2, (int)i + 1);
		}
		gff_free(buf);
	}

	return 1;
}

int FromToLower(lua_State* L) {

	LuaWChar* wchar = lua_towchar(L, 1);
	LuaWChar* result = lua_pushwchar(L);

	result->str = (wchar_t*)gff_calloc(wchar->len + 1, sizeof(wchar_t));

	if (!result->str) {
		luaL_error(L, "out of memory");
		return 0;
	}

	for (size_t i = 0; i < wchar->len; i++)
	{
		result->str[i] = towlower(wchar->str[i]);
	}

	result->len = wchar->len;

	return 1;
}

int FromToUpper(lua_State* L) {

	LuaWChar* wchar = lua_towchar(L, 1);
	LuaWChar* result = lua_pushwchar(L);

	result->str = (wchar_t*)gff_calloc(wchar->len + 1, sizeof(wchar_t));

	if (!result->str) {
		luaL_error(L, "out of memory");
		return 0;
	}

	for (size_t i = 0; i < wchar->len; i++)
	{
		result->str[i] = towupper(wchar->str[i]);
	}

	result->len = wchar->len;

	return 1;
}

int FromSubstring(lua_State* L) {

	LuaWChar* wchar = lua_towchar(L, 1);
	size_t start = (size_t)luaL_checkinteger(L, 2);
	size_t length = (size_t)luaL_optinteger(L, 3, wchar->len - (start - 1));

	if (start > wchar->len || start <= 0) {
		lua_pushnil(L);
	}
	else {
		lua_pushwchar(L, &wchar->str[start - 1], (std::min)(length, wchar->len - (start - 1)));
	}

	return 1;
}

int SetLocale(lua_State* L) {

	setlocale(LC_ALL, luaL_optstring(L, 1, ""));

	return 0;
}

int FromUtf8(lua_State* L) {

	if (lua_gettop(L) < 1) {
		return 0;
	}
	else if (lua_isnone(L, -1)) {
		lua_pushnil(L);
		return 1;
	}
	else if (lua_iswchar(L, -1)) {
		return 1;
	}

	size_t len;
	const char* data = luaL_tolstring(L, -1, &len);
	lua_pop(L, 1);

	LuaWChar* wchar = lua_pushwchar(L);

	wchar->str = (wchar_t*)gff_calloc(len + 1, sizeof(wchar_t));

	if (!wchar->str) {
		luaL_error(L, "out of memory");
		return 0;
	}

#ifdef _WIN32
	wchar->len = MultiByteToWideChar(CP_UTF8, 0, data, (int)len, wchar->str, (int)len);
#else
	wchar->len = utf8_to_wchar(data, len, wchar->str, len);
#endif

	return 1;
}

int GetCodepoints(lua_State* L) {

	LuaWChar* wchar = lua_towchar(L, 1);

	lua_newtable(L);
	int count = 0;

	for (size_t i = 0; i < wchar->len; i++) {
		int codepoint = wchar->str[i];

		if ((codepoint & 0xFC00) == 0xD800 && (i + 1 < wchar->len) && (wchar->str[i + 1] & 0xFC00) == 0xDC00) {
			codepoint = (((codepoint & 0x03FF) << 10) | (wchar->str[i + 1] & 0x03FF)) + 0x10000;
			i++;
		}

		lua_pushinteger(L, codepoint);
		lua_rawseti(L, -2, ++count);
	}

	return 1;
}

int GetCharacterAt(lua_State* L) {

	LuaWChar* wchar = lua_towchar(L, 1);
	size_t nth = luaL_optinteger(L, 2, 1) - 1;

	size_t count = 0;
	size_t strLen = wchar->len;

	for (size_t i = 0; i < strLen; i++) {
		int codepoint = wchar->str[i];

		if ((codepoint & 0xFC00) == 0xD800 && (i + 1 < strLen) && (wchar->str[i + 1] & 0xFC00) == 0xDC00) {
			codepoint = (((codepoint & 0x03FF) << 10) | (wchar->str[i + 1] & 0x03FF)) + 0x10000;
			i++;
		}

		if (count == nth) {
			lua_pushinteger(L, codepoint);
			return 1;
		}

		count++;
	}

	lua_pushnil(L);
	return 1;
}

int FromAnsi(lua_State* L) {

	if (lua_gettop(L) < 1) {
		return 0;
	}
	else if (lua_isnone(L, -1)) {
		lua_pushnil(L);
		return 1;
	}
	else if (lua_iswchar(L, -1)) {
		return 1;
	}

	size_t len;
	const char* data = luaL_tolstring(L, -1, &len);
	lua_pop(L, 1);

	LuaWChar* wchar = lua_pushwchar(L);

	wchar->str = (wchar_t*)gff_calloc(len + 1, sizeof(wchar_t));

	if (!wchar->str) {
		luaL_error(L, "out of memory");
		return 0;
	}

	wchar->len = mbstowcs(wchar->str, data, len);

	if (wchar->len == (size_t)-1) {
		wchar->len = 0;
		wchar->str[0] = L'\0';
	}

	return 1;
}

size_t to_narrow(const wchar_t* src, char* dest, size_t dest_len) {

	size_t i;
	wchar_t code;

	i = 0;

	for (; i < dest_len; i++) {
		code = src[i];
		if (code < 128)
			dest[i] = char(code);
		else {
			dest[i] = '?';
			if (code >= 0xD800 && code <= 0xD8FF)
				// lead surrogate, skip the next code unit, which is the trail
				i++;
		}
	}

	return i;
}

char16_t* wchar_alloc_as_char16(const wchar_t* src, size_t len, size_t* outChar16Len) {
#ifdef _WIN32
	// On Windows wchar_t is 2 bytes (UTF-16), same layout as char16_t; direct memcpy is valid.
	char16_t* dst = (char16_t*)gff_malloc((len + 1) * sizeof(char16_t));
	if (!dst) {
		if (outChar16Len) *outChar16Len = 0;
		return NULL;
	}
	memcpy(dst, src, len * sizeof(char16_t));
	dst[len] = u'\0';
	if (outChar16Len) *outChar16Len = len;
	return dst;
#else
	// On Linux wchar_t is 4 bytes (UTF-32); encode each code point as UTF-16.
	// Worst case: every code point is supplementary, producing 2 char16_t per wchar_t.
	char16_t* dst = (char16_t*)gff_malloc((len * 2 + 1) * sizeof(char16_t));
	if (!dst) {
		if (outChar16Len) *outChar16Len = 0;
		return NULL;
	}
	size_t out = 0;
	for (size_t i = 0; i < len; i++) {
		unsigned int cp = (unsigned int)src[i];
		if (cp <= 0xFFFF) {
			dst[out++] = (char16_t)cp;
		}
		else if (cp <= 0x10FFFF) {
			cp -= 0x10000;
			dst[out++] = (char16_t)(0xD800 + (cp >> 10));
			dst[out++] = (char16_t)(0xDC00 + (cp & 0x3FF));
		}
	}
	dst[out] = u'\0';
	if (outChar16Len) *outChar16Len = out;
	return dst;
#endif
}

wchar_t* char16_alloc_as_wchar(const char16_t* src, size_t charCount, size_t* outWcharLen) {
	wchar_t* dst = (wchar_t*)gff_malloc((charCount + 1) * sizeof(wchar_t));
	if (!dst) {
		if (outWcharLen) *outWcharLen = 0;
		return NULL;
	}
#ifdef _WIN32
	// On Windows wchar_t is 2 bytes (UTF-16), same layout as char16_t; direct memcpy is valid.
	memcpy(dst, src, charCount * sizeof(wchar_t));
	dst[charCount] = L'\0';
	if (outWcharLen) *outWcharLen = charCount;
#else
	// On Linux wchar_t is 4 bytes (UTF-32); decode UTF-16 surrogate pairs.
	size_t out = 0;
	for (size_t i = 0; i < charCount; i++) {
		unsigned int unit = (unsigned int)src[i];
		if (unit >= 0xD800 && unit <= 0xDBFF && i + 1 < charCount) {
			unsigned int trail = (unsigned int)src[i + 1];
			if (trail >= 0xDC00 && trail <= 0xDFFF) {
				dst[out++] = (wchar_t)(((unit - 0xD800) << 10) + (trail - 0xDC00) + 0x10000);
				i++;
				continue;
			}
		}
		dst[out++] = (wchar_t)unit;
	}
	dst[out] = L'\0';
	if (outWcharLen) *outWcharLen = out;
#endif
	return dst;
}

int ToUtf8(lua_State* L) {

	LuaWChar* wchar = lua_towchar(L, -1);

	if (!wchar->str) {

		lua_pushstring(L, "");
		return 1;
	}

	size_t bufferlen = wchar->len * 4;
	unsigned char* utf8String = (unsigned char*)gff_calloc(bufferlen + 1, sizeof(unsigned char));

	if (!utf8String) {
		luaL_error(L, "out of memory");
		return 0;
	}

	#ifdef _WIN32
		int convertedSize = WideCharToMultiByte(CP_UTF8, 0, wchar->str, (int)wchar->len, (char*)utf8String, (int)bufferlen, NULL, NULL);
	#else
		int convertedSize = (int)wchar_to_utf8(wchar->str, wchar->len, (char*)utf8String, bufferlen);
	#endif

	lua_pushlstring(L, (const char*)utf8String, convertedSize);
	gff_free(utf8String);

	return 1;
}

int ToAnsi(lua_State* L) {

	LuaWChar* wchar = lua_towchar(L, -1);

	if (!wchar->str) {

		lua_pushstring(L, "");
		return 1;
	}

	char* real = (char*)gff_calloc(wchar->len + 1, sizeof(char));

	if (!real) {
		luaL_error(L, "out of memory");
		return 0;
	}

	size_t len = to_narrow(wchar->str, real, wchar->len);

	lua_pushlstring(L, (const char*)real, len * sizeof(char));

	gff_free(real);

	return 1;
}

int WcharFind(lua_State* L) {

	LuaWChar* wchar = lua_towchar(L, 1);
	LuaWChar* substr = (LuaWChar*)luaL_testudata(L, 2, LUAWCHAR);
	int offset = (int)(std::max)(luaL_optinteger(L, 3, 1), (lua_Integer)0) - 1;
	wchar_t* find;

	if (!substr) {
		lua_pushvalue(L, 2);
		FromUtf8(L);
		substr = lua_towchar(L, -1);
		lua_pop(L, 2);
	}

	if (!substr || !substr->str || substr->len == 0) {
		lua_pushnil(L);
	}
	else {
		for (size_t i = offset; i < wchar->len; i++)
		{
			if (wchar->str[i] == substr->str[0]) {
				find = wcsstr(&wchar->str[i], substr->str);
				if (find) {
					lua_pushinteger(L, i + 1);
					return 1;
				}
			}
		}
	}

	lua_pushnil(L);

	return 1;
}

LuaWChar* lua_stringtowchar(lua_State* L, int index) {

	LuaWChar* wchar;

	if (lua_type(L, index) == LUA_TUSERDATA) {
		wchar = (LuaWChar*)luaL_checkudata(L, index, LUAWCHAR);
		if (wchar) {
			return wchar;
		}
	}

	lua_pushvalue(L, index);
	FromUtf8(L);

	wchar = lua_towchar(L, -1);
	lua_pop(L, 2);

	return wchar;
}

int lua_iswchar(lua_State* L, int index) {

	if (lua_type(L, index) != LUA_TUSERDATA) {
		return 0;
	}
	else if (!lua_getmetatable(L, index)) {
		return 0;
	}

	luaL_getmetatable(L, LUAWCHAR);
	int result = lua_rawequal(L, -1, -2);
	lua_pop(L, 2);

	return result;
}

LuaWChar* lua_pushwchar(lua_State* L) {
	LuaWChar* wchar = (LuaWChar*)lua_newuserdata(L, sizeof(LuaWChar));
	if (wchar == NULL) {
		luaL_error(L, "Unable to push wchar");
		return NULL;
	}
	luaL_getmetatable(L, LUAWCHAR);
	lua_setmetatable(L, -2);
	memset(wchar, 0, sizeof(LuaWChar));

	return wchar;
}

LuaWChar* lua_towchar(lua_State* L, int index) {
	LuaWChar* wchar = (LuaWChar*)luaL_checkudata(L, index, LUAWCHAR);
	if (wchar == NULL) {
		luaL_error(L, "parameter is not a %s", LUAWCHAR);
		return NULL;
	}
	return wchar;
}

int wchar_gc(lua_State* L) {

	LuaWChar* wchar = lua_towchar(L, 1);

	if (wchar->str) {
		gff_free(wchar->str);
	}

	memset(wchar, 0, sizeof(LuaWChar));

	return 0;
}

int wchar_tostring(lua_State* L) {
	return ToUtf8(L);
}

int wchar_len(lua_State* L) {

	LuaWChar* a = lua_towchar(L, 1);

	lua_pushinteger(L, a->len);

	return 1;
}

int wchar_eq(lua_State* L) {

	LuaWChar* a = lua_towchar(L, 1);

	if (luaL_testudata(L, 2, LUAWCHAR)) {

		LuaWChar* b = lua_towchar(L, 2);

		if (b->len != a->len) {
			lua_pushboolean(L, false);
		}
		else if (a->len == 0) {
			lua_pushboolean(L, true);
		}
		else {
			lua_pushboolean(L, wcsncmp(a->str, b->str, a->len) == 0);
		}
	}
	else {
		lua_pushboolean(L, false);
	}

	return 1;
}

int wchar_concat(lua_State* L) {

	bool swapidx = luaL_testudata(L, 1, LUAWCHAR) == NULL;

	LuaWChar* a = lua_towchar(L, swapidx ? 2 : 1);
	LuaWChar* result;

	if (luaL_testudata(L, swapidx ? 1 : 2, LUAWCHAR)) {

		LuaWChar* b = lua_towchar(L, swapidx ? 1 : 2);

		result = lua_pushwchar(L);

		result->str = (wchar_t*)gff_calloc(a->len + b->len + 1, sizeof(wchar_t));

		if (!result->str) {
			luaL_error(L, "out of memory");
			return 0;
		}

		memcpy(result->str, a->str, a->len * sizeof(wchar_t));
		memcpy(&result->str[a->len], b->str, b->len * sizeof(wchar_t));

		result->len = a->len + b->len;
	}
	else {

		size_t len;
		const char* data = luaL_tolstring(L, swapidx ? 1 : 2, &len);
		lua_pop(L, 1);

		result = lua_pushwchar(L);

		#ifdef _WIN32
				int wcharsNeeded = (len > 0) ? MultiByteToWideChar(CP_UTF8, 0, data, (int)len, NULL, 0) : 0;
		#else
				int wcharsNeeded = (int)len;  // conservative upper bound: at most as many wchar_t as UTF-8 bytes
		#endif
		if (wcharsNeeded < 0) wcharsNeeded = 0;

		result->str = (wchar_t*)gff_calloc(a->len + (size_t)wcharsNeeded + 1, sizeof(wchar_t));

		if (!result->str) {
			luaL_error(L, "out of memory");
			return 0;
		}

		int actualLen = 0;

		if (swapidx) {
			if (wcharsNeeded > 0) {
#ifdef _WIN32
				actualLen = MultiByteToWideChar(CP_UTF8, 0, data, (int)len, result->str, wcharsNeeded);
#else
				actualLen = (int)utf8_to_wchar(data, len, result->str, wcharsNeeded);
#endif
			}
			if (a->str && a->len > 0)
				memcpy(&result->str[actualLen], a->str, a->len * sizeof(wchar_t));
		}
		else {
			if (a->str && a->len > 0)
				memcpy(result->str, a->str, a->len * sizeof(wchar_t));
			if (wcharsNeeded > 0) {
#ifdef _WIN32
				actualLen = MultiByteToWideChar(CP_UTF8, 0, data, (int)len, &result->str[a->len], wcharsNeeded);
#else
				actualLen = (int)utf8_to_wchar(data, len, &result->str[a->len], wcharsNeeded);
#endif
			}
		}
		result->len = a->len + (size_t)actualLen;
	}

	return 1;
}

// ── lua_topathutf8 ────────────────────────────────────────────────────────────
// Accepts either a plain Lua string or a LuaWChar at stack index idx and returns
// a pointer to a static 4 KiB buffer containing the UTF-8 encoded path.
// This lets every FileSystem function accept both string and Wchar inputs uniformly.

static char _topathutf8_buf[4096];

const char* lua_topathutf8(lua_State* L, int idx) {
	LuaWChar* w = (LuaWChar*)luaL_testudata(L, idx, LUAWCHAR);
	if (w && w->str && w->len > 0) {
#ifdef _WIN32
		int n = WideCharToMultiByte(CP_UTF8, 0, w->str, (int)w->len,
			_topathutf8_buf, (int)sizeof(_topathutf8_buf) - 1, NULL, NULL);
		if (n < 0)
			n = 0;
		_topathutf8_buf[n] = '\0';
#else
		size_t n = wchar_to_utf8(w->str, w->len, _topathutf8_buf, sizeof(_topathutf8_buf) - 1);
		_topathutf8_buf[n] = '\0';
#endif
		return _topathutf8_buf;
	}
	size_t len;
	const char* s = luaL_checklstring(L, idx, &len);
	if (len >= sizeof(_topathutf8_buf))
		luaL_error(L, "path too long");
	memcpy(_topathutf8_buf, s, len + 1);
	return _topathutf8_buf;
}
