#include "stream.h"
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <compressapi.h>
#pragma comment(lib, "Cabinet.lib")
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

// ── Centralized C-level helpers for pos/len/setpos ────────────────────────────
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
		stream->vtbl->close(stream->native);
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

static const BYTE* StreamRead(lua_State* L, LuaStream* s, size_t len, size_t* outLen) {
	if (s->vtbl)
		return s->vtbl->read(s->native, L, len, outLen);
	lua_rawgeti(L, LUA_REGISTRYINDEX, s->backendRef);
	lua_pushinteger(L, STREAM_OP_READ);
	lua_pushinteger(L, len);
	lua_call_nohook(L, 2, 1);
	return (const BYTE*)lua_tolstring(L, -1, outLen);
}

int lua_stream_read_chunk(lua_State* L, LuaStream* s, size_t len) {
	size_t outLen = 0;
	StreamRead(L, s, len, &outLen);
	// Normalise non-string or empty-string results to nil so streaming consumers
	// get a clean EOF signal regardless of which backend produced them.
	if (lua_type(L, -1) != LUA_TSTRING || outLen == 0) {
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

int ReadLuaStream(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	StreamRead(L, s, luaL_optinteger(L, 2, 0), NULL);
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
	size_t outLen = 0;
	const BYTE* r = StreamRead(L, s, 1, &outLen);
	if (!r || outLen < 1) {
		lua_pop(L, 1);
		lua_pushinteger(L, -1);
		return 1;
	}
	BYTE b = *r;
	lua_pop(L, 1);
	lua_pushinteger(L, (lua_Integer)b);
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
	size_t outLen = 0;
	const BYTE* r = StreamRead(L, s, 1, &outLen);
	int result;
	if (!r || outLen < 1) {
		lua_pop(L, 1);
		result = -1;
	} else {
		result = (int)(unsigned char)*r;
		lua_pop(L, 1);
	}
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
	size_t outLen = 0;
	const BYTE* r = StreamRead(L, s, sizeof(float), &outLen);
	if (!r || outLen != sizeof(float)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	float v;
	memcpy(&v, r, sizeof(float));
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
	size_t outLen = 0;
	const BYTE* r = StreamRead(L, s, sizeof(double), &outLen);
	if (!r || outLen != sizeof(double)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	double v;
	memcpy(&v, r, sizeof(double));
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
	size_t outLen = 0;
	const BYTE* r = StreamRead(L, s, sizeof(short), &outLen);
	if (!r || outLen != sizeof(short)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	short v;
	memcpy(&v, r, sizeof(short));
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
	size_t outLen = 0;
	const BYTE* r = StreamRead(L, s, sizeof(unsigned short), &outLen);
	if (!r || outLen != sizeof(unsigned short)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	unsigned short v;
	memcpy(&v, r, sizeof(unsigned short));
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
	size_t outLen = 0;
	const BYTE* r = StreamRead(L, s, sizeof(int), &outLen);
	if (!r || outLen != sizeof(int)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	int v;
	memcpy(&v, r, sizeof(int));
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
	size_t outLen = 0;
	const BYTE* r = StreamRead(L, s, sizeof(unsigned int), &outLen);
	if (!r || outLen != sizeof(unsigned int)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	unsigned int v;
	memcpy(&v, r, sizeof(unsigned int));
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
	size_t outLen = 0;
	const BYTE* r = StreamRead(L, s, sizeof(long long), &outLen);
	if (!r || outLen != sizeof(long long)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	long long v;
	memcpy(&v, r, sizeof(long long));
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
	size_t outLen = 0;
	const BYTE* r = StreamRead(L, s, sizeof(unsigned long long), &outLen);
	if (!r || outLen != sizeof(unsigned long long)) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	unsigned long long v;
	memcpy(&v, r, sizeof(unsigned long long));
	lua_pop(L, 1);
	lua_pushinteger(L, (lua_Integer)v);
	return 1;
}

int ReadUtf8(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		return 1;
	}
	size_t firstLen = 0;
	const BYTE* first = StreamRead(L, s, 1, &firstLen);
	if (!first || firstLen < 1) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}
	BYTE lead = *first;
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
		size_t remLen = 0;
		const BYTE* rem = StreamRead(L, s, seqLen - 1, &remLen);
		if (!rem || remLen != seqLen - 1) {
			lua_pop(L, 1);
			lua_pushnil(L);
			return 1;
		}
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
	BYTE* buf = (BYTE*)gff_malloc(len * 2 + 1);
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
	gff_free(buf);
	lua_pushboolean(L, ok);
	return 1;
}

static const DWORD STREAM_COMPRESS_CHUNK = 65536u;

int CompressStream(lua_State* L) {
	LuaStream* src = lua_toluastream(L, 1);
	if (!(src->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		lua_pushstring(L, "stream is not readable");
		return 2;
	}
	DWORD algorithm = (DWORD)luaL_optinteger(L, 2, COMPRESS_ALGORITHM_MSZIP);
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
	COMPRESSOR_HANDLE compressor = NULL;
	if (!CreateCompressor(algorithm, NULL, &compressor)) {
		lua_pop(L, 1);
		luaL_error(L, "CreateCompressor failed (%d)", (int)GetLastError());
		return 0;
	}
	bool first = true;
	for (;;) {
		size_t chunkLen = 0;
		const BYTE* chunk = StreamRead(L, src, STREAM_COMPRESS_CHUNK, &chunkLen);
		if (!chunk || chunkLen == 0) {
			lua_pop(L, 1);
			break;
		}
		if (!first && !ResetCompressor(compressor)) {
			lua_pop(L, 1);
			CloseCompressor(compressor);
			lua_pop(L, 1);
			luaL_error(L, "ResetCompressor failed (%d)", (int)GetLastError());
			return 0;
		}
		first = false;
		SIZE_T compressedSize = 0;
		Compress(compressor, chunk, chunkLen, NULL, 0, &compressedSize);
		BYTE* buf = (BYTE*)gff_malloc(compressedSize);
		if (!buf) {
			lua_pop(L, 1);
			CloseCompressor(compressor);
			lua_pop(L, 1);
			luaL_error(L, "out of memory");
			return 0;
		}
		SIZE_T finalSize = 0;
		BOOL ok = Compress(compressor, chunk, chunkLen, buf, compressedSize, &finalSize);
		lua_pop(L, 1);
		if (!ok) {
			gff_free(buf);
			CloseCompressor(compressor);
			lua_pop(L, 1);
			luaL_error(L, "Compress failed (%d)", (int)GetLastError());
			return 0;
		}
		DWORD hdr[2] = { (DWORD)chunkLen, (DWORD)finalSize };
		StreamWrite(L, dst, (const BYTE*)hdr, sizeof(hdr));
		StreamWrite(L, dst, buf, (size_t)finalSize);
		gff_free(buf);
	}
	CloseCompressor(compressor);
	if (ownDst)
		StreamSetPosC(L, dst, 0);
	return 1;
}

int DecompressStream(lua_State* L) {
	LuaStream* src = lua_toluastream(L, 1);
	if (!(src->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		lua_pushstring(L, "stream is not readable");
		return 2;
	}
	DWORD algorithm = (DWORD)luaL_optinteger(L, 2, COMPRESS_ALGORITHM_MSZIP);
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
	DECOMPRESSOR_HANDLE decompressor = NULL;
	if (!CreateDecompressor(algorithm, NULL, &decompressor)) {
		lua_pop(L, 1);
		luaL_error(L, "CreateDecompressor failed (%d)", (int)GetLastError());
		return 0;
	}
	bool first = true;
	for (;;) {
		size_t hdrLen = 0;
		const BYTE* hdrData = StreamRead(L, src, sizeof(DWORD) * 2, &hdrLen);
		if (!hdrData || hdrLen < sizeof(DWORD) * 2) {
			lua_pop(L, 1);
			break;
		}
		DWORD uncompressedSize, compressedSize;
		memcpy(&uncompressedSize, hdrData, sizeof(DWORD));
		memcpy(&compressedSize, hdrData + sizeof(DWORD), sizeof(DWORD));
		lua_pop(L, 1);
		if (uncompressedSize == 0 || compressedSize == 0)
			break;
		size_t compLen = 0;
		const BYTE* compData = StreamRead(L, src, (size_t)compressedSize, &compLen);
		if (!compData || compLen != (size_t)compressedSize) {
			lua_pop(L, 1);
			CloseDecompressor(decompressor);
			lua_pop(L, 1);
			luaL_error(L, "truncated compressed stream");
			return 0;
		}
		if (!first && !ResetDecompressor(decompressor)) {
			lua_pop(L, 1);
			CloseDecompressor(decompressor);
			lua_pop(L, 1);
			luaL_error(L, "ResetDecompressor failed (%d)", (int)GetLastError());
			return 0;
		}
		first = false;
		BYTE* buf = (BYTE*)gff_malloc((size_t)uncompressedSize);
		if (!buf) {
			lua_pop(L, 1);
			CloseDecompressor(decompressor);
			lua_pop(L, 1);
			luaL_error(L, "out of memory");
			return 0;
		}
		SIZE_T finalSize = 0;
		BOOL ok = Decompress(decompressor, compData, (SIZE_T)compressedSize, buf, (SIZE_T)uncompressedSize, &finalSize);
		lua_pop(L, 1);
		if (!ok) {
			gff_free(buf);
			CloseDecompressor(decompressor);
			lua_pop(L, 1);
			luaL_error(L, "Decompress failed (%d)", (int)GetLastError());
			return 0;
		}
		StreamWrite(L, dst, buf, (size_t)finalSize);
		gff_free(buf);
	}
	CloseDecompressor(decompressor);
	if (ownDst)
		StreamSetPosC(L, dst, 0);
	return 1;
}
