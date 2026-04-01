#pragma once
#include "stream.h"

// Push a Lua-callable file-backed stream onto the stack.
// Opens 'filename' with 'mode' (fopen conventions: "rb", "wb", "r+b", etc.).
// Throws a Lua error on failure (file not found, permission denied, etc.).
// The returned LuaStream* is the userdata at the top of the stack.
LuaStream* lua_pushfilestream(lua_State* L, const char* filename, const char* mode);
