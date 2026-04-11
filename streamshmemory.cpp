#include "streamshmemory.h"
#include <atomic>
#include <mutex>
#include <string.h>

// Atomically OR a flag mask into a flags byte, preventing a concurrent reader
// from silently clearing bits we set (and vice versa).
static inline void flags_atomic_or(uint8_t* flags, uint8_t mask) {
	// Cast to std::atomic<uint8_t>*: safe because sizeof/alignof are the same
	// and the byte is always naturally aligned.  The reinterpret_cast is the
	// standard-blessed way to add atomic ops to a plain byte field.
	reinterpret_cast<std::atomic<uint8_t>*>(flags)->fetch_or(mask, std::memory_order_relaxed);
}

struct InSharedMemoryStream {
	SharedMemoryBlock* block;
	size_t             pos;
};

// -- vtable implementations ----------------------------------------------------

static int shmem_read(void* native, lua_State* L, size_t len) {
	InSharedMemoryStream* s = (InSharedMemoryStream*)native;
	if (!s->block || s->pos >= s->block->size) {
		lua_pushboolean(L, false);
		return 1;
	}
	bool readonly = (s->block->flags & KITSUNE_SHARED_MEMORY_FLAG_READONLY) != 0;
	if (!readonly)
		s->block->flags |= KITSUNE_SHARED_MEMORY_FLAG_LOCKED;
	size_t avail  = s->block->size - s->pos;
	size_t toRead = (len == 0 || len > avail) ? avail : len;
	lua_pushlstring(L, (const char*)(s->block->data + s->pos), toRead);
	s->pos += toRead;
	if (!readonly)
		s->block->flags &= ~KITSUNE_SHARED_MEMORY_FLAG_LOCKED;
	return 1;
}

static bool shmem_write(void* native, const BYTE* data, size_t len) {
	InSharedMemoryStream* s = (InSharedMemoryStream*)native;
	if (!s->block)
		return false;
	if (s->block->flags & KITSUNE_SHARED_MEMORY_FLAG_READONLY)
		return false;
	if (!data || len == 0)
		return true;
	if (s->pos + len > s->block->size)
		return false;
	s->block->flags |= KITSUNE_SHARED_MEMORY_FLAG_LOCKED;
	memcpy(s->block->data + s->pos, data, len);
	s->pos += len;
	s->block->flags &= ~KITSUNE_SHARED_MEMORY_FLAG_LOCKED;
	return true;
}

static bool shmem_setpos(void* native, lua_Integer pos) {
	InSharedMemoryStream* s = (InSharedMemoryStream*)native;
	if (pos < 0)
		pos = 0;
	if (s->block && (size_t)pos > s->block->size)
		pos = (lua_Integer)s->block->size;
	s->pos = (size_t)pos;
	return true;
}

static lua_Integer shmem_curpos(void* native) {
	return (lua_Integer)((InSharedMemoryStream*)native)->pos;
}

static lua_Integer shmem_getlen(void* native) {
	InSharedMemoryStream* s = (InSharedMemoryStream*)native;
	return s->block ? (lua_Integer)s->block->size : 0;
}

static void shmem_close(void* native, lua_State* L) {
	InSharedMemoryStream* s = (InSharedMemoryStream*)native;
	if (s->block)
		flags_atomic_or(&s->block->flags, KITSUNE_SHARED_MEMORY_FLAG_OWNER_DISPOSED);
	kitsune_free(s);
}

static int shmem_info(void* native, lua_State* L) {
	InSharedMemoryStream* s = (InSharedMemoryStream*)native;
	lua_createtable(L, 0, 4);
	lua_pushinteger(L, (lua_Integer)s->pos);
	lua_setfield(L, -2, "pos");
	lua_pushinteger(L, s->block ? (lua_Integer)s->block->size : 0);
	lua_setfield(L, -2, "size");
	lua_pushboolean(L, (s->block && (s->block->flags & KITSUNE_SHARED_MEMORY_FLAG_READONLY)) ? 1 : 0);
	lua_setfield(L, -2, "readonly");
	lua_pushstring(L, "sharedmemory");
	lua_setfield(L, -2, "type");
	return 1;
}

static const LuaStreamVtable g_shmem_vtbl = {
	shmem_read,
	shmem_write,
	shmem_setpos,
	shmem_curpos,
	shmem_getlen,
	shmem_close,
	shmem_info,
};

// -- Inbound public constructor ------------------------------------------------

LuaStream* lua_push_sharedmemory_stream(lua_State* L, SharedMemoryBlock* block) {
	LuaStream* stream = (LuaStream*)lua_newuserdata(L, sizeof(LuaStream));
	luaL_getmetatable(L, STREAM);
	lua_setmetatable(L, -2);
	memset(stream, 0, sizeof(LuaStream));
	stream->backendRef = LUA_NOREF;

	InSharedMemoryStream* s = (InSharedMemoryStream*)kitsune_malloc(sizeof(InSharedMemoryStream));
	if (!s) {
		luaL_error(L, "Out of memory");
		return NULL;
	}
	s->block = block;
	s->pos   = 0;

	// Mark that a Lua stream now references this block.
	block->flags |= KITSUNE_SHARED_MEMORY_FLAG_LUA_REFERENCED;

	stream->vtbl   = &g_shmem_vtbl;
	stream->native = s;
	stream->Caps   = (block->flags & KITSUNE_SHARED_MEMORY_FLAG_READONLY)
		? (STREAM_CAP_READ | STREAM_CAP_SEEK)
		: (STREAM_CAP_READ | STREAM_CAP_WRITE | STREAM_CAP_SEEK);
	return stream;
}


// -- Global block registry -----------------------------------------------------
// Every SharedMemoryBlock lives in this intrusive linked list from allocation
// until both OWNER_DISPOSED and ACCESSOR_DISPOSED flags are set, at which point
// lua_shmem_sweep_disposed_blocks (called by the scheduler each cycle) frees it.

static std::mutex    g_shmem_lock;
static SharedMemoryBlock* g_shmem_head = NULL;

void lua_shmem_list_add(SharedMemoryBlock* block) {
	g_shmem_lock.lock();
	block->next  = g_shmem_head;
	g_shmem_head = block;
	g_shmem_lock.unlock();
}

void lua_shmem_sweep_disposed_blocks() {
	// Phase 1 (under lock): unlink fully-disposed blocks into a local list.
	const BYTE mask = KITSUNE_SHARED_MEMORY_FLAG_OWNER_DISPOSED
					| KITSUNE_SHARED_MEMORY_FLAG_ACCESSOR_DISPOSED;
	SharedMemoryBlock* free_list = NULL;
	g_shmem_lock.lock();
	SharedMemoryBlock** prev  = &g_shmem_head;
	SharedMemoryBlock*  block = g_shmem_head;
	while (block) {
		SharedMemoryBlock* next = block->next;
		if ((block->flags & mask) == mask) {
			*prev         = next;
			block->next   = free_list;
			free_list     = block;
		} else {
			prev = &block->next;
		}
		block = next;
	}
	g_shmem_lock.unlock();
	// Phase 2 (outside lock): free collected blocks.
	while (free_list) {
		SharedMemoryBlock* next = free_list->next;
		kitsune_free(free_list);
		free_list = next;
	}
}

// shmem_out_close: same as shmem_close — set OWNER_DISPOSED and free the native struct.
// The block itself is freed by lua_shmem_sweep_disposed_blocks once the accessor also disposes.
static void shmem_out_close(void* native, lua_State* L) {
	shmem_close(native, NULL);
}

static int shmem_out_info(void* native, lua_State* L) {
	InSharedMemoryStream* s = (InSharedMemoryStream*)native;
	lua_createtable(L, 0, 4);
	lua_pushinteger(L, (lua_Integer)s->pos);
	lua_setfield(L, -2, "pos");
	lua_pushinteger(L, s->block ? (lua_Integer)s->block->size : 0);
	lua_setfield(L, -2, "size");
	lua_pushboolean(L, 0);  // outbound streams are always read-write
	lua_setfield(L, -2, "readonly");
	lua_pushstring(L, "sharedmemory_out");
	lua_setfield(L, -2, "type");
	return 1;
}

static const LuaStreamVtable g_shmem_out_vtbl = {
	shmem_read,
	shmem_write,
	shmem_setpos,
	shmem_curpos,
	shmem_getlen,
	shmem_out_close,
	shmem_out_info,
};

// -- Outbound public constructor -----------------------------------------------

LuaStream* lua_push_sharedmemory_stream_outbound(lua_State* L, size_t size) {
	SharedMemoryBlock* block = (SharedMemoryBlock*)kitsune_malloc(sizeof(SharedMemoryBlock) + size);
	if (!block) {
		luaL_error(L, "Out of memory");
		return NULL;
	}
	memset(block, 0, sizeof(SharedMemoryBlock) + size);
	block->size  = size;
	// ACCESSOR_DISPOSED=1: no C# accessor yet. LUA_REFERENCED=1: Lua owns this stream.
	block->flags = KITSUNE_SHARED_MEMORY_FLAG_ACCESSOR_DISPOSED
				 | KITSUNE_SHARED_MEMORY_FLAG_LUA_REFERENCED;
	lua_shmem_list_add(block);

	LuaStream* stream = (LuaStream*)lua_newuserdata(L, sizeof(LuaStream));
	luaL_getmetatable(L, STREAM);
	lua_setmetatable(L, -2);
	memset(stream, 0, sizeof(LuaStream));
	stream->backendRef = LUA_NOREF;

	InSharedMemoryStream* s = (InSharedMemoryStream*)kitsune_malloc(sizeof(InSharedMemoryStream));
	if (!s) {
		flags_atomic_or(&block->flags, KITSUNE_SHARED_MEMORY_FLAG_OWNER_DISPOSED);  // mark for ticker cleanup
		luaL_error(L, "Out of memory");
		return NULL;
	}
	s->block = block;
	s->pos   = 0;

	stream->vtbl   = &g_shmem_out_vtbl;
	stream->native = s;
	stream->Caps   = STREAM_CAP_READ | STREAM_CAP_WRITE | STREAM_CAP_SEEK;
	return stream;
}

bool lua_is_outbound_sharedmemory_stream(const LuaStream* stream) {
	return stream && stream->vtbl == &g_shmem_out_vtbl;
}

SharedMemoryBlock* lua_get_outbound_sharedmemory_block(const LuaStream* stream) {
	return ((InSharedMemoryStream*)stream->native)->block;
}

LuaStream* lua_try_push_sharedmemory_stream_outbound_copy(lua_State* L, const void* data, size_t size) {
	SharedMemoryBlock* block = (SharedMemoryBlock*)kitsune_malloc(sizeof(SharedMemoryBlock) + size);
	if (!block)
		return NULL;
	memset(block, 0, sizeof(SharedMemoryBlock) + size);
	block->size  = size;
	block->flags = KITSUNE_SHARED_MEMORY_FLAG_ACCESSOR_DISPOSED
				 | KITSUNE_SHARED_MEMORY_FLAG_LUA_REFERENCED;
	if (data && size > 0)
		memcpy(block->data, data, size);
	lua_shmem_list_add(block);

	LuaStream* stream = (LuaStream*)lua_newuserdata(L, sizeof(LuaStream));
	luaL_getmetatable(L, STREAM);
	lua_setmetatable(L, -2);
	memset(stream, 0, sizeof(LuaStream));
	stream->backendRef = LUA_NOREF;

	InSharedMemoryStream* s = (InSharedMemoryStream*)kitsune_malloc(sizeof(InSharedMemoryStream));
	if (!s) {
		flags_atomic_or(&block->flags, KITSUNE_SHARED_MEMORY_FLAG_OWNER_DISPOSED);  // mark for ticker cleanup
		lua_pop(L, 1);  // remove the partially-constructed userdata
		return NULL;
	}
	s->block = block;
	s->pos   = 0;

	stream->vtbl   = &g_shmem_out_vtbl;
	stream->native = s;
	stream->Caps   = STREAM_CAP_READ | STREAM_CAP_WRITE | STREAM_CAP_SEEK;

	return stream;  // userdata is on top of L
}

LuaStream* lua_try_push_sharedmemory_stream_outbound_from_stream(lua_State* L, LuaStream* src) {
	lua_Integer len = lua_stream_getlen(L, src);
	lua_stream_setpos(L, src, 0);

	const char* rawData = NULL;
	size_t actualRead   = 0;
	bool hasReadStr     = false;

	if (len > 0) {
		lua_stream_read_chunk(L, src, (size_t)len);  // pushes string or nil
		if (lua_type(L, -1) != LUA_TSTRING) {
			lua_pop(L, 1);
			return NULL;
		}
		rawData    = lua_tolstring(L, -1, &actualRead);
		hasReadStr = true;
	}

	LuaStream* outStream = lua_try_push_sharedmemory_stream_outbound_copy(L, rawData, actualRead);
	if (!outStream) {
		if (hasReadStr)
			lua_pop(L, 1);
		return NULL;
	}

	// Stack: [..., string (optional), outbound userdata]
	if (hasReadStr)
		lua_remove(L, -2);  // drop read string, keep outbound userdata on top

	return outStream;  // userdata is on top of L; NOT yet anchored
}
