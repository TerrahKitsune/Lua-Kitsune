#include "stream_inmemory.h"
#include <string.h>
#include <stdlib.h>

int heap_backend(lua_State* L) {
    int op = (int)lua_tointeger(L, 1);
    if (op == STREAM_OP_OPEN) {
        lua_pushinteger(L, HEAP_STREAM_CAPS);
        return 1;
    }
    lua_pushboolean(L, true);
    return 1;
}

void set_heap_backend(lua_State* L, LuaStream* stream) {
    lua_getfield(L, LUA_REGISTRYINDEX, HEAP_BACKEND_KEY);
    stream->backendRef = luaL_ref(L, LUA_REGISTRYINDEX);
}

bool StreamGrow(lua_State* L, LuaStream* stream, size_t neededAlloc) {
    if (neededAlloc <= stream->alloc)
        return true;
    if (stream->alloc == 0) {
        luaL_error(L, "stream is not resizable");
        return false;
    }
    size_t newAlloc = stream->alloc * 2;
    if (newAlloc < neededAlloc)
        newAlloc = neededAlloc;
    if (newAlloc < MIN_STREAM_SIZE)
        newAlloc = MIN_STREAM_SIZE;
    void* newData = gff_realloc(stream->data, newAlloc);
    if (!newData) {
        luaL_error(L, "stream: out of memory");
        return false;
    }
    stream->data  = (BYTE*)newData;
    stream->alloc = newAlloc;
    return true;
}

bool StreamWrite(lua_State* L, LuaStream* stream, const BYTE* data, size_t len) {
    if (!stream || !stream->data || len == 0)
        return false;
    if (!(stream->caps & STREAM_CAP_WRITE))
        return false;
    if (stream->pos + len > stream->alloc && !StreamGrow(L, stream, stream->pos + len))
        return false;
    memcpy(&stream->data[stream->pos], data, len);
    if (stream->pos + len > stream->len)
        stream->len = stream->pos + len;
    stream->pos += len;
    return true;
}

const BYTE* StreamRead(LuaStream* stream, size_t len) {
    if (!stream || !stream->data || len == 0 || stream->pos + len > stream->len)
        return NULL;
    const BYTE* result = &stream->data[stream->pos];
    stream->pos += len;
    return result;
}

const BYTE* StreamPeek(LuaStream* stream, size_t len) {
    if (!stream || !stream->data || len == 0 || stream->pos + len > stream->len)
        return NULL;
    return &stream->data[stream->pos];
}
