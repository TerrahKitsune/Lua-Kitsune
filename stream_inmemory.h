#pragma once
#include "stream.h"

static const size_t MIN_STREAM_SIZE = 1024;
static const BYTE HEAP_STREAM_CAPS = STREAM_CAP_READ | STREAM_CAP_WRITE | STREAM_CAP_SEEK | STREAM_CAP_PEEK;

bool        StreamGrow(lua_State* L, LuaStream* stream, size_t neededAlloc);
bool        StreamWrite(lua_State* L, LuaStream* stream, const BYTE* data, size_t len);
const BYTE* StreamRead(LuaStream* stream, size_t len);
const BYTE* StreamPeek(LuaStream* stream, size_t len);
void        set_heap_backend(lua_State* L, LuaStream* stream);
