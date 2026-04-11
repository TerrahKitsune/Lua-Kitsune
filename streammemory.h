#pragma once
#include "stream.h"

// Allocates an InMemoryStream and wires stream->vtbl, stream->native, stream->Caps.
// Calls luaL_error on out-of-memory.
void lua_setup_inmemory_stream(lua_State* L, LuaStream* stream);

// Returns true only when 'stream' is backed by the built-in in-memory vtable.
// Used to gate operations (e.g. __tostring) that should never silently consume
// a file or user-defined backend.
bool lua_is_inmemory_stream(const LuaStream* stream);

// Returns a heap-allocated copy of the in-memory stream's used bytes [0..len).
// *outLen is set to the byte count (m->len, not the allocated capacity).
// Returns NULL with *outLen == 0 for an empty stream (not an error).
// Returns NULL with *outLen  > 0 on allocation failure (OOM).
// Only valid when lua_is_inmemory_stream returns true. Free the result with kitsune_free.
unsigned char* lua_copy_inmemory_stream_data(const LuaStream* stream, size_t* outLen);
