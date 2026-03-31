#include "stream_inmemory.h"
#include <string.h>
#include <stdlib.h>
#include <windows.h>
#include <stdio.h>
#include <compressapi.h>
#include <cstdint>
#include <aclapi.h>

#pragma comment(lib, "Cabinet.lib")

LuaStream* lua_pushluastream(lua_State* L) {
	LuaStream* stream = (LuaStream*)lua_newuserdata(L, sizeof(LuaStream));
	luaL_getmetatable(L, STREAM);
	lua_setmetatable(L, -2);
	memset(stream, 0, sizeof(LuaStream));
	stream->backendRef = LUA_NOREF;
	return stream;
}

LuaStream* lua_pushluastream(lua_State* L, const BYTE* data, size_t len) {
	LuaStream* stream = lua_pushluastream(L);
	size_t alloc = len > MIN_STREAM_SIZE ? len : MIN_STREAM_SIZE;
	stream->data = (BYTE*)gff_malloc(alloc);
	if (!stream->data) {
		luaL_error(L, "stream: out of memory");
		return NULL;
	}
	memcpy(stream->data, data, len);
	stream->alloc = alloc;
	stream->len   = len;
	stream->caps  = HEAP_STREAM_CAPS;
	set_heap_backend(L, stream);
	return stream;
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
	LuaStream* stream = (LuaStream*)luaL_checkudata(L, index, STREAM);
	if (!stream)
		luaL_error(L, "parameter is not a %s", STREAM);
	return stream;
}

int luastream_gc(lua_State* L) {
	LuaStream* stream = lua_toluastream(L, 1);
	if (stream->backendRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, stream->backendRef);
		lua_pushinteger(L, STREAM_OP_CLOSE);
		lua_pcall_nohook(L, 1, 0, 0);
		luaL_unref(L, LUA_REGISTRYINDEX, stream->backendRef);
		stream->backendRef = LUA_NOREF;
	}
	if (stream->data) {
		gff_free(stream->data);
		stream->data = NULL;
	}
	memset(stream, 0, sizeof(LuaStream));
	stream->backendRef = LUA_NOREF;
	return 0;
}

int luastream_tostring(lua_State* L) {
	return ReadLuaStream(L);
}

int NewStream(lua_State* L) {
	if (lua_isfunction(L, 1)) {
		// Custom (non-addressable) backend: Stream.Create(function(op, ...) ... end)
		LuaStream* stream = lua_pushluastream(L);
		lua_pushvalue(L, 1);
		stream->backendRef = luaL_ref(L, LUA_REGISTRYINDEX);
		lua_rawgeti(L, LUA_REGISTRYINDEX, stream->backendRef);
		lua_pushinteger(L, STREAM_OP_OPEN);
		if (lua_pcall_nohook(L, 1, 1, 0) == LUA_OK) {
			stream->caps = (BYTE)lua_tointeger(L, -1);
			lua_pop(L, 1);
		}
		return 1;
	}
	// Heap stream
	size_t init = (size_t)luaL_optinteger(L, 1, (lua_Integer)MIN_STREAM_SIZE);
	if (init < MIN_STREAM_SIZE)
		init = MIN_STREAM_SIZE;
	LuaStream* stream = lua_pushluastream(L);
	stream->data = (BYTE*)gff_malloc(init);
	if (!stream->data) {
		luaL_error(L, "stream: out of memory");
		return 0;
	}
	stream->alloc = init;
	stream->caps  = HEAP_STREAM_CAPS;
	set_heap_backend(L, stream);
	return 1;
}

int NewStreamFromString(lua_State* L) {
	size_t len;
	const char* data;
	if (lua_isstring(L, 1)) {
		data = lua_tolstring(L, 1, &len);
	}
	else {
		data = luaL_tolstring(L, 1, &len);
		lua_pop(L, 1);
	}
	lua_pushluastream(L, (const BYTE*)data, len);
	return 1;
}

int OpenFileToStream(lua_State* L) {
	const char* file = luaL_checkstring(L, 1);
	FILE* f = fopen(file, "rb");
	if (!f) {
		luaL_error(L, "unable to open file: %s", file);
		return 0;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		luaL_error(L, "unable to seek in file: %s", file);
		return 0;
	}
	long fileSize = ftell(f);
	rewind(f);
	size_t alloc = (fileSize > 0 ? (size_t)fileSize : 0);
	if (alloc < MIN_STREAM_SIZE)
		alloc = MIN_STREAM_SIZE;
	LuaStream* stream = lua_pushluastream(L);
	stream->data = (BYTE*)gff_malloc(alloc);
	if (!stream->data) {
		fclose(f);
		luaL_error(L, "stream: out of memory");
		return 0;
	}
	stream->alloc = alloc;
	stream->caps  = HEAP_STREAM_CAPS;
	stream->len   = (fileSize > 0) ? fread(stream->data, 1, (size_t)fileSize, f) : 0;
	fclose(f);
	set_heap_backend(L, stream);
	return 1;
}

int StreamPos(lua_State* L) {
	lua_pushinteger(L, (lua_Integer)lua_toluastream(L, 1)->pos);
	return 1;
}

int StreamLen(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	lua_pushinteger(L, (lua_Integer)s->len);
	lua_pushinteger(L, (lua_Integer)s->alloc);
	return 2;
}

int GetStreamInfo(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!s->data) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, (lua_Integer)s->pos);
	lua_pushinteger(L, (lua_Integer)s->len);
	lua_pushinteger(L, (lua_Integer)s->alloc);
	return 3;
}

int StreamSetPos(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!s->data && s->backendRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->backendRef);
		lua_pushinteger(L, STREAM_OP_SEEK);
		lua_pushinteger(L, luaL_optinteger(L, 2, 0));
		if (lua_pcall_nohook(L, 2, 1, 0) != LUA_OK)
			lua_pushboolean(L, false);
		return 1;
	}
	size_t newpos = (size_t)luaL_optinteger(L, 2, 0);
	if (newpos > s->len)
		newpos = s->len;
	s->pos = newpos;
	return 0;
}

int SetLength(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	size_t size = (size_t)luaL_checkinteger(L, 2);
	if (size > s->alloc)
		size = s->alloc;
	s->len = size;
	s->pos = 0;
	return 0;
}

int StreamShrink(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (s->pos == 0)
		return 0;
	if (s->pos >= s->len) {
		s->len = 0;
		s->pos = 0;
	}
	else {
		size_t remaining = s->len - s->pos;
		memcpy(s->data, &s->data[s->pos], remaining);
		s->len = remaining;
		s->pos = 0;
	}
	return 0;
}

int StreamIndexOf(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!s->data) {
		lua_pushnil(L);
		return 1;
	}
	if (lua_type(L, 2) == LUA_TSTRING) {
		size_t len;
		const char* data = lua_tolstring(L, 2, &len);
		if (len == 0 || len > s->len - s->pos) {
			lua_pushnil(L);
			return 1;
		}
		for (size_t i = s->pos; i + len <= s->len; i++) {
			if (memcmp(&s->data[i], data, len) == 0) {
				lua_pushinteger(L, (lua_Integer)i);
				return 1;
			}
		}
	}
	else {
		int raw = (int)luaL_optinteger(L, 2, 0);
		if (raw < 0 || raw > 255) {
			luaL_error(L, "parameter 2 must be a string or a byte value 0-255");
			return 0;
		}
		BYTE lookfor = (BYTE)raw;
		for (size_t i = s->pos; i < s->len; i++) {
			if (s->data[i] == lookfor) {
				lua_pushinteger(L, (lua_Integer)i);
				return 1;
			}
		}
	}
	lua_pushnil(L);
	return 1;
}

int ReadLuaStream(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!s->data && s->backendRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->backendRef);
		lua_pushinteger(L, STREAM_OP_READ);
		lua_pushinteger(L, luaL_optinteger(L, 2, 0));
		if (lua_pcall_nohook(L, 2, 1, 0) != LUA_OK)
			lua_pushnil(L);
		return 1;
	}
	if (s->pos >= s->len) {
		lua_pushnil(L);
		return 1;
	}
	size_t len = (size_t)luaL_optinteger(L, 2, (lua_Integer)(s->len - s->pos));
	if (len > s->len - s->pos)
		len = s->len - s->pos;
	const BYTE* result = StreamRead(s, len);
	if (!result) {
		lua_pushlstring(L, "", 0);
		return 1;
	}
	lua_pushlstring(L, (const char*)result, len);
	return 1;
}

int ReadUntilLuaStream(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (s->pos >= s->len) {
		lua_pushnil(L);
		return 1;
	}
	int raw = (int)luaL_checkinteger(L, 2);
	if (raw < 0 || raw > 255) {
		luaL_error(L, "parameter 2 must be a byte value 0-255");
		return 0;
	}
	BYTE find = (BYTE)raw;
	size_t end = s->len;
	for (size_t i = s->pos; i < s->len; i++) {
		if (s->data[i] == find) {
			end = i;
			break;
		}
	}
	size_t len = end - s->pos;
	if (len == 0) {
		lua_pushlstring(L, "", 0);
		return 1;
	}
	const BYTE* result = StreamRead(s, len);
	lua_pushlstring(L, (const char*)result, len);
	return 1;
}

int ReadStreamByte(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	const BYTE* r = StreamRead(s, 1);
	lua_pushinteger(L, r ? (lua_Integer)*r : -1);
	return 1;
}

int PeekStreamByte(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	size_t pos = (size_t)luaL_optinteger(L, 2, (lua_Integer)s->pos);
	lua_pushinteger(L, pos < s->len ? (lua_Integer)s->data[pos] : -1);
	return 1;
}

int ReadFloat(lua_State* L) {
	const BYTE* r = StreamRead(lua_toluastream(L, 1), sizeof(float));
	if (!r) {
		lua_pushnil(L);
		return 1;
	}
	float v;
	memcpy(&v, r, sizeof(float));
	lua_pushnumber(L, (lua_Number)v);
	return 1;
}

int ReadDouble(lua_State* L) {
	const BYTE* r = StreamRead(lua_toluastream(L, 1), sizeof(double));
	if (!r) {
		lua_pushnil(L);
		return 1;
	}
	double v;
	memcpy(&v, r, sizeof(double));
	lua_pushnumber(L, (lua_Number)v);
	return 1;
}

int ReadShort(lua_State* L) {
	const BYTE* r = StreamRead(lua_toluastream(L, 1), sizeof(short));
	if (!r) {
		lua_pushnil(L);
		return 1;
	}
	short v;
	memcpy(&v, r, sizeof(short));
	lua_pushinteger(L, (lua_Integer)v);
	return 1;
}

int ReadUShort(lua_State* L) {
	const BYTE* r = StreamRead(lua_toluastream(L, 1), sizeof(unsigned short));
	if (!r) {
		lua_pushnil(L);
		return 1;
	}
	unsigned short v;
	memcpy(&v, r, sizeof(unsigned short));
	lua_pushinteger(L, (lua_Integer)v);
	return 1;
}

int ReadInt(lua_State* L) {
	const BYTE* r = StreamRead(lua_toluastream(L, 1), sizeof(int));
	if (!r) {
		lua_pushnil(L);
		return 1;
	}
	int v;
	memcpy(&v, r, sizeof(int));
	lua_pushinteger(L, (lua_Integer)v);
	return 1;
}

int ReadUInt(lua_State* L) {
	const BYTE* r = StreamRead(lua_toluastream(L, 1), sizeof(unsigned int));
	if (!r) {
		lua_pushnil(L);
		return 1;
	}
	unsigned int v;
	memcpy(&v, r, sizeof(unsigned int));
	lua_pushinteger(L, (lua_Integer)v);
	return 1;
}

int ReadLong(lua_State* L) {
	const BYTE* r = StreamRead(lua_toluastream(L, 1), sizeof(long long));
	if (!r) {
		lua_pushnil(L);
		return 1;
	}
	long long v;
	memcpy(&v, r, sizeof(long long));
	lua_pushinteger(L, (lua_Integer)v);
	return 1;
}

int ReadUnsignedLong(lua_State* L) {
	const BYTE* r = StreamRead(lua_toluastream(L, 1), sizeof(unsigned long long));
	if (!r) {
		lua_pushnil(L);
		return 1;
	}
	unsigned long long v;
	memcpy(&v, r, sizeof(unsigned long long));
	lua_pushinteger(L, (lua_Integer)v);
	return 1;
}

int ReadUtf8(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	long avail = (long)(s->len - s->pos);
	if (!s->data || avail <= 0) {
		lua_pushnil(L);
		return 1;
	}
	BYTE first = s->data[s->pos];
	DWORD code  = 0;
	DWORD shift = 0;
	if ((first & 0xF8) == 0xF0) {
		if (avail < 4 ||
			(s->data[s->pos + 1] & 0xC0) != 0x80 ||
			(s->data[s->pos + 2] & 0xC0) != 0x80 ||
			(s->data[s->pos + 3] & 0xC0) != 0x80) {
			lua_pushnil(L);
			s->pos += min(4, (size_t)avail);
			return 1;
		}
		memcpy(&code, &s->data[s->pos], 4);
		lua_pushlstring(L, (const char*)&code, 4);
		code  = s->data[s->pos + 3] & 0x3F;
		shift = s->data[s->pos + 2] << 6;  code |= (shift & 0xFC0);
		shift = s->data[s->pos + 1] << 12; code |= (shift & 0x3F000);
		shift = s->data[s->pos]     << 18; code |= (shift & 0x1C0000);
		code &= 0x1FFFFF;
		s->pos += 4;
	}
	else if ((first & 0xF0) == 0xE0) {
		if (avail < 3 ||
			(s->data[s->pos + 1] & 0xC0) != 0x80 ||
			(s->data[s->pos + 2] & 0xC0) != 0x80) {
			lua_pushnil(L);
			s->pos += min(3, (size_t)avail);
			return 1;
		}
		memcpy(&code, &s->data[s->pos], 3);
		lua_pushlstring(L, (const char*)&code, 3);
		code  = s->data[s->pos + 2] & 0x3F;
		shift = s->data[s->pos + 1] << 6;  code |= (shift & 0xFC0);
		shift = s->data[s->pos]     << 12; code |= (shift & 0xF000);
		code &= 0xFFFF;
		s->pos += 3;
	}
	else if ((first & 0xE0) == 0xC0) {
		if (avail < 2 || (s->data[s->pos + 1] & 0xC0) != 0x80) {
			lua_pushnil(L);
			s->pos += min(2, (size_t)avail);
			return 1;
		}
		memcpy(&code, &s->data[s->pos], 2);
		lua_pushlstring(L, (const char*)&code, 2);
		code  = s->data[s->pos + 1] & 0x3F;
		shift = s->data[s->pos]     << 6;
		code |= shift;
		code &= 0x7FF;
		s->pos += 2;
	}
	else if ((first & 0x80) == 0x0) {
		memcpy(&code, &s->data[s->pos], 1);
		lua_pushlstring(L, (const char*)&code, 1);
		s->pos += 1;
	}
	else {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, (lua_Integer)code);
	return 2;
}

int WriteStreamByte(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	int byte = (int)lua_tointeger(L, 2);
	if (byte < 0 || byte > 255) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "byte value out of range 0-255");
		return 2;
	}
	BYTE raw = (BYTE)byte;
	lua_pushboolean(L, StreamWrite(L, s, &raw, 1));
	return 1;
}

int SetStreamByte(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	BYTE value = (BYTE)lua_tointeger(L, 2);
	size_t pos = (size_t)luaL_optinteger(L, 3, (lua_Integer)s->pos);
	if (pos < s->len)
		s->data[pos] = value;
	return 0;
}

int StreamBuffer(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	size_t len;
	const char* data = lua_tolstring(L, 2, &len);
	if (!data || len == 0) {
		lua_pushinteger(L, 0);
		return 1;
	}
	size_t saved = s->pos;
	s->pos = s->len;
	bool ok = StreamWrite(L, s, (const BYTE*)data, len);
	s->pos = saved;
	lua_pushinteger(L, ok ? (lua_Integer)len : 0);
	return 1;
}

int WriteFloat(lua_State* L) {
	float v = (float)lua_tonumber(L, 2);
	lua_pushboolean(L, StreamWrite(L, lua_toluastream(L, 1), (const BYTE*)&v, sizeof(float)));
	return 1;
}

int WriteDouble(lua_State* L) {
	double v = (double)lua_tonumber(L, 2);
	lua_pushboolean(L, StreamWrite(L, lua_toluastream(L, 1), (const BYTE*)&v, sizeof(double)));
	return 1;
}

int WriteShort(lua_State* L) {
	short v = (short)lua_tointeger(L, 2);
	lua_pushboolean(L, StreamWrite(L, lua_toluastream(L, 1), (const BYTE*)&v, sizeof(short)));
	return 1;
}

int WriteUShort(lua_State* L) {
	unsigned short v = (unsigned short)lua_tointeger(L, 2);
	lua_pushboolean(L, StreamWrite(L, lua_toluastream(L, 1), (const BYTE*)&v, sizeof(unsigned short)));
	return 1;
}

int WriteInt(lua_State* L) {
	int v = (int)lua_tointeger(L, 2);
	lua_pushboolean(L, StreamWrite(L, lua_toluastream(L, 1), (const BYTE*)&v, sizeof(int)));
	return 1;
}

int WriteUInt(lua_State* L) {
	unsigned int v = (unsigned int)lua_tointeger(L, 2);
	lua_pushboolean(L, StreamWrite(L, lua_toluastream(L, 1), (const BYTE*)&v, sizeof(unsigned int)));
	return 1;
}

int WriteLong(lua_State* L) {
	long long v = (long long)lua_tointeger(L, 2);
	lua_pushboolean(L, StreamWrite(L, lua_toluastream(L, 1), (const BYTE*)&v, sizeof(long long)));
	return 1;
}

int WriteUnsignedLong(lua_State* L) {
	unsigned long long v = (unsigned long long)lua_tointeger(L, 2);
	lua_pushboolean(L, StreamWrite(L, lua_toluastream(L, 1), (const BYTE*)&v, sizeof(unsigned long long)));
	return 1;
}

int WriteLuaValue(lua_State* L) {
	LuaStream* stream = lua_toluastream(L, 1);
	if (!stream->data && stream->backendRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, stream->backendRef);
		lua_pushinteger(L, STREAM_OP_WRITE);
		lua_pushvalue(L, 2);
		if (lua_pcall_nohook(L, 2, 1, 0) != LUA_OK)
			lua_pushinteger(L, 0);
		return 1;
	}
	size_t limit = (size_t)luaL_optinteger(L, 3, 0);
	const BYTE* raw = NULL;
	size_t len = 0;
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
	case LUA_TUSERDATA: {
		LuaStream* other = (LuaStream*)luaL_testudata(L, 2, STREAM);
		if (other && other != stream && other->data) {
			raw = &other->data[other->pos];
			len = other->len - other->pos;
		}
		break;
	}
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
	size_t len;
	const char* str = luaL_checklstring(L, 2, &len);
	BYTE* buf = (BYTE*)gff_malloc(len * 2 + 1);
	if (!buf) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "out of memory");
		return 2;
	}
	unsigned char* in  = (unsigned char*)str;
	unsigned char* out = (unsigned char*)buf;
	while (*in)
		if (*in < 128) *out++ = *in++;
		else *out++ = 0xc2 + (*in > 0xbf), *out++ = (*in++ & 0x3f) + 0x80;
	bool ok = StreamWrite(L, s, buf, (size_t)(out - buf));
	gff_free(buf);
	lua_pushboolean(L, ok);
	return 1;
}

int ReadFromFile(lua_State* L) {
	LuaStream* stream = lua_toluastream(L, 1);
	const char* file  = luaL_checkstring(L, 2);
	size_t pos = (size_t)luaL_checkinteger(L, 3);
	size_t len = (size_t)luaL_checkinteger(L, 4);
	FILE* f = fopen(file, "rb");
	if (!f) {
		luaL_error(L, "unable to open file: %s", file);
		return 0;
	}
	if (fseek(f, (long)pos, SEEK_SET) != 0) {
		fclose(f);
		luaL_error(L, "unable to seek in file: %s", file);
		return 0;
	}
	BYTE* temp = (BYTE*)gff_malloc(len);
	if (!temp) {
		fclose(f);
		luaL_error(L, "stream: out of memory");
		return 0;
	}
	fread(temp, 1, len, f);
	fclose(f);
	bool ok = StreamWrite(L, stream, temp, len);
	gff_free(temp);
	if (!ok) {
		luaL_error(L, "unable to write to stream");
		return 0;
	}
	return 0;
}

int WriteToFile(lua_State* L) {
	LuaStream* stream = lua_toluastream(L, 1);
	const char* file  = luaL_checkstring(L, 2);
	size_t pos = (size_t)luaL_checkinteger(L, 3);
	size_t len = (size_t)luaL_checkinteger(L, 4);
	const BYTE* data = StreamRead(stream, len);
	if (!data) {
		luaL_error(L, "stream out of bounds");
		return 0;
	}
	FILE* f = fopen(file, "r+b");
	if (!f) {
		luaL_error(L, "unable to open file: %s", file);
		return 0;
	}
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		luaL_error(L, "unable to seek in file: %s", file);
		return 0;
	}
	size_t fileSize = (size_t)ftell(f);
	if (pos > fileSize) {
		while (pos > fileSize) {
			if (fputc('\0', f) == EOF) {
				fclose(f);
				luaL_error(L, "unable to write padding to file");
				return 0;
			}
			fileSize++;
		}
	}
	else {
		if (fseek(f, (long)pos, SEEK_SET) != 0) {
			fclose(f);
			luaL_error(L, "unable to seek in file: %s", file);
			return 0;
		}
	}
	fwrite(data, 1, len, f);
	fflush(f);
	fclose(f);
	return 0;
}

int DumpToFile(lua_State* L) {
	LuaStream* stream = lua_toluastream(L, 1);
	const char* file  = luaL_checkstring(L, 2);
	FILE* f = fopen(file, "wb");
	if (!f) {
		luaL_error(L, "unable to open file: %s", file);
		return 0;
	}
	fwrite(stream->data, 1, stream->len, f);
	fflush(f);
	fclose(f);
	return 0;
}

int Compress(lua_State* L) {
	LuaStream* stream = lua_toluastream(L, 1);
	DWORD algo = (DWORD)luaL_optinteger(L, 2, COMPRESS_ALGORITHM_MSZIP);
	COMPRESSOR_HANDLE hComp = NULL;
	SIZE_T compressedSize;
	if (!CreateCompressor(algo, NULL, &hComp)) {
		lua_pushnil(L);
		lua_pushinteger(L, (lua_Integer)GetLastError());
		return 2;
	}
	if (!Compress(hComp, stream->data, stream->len, NULL, 0, &compressedSize) &&
		GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
		CloseCompressor(hComp);
		lua_pushnil(L);
		lua_pushinteger(L, (lua_Integer)GetLastError());
		return 2;
	}
	LuaStream* out = lua_pushluastream(L);
	out->data = (BYTE*)gff_malloc(compressedSize);
	if (!out->data) {
		CloseCompressor(hComp);
		memset(out, 0, sizeof(LuaStream));
		out->backendRef = LUA_NOREF;
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushinteger(L, ERROR_INSUFFICIENT_BUFFER);
		return 2;
	}
	out->alloc = compressedSize;
	out->caps  = HEAP_STREAM_CAPS;
	if (!Compress(hComp, stream->data, stream->len, out->data, out->alloc, &compressedSize)) {
		CloseCompressor(hComp);
		gff_free(out->data);
		memset(out, 0, sizeof(LuaStream));
		out->backendRef = LUA_NOREF;
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushinteger(L, (lua_Integer)GetLastError());
		return 2;
	}
	out->len = compressedSize;
	set_heap_backend(L, out);
	CloseCompressor(hComp);
	return 1;
}

int Decompress(lua_State* L) {
	LuaStream* stream = lua_toluastream(L, 1);
	DWORD algo = (DWORD)luaL_optinteger(L, 2, COMPRESS_ALGORITHM_MSZIP);
	DECOMPRESSOR_HANDLE hDecomp = NULL;
	SIZE_T decompressedSize;
	if (!CreateDecompressor(algo, NULL, &hDecomp)) {
		lua_pushnil(L);
		lua_pushinteger(L, (lua_Integer)GetLastError());
		return 2;
	}
	if (!Decompress(hDecomp, stream->data, stream->len, NULL, 0, &decompressedSize) &&
		GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
		CloseDecompressor(hDecomp);
		lua_pushnil(L);
		lua_pushinteger(L, (lua_Integer)GetLastError());
		return 2;
	}
	LuaStream* out = lua_pushluastream(L);
	out->data = (BYTE*)gff_malloc(decompressedSize);
	if (!out->data) {
		CloseDecompressor(hDecomp);
		memset(out, 0, sizeof(LuaStream));
		out->backendRef = LUA_NOREF;
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushinteger(L, ERROR_INSUFFICIENT_BUFFER);
		return 2;
	}
	out->alloc = decompressedSize;
	out->caps  = HEAP_STREAM_CAPS;
	if (!Decompress(hDecomp, stream->data, stream->len, out->data, out->alloc, &decompressedSize)) {
		CloseDecompressor(hDecomp);
		gff_free(out->data);
		memset(out, 0, sizeof(LuaStream));
		out->backendRef = LUA_NOREF;
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushinteger(L, (lua_Integer)GetLastError());
		return 2;
	}
	out->len = decompressedSize;
	set_heap_backend(L, out);
	CloseDecompressor(hDecomp);
	return 1;
}

int GetSharedMemoryStreamInfo(lua_State* L) {
	const char* name = luaL_checkstring(L, 1);
	HANDLE h = OpenFileMapping(FILE_MAP_READ, FALSE, name);
	if (!h) {
		lua_pushnil(L);
		lua_pushinteger(L, (lua_Integer)GetLastError());
		return 2;
	}
	void* ptr = MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);
	if (!ptr) {
		CloseHandle(h);
		lua_pushnil(L);
		lua_pushinteger(L, (lua_Integer)GetLastError());
		return 2;
	}
	MEMORY_BASIC_INFORMATION info;
	SIZE_T queried = VirtualQuery(ptr, &info, sizeof(info));
	UnmapViewOfFile(ptr);
	CloseHandle(h);
	if (queried < sizeof(info)) {
		lua_pushnil(L);
		lua_pushinteger(L, (lua_Integer)GetLastError());
		return 2;
	}
	lua_createtable(L, 0, 7);
	lua_pushinteger(L, (lua_Integer)(uintptr_t)info.AllocationBase);  lua_setfield(L, -2, "AllocationBase");
	lua_pushinteger(L, (lua_Integer)info.AllocationProtect);          lua_setfield(L, -2, "AllocationProtect");
	lua_pushinteger(L, (lua_Integer)(uintptr_t)info.BaseAddress);     lua_setfield(L, -2, "BaseAddress");
	lua_pushinteger(L, (lua_Integer)info.Protect);                    lua_setfield(L, -2, "Protect");
	lua_pushinteger(L, (lua_Integer)info.RegionSize);                 lua_setfield(L, -2, "RegionSize");
	lua_pushinteger(L, (lua_Integer)info.State);                      lua_setfield(L, -2, "State");
	lua_pushinteger(L, (lua_Integer)info.Type);                       lua_setfield(L, -2, "Type");
	return 1;
}

