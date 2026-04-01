#pragma once
#include "lua_main_incl.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>

#define LUAJSON "LUAJSON"

// Forward declaration — full definition in stream.h, used only for streaming encode.
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

    // Streaming encode: when non-NULL, jbuf_grow flushes the output buffer to
    // this stream before allocating more memory, keeping usage bounded.
    struct LuaStream* encStream;
} LuaJson;

// Returns the unique pointer address used as the JSON null sentinel.
// lua_pushlightuserdata(L, lua_json_null()) produces the Json.Null value.
void*    lua_json_null(void);

LuaJson* lua_json_push(lua_State* L);           // push a fresh GC-managed instance
LuaJson* lua_json_check(lua_State* L, int idx); // check + return instance at idx

int lua_json_gc(lua_State* L);
int lua_json_tostring(lua_State* L);
int lua_json_new(lua_State* L);                 // Json.New([pretty])

// Both functions handle the static and instance calling conventions:
//   Json.Decode(str)             /  json:Decode(str)
//   Json.Decode(fn)              /  json:Decode(fn)   -- fn() returns chunks
//   Json.Encode(value [,pretty]) /  json:Encode(value)
// For the chunked form fn is called repeatedly with no arguments.
// It must return a non-empty string for each chunk; returning nil or an empty
// string signals end of input.
int lua_json_decode(lua_State* L);
int lua_json_encode(lua_State* L);
int lua_json_encode_into_stream(lua_State* L);
int lua_json_decode_into_stream(lua_State* L);
