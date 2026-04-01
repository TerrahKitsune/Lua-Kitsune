#include "streammemory.h"
#include <string.h>

struct InMemoryStream {
	BYTE*  data;
	size_t len;
	size_t pos;
	size_t alloc;
};

static int pushinmemory_backend(lua_State* L) {
	int type = (int)lua_tointeger(L, 1);
	InMemoryStream* stream = (InMemoryStream*)lua_touserdata(L, lua_upvalueindex(1));

	if (!stream) {
		lua_pushboolean(L, false);
		return 1;
	}

	switch (type) {
	case STREAM_OP_OPEN:
		lua_pushinteger(L, HEAP_STREAM_CAPS);
		return 1;

	case STREAM_OP_CLOSE:
		if (stream->data)
			gff_free(stream->data);
		gff_free(stream);
		lua_pushboolean(L, true);
		return 1;

	case STREAM_OP_READ: {
		size_t requested = (size_t)luaL_optinteger(L, 2, 0);
		if (!stream->data || stream->pos >= stream->len) {
			lua_pushboolean(L, false);
			return 1;
		}
		size_t avail  = stream->len - stream->pos;
		size_t toRead = (requested == 0 || requested > avail) ? avail : requested;
		lua_pushlstring(L, (const char*)(stream->data + stream->pos), toRead);
		stream->pos += toRead;
		return 1;
	}

	case STREAM_OP_WRITE: {
		size_t len;
		const char* data = lua_tolstring(L, 2, &len);
		if (!data || len == 0) {
			lua_pushboolean(L, true);
			return 1;
		}
		if (stream->pos + len > stream->alloc) {
			size_t newAlloc = stream->alloc == 0 ? MIN_STREAM_SIZE : stream->alloc * 2;
			if (newAlloc < stream->pos + len)
				newAlloc = stream->pos + len;
			void* newData = gff_realloc(stream->data, newAlloc);
			if (!newData) {
				lua_pushboolean(L, false);
				return 1;
			}
			stream->data  = (BYTE*)newData;
			stream->alloc = newAlloc;
		}
		memcpy(stream->data + stream->pos, data, len);
		stream->pos += len;
		if (stream->pos > stream->len)
			stream->len = stream->pos;
		lua_pushboolean(L, true);
		return 1;
	}

	case STREAM_OP_SETPOS: {
		lua_Integer pos = luaL_checkinteger(L, 2);
		if (pos < 0)
			pos = 0;
		if ((size_t)pos > stream->len)
			pos = (lua_Integer)stream->len;
		stream->pos = (size_t)pos;
		lua_pushboolean(L, true);
		return 1;
	}

	case STREAM_OP_INFO: {
		lua_createtable(L, 0, 3);
		lua_pushinteger(L, (lua_Integer)stream->pos);
		lua_setfield(L, -2, "pos");
		lua_pushinteger(L, (lua_Integer)stream->len);
		lua_setfield(L, -2, "len");
		lua_pushinteger(L, (lua_Integer)stream->alloc);
		lua_setfield(L, -2, "alloc");
		lua_pushstring(L, "memory");
		lua_setfield(L, -2, "type");
		return 1;
	}

	case STREAM_OP_CURPOS:
		lua_pushinteger(L, (lua_Integer)stream->pos);
		return 1;

	case STREAM_OP_LEN:
		lua_pushinteger(L, (lua_Integer)stream->len);
		return 1;

	default:
		lua_pushboolean(L, false);
		return 1;
	}
}

void lua_pushinmemory_backend(lua_State* L) {
	InMemoryStream* stream = (InMemoryStream*)gff_malloc(sizeof(InMemoryStream));
	if (!stream) {
		luaL_error(L, "Out of memory");
		return;
	}
	ZeroMemory(stream, sizeof(InMemoryStream));
	lua_pushlightuserdata(L, stream);
	lua_pushcclosure(L, pushinmemory_backend, 1);
}
