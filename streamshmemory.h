#pragma once
#include "stream.h"
#include "KitsuneEngine.h"

// ── Inbound: host → Lua ───────────────────────────────────────────────────────
// Push a Lua stream userdata wrapping 'block' as a shared-memory backend.
// Caps are derived from KITSUNE_SHARED_MEMORY_FLAG_READONLY in block->flags:
//   readonly  → STREAM_CAP_READ | STREAM_CAP_SEEK
//   read-write → STREAM_CAP_READ | STREAM_CAP_WRITE | STREAM_CAP_SEEK
// The stream takes ownership of the block: shmem_close will call
// block->close(block) exactly once when the Lua userdata is GC'd.
// Calls luaL_error on out-of-memory.
LuaStream* lua_push_sharedmemory_stream(lua_State* L, SharedMemoryBlock* block);

// ── Outbound: Lua → host ──────────────────────────────────────────────────────
// Allocates a new SharedMemoryBlock of 'size' bytes, wraps it in a Lua stream
// userdata, and anchors the userdata in the Lua registry so GC cannot collect
// it until the host calls block->close(block).
//
// block->close, when called by the host from any thread, enqueues a pending
// registry-unref and returns immediately without touching the Lua state.
// Call lua_shmem_drain_pending_unrefs from the Lua scheduler thread to process
// the queue and actually release the anchor (allowing GC to reclaim the stream).
// The stream's vtable close then frees the block itself.
//
// Caps: STREAM_CAP_READ | STREAM_CAP_WRITE | STREAM_CAP_SEEK.
// Calls luaL_error on out-of-memory.
LuaStream* lua_push_sharedmemory_stream_outbound(lua_State* L, size_t size);

// Returns true only when 'stream' is backed by the outbound shared-memory vtable
// (i.e. created by lua_push_sharedmemory_stream_outbound / Stream.OpenSharedMemory).
bool lua_is_outbound_sharedmemory_stream(const LuaStream* stream);

// Returns the SharedMemoryBlock* for an outbound stream. Only valid when
// lua_is_outbound_sharedmemory_stream returns true.
SharedMemoryBlock* lua_get_outbound_sharedmemory_block(const LuaStream* stream);

// Anchors 'stream' in the Lua registry so GC cannot collect it until the host
// calls block->close.  Call exactly once, from the Lua scheduler thread, at the
// moment the stream is being handed to the host (i.e. from SetSlotResult).
// If the stream is never returned to the host it is GC'd normally without
// ever touching the registry.
void lua_anchor_outbound_sharedmemory_stream(lua_State* L, const LuaStream* stream, int idx);

// Creates and pushes an outbound stream pre-filled with a copy of [data, data+size).
// Does NOT call luaL_error on failure — returns NULL instead (stack unchanged).
// On success the userdata is on top of L and is NOT yet anchored; call
// lua_anchor_outbound_sharedmemory_stream before handing it to the host.
LuaStream* lua_try_push_sharedmemory_stream_outbound_copy(lua_State* L, const void* data, size_t size);

// Reads the full contents of a readable+seekable stream and pushes them as a
// new outbound shared-memory stream.  Seeks src to 0 before reading.
// Does NOT call luaL_error on failure — returns NULL instead (stack unchanged).
// On success the userdata is on top of L and is NOT yet anchored.
LuaStream* lua_try_push_sharedmemory_stream_outbound_from_stream(lua_State* L, LuaStream* src);

// Drains all pending registry-unref operations that were enqueued by block->close
// callbacks running on host threads.  MUST be called only from the Lua scheduler
// thread (which already holds exclusive Lua access).  Call once per scheduler
// cycle and once during KitsuneCleanup before lua_close.
void lua_shmem_drain_pending_unrefs(lua_State* L);
