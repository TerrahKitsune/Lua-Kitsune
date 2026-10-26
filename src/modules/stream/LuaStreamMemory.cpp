#include "stream/LuaStreamMemory.h"
#include <string.h>

struct InMemoryStream {
	BYTE*  data;
	size_t len;
	size_t pos;
	size_t alloc;
};

// -- vtable implementations ----------------------------------------------------

static int InmemStreamRead(void* native, lua_State* L, size_t len) {
	InMemoryStream* m = (InMemoryStream*)native;
	if (!m->data || m->pos >= m->len) {
		lua_pushboolean(L, false);
		return 1;
	}
	size_t avail  = m->len - m->pos;
	size_t toRead = (len == 0 || len > avail) ? avail : len;
	lua_pushlstring(L, (const char*)(m->data + m->pos), toRead);
	m->pos += toRead;
	return 1;
}

static bool InmemStreamWrite(void* native, const BYTE* data, size_t len) {
	InMemoryStream* m = (InMemoryStream*)native;
	if (!data || len == 0)
		return true;
	if (m->pos + len > m->alloc) {
		size_t newAlloc = m->alloc == 0 ? MIN_STREAM_SIZE : m->alloc * 2;
		if (newAlloc < m->pos + len)
			newAlloc = m->pos + len;
		void* newData = kitsune_realloc(m->data, newAlloc);
		if (!newData)
			return false;
		m->data  = (BYTE*)newData;
		m->alloc = newAlloc;
	}
	memcpy(m->data + m->pos, data, len);
	m->pos += len;
	if (m->pos > m->len)
		m->len = m->pos;
	return true;
}

static bool InmemStreamSetPos(void* native, lua_Integer pos) {
	InMemoryStream* m = (InMemoryStream*)native;
	if (pos < 0)
		pos = 0;
	if ((size_t)pos > m->len)
		pos = (lua_Integer)m->len;
	m->pos = (size_t)pos;
	return true;
}

static lua_Integer InmemStreamCurPos(void* native) {
	return (lua_Integer)((InMemoryStream*)native)->pos;
}

static lua_Integer InmemStreamGetLen(void* native) {
	return (lua_Integer)((InMemoryStream*)native)->len;
}

static void InmemStreamClose(void* native, lua_State* L) {
	InMemoryStream* m = (InMemoryStream*)native;
	if (m->data)
		kitsune_free(m->data);
	kitsune_free(m);
}

static int InmemStreamGetInfo(void* native, lua_State* L) {
	InMemoryStream* m = (InMemoryStream*)native;
	lua_createtable(L, 0, 4);
	lua_pushinteger(L, (lua_Integer)m->pos);
	lua_setfield(L, -2, "pos");
	lua_pushinteger(L, (lua_Integer)m->len);
	lua_setfield(L, -2, "len");
	lua_pushinteger(L, (lua_Integer)m->alloc);
	lua_setfield(L, -2, "alloc");
	lua_pushstring(L, "memory");
	lua_setfield(L, -2, "type");
	return 1;
}

static uint64_t InmemStreamGetId(void* native) {
	return (uint64_t)((InMemoryStream*)native)->data;
}

static const LuaStreamVtable g_inmem_vtbl = {
	InmemStreamRead,
	InmemStreamWrite,
	InmemStreamSetPos,
	InmemStreamCurPos,
	InmemStreamGetLen,
	InmemStreamClose,
	InmemStreamGetInfo,
	NULL,
	InmemStreamGetId,
};

// -- Public constructor helper -------------------------------------------------

void LuaSetupInmemoryStream(lua_State* L, LuaStream* stream) {
	InMemoryStream* m = (InMemoryStream*)kitsune_malloc(sizeof(InMemoryStream));
	if (!m) {
		luaL_error(L, "Out of memory");
		return;
	}
	ZeroMemory(m, sizeof(InMemoryStream));
	stream->vtbl   = &g_inmem_vtbl;
	stream->native = m;
	stream->Caps   = HEAP_STREAM_CAPS;
}

bool LuaIsInmemoryStream(const LuaStream* stream) {
	return stream->vtbl == &g_inmem_vtbl;
}

unsigned char* LuaCopyInmemoryStreamData(const LuaStream* stream, size_t* outLen) {
	InMemoryStream* m = (InMemoryStream*)stream->native;
	*outLen = m->len;
	if (m->len == 0)
		return NULL;
	unsigned char* buf = (unsigned char*)kitsune_malloc(m->len);
	if (buf)
		memcpy(buf, m->data, m->len);
	return buf;  // NULL with *outLen > 0 signals OOM
}
