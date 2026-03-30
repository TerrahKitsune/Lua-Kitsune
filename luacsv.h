#pragma once
#include "lua_main_incl.h"
#include <Windows.h>
#include "luawchar.h"

// Internal parser state; not exposed to Lua.
typedef struct LuaCsv {
    // ── String-mode source (data != NULL) ────────────────────────────────────
    int       pos;
    LuaWChar* data;
    wchar_t   last;
    // ── Field write buffer ───────────────────────────────────────────────────
    size_t    len;
    size_t    alloc;
    wchar_t*  buffer;
    wchar_t   delimiter;
    // ── Streaming-mode source (data == NULL, streamFuncRef != LUA_NOREF) ─────
    lua_State* streamL;        // current calling thread; updated on each iterator call
    int        streamFuncRef;  // LUA_REGISTRYINDEX ref to the chunk-supplier function
    wchar_t*   streamBuf;      // decoded chunk waiting to be parsed
    int        streamPos;      // read cursor into streamBuf
    size_t     streamLen;      // valid wchar_t count in streamBuf
    size_t     streamAlloc;    // allocated capacity of streamBuf
    bool       streamDone;     // supplier returned nil/false/empty — no more data
} LuaCsv;

// CSV.Decode(str_or_wchar [, delimiter])
//   Returns {Comments={...}, Rows={{field,...},...}}; fields are Wchar objects.
//   Pass "auto" (or boolean true) as delimiter to let the parser sniff it.
int LuaDecodeCsv(lua_State* L);

// CSV.Encode(rows [, delimiter])
//   rows: array-of-arrays; each field is converted via tostring.
//   Returns a UTF-8 CSV string.
int LuaEncodeCsv(lua_State* L);

// CSV.DecodeFromFunction(fn [, delimiter]) → iterator
//   Calls fn() repeatedly for chunks (string, Wchar, or nil/false/"" to stop).
//   Each iteration of the returned iterator yields one row as a Wchar-field table.
int LuaDecodeFromFunction(lua_State* L);

// CSV.New([delimiter]) → {Decode, Encode, DecodeFromFunction}
//   Returns a lightweight CSV object with the delimiter bound to all three methods.
//   Omitting delimiter (or passing nil) enables auto-detection on each Decode call.
int LuaCsvNew(lua_State* L);
