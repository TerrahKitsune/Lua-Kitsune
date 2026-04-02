#pragma once
#include "stream.h"

// Allocates an InMemoryStream and wires stream->vtbl, stream->native, stream->Caps.
// Calls luaL_error on out-of-memory.
void lua_setup_inmemory_stream(lua_State* L, LuaStream* stream);

// Returns true only when 'stream' is backed by the built-in in-memory vtable.
// Used to gate operations (e.g. __tostring) that should never silently consume
// a file or user-defined backend.
bool lua_is_inmemory_stream(const LuaStream* stream);
