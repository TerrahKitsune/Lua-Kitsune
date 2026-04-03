#include "streamshmemory.h"
#include <mutex>
#include <string.h>

struct InSharedMemoryStream {
	SharedMemoryBlock* block;
	size_t             pos;
};

// ── vtable implementations ────────────────────────────────────────────────────

static const BYTE* shmem_read(void* native, lua_State* L, size_t len, size_t* outLen) {
	InSharedMemoryStream* s = (InSharedMemoryStream*)native;
	if (s->pos >= s->block->size) {
		lua_pushboolean(L, false);
		if (outLen)
			*outLen = 0;
		return NULL;
	}
	// READONLY blocks advertise that the locked flag can be ignored by accessors.
	bool readonly = (s->block->flags & KITSUNE_SHARED_MEMORY_FLAG_READONLY) != 0;
	if (!readonly)
		s->block->flags |= KITSUNE_SHARED_MEMORY_FLAG_LOCKED;
	size_t avail  = s->block->size - s->pos;
	size_t toRead = (len == 0 || len > avail) ? avail : len;
	// lua_pushlstring copies the bytes into the Lua string, so the block can be
	// unlocked immediately after — the returned pointer into the Lua string is
	// stable for the lifetime of the string on the stack.
	lua_pushlstring(L, (const char*)(s->block->data + s->pos), toRead);
	s->pos += toRead;
	if (!readonly)
		s->block->flags &= ~KITSUNE_SHARED_MEMORY_FLAG_LOCKED;
	if (outLen)
		*outLen = toRead;
	return (const BYTE*)lua_tolstring(L, -1, NULL);
}

static bool shmem_write(void* native, const BYTE* data, size_t len) {
	InSharedMemoryStream* s = (InSharedMemoryStream*)native;
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
	if ((size_t)pos > s->block->size)
		pos = (lua_Integer)s->block->size;
	s->pos = (size_t)pos;
	return true;
}

static lua_Integer shmem_curpos(void* native) {
	return (lua_Integer)((InSharedMemoryStream*)native)->pos;
}

static lua_Integer shmem_getlen(void* native) {
	return (lua_Integer)((InSharedMemoryStream*)native)->block->size;
}

static void shmem_close(void* native) {
	InSharedMemoryStream* s = (InSharedMemoryStream*)native;
	if (s->block && s->block->close)
		s->block->close(s->block);
	gff_free(s);
}

static int shmem_info(void* native, lua_State* L) {
	InSharedMemoryStream* s = (InSharedMemoryStream*)native;
	lua_createtable(L, 0, 4);
	lua_pushinteger(L, (lua_Integer)s->pos);
	lua_setfield(L, -2, "pos");
	lua_pushinteger(L, (lua_Integer)s->block->size);
	lua_setfield(L, -2, "size");
	lua_pushboolean(L, (s->block->flags & KITSUNE_SHARED_MEMORY_FLAG_READONLY) ? 1 : 0);
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

// ── Inbound public constructor ────────────────────────────────────────────────

LuaStream* lua_push_sharedmemory_stream(lua_State* L, SharedMemoryBlock* block) {
	LuaStream* stream = (LuaStream*)lua_newuserdata(L, sizeof(LuaStream));
	luaL_getmetatable(L, STREAM);
	lua_setmetatable(L, -2);
	memset(stream, 0, sizeof(LuaStream));
	stream->backendRef = LUA_NOREF;

	InSharedMemoryStream* s = (InSharedMemoryStream*)gff_malloc(sizeof(InSharedMemoryStream));
	if (!s) {
		luaL_error(L, "Out of memory");
		return NULL;
	}
	s->block = block;
	s->pos   = 0;

	stream->vtbl   = &g_shmem_vtbl;
	stream->native = s;
	stream->Caps   = (block->flags & KITSUNE_SHARED_MEMORY_FLAG_READONLY)
		? (STREAM_CAP_READ | STREAM_CAP_SEEK)
		: (STREAM_CAP_READ | STREAM_CAP_WRITE | STREAM_CAP_SEEK);
	return stream;
}

// ── Pending registry-unref queue (outbound close path) ───────────────────────
// block->close is called from an arbitrary host thread and must not touch the
// Lua state directly.  Instead it pushes a registry ref onto this lock-free
// LIFO list; the scheduler thread drains it at the top of every cycle via
// lua_shmem_drain_pending_unrefs, which holds exclusive Lua access at that point.

struct PendingUnref {
	int           ref;
	PendingUnref* next;
};

static std::mutex    g_unref_lock;
static PendingUnref* g_unref_head = NULL;

static void push_pending_unref(int ref) {
	PendingUnref* node = (PendingUnref*)gff_malloc(sizeof(PendingUnref));
	if (!node)
		return;  // OOM: registry ref leaks until lua_close, acceptable under memory pressure
	node->ref = ref;
	g_unref_lock.lock();
	node->next   = g_unref_head;
	g_unref_head = node;
	g_unref_lock.unlock();
}

void lua_shmem_drain_pending_unrefs(lua_State* L) {
	g_unref_lock.lock();
	PendingUnref* list = g_unref_head;
	g_unref_head = NULL;
	g_unref_lock.unlock();

	while (list) {
		PendingUnref* next = list->next;
		luaL_unref(L, LUA_REGISTRYINDEX, list->ref);
		gff_free(list);
		list = next;
	}
}

// ── Outbound block->close callback ───────────────────────────────────────────
// Called by the host on any thread when it is done with the block.
// Enqueues the registry ref for deferred release on the Lua scheduler thread.
// Does NOT free the block — shmem_out_close (called by GC) owns that.

struct OutboundCloseCtx {
	int                   ref;  // Lua registry ref anchoring the stream userdata
	InSharedMemoryStream* s;   // back-pointer so the block can be freed immediately
};

static void on_block_close(SharedMemoryBlock* block) {
	OutboundCloseCtx* ctx = (OutboundCloseCtx*)block->userdata;
	int ref = ctx->ref;
	InSharedMemoryStream* s = ctx->s;
	gff_free(ctx);
	block->userdata = NULL;
	block->close    = NULL;  // prevent accidental re-entry
	// Free the data block immediately — the host is done with it.
	// Null the pointer so shmem_out_close does not double-free when GC fires.
	s->block = NULL;
	gff_free(block);
	push_pending_unref(ref);
}

// ── Outbound vtable ───────────────────────────────────────────────────────────
// Shares read/write/seek/pos/len/info with the inbound vtable.
// The close implementation differs: the block is Lua-owned so GC frees it
// here instead of delegating to block->close (already called by the host).

static void shmem_out_close(void* native) {
	InSharedMemoryStream* s = (InSharedMemoryStream*)native;
	if (s->block) {
		// Stream was GC'd before the host received it — on_block_close was never called.
		// Free the OutboundCloseCtx stored in block->userdata, then free the block itself.
		gff_free(s->block->userdata);
		gff_free(s->block);
	}
	gff_free(s);
}

static int shmem_out_info(void* native, lua_State* L) {
	InSharedMemoryStream* s = (InSharedMemoryStream*)native;
	lua_createtable(L, 0, 4);
	lua_pushinteger(L, (lua_Integer)s->pos);
	lua_setfield(L, -2, "pos");
	lua_pushinteger(L, (lua_Integer)s->block->size);
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

// ── Outbound public constructor ───────────────────────────────────────────────

LuaStream* lua_push_sharedmemory_stream_outbound(lua_State* L, size_t size) {
	// Allocate SharedMemoryBlock header + data in a single heap block.
	SharedMemoryBlock* block = (SharedMemoryBlock*)gff_malloc(sizeof(SharedMemoryBlock) + size);
	if (!block) {
		luaL_error(L, "Out of memory");
		return NULL;
	}
	memset(block, 0, sizeof(SharedMemoryBlock) + size);
	block->size = size;

	OutboundCloseCtx* ctx = (OutboundCloseCtx*)gff_malloc(sizeof(OutboundCloseCtx));
	if (!ctx) {
		gff_free(block);
		luaL_error(L, "Out of memory");
		return NULL;
	}
	ctx->ref        = LUA_NOREF;  // filled in below after anchoring
	block->userdata = ctx;
	block->close    = on_block_close;

	// Create the LuaStream userdata (pushed by lua_newuserdata).
	LuaStream* stream = (LuaStream*)lua_newuserdata(L, sizeof(LuaStream));
	luaL_getmetatable(L, STREAM);
	lua_setmetatable(L, -2);
	memset(stream, 0, sizeof(LuaStream));
	stream->backendRef = LUA_NOREF;

	InSharedMemoryStream* s = (InSharedMemoryStream*)gff_malloc(sizeof(InSharedMemoryStream));
	if (!s) {
		gff_free(ctx);
		gff_free(block);
		// stream userdata is on stack with NULL vtbl — luastream_gc handles this safely
		luaL_error(L, "Out of memory");
		return NULL;
	}
	s->block = block;
	s->pos   = 0;

	stream->vtbl   = &g_shmem_out_vtbl;
	stream->native = s;
	stream->Caps   = STREAM_CAP_READ | STREAM_CAP_WRITE | STREAM_CAP_SEEK;

	ctx->s = s;

	return stream;
}

bool lua_is_outbound_sharedmemory_stream(const LuaStream* stream) {
	return stream && stream->vtbl == &g_shmem_out_vtbl;
}

SharedMemoryBlock* lua_get_outbound_sharedmemory_block(const LuaStream* stream) {
	return ((InSharedMemoryStream*)stream->native)->block;
}

void lua_anchor_outbound_sharedmemory_stream(lua_State* L, const LuaStream* stream, int idx) {
	InSharedMemoryStream* s = (InSharedMemoryStream*)stream->native;
	OutboundCloseCtx* ctx = (OutboundCloseCtx*)s->block->userdata;
	lua_pushvalue(L, idx);
	ctx->ref = luaL_ref(L, LUA_REGISTRYINDEX);
}

LuaStream* lua_try_push_sharedmemory_stream_outbound_copy(lua_State* L, const void* data, size_t size) {
	SharedMemoryBlock* block = (SharedMemoryBlock*)gff_malloc(sizeof(SharedMemoryBlock) + size);
	if (!block)
		return NULL;
	memset(block, 0, sizeof(SharedMemoryBlock) + size);
	block->size = size;
	if (data && size > 0)
		memcpy(block->data, data, size);

	OutboundCloseCtx* ctx = (OutboundCloseCtx*)gff_malloc(sizeof(OutboundCloseCtx));
	if (!ctx) {
		gff_free(block);
		return NULL;
	}
	ctx->ref     = LUA_NOREF;
	block->userdata = ctx;
	block->close    = on_block_close;

	LuaStream* stream = (LuaStream*)lua_newuserdata(L, sizeof(LuaStream));
	luaL_getmetatable(L, STREAM);
	lua_setmetatable(L, -2);
	memset(stream, 0, sizeof(LuaStream));
	stream->backendRef = LUA_NOREF;

	InSharedMemoryStream* s = (InSharedMemoryStream*)gff_malloc(sizeof(InSharedMemoryStream));
	if (!s) {
		gff_free(ctx);
		gff_free(block);
		lua_pop(L, 1);  // remove the partially-constructed userdata
		return NULL;
	}
	s->block = block;
	s->pos   = 0;

	stream->vtbl   = &g_shmem_out_vtbl;
	stream->native = s;
	stream->Caps   = STREAM_CAP_READ | STREAM_CAP_WRITE | STREAM_CAP_SEEK;
	ctx->s = s;

	return stream;  // userdata is on top of L; NOT yet anchored
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
