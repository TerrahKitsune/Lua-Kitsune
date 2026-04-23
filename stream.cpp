#include "stream.h"
#include "luawchar.h"
#include "luadecimal.h"
#include "luaidentifier.h"
#include "luadatetime.h"
#include "luauint.h"
#include "luatimespan.h"
#include <string.h>
#include <stdlib.h>
#include "platform.h"
#include "miniz.h"
#include "streammemory.h"
#include "streamfile.h"

// Forward declarations for the centralized dispatch helpers defined later.
static bool        StreamWrite(lua_State* L, LuaStream* s, const BYTE* data, size_t len);
static bool        StreamSetPosC(lua_State* L, LuaStream* s, lua_Integer pos);
static lua_Integer StreamCurPosC(lua_State* L, LuaStream* s);
static lua_Integer StreamGetLenC(lua_State* L, LuaStream* s);

LuaStream* lua_pushluastream(lua_State* L) {
	LuaStream* stream = (LuaStream*)lua_newuserdata(L, sizeof(LuaStream));
	luaL_getmetatable(L, STREAM);
	lua_setmetatable(L, -2);
	memset(stream, 0, sizeof(LuaStream));
	stream->backendRef = LUA_NOREF;
	lua_setup_inmemory_stream(L, stream);
	return stream;
}

LuaStream* lua_pushluastream(lua_State* L, const BYTE* data, size_t len) {
	LuaStream* stream = lua_pushluastream(L);
	if (data && len > 0) {
		StreamWrite(L, stream, data, len);
		StreamSetPosC(L, stream, 0);
	}
	return stream;
}

LuaStream* lua_pushluastream_native(lua_State* L, const LuaStreamVtable* vtbl,
	void* native, BYTE caps) {
	LuaStream* s = (LuaStream*)lua_newuserdata(L, sizeof(LuaStream));
	memset(s, 0, sizeof(LuaStream));
	luaL_getmetatable(L, STREAM);
	lua_setmetatable(L, -2);
	s->backendRef = LUA_NOREF;
	s->vtbl       = vtbl;
	s->native     = native;
	s->Caps       = caps;
	return s;
}

static bool StreamWrite(lua_State* L, LuaStream* s, const BYTE* data, size_t len) {
	if (s->vtbl)
		return s->vtbl->write(s->native, data, len);
	lua_rawgeti(L, LUA_REGISTRYINDEX, s->backendRef);
	lua_pushinteger(L, STREAM_OP_WRITE);
	lua_pushlstring(L, (const char*)data, len);
	lua_call_nohook(L, 2, 1);
	bool result = lua_toboolean(L, -1) != 0;
	lua_pop(L, 1);
	return result;
}

// -- Centralized C-level helpers for pos/len/setpos ----------------------------
// These never push to the Lua stack — they return C values directly.
// All Lua API functions and internal operations route through these so call
// sites stay clean regardless of whether the stream uses a vtable or Lua fn.

static lua_Integer StreamCurPosC(lua_State* L, LuaStream* s) {
	if (s->vtbl)
		return s->vtbl->curpos(s->native);
	lua_rawgeti(L, LUA_REGISTRYINDEX, s->backendRef);
	lua_pushinteger(L, STREAM_OP_CURPOS);
	lua_call_nohook(L, 1, 1);
	lua_Integer result = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return result;
}

static bool StreamSetPosC(lua_State* L, LuaStream* s, lua_Integer pos) {
	if (s->vtbl)
		return s->vtbl->setpos(s->native, pos);
	lua_rawgeti(L, LUA_REGISTRYINDEX, s->backendRef);
	lua_pushinteger(L, STREAM_OP_SETPOS);
	lua_pushinteger(L, pos);
	lua_call_nohook(L, 2, 1);
	bool result = lua_toboolean(L, -1) != 0;
	lua_pop(L, 1);
	return result;
}

static lua_Integer StreamGetLenC(lua_State* L, LuaStream* s) {
	if (s->vtbl)
		return s->vtbl->getlen(s->native);
	lua_rawgeti(L, LUA_REGISTRYINDEX, s->backendRef);
	lua_pushinteger(L, STREAM_OP_LEN);
	lua_call_nohook(L, 1, 1);
	lua_Integer result = lua_tointeger(L, -1);
	lua_pop(L, 1);
	return result;
}

	int lua_isstream(lua_State* L, int index) {
		if (lua_type(L, index) != LUA_TUSERDATA)
			return 0;
		if (!lua_getmetatable(L, index))
			return 0;
		luaL_getmetatable(L, STREAM);
		int result = lua_rawequal(L, -1, -2);
		lua_pop(L, 2);
		return result;
	}

LuaStream* lua_toluastream(lua_State* L, int index) {
	return (LuaStream*)luaL_checkudata(L, index, STREAM);
}

int luastream_gc(lua_State* L) {
	LuaStream* stream = lua_toluastream(L, 1);
	if (stream->vtbl) {
		stream->vtbl->close(stream->native, L);
	} else if (stream->backendRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, stream->backendRef);
		lua_pushinteger(L, STREAM_OP_CLOSE);
		lua_pcall_nohook(L, 1, 0, 0);
		luaL_unref(L, LUA_REGISTRYINDEX, stream->backendRef);
	}
	memset(stream, 0, sizeof(LuaStream));
	stream->backendRef = LUA_NOREF;
	return 0;
}

int luastream_tostring(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if ((s->Caps & STREAM_CAP_READ) && (s->Caps & STREAM_CAP_SEEK) && lua_is_inmemory_stream(s))
		return ReadLuaStream(L);
	lua_pushfstring(L, "Stream(%p)", s);
	return 1;
}

int OpenFile(lua_State* L) {
	const char* name = luaL_checkstring(L, 1);
	const char* mode = luaL_optstring(L, 2, "rb");
	lua_pushfilestream(L, name, mode);
	return 1;
}

int NewStream(lua_State* L) {
	if (lua_type(L, 1) == LUA_TFUNCTION) {

		LuaStream* stream = (LuaStream*)lua_newuserdata(L, sizeof(LuaStream));
		luaL_getmetatable(L, STREAM);
		lua_setmetatable(L, -2);
		memset(stream, 0, sizeof(LuaStream));
		stream->backendRef = LUA_NOREF;
		lua_pushvalue(L, 1);
		stream->backendRef = luaL_ref(L, LUA_REGISTRYINDEX);
		lua_rawgeti(L, LUA_REGISTRYINDEX, stream->backendRef);
		lua_pushinteger(L, STREAM_OP_OPEN);
		lua_pcall_nohook(L, 1, 1, 0);

		// If its not a number assume it failed
		if (!lua_isnumber(L, -1)) {
			luaL_error(L, "Backend function failed to open");
			return 0;
		}
		stream->Caps = (BYTE)lua_tointeger(L, -1);

		// If Caps is 0 the backend opened but exposed nothing useful
		if (stream->Caps == 0) {
			luaL_error(L, "Backend function failed to open");
			return 0;
		}

		lua_pop(L, 1);

	} else if (lua_type(L, 1) == LUA_TSTRING) {
		size_t len = 0;
		const char* str = lua_tolstring(L, 1, &len);
		lua_pushluastream(L, (const BYTE*)str, len);
	} else {
		lua_pushluastream(L);
	}
	return 1;
}

int StreamPos(lua_State* L) {
	LuaStream* stream = lua_toluastream(L, 1);
	if (!(stream->Caps & STREAM_CAP_SEEK)) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, StreamCurPosC(L, stream));
	return 1;
}

int StreamLen(lua_State* L) {
	LuaStream* stream = lua_toluastream(L, 1);
	if (!(stream->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, StreamGetLenC(L, stream));
	return 1;
}

int GetStreamInfo(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (s->vtbl && s->vtbl->hasdata) {
		// Async/network streams: info may yield (e.g. HTTP waits for response headers).
		// Returns 1 value — the stream-specific info table.
		if (s->vtbl->info)
			return s->vtbl->info(s->native, L);
		lua_pushnil(L);
		return 1;
	}
	// Sync streams: returns 2 values — caps table + backend info table.
	lua_createtable(L, 0, 1);
	lua_pushinteger(L, s->Caps);
	lua_setfield(L, -2, "Caps");
	if (s->vtbl) {
		s->vtbl->info(s->native, L);
	} else {
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->backendRef);
		lua_pushinteger(L, STREAM_OP_INFO);
		lua_call_nohook(L, 1, 1);
	}
	return 2;
}

int StreamSetPos(lua_State* L) {
	LuaStream* stream = lua_toluastream(L, 1);
	if (!(stream->Caps & STREAM_CAP_SEEK)) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_Integer pos = luaL_checkinteger(L, 2);
	lua_pushboolean(L, StreamSetPosC(L, stream, pos));
	return 1;
}

static void StreamRead(lua_State* L, LuaStream* s, size_t len) {
	if (s->vtbl) {
		s->vtbl->read(s->native, L, len);
		return;
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, s->backendRef);
	lua_pushinteger(L, STREAM_OP_READ);
	lua_pushinteger(L, len);
	lua_call_nohook(L, 2, 1);
}

int lua_stream_read_chunk(lua_State* L, LuaStream* s, size_t len) {
	StreamRead(L, s, len);
	// Normalise non-string or empty-string results to nil so streaming consumers
	// get a clean EOF signal regardless of which backend produced them.
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) == 0) {
		lua_pop(L, 1);
		lua_pushnil(L);
	}
	return 1;
}

int lua_stream_write_bytes(lua_State* L, LuaStream* s, const char* data, size_t len) {
	lua_pushboolean(L, StreamWrite(L, s, (const BYTE*)data, len));
	return 1;
}

lua_Integer lua_stream_curpos(lua_State* L, LuaStream* s) {
	return StreamCurPosC(L, s);
}

bool lua_stream_setpos(lua_State* L, LuaStream* s, lua_Integer pos) {
	return StreamSetPosC(L, s, pos);
}

lua_Integer lua_stream_getlen(lua_State* L, LuaStream* s) {
	return StreamGetLenC(L, s);
}

// Continuation for the fn-backend Read path.
// Normalises whatever the Lua function returned: non-string ? nil.
// Called both as the lua_callk continuation (when the backend yielded, e.g.
// Sleep was called) and directly when the backend returned synchronously.
static int fn_read_cont(lua_State* L, int status, lua_KContext ctx) {
	if (lua_type(L, -1) != LUA_TSTRING) {
		lua_pop(L, 1);
		lua_pushnil(L);
	}
	return 1;
}

int ReadLuaStream(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	if (s->vtbl == NULL) {
		// Lua function backend: use lua_callk so the backend can yield
		// (e.g. Sleep, or any other yieldable call) without hitting
		// "attempt to yield across a C-call boundary".
		size_t len = (size_t)luaL_optinteger(L, 2, 0);
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->backendRef);
		lua_pushinteger(L, STREAM_OP_READ);
		lua_pushinteger(L, (lua_Integer)len);
		lua_callk(L, 2, 1, 0, fn_read_cont);
		return fn_read_cont(L, LUA_OK, 0);
	}
	StreamRead(L, s, (size_t)luaL_optinteger(L, 2, 0));
	if (lua_type(L, -1) != LUA_TSTRING) {
		lua_pop(L, 1);
		lua_pushnil(L);
	}
	return 1;
}

int ReadStreamByte(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushinteger(L, -1);
		return 1;
	}
	StreamRead(L, s, 1);
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) < 1) {
		lua_pop(L, 1);
		lua_pushinteger(L, -1);
		return 1;
	}
	BYTE b = (BYTE)lua_tolstring(L, -1, NULL)[0];
	lua_pop(L, 1);
	lua_pushinteger(L, (lua_Integer)b);
	return 1;
}

// Helper: map the integer result of a hasdata query to a Lua value.
// 0  ? boolean false  (no data)
// 1  ? boolean true   (data ready, quantity unknown)
// n>1 ? integer n     (n bytes are ready, e.g. network socket buffer)
static void push_hasdata_result(lua_State* L, lua_Integer n) {
	if (n < 0)
		lua_pushinteger(L, n);  // negative = stream is dead/closed
	else if (n == 0)
		lua_pushboolean(L, 0);  // alive but no data ready
	else if (n == 1)
		lua_pushboolean(L, 1);  // data ready, quantity unknown
	else
		lua_pushinteger(L, n);  // n bytes ready
}

int HasDataLuaStream(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (s->vtbl) {
		if (s->vtbl->hasdata) {
			// Async / network streams: dedicated non-blocking check.
			push_hasdata_result(L, s->vtbl->hasdata(s->native));
		} else {
			// Sync streams (memory, file, shmem): bytes remaining = len - pos.
			lua_Integer pos = s->vtbl->curpos ? s->vtbl->curpos(s->native) : 0;
			lua_Integer len = s->vtbl->getlen ? s->vtbl->getlen(s->native) : 0;
			push_hasdata_result(L, len > pos ? len - pos : 0);
		}
	} else if (s->backendRef != LUA_NOREF) {
		// Lua function backend: dispatch STREAM_OP_HASDATA and pass the result
		// through as-is (caller may return false, nil, a boolean, or a count).
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->backendRef);
		lua_pushinteger(L, STREAM_OP_HASDATA);
		lua_call_nohook(L, 1, 1);
	} else {
		// Closed / zeroed stream: return -1 to signal "stream is dead".
		// Callers can distinguish no-data-yet (false) from dead (-1).
		push_hasdata_result(L, -1);
	}
	return 1;
}

int PeekStreamByte(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ) || !(s->Caps & STREAM_CAP_SEEK)) {
		lua_pushinteger(L, -1);
		return 1;
	}
	lua_Integer savedPos = StreamCurPosC(L, s);
	lua_Integer peekPos  = luaL_optinteger(L, 2, savedPos);
	if (peekPos != savedPos)
		StreamSetPosC(L, s, peekPos);
	StreamRead(L, s, 1);
	int result;
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) < 1) {
		result = -1;
	} else {
		result = (int)(unsigned char)lua_tolstring(L, -1, NULL)[0];
	}
	lua_pop(L, 1);
	StreamSetPosC(L, s, savedPos);
	lua_pushinteger(L, result);
	return 1;
}

int ReadFloat(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	StreamRead(L, s, sizeof(float));
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) != sizeof(float)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	float v;
	memcpy(&v, lua_tolstring(L, -1, NULL), sizeof(float));
	lua_pop(L, 1);
	lua_pushnumber(L, (lua_Number)v);
	return 1;
}

int ReadDouble(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	StreamRead(L, s, sizeof(double));
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) != sizeof(double)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	double v;
	memcpy(&v, lua_tolstring(L, -1, NULL), sizeof(double));
	lua_pop(L, 1);
	lua_pushnumber(L, (lua_Number)v);
	return 1;
}

int ReadShort(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	StreamRead(L, s, sizeof(short));
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) != sizeof(short)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	short v;
	memcpy(&v, lua_tolstring(L, -1, NULL), sizeof(short));
	lua_pop(L, 1);
	lua_pushinteger(L, (lua_Integer)v);
	return 1;
}

int ReadUShort(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	StreamRead(L, s, sizeof(unsigned short));
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) != sizeof(unsigned short)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	unsigned short v;
	memcpy(&v, lua_tolstring(L, -1, NULL), sizeof(unsigned short));
	lua_pop(L, 1);
	lua_pushinteger(L, (lua_Integer)v);
	return 1;
}

int ReadInt(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	StreamRead(L, s, sizeof(int));
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) != sizeof(int)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	int v;
	memcpy(&v, lua_tolstring(L, -1, NULL), sizeof(int));
	lua_pop(L, 1);
	lua_pushinteger(L, (lua_Integer)v);
	return 1;
}

int ReadUInt(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	StreamRead(L, s, sizeof(unsigned int));
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) != sizeof(unsigned int)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	unsigned int v;
	memcpy(&v, lua_tolstring(L, -1, NULL), sizeof(unsigned int));
	lua_pop(L, 1);
	lua_pushinteger(L, (lua_Integer)v);
	return 1;
}

int ReadLong(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	StreamRead(L, s, sizeof(long long));
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) != sizeof(long long)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	long long v;
	memcpy(&v, lua_tolstring(L, -1, NULL), sizeof(long long));
	lua_pop(L, 1);
	lua_pushinteger(L, (lua_Integer)v);
	return 1;
}

int ReadUnsignedLong(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	StreamRead(L, s, sizeof(unsigned long long));
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) != sizeof(unsigned long long)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	unsigned long long v;
	memcpy(&v, lua_tolstring(L, -1, NULL), sizeof(unsigned long long));
	lua_pop(L, 1);
	// Values that exceed LUA_MAXINTEGER cannot be represented faithfully as a signed integer;
	// push a LuaUInt so the caller gets the full uint64 range without silent truncation.
	if (v > (unsigned long long)LUA_MAXINTEGER)
		lua_pushuint(L)->value = (uint64_t)v;
	else
		lua_pushinteger(L, (lua_Integer)v);
	return 1;
}

int ReadWchar(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	size_t byteCount;
	if (lua_isnoneornil(L, 2)) {
		byteCount = 0;  // 0 = read all remaining (StreamRead convention)
	}
	else {
		lua_Integer n = luaL_checkinteger(L, 2);
		if (n <= 0) {
			lua_pushnil(L);
			return 1;
		}
		byteCount = (size_t)n * sizeof(char16_t);  // always 2 bytes per unit on every platform
	}
	StreamRead(L, s, byteCount);
	size_t outLen = 0;
	const char* raw = lua_type(L, -1) == LUA_TSTRING ? lua_tolstring(L, -1, &outLen) : NULL;
	if (!raw || outLen < sizeof(char16_t)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	size_t char16Count = outLen / sizeof(char16_t);
	size_t wcharLen = 0;
	wchar_t* wstr = char16_alloc_as_wchar((const char16_t*)raw, char16Count, &wcharLen);
	lua_pop(L, 1);  // pop the string; raw is no longer valid
	if (!wstr) {
		lua_pushnil(L);
		return 1;
	}
	LuaWChar* wch = lua_pushwchar(L);
	wch->str = wstr;
	wch->len = wcharLen;
	return 1;
}

// Reads 8 bytes and pushes a LuaUInt. Returns nil on EOF/short read.
int ReadUInt64(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	StreamRead(L, s, sizeof(uint64_t));
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) != sizeof(uint64_t)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	uint64_t v;
	memcpy(&v, lua_tolstring(L, -1, NULL), sizeof(uint64_t));
	lua_pop(L, 1);
	lua_pushuint(L)->value = v;
	return 1;
}

// Reads a null-terminated decimal string from the stream and pushes a LuaDecimal.
// Reads until a NUL byte, newline, or EOF. Returns nil on parse failure.
int ReadDecimal(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	// Read bytes one at a time until NUL, newline, or EOF.
	char buf[64];
	size_t pos = 0;
	while (pos < sizeof(buf) - 1) {
		StreamRead(L, s, 1);
		if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) < 1) {
			lua_pop(L, 1);
			break;
		}
		char c = lua_tolstring(L, -1, NULL)[0];
		lua_pop(L, 1);
		if (c == '\0' || c == '\n')
			break;
		buf[pos++] = c;
	}
	buf[pos] = '\0';
	if (pos == 0) {
		lua_pushnil(L);
		return 1;
	}
	LuaDecimal* d = lua_pushdecimal(L);
	if (!decimal_parse_c(buf, pos, d)) {
		lua_pop(L, 1);
		lua_pushnil(L);
	}
	return 1;
}

// Reads exactly 16 bytes and pushes a LuaIdentifier (UUID). Returns nil on short read.
int ReadIdentifier(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	StreamRead(L, s, 16);
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) != 16) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	const char* raw = lua_tolstring(L, -1, NULL);
	LuaIdentifier* id = lua_pushidentifier(L);
	id->type = IDENTIFIER_UUID;
	id->len  = 16;
	memcpy(id->bytes, raw, 16);
	lua_remove(L, -2); // pop the string
	return 1;
}

int ReadDateTime(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	StreamRead(L, s, 10);
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) != 10) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	const char* raw = lua_tolstring(L, -1, NULL);
	int64_t  ticks;
	int16_t  offset_minutes;
	memcpy(&ticks,          raw,     sizeof(int64_t));
	memcpy(&offset_minutes, raw + 8, sizeof(int16_t));
	lua_pop(L, 1);
	LuaDateTime* dt = lua_pushdatetime(L);
	dt->ticks          = ticks;
	dt->offset_minutes = offset_minutes;
	return 1;
}

// Reads 8 bytes (int64 ticks) and pushes a LuaTimeSpan. Returns nil on short read.
int ReadTimeSpan(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	StreamRead(L, s, sizeof(int64_t));
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) != sizeof(int64_t)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	int64_t ticks;
	memcpy(&ticks, lua_tolstring(L, -1, NULL), sizeof(int64_t));
	lua_pop(L, 1);
	lua_pushtimespan(L)->ticks = ticks;
	return 1;
}

int ReadUtf8(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	StreamRead(L, s, 1);
	if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) < 1) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	BYTE lead = (BYTE)lua_tolstring(L, -1, NULL)[0];
	lua_pop(L, 1);
	size_t seqLen;
	DWORD code;
	if      ((lead & 0x80) == 0x00) { seqLen = 1; code = lead; }
	else if ((lead & 0xE0) == 0xC0) { seqLen = 2; code = lead & 0x1F; }
	else if ((lead & 0xF0) == 0xE0) { seqLen = 3; code = lead & 0x0F; }
	else if ((lead & 0xF8) == 0xF0) { seqLen = 4; code = lead & 0x07; }
	else { lua_pushnil(L); return 1; }
	BYTE seqBuf[4] = { lead, 0, 0, 0 };
	if (seqLen > 1) {
		StreamRead(L, s, seqLen - 1);
		if (lua_type(L, -1) != LUA_TSTRING || lua_rawlen(L, -1) != seqLen - 1) {
			lua_pop(L, 1);
			lua_pushnil(L);
			return 1;
		}
		const BYTE* rem = (const BYTE*)lua_tolstring(L, -1, NULL);
		for (size_t i = 0; i < seqLen - 1; i++) {
			if ((rem[i] & 0xC0) != 0x80) {
				lua_pop(L, 1);
				lua_pushnil(L);
				return 1;
			}
			seqBuf[i + 1] = rem[i];
			code = (code << 6) | (rem[i] & 0x3F);
		}
		lua_pop(L, 1);
	}
	lua_pushlstring(L, (const char*)seqBuf, seqLen);
	lua_pushinteger(L, (lua_Integer)code);
	return 2;
}

int WriteStreamByte(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_WRITE)) {
		lua_pushboolean(L, false);
		return 1;
	}
	int byte = (int)lua_tointeger(L, 2);
	if (byte < 0 || byte > 255) {
		lua_pushboolean(L, false);
		return 1;
	}
	BYTE raw = (BYTE)byte;
	lua_pushboolean(L, StreamWrite(L, s, &raw, 1));
	return 1;
}

int SetStreamByte(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	BYTE value = (BYTE)lua_tointeger(L, 2);
	if (!lua_isnoneornil(L, 3)) {
		if (!(s->Caps & STREAM_CAP_WRITE) || !(s->Caps & STREAM_CAP_SEEK))
			return 0;
		lua_Integer pos      = luaL_checkinteger(L, 3);
		lua_Integer savedPos = StreamCurPosC(L, s);
		StreamSetPosC(L, s, pos);
		StreamWrite(L, s, &value, 1);
		StreamSetPosC(L, s, savedPos);
	} else {
		if (!(s->Caps & STREAM_CAP_WRITE))
			return 0;
		StreamWrite(L, s, &value, 1);
	}
	return 0;
}

int WriteFloat(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_WRITE)) {
		lua_pushboolean(L, false);
		return 1;
	}
	float v = (float)lua_tonumber(L, 2);
	lua_pushboolean(L, StreamWrite(L, s, (const BYTE*)&v, sizeof(float)));
	return 1;
}

int WriteDouble(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_WRITE)) {
		lua_pushboolean(L, false);
		return 1;
	}
	double v = (double)lua_tonumber(L, 2);
	lua_pushboolean(L, StreamWrite(L, s, (const BYTE*)&v, sizeof(double)));
	return 1;
}

int WriteShort(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_WRITE)) {
		lua_pushboolean(L, false);
		return 1;
	}
	short v = (short)lua_tointeger(L, 2);
	lua_pushboolean(L, StreamWrite(L, s, (const BYTE*)&v, sizeof(short)));
	return 1;
}

int WriteUShort(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_WRITE)) {
		lua_pushboolean(L, false);
		return 1;
	}
	unsigned short v = (unsigned short)lua_tointeger(L, 2);
	lua_pushboolean(L, StreamWrite(L, s, (const BYTE*)&v, sizeof(unsigned short)));
	return 1;
}

int WriteInt(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_WRITE)) {
		lua_pushboolean(L, false);
		return 1;
	}
	int v = (int)lua_tointeger(L, 2);
	lua_pushboolean(L, StreamWrite(L, s, (const BYTE*)&v, sizeof(int)));
	return 1;
}

int WriteUInt(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_WRITE)) {
		lua_pushboolean(L, false);
		return 1;
	}
	unsigned int v = (unsigned int)lua_tointeger(L, 2);
	lua_pushboolean(L, StreamWrite(L, s, (const BYTE*)&v, sizeof(unsigned int)));
	return 1;
}

int WriteLong(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_WRITE)) {
		lua_pushboolean(L, false);
		return 1;
	}
	long long v = (long long)lua_tointeger(L, 2);
	lua_pushboolean(L, StreamWrite(L, s, (const BYTE*)&v, sizeof(long long)));
	return 1;
}

int WriteUnsignedLong(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_WRITE)) {
		lua_pushboolean(L, false);
		return 1;
	}
	unsigned long long v = (unsigned long long)lua_tointeger(L, 2);
	lua_pushboolean(L, StreamWrite(L, s, (const BYTE*)&v, sizeof(unsigned long long)));
	return 1;
}

int WriteLuaValue(lua_State* L) {
	LuaStream* stream = lua_toluastream(L, 1);
	if (!(stream->Caps & STREAM_CAP_WRITE)) {
		lua_pushinteger(L, 0);
		return 1;
	}
	size_t limit = (size_t)luaL_optinteger(L, 3, 0);
	size_t len = 0;
	const BYTE* raw = NULL;
	LUA_NUMBER number;
	BYTE boolean;
	switch (lua_type(L, 2)) {
	case LUA_TNUMBER:
		number = lua_tonumber(L, 2);
		raw = (const BYTE*)&number;
		len = sizeof(LUA_NUMBER);
		break;
	case LUA_TBOOLEAN:
		boolean = (BYTE)lua_toboolean(L, 2);
		raw = &boolean;
		len = 1;
		break;
	case LUA_TSTRING:
		raw = (const BYTE*)lua_tolstring(L, 2, &len);
		break;
	case LUA_TUSERDATA:
		if (lua_iswchar(L, 2)) {
			LuaWChar* wch = (LuaWChar*)lua_touserdata(L, 2);
			if (wch && wch->str && wch->len > 0) {
				size_t char16Count = 0;
				char16_t* buf = wchar_alloc_as_char16(wch->str, wch->len, &char16Count);
				if (!buf) {
					lua_pushinteger(L, 0);
					return 1;
				}
				size_t byteLen = char16Count * sizeof(char16_t);
				if (limit > 0 && limit < byteLen)
					byteLen = limit;
				bool ok = StreamWrite(L, stream, (const BYTE*)buf, byteLen);
				kitsune_free(buf);
				lua_pushinteger(L, ok ? (lua_Integer)byteLen : 0);
				return 1;
			}
			lua_pushinteger(L, 0);
			return 1;
		}
		if (lua_isdecimal(L, 2)) {
			// Write the canonical decimal string (UTF-8 bytes, no null terminator).
			lua_decimal_push_string(L, 2);
			size_t dlen;
			const char* ds = lua_tolstring(L, -1, &dlen);
			if (limit > 0 && limit < dlen) dlen = limit;
			bool ok = ds && dlen > 0 && StreamWrite(L, stream, (const BYTE*)ds, dlen);
			lua_pop(L, 1);
			lua_pushinteger(L, ok ? (lua_Integer)dlen : 0);
			return 1;
		}
		if (lua_isidentifier(L, 2)) {
			// Write the raw identifier bytes (16 bytes for UUID, 12 for OID).
			LuaIdentifier* id = lua_toidentifier(L, 2);
			size_t idlen = (size_t)id->len;
			if (limit > 0 && limit < idlen) idlen = limit;
			bool ok = idlen > 0 && StreamWrite(L, stream, id->bytes, idlen);
			lua_pushinteger(L, ok ? (lua_Integer)idlen : 0);
			return 1;
		}
		if (lua_isdatetime(L, 2)) {
			// Write ticks (int64, 8 bytes) + offset_minutes (int16, 2 bytes) = 10 bytes total.
			LuaDateTime* dt = lua_todatetime(L, 2);
			unsigned char buf[10];
			memcpy(buf,     &dt->ticks,          sizeof(int64_t));
			memcpy(buf + 8, &dt->offset_minutes, sizeof(int16_t));
			size_t dtlen = 10;
			if (limit > 0 && limit < dtlen) dtlen = limit;
			bool ok = StreamWrite(L, stream, buf, dtlen);
			lua_pushinteger(L, ok ? (lua_Integer)dtlen : 0);
			return 1;
		}
		if (lua_isuint(L, 2)) {
			// Write the uint64 value (8 bytes, native endian).
			uint64_t uv = lua_touint(L, 2)->value;
			size_t uvlen = sizeof(uint64_t);
			if (limit > 0 && limit < uvlen) uvlen = limit;
			bool ok = StreamWrite(L, stream, (const BYTE*)&uv, uvlen);
			lua_pushinteger(L, ok ? (lua_Integer)uvlen : 0);
			return 1;
		}
		if (lua_istimespan(L, 2)) {
			// Write the int64 tick count (8 bytes, native endian).
			int64_t ticks = lua_totimespan(L, 2)->ticks;
			size_t tslen = sizeof(int64_t);
			if (limit > 0 && limit < tslen) tslen = limit;
			bool ok = StreamWrite(L, stream, (const BYTE*)&ticks, tslen);
			lua_pushinteger(L, ok ? (lua_Integer)tslen : 0);
			return 1;
		}
		break;
	default:
		break;
	}
	if (limit > 0 && limit < len)
		len = limit;
	lua_pushinteger(L, (raw && len > 0 && StreamWrite(L, stream, raw, len)) ? (lua_Integer)len : 0);
	return 1;
}

int WriteUtf8(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_WRITE)) {
		lua_pushboolean(L, false);
		return 1;
	}
	size_t len;
	const char* str = luaL_checklstring(L, 2, &len);
	BYTE* buf = (BYTE*)kitsune_malloc(len * 2 + 1);
	if (!buf) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "out of memory");
		return 2;
	}
	const unsigned char* end = (const unsigned char*)str + len;
	unsigned char* in  = (unsigned char*)str;
	unsigned char* out = (unsigned char*)buf;
	while (in < end) {
		if (*in < 128) {
			*out++ = *in++;
		}
		else {
			*out++ = (unsigned char)(0xc2 + (*in > 0xbf));
			*out++ = (unsigned char)((*in++ & 0x3f) + 0x80);
		}
	}
	bool ok = StreamWrite(L, s, buf, (size_t)(out - buf));
	kitsune_free(buf);
	lua_pushboolean(L, ok);
	return 1;
}

// -- Compress / Decompress -----------------------------------------------------
// Unified implementation using miniz (cross-platform, no system dependency).
//
// Wire format — a stream of zero or more chunks, each:
//   [uint32_le uncompressedSize][uint32_le compressedSize][compressedBytes]
// An empty uncompressedSize or compressedSize field signals end-of-stream.
// compressedBytes are zlib-format (deflate + 2-byte header + 4-byte Adler32).
//
// The second Lua argument is now a compression level (0–9, default MZ_DEFAULT_COMPRESSION).
// The Windows COMPRESS_ALGORITHM_* integer is no longer accepted.

static const size_t STREAM_COMPRESS_CHUNK = 65536u;

// ctx packing for CompressContinuation:
//   bits 0-3: compression level clamped to [0,15] — miniz treats any value >9 as default (-1)
//   bit    8: ownDst flag
// Using only 4 bits for the level avoids the classic MZ_DEFAULT_COMPRESSION = -1 bug:
// (unsigned int)(-1) = 0xFFFFFFFF would set bit 8 and corrupt the ownDst flag.
#define COMPRESS_CTX(level, ownDst) \
	((lua_KContext)(((lua_KContext)(level) & 0xF) | ((ownDst) ? (1 << 8) : 0)))
#define COMPRESS_LEVEL(ctx)   ((int)((ctx) & 0xF))
#define COMPRESS_OWNDST(ctx)  (((ctx) >> 8) & 1)

static int CompressContinuation(lua_State* L, int status, lua_KContext ctx) {
	// dst is always one below the top; chunk/eof is always at the top.
	// This invariant holds both for the first entry and for every loop iteration.
	int dstIdx = lua_gettop(L) - 1;
	// Top of stack: chunk (string) or false/nil (EOF).
	if (lua_type(L, -1) != LUA_TSTRING) {
		lua_pop(L, 1);
		// Write end-of-stream sentinel: two zero uint32s.
		LuaStream* dst = lua_toluastream(L, dstIdx);
		uint32_t sentinel[2] = { 0, 0 };
		StreamWrite(L, dst, (const BYTE*)sentinel, sizeof(sentinel));
		if (COMPRESS_OWNDST(ctx))
			StreamSetPosC(L, dst, 0);
		lua_pushvalue(L, dstIdx);
		return 1;
	}
	size_t chunkLen = 0;
	const char* chunkStr = lua_tolstring(L, -1, &chunkLen);
	LuaStream* dst = lua_toluastream(L, dstIdx);
	mz_ulong bound = mz_compressBound((mz_ulong)chunkLen);
	BYTE* buf = (BYTE*)kitsune_malloc((size_t)bound);
	if (!buf) {
		lua_pop(L, 1);
		luaL_error(L, "out of memory");
		return 0;
	}
	mz_ulong finalSize = bound;
	int rc = mz_compress2(buf, &finalSize, (const unsigned char*)chunkStr, (mz_ulong)chunkLen, COMPRESS_LEVEL(ctx));
	lua_pop(L, 1);
	if (rc != MZ_OK) {
		kitsune_free(buf);
		luaL_error(L, "compression failed (%d)", rc);
		return 0;
	}
	uint32_t hdr[2] = { (uint32_t)chunkLen, (uint32_t)finalSize };
	StreamWrite(L, dst, (const BYTE*)hdr, sizeof(hdr));
	StreamWrite(L, dst, buf, (size_t)finalSize);
	kitsune_free(buf);
	// Request next chunk. If Read completes synchronously, call the continuation
	// directly. If it yields, Lua calls the continuation on resume.
	lua_pushvalue(L, 1);
	lua_getfield(L, -1, "Read");
	lua_insert(L, -2);
	lua_pushinteger(L, (lua_Integer)STREAM_COMPRESS_CHUNK);
	lua_callk(L, 2, 1, ctx, CompressContinuation);
	return CompressContinuation(L, LUA_OK, ctx);
}

int CompressStream(lua_State* L) {
	LuaStream* src = lua_toluastream(L, 1);
	if (!(src->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		lua_pushstring(L, "stream is not readable");
		return 2;
	}
	int level = (int)luaL_optinteger(L, 2, MZ_DEFAULT_COMPRESSION);
	if (src->Caps & STREAM_CAP_SEEK)
		StreamSetPosC(L, src, 0);
	bool ownDst = !lua_isstream(L, 3);
	if (ownDst) {
		lua_pushluastream(L);
	} else {
		lua_pushvalue(L, 3);
	}
	LuaStream* dst = lua_toluastream(L, -1);
	if (!(dst->Caps & STREAM_CAP_WRITE)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushstring(L, "destination stream is not writable");
		return 2;
	}
	// Unified path: call stream:Read() via lua_callk so both sync and async
	// sources work correctly.  Sync backends return from the call immediately;
	// the continuation fires inline.  Async backends may yield.
	{
		lua_KContext ctx = COMPRESS_CTX(level, ownDst);
		lua_pushvalue(L, 1);
		lua_getfield(L, -1, "Read");
		lua_insert(L, -2);
		lua_pushinteger(L, (lua_Integer)STREAM_COMPRESS_CHUNK);
		lua_callk(L, 2, 1, ctx, CompressContinuation);
		return CompressContinuation(L, LUA_OK, ctx);
	}
}

// Accumulation continuation for async Decompress: reads all chunks into a
// memory stream, then decompresses synchronously.
static int DecompressAsyncAccumulateContinuation(lua_State* L, int status, lua_KContext ctx);

static int DecompressAsyncAccumulateContinuation(lua_State* L, int status, lua_KContext ctx) {
	// accum is always one below the top; chunk/eof is always at the top.
	int accumIdx = lua_gettop(L) - 1;
	if (lua_type(L, -1) == LUA_TSTRING) {
		size_t len = 0;
		const char* data = lua_tolstring(L, -1, &len);
		LuaStream* accum = lua_toluastream(L, accumIdx);
		StreamWrite(L, accum, (const BYTE*)data, len);
		lua_pop(L, 1);
		// Request next chunk.
		lua_pushvalue(L, 1);
		lua_getfield(L, -1, "Read");
		lua_insert(L, -2);
		lua_callk(L, 1, 1, ctx, DecompressAsyncAccumulateContinuation);
		return DecompressAsyncAccumulateContinuation(L, LUA_OK, ctx);
	}
	lua_pop(L, 1);
	// EOF — reset accum to 0 and call synchronous DecompressStream on it.
	accumIdx = lua_gettop(L);  // re-derive: now accum is at the top
	LuaStream* accum = lua_toluastream(L, accumIdx);
	StreamSetPosC(L, accum, 0);
	// Replace arg 1 with the accum memory stream, trim all other args, then
	// call DecompressStream so its luaL_optinteger(L,2) sees nothing at L[2].
	lua_pushvalue(L, accumIdx);
	lua_replace(L, 1);
	lua_settop(L, 1);
	return DecompressStream(L);
}

int DecompressStream(lua_State* L) {
	LuaStream* src = lua_toluastream(L, 1);
	if (!(src->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		lua_pushstring(L, "stream is not readable");
		return 2;
	}
	luaL_optinteger(L, 2, 0);
	// Async source (no hasdata vtable = sync; has hasdata = may yield on Read):
	// accumulate all compressed bytes into a memory stream, then decompress.
	if (src->vtbl && src->vtbl->hasdata) {
		lua_pushluastream(L);  // push accum memory stream; continuation finds it at lua_gettop-1
		lua_pushvalue(L, 1);
		lua_getfield(L, -1, "Read");
		lua_insert(L, -2);
		lua_callk(L, 1, 1, 0, DecompressAsyncAccumulateContinuation);
		return DecompressAsyncAccumulateContinuation(L, LUA_OK, 0);
	}
	if (src->Caps & STREAM_CAP_SEEK)
		StreamSetPosC(L, src, 0);
	bool ownDst = !lua_isstream(L, 3);
	if (ownDst) {
		lua_pushluastream(L);
	} else {
		lua_pushvalue(L, 3);
	}
	LuaStream* dst = lua_toluastream(L, -1);
	if (!(dst->Caps & STREAM_CAP_WRITE)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushstring(L, "destination stream is not writable");
		return 2;
	}
	for (;;) {
		StreamRead(L, src, sizeof(uint32_t) * 2);
		if (lua_type(L, -1) != LUA_TSTRING) {
			lua_pop(L, 1);
			break;
		}
		size_t hdrLen = 0;
		const char* hdrData = lua_tolstring(L, -1, &hdrLen);
		if (hdrLen < sizeof(uint32_t) * 2) {
			lua_pop(L, 1);
			break;
		}
		uint32_t uncompressedSize, compressedSize;
		memcpy(&uncompressedSize, hdrData, sizeof(uint32_t));
		memcpy(&compressedSize,   hdrData + sizeof(uint32_t), sizeof(uint32_t));
		lua_pop(L, 1);
		if (uncompressedSize == 0 || compressedSize == 0)
			break;
		StreamRead(L, src, (size_t)compressedSize);
		if (lua_type(L, -1) != LUA_TSTRING) {
			lua_pop(L, 1);
			luaL_error(L, "truncated compressed stream");
			return 0;
		}
		size_t compLen = 0;
		const char* compData = lua_tolstring(L, -1, &compLen);
		if (compLen != (size_t)compressedSize) {
			lua_pop(L, 1);
			lua_pop(L, 1);
			luaL_error(L, "truncated compressed stream");
			return 0;
		}
		BYTE* buf = (BYTE*)kitsune_malloc((size_t)uncompressedSize);
		if (!buf) {
			lua_pop(L, 1);
			lua_pop(L, 1);
			luaL_error(L, "out of memory");
			return 0;
		}
		mz_ulong destLen = (mz_ulong)uncompressedSize;
		int rc = mz_uncompress(buf, &destLen, (const unsigned char*)compData, (mz_ulong)compressedSize);
		lua_pop(L, 1);
		if (rc != MZ_OK) {
			kitsune_free(buf);
			lua_pop(L, 1);
			luaL_error(L, "decompression failed (%d)", rc);
			return 0;
		}
		StreamWrite(L, dst, buf, (size_t)destLen);
		kitsune_free(buf);
	}
	if (ownDst)
		StreamSetPosC(L, dst, 0);
	return 1;
}

