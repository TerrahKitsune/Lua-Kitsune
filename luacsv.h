#pragma once
#include "lua_main_incl.h"
#include "platform.h"
#include "luatext.h"

#define LUACSV "LUACSV"  // metatable name for CSV instance userdata

// Internal parser state; doubles as the Lua-visible instance userdata.
// When used as a LUACSV instance, only 'delimiter' is persistent across calls;
// all other fields are re-initialised at the start of each operation.
// delimiter == L'\0' means auto-detect on every call.
typedef struct LuaCsv {
    // ── String-mode source (data != NULL) ────────────────────────────────────
    int            pos;
    const wchar_t* data;     // decoded input (owned by a userdata on the Lua stack)
    size_t         dataLen;  // wchar_t count in data
    wchar_t        last;
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
    KitsuneUtf8Carry utf8Carry; // UTF-8 sequence split across the previous chunk boundary
    bool       bomChecked;     // first decoded character has been checked for a BOM
    // ── Stream-object source (streamRef != LUA_NOREF) ────────────────────────
    // A LuaStream* is called via lua_callk rather than lua_call_nohook so that
    // async streams can yield cooperatively.  Sync streams simply return
    // immediately without yielding — the lua_callk machinery is transparent.
    // Delimiter sniffing is also deferred to the first newline seen in the
    // buffered data, so auto-detect works correctly for both stream types.
    int        streamRef;      // LUA_REGISTRYINDEX ref to the LuaStream userdata
} LuaCsv;

// ── Entry points ──────────────────────────────────────────────────────────────
// Decode / Encode / DecodeFromFunction accept either a LUACSV instance at arg 1
// (csv:Xxx(...), uses the instance delimiter) or no instance (CSV.Xxx(...),
// optional delimiter as the last argument, default ',').

// CSV.New([delim]) / CSV.Create([delim])  →  LuaCsvInst userdata.
//   Omitting delimiter (or passing nil / "auto") enables auto-detection per call.
//   When called as csv:New([delim]), the existing instance at arg 1 is ignored.
int lua_csv_new(lua_State* L);

// csv:Decode(str) / CSV.Decode(str [, delim])
//   Returns {Comments={...}, Rows={{field,...},...}}; fields are UTF-8 strings.
int lua_csv_decode(lua_State* L);

// csv:Encode(rows) / CSV.Encode(rows [, delim])
//   rows: array-of-arrays; each field is converted via tostring.
//   Returns a UTF-8 CSV string.
int lua_csv_encode(lua_State* L);

// csv:DecodeFromFunction(fn_or_stream) / CSV.DecodeFromFunction(fn_or_stream [, delim])
//   fn:     called with no arguments; returns a string chunk, or nil/false/"" to stop.
//   stream: read with stream:Read() (whatever is available per call); the iterator keeps
//           the stream alive until GC. A UTF-8 character split across reads is reassembled.
//   Each iteration of the returned iterator yields one row as a table of UTF-8 strings.
int lua_csv_decode_from_function(lua_State* L);

// Metamethods registered on the LUACSV metatable.
int lua_csv_gc(lua_State* L);
int lua_csv_tostring(lua_State* L);
