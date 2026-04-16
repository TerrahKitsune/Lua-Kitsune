#pragma once
#include "stream.h"
#include "KitsuneEngine.h"

// -- Inbound: host ? Lua (only via KitsuneCreateMemoryBlock) ------------------
// Push a Lua inbound stream wrapping 'block'.  Called by PushKitsuneVariable;
// the block MUST have KITSUNE_SHARED_MEMORY_FLAG_KITSUNE_OWNED set.
// Sets KITSUNE_SHARED_MEMORY_FLAG_LUA_REFERENCED on the block.
// Caps are derived from KITSUNE_SHARED_MEMORY_FLAG_READONLY in block->flags.
LuaStream* lua_push_sharedmemory_stream(lua_State* L, KitsuneSharedMemoryBlock* block);

// -- Outbound: Lua ? host ------------------------------------------------------
// Allocates a new KitsuneSharedMemoryBlock of 'size' bytes, adds it to the global block
// registry, and wraps it in a Lua stream userdata.
// Sets ACCESSOR_DISPOSED=1 and LUA_REFERENCED=1 on the new block.
// Caps: STREAM_CAP_READ | STREAM_CAP_WRITE | STREAM_CAP_SEEK.
// Calls luaL_error on out-of-memory.
LuaStream* lua_push_sharedmemory_stream_outbound(lua_State* L, size_t size);

// Returns true only when 'stream' is backed by the outbound shared-memory vtable
// (i.e. created by lua_push_sharedmemory_stream_outbound / Stream.OpenSharedMemory).
bool lua_is_outbound_sharedmemory_stream(const LuaStream* stream);

// Returns the KitsuneSharedMemoryBlock* for an outbound stream.
KitsuneSharedMemoryBlock* lua_get_outbound_sharedmemory_block(const LuaStream* stream);

// Creates and pushes an outbound stream pre-filled with a copy of [data, data+size).
// Does NOT call luaL_error on failure — returns NULL instead (stack unchanged).
LuaStream* lua_try_push_sharedmemory_stream_outbound_copy(lua_State* L, const void* data, size_t size);

// Reads the full contents of a readable+seekable stream and pushes them as a
// new outbound shared-memory stream.  Seeks src to 0 before reading.
// Does NOT call luaL_error on failure — returns NULL instead (stack unchanged).
LuaStream* lua_try_push_sharedmemory_stream_outbound_from_stream(lua_State* L, LuaStream* src);

// Adds a block to the global registry (thread-safe).  Called by KitsuneCreateMemoryBlock
// and the outbound/copy constructors.
void lua_shmem_list_add(KitsuneSharedMemoryBlock* block);

// Sweeps the global block registry and frees any block where both
// KITSUNE_SHARED_MEMORY_FLAG_OWNER_DISPOSED and KITSUNE_SHARED_MEMORY_FLAG_ACCESSOR_DISPOSED
// are set.  MUST be called only from the scheduler thread (or during KitsuneCleanup after
// lua_close has been called and the scheduler has stopped).
void lua_shmem_sweep_disposed_blocks();
