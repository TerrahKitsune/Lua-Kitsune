#pragma once
#include "lua_main_incl.h"
#include <Windows.h>
#include "luawchar.h"

#define LUACSV "LUACSV"  // metatable name for CSV instance userdata

// Internal parser state; doubles as the Lua-visible instance userdata.
// When used as a LUACSV instance, only 'delimiter' is persistent across calls;
// all other fields are re-initialised at the start of each operation.
// delimiter == L'\0' means auto-detect on every call.
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

// ── Instance entry points ─────────────────────────────────────────────────────
// All operations require a LuaCsvInst userdata at arg 1.

// CSV.New([delim]) / CSV.Create([delim])  →  LuaCsvInst userdata.
//   Omitting delimiter (or passing nil / "auto") enables auto-detection per call.
//   When called as csv:New([delim]), the existing instance at arg 1 is ignored.
int lua_csv_new(lua_State* L);

// csv:Decode(str_or_wchar)
//   Returns {Comments={...}, Rows={{field,...},...}}; fields are Wchar objects.
int lua_csv_decode(lua_State* L);

// csv:Encode(rows)
//   rows: array-of-arrays; each field is converted via tostring.
//   Returns a UTF-8 CSV string.
int lua_csv_encode(lua_State* L);

// csv:DecodeFromFunction(fn_or_stream)
//   fn:     called with no arguments; returns a string/Wchar chunk, or nil/false/"" to stop.
//   stream: read in 4 KiB chunks; the iterator keeps the stream alive until GC.
//   Each iteration of the returned iterator yields one row as a Wchar-field table.
int lua_csv_decode_from_function(lua_State* L);

// Metamethods registered on the LUACSV metatable.
int lua_csv_gc(lua_State* L);
int lua_csv_tostring(lua_State* L);
