#pragma once
#include "lua_main_incl.h"
#include "platform.h"
static const char* LUAWCHAR = "WCHAR";

typedef struct LuaWChar {

	size_t len;
	wchar_t* str;

} LuaWChar;

LuaWChar* lua_pushwchar(lua_State* L, const wchar_t* str);
LuaWChar* lua_pushwchar(lua_State* L, const wchar_t* str, size_t len);
LuaWChar* lua_towchar(lua_State* L, int index);
LuaWChar* lua_pushwchar(lua_State* L);
LuaWChar* lua_stringtowchar(lua_State* L, int index);
int lua_iswchar(lua_State* L, int index);

int GetCodepoints(lua_State* L);
int GetCharacterAt(lua_State* L);
int ToUtf8(lua_State* L);
int FromUtf8(lua_State* L);
int SetLocale(lua_State* L);
int FromAnsi(lua_State* L);
int ToAnsi(lua_State* L);
int FromSubstring(lua_State* L);
int FromToLower(lua_State* L);
int FromToUpper(lua_State* L);
int WcharFind(lua_State* L);
int ToBytes(lua_State* L);
int FromBytes(lua_State* L);

int wchar_len(lua_State* L);
int wchar_eq(lua_State* L);
int wchar_concat(lua_State* L);
int wchar_gc(lua_State* L);
int wchar_tostring(lua_State* L);

// ── Platform-abstraction helpers for char16_t ↔ wchar_t ─────────────────────
// On Windows (sizeof(wchar_t) == 2 / UTF-16): zero-cost memcpy, no conversion.
// On Linux  (sizeof(wchar_t) == 4 / UTF-32): real UTF-16↔UTF-32 conversion.
// Returned pointers must be freed with gff_free.

// Allocates a char16_t* copy of src (len wchar_t code units, excl. null terminator).
// Sets *outChar16Len to the number of char16_t code units written.
// Returns NULL on OOM.
char16_t* wchar_alloc_as_char16(const wchar_t* src, size_t len, size_t* outChar16Len);

// Allocates a wchar_t* from char16_t stream data (charCount code units, excl. null terminator).
// Sets *outWcharLen to the number of wchar_t code units written.
// Returns NULL on OOM.
wchar_t* char16_alloc_as_wchar(const char16_t* src, size_t charCount, size_t* outWcharLen);