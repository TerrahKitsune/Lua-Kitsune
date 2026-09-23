#pragma once
#include "lua_main_incl.h"
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

// All text crossing into Lua is UTF-8. These helpers convert between UTF-8 and the
// wide strings used by native APIs (wchar_t is UTF-16 on Windows, UTF-32 elsewhere).
// Invalid input never fails: malformed sequences and unpaired surrogates become U+FFFD.

// Decodes one UTF-8 character starting at s[*i] and advances *i past it. Malformed,
// truncated, overlong and surrogate encodings return U+FFFD and advance by one byte.
uint32_t kitsune_utf8_next(const char* s, size_t len, size_t* i);

// True when s[0, len) is entirely well-formed UTF-8.
bool kitsune_utf8_valid(const char* s, size_t len);

// Writes the UTF-8 encoding of code point cp to out (room for 4 bytes); returns the byte
// count. Surrogates and values above U+10FFFF should be replaced by U+FFFD beforehand.
size_t kitsune_utf8_encode(char* out, uint32_t cp);

// Pushes a wide string as a UTF-8 Lua string.
void lua_pushwideasutf8(lua_State* L, const wchar_t* str);
void lua_pushwideasutf8(lua_State* L, const wchar_t* str, size_t len);

// Decodes UTF-8 into dst, which must hold at least len + 1 wchar_t (UTF-8 never needs
// more wide units than bytes). Writes a null terminator; returns the units written
// excluding it.
size_t kitsune_utf8_to_wide(const char* src, size_t len, wchar_t* dst);

// Number of wide units kitsune_utf8_to_wide produces for src[0, len) (terminator excluded).
size_t kitsune_utf8_wide_len(const char* src, size_t len);

// Heap-allocated wide copy of a null-terminated UTF-8 string, for passing Lua strings to
// Windows W APIs. Free with kitsune_free. Returns NULL for NULL input or when out of memory.
wchar_t* kitsune_utf8_to_wide_alloc(const char* src);

// Incremental UTF-8 -> wide decoding for text that arrives in chunks (streams, supplier
// functions, network reads). A character split across two chunks is carried over to the
// next call instead of decoding as two U+FFFD. Zero-initialise before the first call.
typedef struct KitsuneUtf8Carry {
	unsigned char bytes[4];
	unsigned char len;
} KitsuneUtf8Carry;

// Decodes carry + src into dst, which must hold at least len + 4 wchar_t, and keeps an
// incomplete trailing sequence in carry. Pass src = NULL at end of input to flush the
// carry (an incomplete sequence then becomes U+FFFD). Writes a null terminator; returns
// the units written excluding it (0 when the whole chunk was carried over).
size_t kitsune_utf8_to_wide_chunk(KitsuneUtf8Carry* carry, const char* src, size_t len, wchar_t* dst);

// Pushes the UTF-16LE encoding of a UTF-8 string as a Lua byte string.
void lua_pushutf16lefromutf8(lua_State* L, const char* src, size_t len);

// Pushes UTF-16LE bytes decoded to a UTF-8 Lua string. len is in bytes; a trailing odd
// byte is ignored.
void lua_pushutf8fromutf16le(lua_State* L, const char* src, size_t len);

// Text module functions (registered by luaopen_text).
int TextToUtf16(lua_State* L);
int TextFromUtf16(lua_State* L);
int TextFromCodepage(lua_State* L);
int TextToCodepage(lua_State* L);
int TextLower(lua_State* L);
int TextUpper(lua_State* L);
