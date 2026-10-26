#pragma once
#include "LuaMainInclude.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

#define LUAJSON "LUAJSON"

// Full definition in LuaStream.h, used only for streaming encode.
struct LuaStream;

typedef struct LuaJson {
    int    pretty;      // 0 = compact, 1 = pretty-printed (2 spaces per level)

    // Encode output buffer (grows as needed)
    char*  out;
    size_t outLen;
    size_t outCap;

    // Anti-recursion stack (table pointer addresses while encoding)
    uintptr_t* rec;
    size_t     recLen;
    size_t     recCap;

    // Decode input (GC-rooted by the caller for the duration of the call)
    const char* src;
    size_t      srcLen;
    size_t      srcPos;

    // Single-char pushback for the decoder (LIFO, max 8 chars)
    char unget[8];
    int  ungetLen;

    // Error position tracking for decode errors
    size_t errLine;
    size_t errCol;

    // Chunked decode: when chunkFnIdx != 0 the function at that absolute Lua
    // stack index is called (0 args, 1 result) each time the buffer runs dry.
    // It must return a non-empty string for each chunk, or nil/empty to signal
    // end of input.  chunkBuf is an owned copy of the most-recent chunk.
    int        chunkFnIdx;
    lua_State* chunkL;
    char*      chunkBuf;
    size_t     chunkBufCap;

    // Streaming encode: when non-NULL, JbufGrow flushes the output buffer to
    // this stream before allocating more memory, keeping usage bounded.
    struct LuaStream* encStream;

    // When non-zero, JSON null is decoded as the Json.Null sentinel instead of Lua nil.
    // Default 0 (nil — Lua falsy). Set to 1 via SetDecodeNull(true) for round-trip safety.
    int nullAsSentinel;

    // When non-zero, an empty Lua table encodes as {} instead of [] and a decoded
    // {} is returned as the Json.EmptyObject sentinel instead of an empty table.
    // Default 0 ([] — treats empty tables as arrays). Set to 1 via SetEncodeEmptyObject(true).
    int emptyObjectAsSentinel;
} LuaJson;

// Returns the unique pointer address used as the JSON null sentinel.
// lua_pushlightuserdata(L, LuaJsonNull()) produces the Json.Null value.
void*    LuaJsonNull(void);

// Returns the unique pointer address used as the JSON empty-object sentinel.
// lua_pushlightuserdata(L, LuaJsonEmptyObject()) produces the Json.EmptyObject value.
void*    LuaJsonEmptyObject(void);

// Returns the unique pointer address used as the registry key for the shared
// bridge LuaJson instance stored by KitsuneInit.
// Use:  lua_rawgetp(L, LUA_REGISTRYINDEX, LuaJsonBridgeRegistryKey())
void*    LuaJsonBridgeRegistryKey(void);

LuaJson* lua_pushjson(lua_State* L);           // push a fresh GC-managed instance
LuaJson* luaL_checkjson(lua_State* L, int idx); // check + return instance at idx

// json:Encode(value) — encodes Lua value at position 2 to a JSON string.
// Arg 1 must be a LuaJson instance (use LuaJsonBridgeRegistryKey()).
int      Json_Encode(lua_State* L);

// json:Decode(str | fn | stream) — decodes JSON at position 2 to a Lua value.
// Arg 1 must be a LuaJson instance (use LuaJsonBridgeRegistryKey()).
int      Json_Decode(lua_State* L);

int Json_Dispose(lua_State* L);
int Json_ToString(lua_State* L);
int Json_New(lua_State* L);                      // Json.New([pretty])
int Json_SetDecodeNull(lua_State* L);          // json:SetDecodeNull(bool)
int Json_SetEncodeEmptyObject(lua_State* L);  // json:SetEncodeEmptyObject(bool)

// Both functions handle the static and instance calling conventions:
//   Json.Decode(str)             /  json:Decode(str)
//   Json.Decode(fn)              /  json:Decode(fn)    -- fn() returns chunks
//   Json.Decode(stream)          /  json:Decode(stream) -- sync or async LuaStream
//   Json.Encode(value [,pretty]) /  json:Encode(value)
// For the chunked form fn is called repeatedly with no arguments.
// It must return a non-empty string for each chunk; returning nil or an empty
// string signals end of input.
// For the stream form: sync streams are read chunk-by-chunk directly; async
// streams (vtbl->hasdata != NULL) accumulate via lua_callk (may yield), then
// parse the complete buffer — identical result, no manual accumulation in Lua.
int Json_Decode(lua_State* L);
int Json_Encode(lua_State* L);
int Json_EncodeIntoStream(lua_State* L);
int Json_DecodeFromStream(lua_State* L);
