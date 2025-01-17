#include "stream.h"
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h> 
#include <windows.h> 
#include <stdio.h>
#include <compressapi.h>
#include <cstdint>
#include <aclapi.h>

#pragma comment(lib, "Cabinet.lib")

const size_t MIN_STREAM_SIZE = 1024;

size_t AllocAddSize(lua_State* L, LuaStream* stream, size_t requestedsize) {

	if (stream && stream->hSharedMemory) {
		luaL_error(L, "Shared memory full");
		return 0;
	}

	if (stream && stream->allocfunc != LUA_NOREF) {

		lua_rawgeti(L, LUA_REGISTRYINDEX, stream->allocfunc);
		lua_pushinteger(L, requestedsize);

		if (lua_pcall(L, 1, 1, NULL) != 0) {

			lua_error(L);
			return 0;
		}

		if (lua_type(L, -1) == LUA_TNUMBER) {
			size_t t = (size_t)lua_tointeger(L, -1);
			lua_pop(L, 1);
			return t;
		}
	}

	return MIN_STREAM_SIZE;
}

bool CheckStreamSize(lua_State* L, LuaStream* stream, size_t requestedsize) {

	if (!stream->data) {
		return false;
	}
	else if (stream->pos + requestedsize > stream->len) {
		requestedsize = (stream->pos + requestedsize) - stream->len;
	}
	else if (stream->pos + requestedsize <= stream->len) {
		return true;
	}

	if (stream->len + requestedsize > stream->alloc) {

		size_t ne = AllocAddSize(L, stream, requestedsize);

		if (ne > requestedsize) {
			requestedsize = ne;
		}

		size_t newsize = (stream->len + requestedsize) - stream->alloc;

		if (ne == 0 && newsize < MIN_STREAM_SIZE) {
			newsize = MIN_STREAM_SIZE;
		}

		void* temp = gff_realloc(stream->data, stream->alloc + newsize);
		if (!temp) {
			return false;
		}
		else {
			stream->data = (BYTE*)temp;
			stream->alloc = stream->alloc + newsize;
			return true;
		}
	}
	else {
		return true;
	}
}

int SetLength(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	size_t size = (size_t)luaL_checkinteger(L, 2);

	if (size > stream->alloc) {
		size = stream->alloc;
	}
	else if (size < 0) {
		size = 0;
	}

	stream->len = size;
	stream->pos = 0;

	return 0;
}

bool StreamWrite(lua_State* L, LuaStream* stream, BYTE* data, size_t len) {

	if (!stream || !stream->data) {
		return false;
	}
	else if (!CheckStreamSize(L, stream, len)) {
		return false;
	}

	memcpy(&stream->data[stream->pos], data, len);

	long mod = (stream->pos + len) - stream->len;

	if (mod > 0) {
		stream->len += mod;
	}

	stream->pos += len;

	return true;
}

int StreamBuffer(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	size_t len;
	size_t current = stream->pos;
	const char* data = lua_tolstring(L, 2, &len);

	if (len <= 0 || !data) {
		lua_pop(L, lua_gettop(L));
		lua_pushinteger(L, 0);
		return 1;
	}

	stream->pos = stream->len;

	if (StreamWrite(L, stream, (BYTE*)data, len)) {
		lua_pop(L, lua_gettop(L));
		lua_pushinteger(L, len);
	}
	else {
		lua_pop(L, lua_gettop(L));
		lua_pushinteger(L, 0);
	}

	stream->pos = current;

	return 1;
}

const BYTE* ReadStream(LuaStream* stream, size_t len) {

	if (len <= 0 || !stream || !stream->data || (stream->pos + len) > stream->len) {
		return NULL;
	}

	const BYTE* result = &stream->data[stream->pos];
	stream->pos += len;

	return result;
}

int WriteFloat(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	float f = (float)lua_tonumber(L, 2);

	lua_pop(L, lua_gettop(L));

	lua_pushboolean(L, StreamWrite(L, stream, (BYTE*)&f, sizeof(float)));

	return 1;
}

int ReadFloat(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);

	const BYTE* raw = ReadStream(stream, sizeof(float));

	lua_pop(L, lua_gettop(L));

	if (!raw) {
		lua_pushnil(L);
	}
	else {
		float f;
		memcpy(&f, raw, sizeof(float));
		lua_pushnumber(L, f);
	}

	return 1;
}

int WriteDouble(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	double f = lua_tonumber(L, 2);

	lua_pop(L, lua_gettop(L));

	lua_pushboolean(L, StreamWrite(L, stream, (BYTE*)&f, sizeof(double)));

	return 1;
}

int ReadDouble(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);

	const BYTE* raw = ReadStream(stream, sizeof(double));

	lua_pop(L, lua_gettop(L));

	if (!raw) {
		lua_pushnil(L);
	}
	else {
		double f;
		memcpy(&f, raw, sizeof(double));
		lua_pushnumber(L, f);
	}

	return 1;
}

int WriteShort(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	short n = (short)lua_tointeger(L, 2);

	lua_pop(L, lua_gettop(L));

	lua_pushboolean(L, StreamWrite(L, stream, (BYTE*)&n, sizeof(short)));

	return 1;
}

int ReadShort(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);

	const BYTE* raw = ReadStream(stream, sizeof(short));

	lua_pop(L, lua_gettop(L));

	if (!raw) {
		lua_pushnil(L);
	}
	else {
		short f;
		memcpy(&f, raw, sizeof(short));
		lua_pushinteger(L, f);
	}

	return 1;
}

int WriteUShort(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	unsigned short n = (unsigned short)lua_tointeger(L, 2);

	lua_pop(L, lua_gettop(L));

	lua_pushboolean(L, StreamWrite(L, stream, (BYTE*)&n, sizeof(unsigned short)));

	return 1;
}

int ReadUShort(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);

	const BYTE* raw = ReadStream(stream, sizeof(unsigned short));

	lua_pop(L, lua_gettop(L));

	if (!raw) {
		lua_pushnil(L);
	}
	else {
		short f;
		memcpy(&f, raw, sizeof(unsigned short));
		lua_pushinteger(L, f);
	}

	return 1;
}

int WriteInt(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	int n = (int)lua_tointeger(L, 2);

	lua_pop(L, lua_gettop(L));

	lua_pushboolean(L, StreamWrite(L, stream, (BYTE*)&n, sizeof(int)));

	return 1;
}

int ReadInt(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);

	const BYTE* raw = ReadStream(stream, sizeof(int));

	lua_pop(L, lua_gettop(L));

	if (!raw) {
		lua_pushnil(L);
	}
	else {
		int f;
		memcpy(&f, raw, sizeof(int));
		lua_pushinteger(L, f);
	}

	return 1;
}

int WriteUInt(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	unsigned int n = (unsigned int)lua_tointeger(L, 2);

	lua_pop(L, lua_gettop(L));

	lua_pushboolean(L, StreamWrite(L, stream, (BYTE*)&n, sizeof(unsigned int)));

	return 1;
}

int ReadUInt(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);

	const BYTE* raw = ReadStream(stream, sizeof(unsigned int));

	lua_pop(L, lua_gettop(L));

	if (!raw) {
		lua_pushnil(L);
	}
	else {
		unsigned int f;
		memcpy(&f, raw, sizeof(unsigned int));
		lua_pushinteger(L, f);
	}

	return 1;
}

int ReadLong(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);

	const BYTE* raw = ReadStream(stream, sizeof(long long));

	lua_pop(L, lua_gettop(L));

	if (!raw) {
		lua_pushnil(L);
	}
	else {
		long long f;
		memcpy(&f, raw, sizeof(long long));
		lua_pushinteger(L, f);
	}

	return 1;
}

int WriteLong(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	long long n = lua_tointeger(L, 2);

	lua_pop(L, lua_gettop(L));

	lua_pushboolean(L, StreamWrite(L, stream, (BYTE*)&n, sizeof(long long)));

	return 1;
}

int WriteUnsignedLong(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	unsigned long long n = lua_tointeger(L, 2);

	lua_pop(L, lua_gettop(L));

	lua_pushboolean(L, StreamWrite(L, stream, (BYTE*)&n, sizeof(unsigned long long)));

	return 1;
}

int ReadUnsignedLong(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);

	const BYTE* raw = ReadStream(stream, sizeof(unsigned long long));

	lua_pop(L, lua_gettop(L));

	if (!raw) {
		lua_pushnil(L);
	}
	else {
		unsigned long long f;
		memcpy(&f, raw, sizeof(unsigned long long));
		lua_pushinteger(L, f);
	}

	return 1;
}

int StreamSetPos(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	size_t newpos = (size_t)luaL_optinteger(L, 2, 0);

	if (!stream->data) {
		lua_pop(L, lua_gettop(L));
		return 0;
	}

	if (newpos > stream->len) {
		newpos = stream->len;
	}

	if (newpos < 0) {
		newpos = 0;
	}

	stream->pos = newpos;

	lua_pop(L, lua_gettop(L));
	return 0;
}

int GetStreamInfo(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);

	if (!stream->data) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}

	lua_pop(L, lua_gettop(L));

	lua_pushinteger(L, stream->pos);
	lua_pushinteger(L, stream->len);
	lua_pushinteger(L, stream->alloc);

	return 3;
}

int StreamShrink(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);

	if (stream->pos <= 0) {
		return 0;
	}
	else if (stream->pos >= stream->len) {
		stream->len = 0;
		stream->pos = 0;
	}
	else {
		size_t mod = stream->len - stream->pos;
		memcpy(&stream->data[0], &stream->data[stream->pos], mod);
		stream->len = mod;
		stream->pos = 0;
	}

	return 0;
}

int SetStreamByte(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	BYTE data = (BYTE)lua_tointeger(L, 2);
	size_t pos = (size_t)luaL_optinteger(L, 3, stream->pos);

	lua_pop(L, lua_gettop(L));

	if (pos >= stream->len || pos < 0) {
		stream->data[pos] = data;
	}

	return 0;
}

int PeekStreamByte(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	size_t pos = (size_t)luaL_optinteger(L, 2, stream->pos);

	lua_pop(L, lua_gettop(L));

	if (pos >= stream->len || pos < 0) {
		lua_pushinteger(L, -1);
	}
	else {
		lua_pushinteger(L, stream->data[pos]);
	}

	return 1;
}

int StreamLen(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	lua_pop(L, lua_gettop(L));
	lua_pushinteger(L, stream->len);
	lua_pushinteger(L, stream->alloc);
	return 2;
}

int StreamPos(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	lua_pop(L, lua_gettop(L));
	lua_pushinteger(L, stream->pos);
	return 1;
}

int StreamIndexOf(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);

	if (lua_type(L, 2) == LUA_TSTRING) {

		size_t len;
		const char* data = lua_tolstring(L, 2, &len);

		if (len <= 0 || len >= (stream->len - stream->pos)) {
			lua_pushnil(L);
			return 1;
		}

		for (size_t i = stream->pos; i < stream->len - len; i++)
		{
			if (memcmp(&stream->data[i], data, len) == 0) {

				lua_pushinteger(L, i);
				return 1;
			}
		}

		lua_pushnil(L);
		return 1;
	}
	else if (lua_type(L, 2) == LUA_TNUMBER || lua_isnone(L, 2)) {

		int raw = (int)luaL_optinteger(L, 2, 0);

		if (raw < 0 || raw > 255) {
			luaL_error(L, "Parameter 2 must be a string or a byte");
			return 0;
		}

		BYTE lookfor = (BYTE)raw;

		for (size_t i = stream->pos; i < stream->len; i++)
		{
			if (stream->data[i] == lookfor) {

				lua_pushinteger(L, i);
				return 1;
			}
		}

		lua_pushnil(L);
		return 1;
	}
	else {
		lua_pushnil(L);
		return 1;
	}
}

int ReadStreamByte(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	const BYTE* result = ReadStream(stream, 1);

	if (!result) {
		lua_pop(L, lua_gettop(L));
		lua_pushinteger(L, -1);
		return 1;
	}

	lua_pop(L, lua_gettop(L));
	lua_pushinteger(L, *result);

	return 1;
}

int ReadLuaStream(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	size_t len = (size_t)luaL_optinteger(L, 2, stream->len - stream->pos);

	if (stream->pos >= stream->len) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}
	else if (len > stream->len - stream->pos) {
		len = stream->len - stream->pos;
	}

	const BYTE* result = ReadStream(stream, len);

	if (!result) {
		lua_pop(L, lua_gettop(L));
		lua_pushlstring(L, "", 0);
		return 1;
	}

	lua_pop(L, lua_gettop(L));
	lua_pushlstring(L, (char*)result, len);

	return 1;
}

int ReadUntilLuaStream(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	long len = (long)luaL_optinteger(L, 2, 0);

	if (stream->pos >= stream->len) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		return 1;
	}
	else if (len <= 0 || len > 255) {
		luaL_error(L, "Parameter 2 must be a byte 0 - 255");
	}

	BYTE find = (BYTE)len;
	len = stream->len;

	for (size_t i = stream->pos; i < stream->len; i++)
	{
		if (stream->data[i] == find) {
			len = i;
			break;
		}
	}

	len -= stream->pos;

	if (len <= 0) {
		lua_pop(L, lua_gettop(L));
		lua_pushlstring(L, "", 0);
		return 1;
	}

	const BYTE* result = ReadStream(stream, len);

	if (!result) {
		lua_pop(L, lua_gettop(L));
		lua_pushlstring(L, "", 0);
		return 1;
	}

	lua_pop(L, lua_gettop(L));
	lua_pushlstring(L, (char*)result, len);

	return 1;
}

int WriteLuaValue(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	size_t size = (size_t)luaL_optinteger(L, 3, 0);

	LUA_NUMBER number;
	BYTE boolean;
	void* p;
	LuaStream* other;

	BYTE* raw;
	size_t len;

	switch (lua_type(L, 2)) {

	case LUA_TNUMBER:
		number = lua_tonumber(L, 2);
		raw = (BYTE*)&number;
		len = sizeof(LUA_NUMBER);
		break;

	case LUA_TBOOLEAN:
		boolean = lua_toboolean(L, 2);
		raw = &boolean;
		len = 1;
		break;

	case LUA_TSTRING:
		raw = (BYTE*)lua_tolstring(L, 2, &len);
		break;

	case LUA_TUSERDATA:
		p = luaL_testudata(L, 2, STREAM);
		if (p && p != (void*)stream) {
			other = (LuaStream*)p;
			raw = &other->data[other->pos];
			len = other->len - other->pos;
		}
		else {
			raw = NULL;
			len = 0;
		}

		break;

	default:
		len = 0;
		raw = NULL;
		break;
	}

	lua_pop(L, lua_gettop(L));

	if (size > 0 && size < len) {
		len = size;
	}

	if (len <= 0 || !raw || !StreamWrite(L, stream, raw, len)) {
		lua_pushinteger(L, 0);
	}
	else {
		lua_pushinteger(L, len);
	}

	return 1;
}

int WriteStreamByte(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	int byte = (int)lua_tointeger(L, 2);

	if (byte > 255 || byte < 0) {

		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, "Byte out of range, must be between 0 and 255");
		return 2;
	}

	BYTE raw = byte;

	if (!StreamWrite(L, stream, &raw, 1)) {

		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, "Unable to allocate memory");
		return 2;
	}

	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, true);

	return 1;
}

int WriteUtf8(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	size_t len;
	const char* str = luaL_checklstring(L, 2, &len);

	if (stream->tempBuffer) {
		gff_free(stream->tempBuffer);
	}

	stream->tempBuffer = (BYTE*)gff_calloc(len + 1, sizeof(unsigned short));

	if (!stream->tempBuffer) {

		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, "Unable to allocate memory");
		return 2;
	}

	unsigned char* in = (unsigned char*)str;
	unsigned char* out = (unsigned char*)stream->tempBuffer;

	while (*in)
		if (*in < 128) *out++ = *in++;
		else *out++ = 0xc2 + (*in > 0xbf), *out++ = (*in++ & 0x3f) + 0x80;

	if (!StreamWrite(L, stream, stream->tempBuffer, strlen((const char*)stream->tempBuffer))) {

		gff_free(stream->tempBuffer);
		stream->tempBuffer = NULL;
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, false);
		lua_pushstring(L, "Unable to allocate memory");
		return 2;
	}

	gff_free(stream->tempBuffer);
	stream->tempBuffer = NULL;
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, true);

	return 1;
}

int ReadUtf8(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);

	long avail = stream->len - stream->pos;

	if (!stream->data || avail <= 0) {
		lua_pushnil(L);
		return 1;
	}

	BYTE first = stream->data[stream->pos];
	DWORD code = 0;
	DWORD shift = 0;

	if ((first & 0xF8) == 0xF0) {

		if (avail < 4 ||
			(stream->data[stream->pos + 1] & 0xC0) != 0x80 ||
			(stream->data[stream->pos + 2] & 0xC0) != 0x80 ||
			(stream->data[stream->pos + 3] & 0xC0) != 0x80) {
			lua_pushnil(L);
			stream->pos += min(4, avail);
			return 1;
		}

		memcpy(&code, &stream->data[stream->pos], 4);
		lua_pushlstring(L, (const char*)&code, 4);

		code = stream->data[stream->pos + 3] & 0x3F;
		shift = stream->data[stream->pos + 2] << 6;
		code |= (shift & 0xFC0);
		shift = stream->data[stream->pos + 1] << 12;
		code |= (shift & 0x3F000);
		shift = stream->data[stream->pos] << 18;
		code |= (shift & 0x1C0000);

		code &= 0x1FFFFF;

		stream->pos += 4;
	}
	else if ((first & 0xF0) == 0xE0) {

		if (avail < 3 ||
			(stream->data[stream->pos + 1] & 0xC0) != 0x80 ||
			(stream->data[stream->pos + 2] & 0xC0) != 0x80) {
			lua_pushnil(L);
			stream->pos += min(3, avail);
			return 1;
		}

		memcpy(&code, &stream->data[stream->pos], 3);
		lua_pushlstring(L, (const char*)&code, 3);

		code = stream->data[stream->pos + 2] & 0x3F;
		shift = stream->data[stream->pos + 1] << 6;
		code |= (shift & 0xFC0);
		shift = stream->data[stream->pos] << 12;
		code |= (shift & 0xF000);

		code &= 0xFFFF;

		stream->pos += 3;
	}
	else if ((first & 0xE0) == 0xC0) {

		if (avail < 2 || (stream->data[stream->pos + 1] & 0xC0) != 0x80) {
			lua_pushnil(L);
			stream->pos += min(2, avail);
			return 1;
		}

		memcpy(&code, &stream->data[stream->pos], 2);
		lua_pushlstring(L, (const char*)&code, 2);

		code = stream->data[stream->pos + 1] & 0x3F;
		shift = stream->data[stream->pos] << 6;

		code |= shift;
		code &= 0x7FF;

		stream->pos += 2;
	}
	else if ((first & 0x80) == 0x0) {

		memcpy(&code, &stream->data[stream->pos], 1);
		lua_pushlstring(L, (const char*)&code, 1);

		stream->pos += 1;
	}
	else {
		lua_pushnil(L);
		return 1;
	}

	lua_pushinteger(L, code);
	return 2;
}

int ReadFromFile(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	const char* file = luaL_checkstring(L, 2);
	size_t pos = (size_t)luaL_checkinteger(L, 3);
	size_t len = (size_t)luaL_checkinteger(L, 4);

	FILE* f = fopen(file, "rb");

	if (!f) {
		luaL_error(L, "Unable to open file");
		return 0;
	}

	if (fseek(f, pos + len, SEEK_SET) != 0) {
		fclose(f);
		luaL_error(L, "Unable to seek in file");
		return 0;
	}
	else if (fseek(f, pos, SEEK_SET) != 0) {
		fclose(f);
		luaL_error(L, "Unable to seek in file");
		return 0;
	}

	BYTE* temp = (BYTE*)gff_malloc(len);
	if (!temp) {
		fclose(f);
		luaL_error(L, "Unable to allocate memory");
		return 0;
	}

	fread(temp, sizeof(BYTE), len, f);

	fclose(f);

	if (!StreamWrite(L, stream, temp, len)) {
		luaL_error(L, "Unable to write to stream");
		return 0;
	}

	return 0;
}

int WriteToFile(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	const char* file = luaL_checkstring(L, 2);
	size_t pos = (size_t)luaL_checkinteger(L, 3);
	size_t len = (size_t)luaL_checkinteger(L, 4);

	const BYTE* data = ReadStream(stream, len);

	if (data == NULL) {
		luaL_error(L, "Stream out of bounds");
		return 0;
	}

	FILE* f = fopen(file, "r+b");

	if (!f) {
		luaL_error(L, "Unable to open file");
		return 0;
	}

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		luaL_error(L, "Unable to seek in file");
		return 0;
	}

	size_t size = (size_t)ftell(f);

	if (pos > size) {

		while (pos > size) {

			if (fputc('\0', f) == EOF) {
				fclose(f);
				luaL_error(L, "Unable to write padding");
				return 0;
			}

			size++;
		}
	}
	else {
		if (fseek(f, pos, SEEK_SET) != 0) {
			fclose(f);
			luaL_error(L, "Unable to seek in file");
			return 0;
		}
	}

	fwrite(data, sizeof(BYTE), len, f);
	fflush(f);
	fclose(f);

	return 0;
}

int DumpToFile(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	const char* file = luaL_checkstring(L, 2);

	FILE* f = fopen(file, "wb");

	if (!f) {
		luaL_error(L, "Unable to open file");
		return 0;
	}

	fwrite(stream->data, sizeof(BYTE), stream->len, f);
	fflush(f);
	fclose(f);

	return 0;
}

int OpenFileToStream(lua_State* L) {

	const char* file = luaL_checkstring(L, 1);

	FILE* f = fopen(file, "rb");

	if (!f) {
		luaL_error(L, "Unable to open file");
		return 1;
	}

	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		luaL_error(L, "Unable to seek in file");
		return 1;
	}

	long size = ftell(f);
	rewind(f);
	int alloc = size;

	lua_pop(L, lua_gettop(L));

	LuaStream* stream = lua_pushluastream(L);

	if (alloc < MIN_STREAM_SIZE) {
		alloc = MIN_STREAM_SIZE;
	}

	stream->data = (BYTE*)gff_malloc(alloc);
	if (!stream->data) {
		fclose(f);
		luaL_error(L, "Unable to allocate memory");
		return 0;
	}

	stream->alloc = alloc;
	stream->allocfunc = LUA_NOREF;
	stream->pos = 0;

	if (size > 0) {
		stream->len = fread(stream->data, sizeof(BYTE), size, f);
	}
	else {
		stream->len = 0;
	}

	fclose(f);

	return 1;
}

int Compress(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	COMPRESSOR_HANDLE Compressor = NULL;
	SIZE_T CompressedBufferSize;
	DWORD compressionAlgorithm = (DWORD)luaL_optinteger(L, 2, COMPRESS_ALGORITHM_MSZIP);
	LuaStream* compressed = lua_pushluastream(L);

	BOOL Success = CreateCompressor(compressionAlgorithm, NULL, &Compressor);

	if (!Success) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		return 2;
	}

	Success = Compress(Compressor, stream->data, stream->len, NULL, 0, &CompressedBufferSize);

	if (!Success && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
		CloseCompressor(Compressor);
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		return 2;
	}

	compressed->data = (BYTE*)gff_malloc(CompressedBufferSize);

	if (!compressed->data) {
		CloseCompressor(Compressor);
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, ERROR_INSUFFICIENT_BUFFER);
		return 2;
	}

	compressed->alloc = CompressedBufferSize;

	Success = Compress(Compressor, stream->data, stream->len, compressed->data, compressed->alloc, &CompressedBufferSize);

	if (!Success) {

		CloseCompressor(Compressor);
		gff_free(compressed->data);
		memset(compressed, 0, sizeof(LuaStream));

		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		return 2;
	}

	compressed->len = CompressedBufferSize;

	CloseCompressor(Compressor);

	return 1;
}

int Decompress(lua_State* L) {

	LuaStream* stream = lua_toluastream(L, 1);
	DECOMPRESSOR_HANDLE Decompressor = NULL;
	DWORD compressionAlgorithm = (DWORD)luaL_optinteger(L, 2, COMPRESS_ALGORITHM_MSZIP);
	BOOL Success = CreateDecompressor(compressionAlgorithm, NULL, &Decompressor);
	LuaStream* uncompressed = lua_pushluastream(L);
	SIZE_T DecompressedBufferSize;

	if (!Success) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		return 2;
	}

	Success = Decompress(Decompressor, stream->data, (SIZE_T)stream->len, NULL, 0, &DecompressedBufferSize);

	if (!Success && GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
		CloseDecompressor(Decompressor);
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		return 2;
	}

	uncompressed->data = (BYTE*)gff_malloc(DecompressedBufferSize);

	if (!uncompressed->data) {
		CloseDecompressor(Decompressor);
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, ERROR_INSUFFICIENT_BUFFER);
		return 2;
	}

	uncompressed->alloc = DecompressedBufferSize;

	Success = Decompress(Decompressor, stream->data, stream->len, uncompressed->data, uncompressed->alloc, &DecompressedBufferSize);

	if (!Success) {

		CloseDecompressor(Decompressor);
		gff_free(uncompressed->data);
		memset(uncompressed, 0, sizeof(LuaStream));

		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		return 2;
	}

	uncompressed->len = DecompressedBufferSize;

	CloseDecompressor(Decompressor);

	return 1;
}

int NewStream(lua_State* L) {

	if (lua_type(L, 1) == LUA_TFUNCTION) {

		int ref = luaL_ref(L, LUA_REGISTRYINDEX);

		LuaStream* stream = lua_pushluastream(L);
		stream->allocfunc = ref;
		size_t size = AllocAddSize(L, stream, MIN_STREAM_SIZE);

		stream->data = (BYTE*)gff_malloc(size);
		if (!stream->data) {
			luaL_error(L, "Unable to allocate memory");
			return 0;
		}
		stream->alloc = size;
		stream->len = 0;

		return 1;
	}

	int init = (int)luaL_optinteger(L, 1, 1048576);

	if (init <= 0) {
		init = MIN_STREAM_SIZE;
	}

	lua_pop(L, lua_gettop(L));

	LuaStream* stream = lua_pushluastream(L);

	stream->data = (BYTE*)gff_malloc(init);
	if (!stream->data) {
		luaL_error(L, "Unable to allocate memory");
		return 0;
	}

	stream->alloc = init;
	stream->len = 0;
	stream->pos = 0;
	stream->allocfunc = LUA_NOREF;

	return 1;
}

int GetSharedMemoryStreamInfo(lua_State* L) {

	const char* name = luaL_checkstring(L, 1);

	HANDLE h = OpenFileMapping(FILE_MAP_READ, FALSE, name);

	if (h == NULL) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		return 2;
	}

	void* ptr = MapViewOfFile(h, FILE_MAP_READ, 0, 0, 0);

	if (ptr == NULL) {
		CloseHandle(h);
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		return 2;
	}

	MEMORY_BASIC_INFORMATION info;
	SIZE_T len = VirtualQuery(ptr, &info, sizeof(MEMORY_BASIC_INFORMATION));

	if (len < sizeof(MEMORY_BASIC_INFORMATION)) {
		UnmapViewOfFile(ptr);
		CloseHandle(h);
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		return 2;
	}
	else {

		lua_pop(L, lua_gettop(L));

		lua_createtable(L, 0, 7);

		lua_pushstring(L, "AllocationBase");
		lua_pushinteger(L, (DWORD)info.AllocationBase);
		lua_settable(L, -3);

		lua_pushstring(L, "AllocationProtect");
		lua_pushinteger(L, info.AllocationProtect);
		lua_settable(L, -3);

		lua_pushstring(L, "BaseAddress");
		lua_pushinteger(L, (DWORD)info.BaseAddress);
		lua_settable(L, -3);

		lua_pushstring(L, "Protect");
		lua_pushinteger(L, info.Protect);
		lua_settable(L, -3);

		lua_pushstring(L, "RegionSize");
		lua_pushinteger(L, info.RegionSize);
		lua_settable(L, -3);

		lua_pushstring(L, "State");
		lua_pushinteger(L, info.State);
		lua_settable(L, -3);

		lua_pushstring(L, "Type");
		lua_pushinteger(L, info.Type);
		lua_settable(L, -3);

		UnmapViewOfFile(ptr);
		CloseHandle(h);

		return 1;
	}
}

int OpenSharedMemoryStream(lua_State* L) {

	const char* name = luaL_checkstring(L, 1);
	int readOnly = lua_isboolean(L, 2) && lua_toboolean(L, 2);

	LuaStream* stream = lua_pushluastream(L);

	stream->hSharedMemory = OpenFileMapping(readOnly ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS, FALSE, name);

	if (stream->hSharedMemory == NULL) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		return 2;
	}

	stream->data = (BYTE*)MapViewOfFile(stream->hSharedMemory, readOnly ? FILE_MAP_READ : FILE_MAP_ALL_ACCESS, 0, 0, 0);

	if (stream->data == NULL) {
		CloseHandle(stream->hSharedMemory);
		stream->hSharedMemory = NULL;
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		return 2;
	}

	MEMORY_BASIC_INFORMATION info;
	stream->alloc = VirtualQuery(stream->data, &info, sizeof(MEMORY_BASIC_INFORMATION));

	if (stream->alloc == 0) {

		UnmapViewOfFile(stream->data);
		stream->data = NULL;
		CloseHandle(stream->hSharedMemory);
		stream->hSharedMemory = NULL;

		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		return 2;
	}
	else {
		stream->alloc = info.RegionSize;
		stream->len = info.RegionSize;
	}

	return 1;
}

int NewSharedMemoryStream(lua_State* L) {

	const char* name = luaL_checkstring(L, 1);
	int size = (int)luaL_checkinteger(L, 2);

	LuaStream* stream = lua_pushluastream(L);

	SECURITY_ATTRIBUTES sa;
	PSECURITY_DESCRIPTOR psd = NULL;
	PACL pAcl = NULL;
	SID_IDENTIFIER_AUTHORITY SIDAuthWorld = SECURITY_WORLD_SID_AUTHORITY;
	PSID pEveryoneSID = NULL;
	SID_IDENTIFIER_AUTHORITY SIDAuthNT = SECURITY_NT_AUTHORITY;
	PSID pCurrentUserSID = NULL;
	EXPLICIT_ACCESS ea[2];

	if (!AllocateAndInitializeSid(&SIDAuthWorld, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &pEveryoneSID)) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		goto Cleanup;
	}

	if (!AllocateAndInitializeSid(&SIDAuthNT, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &pCurrentUserSID)) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		goto Cleanup;
	}

	memset(&ea, NULL, 2 * sizeof(EXPLICIT_ACCESS));
	ea[0].grfAccessPermissions = GENERIC_READ;
	ea[0].grfAccessMode = SET_ACCESS;
	ea[0].grfInheritance = NO_INHERITANCE;
	ea[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
	ea[0].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
	ea[0].Trustee.ptstrName = (LPTSTR)pEveryoneSID;

	ea[1].grfAccessPermissions = GENERIC_ALL;
	ea[1].grfAccessMode = SET_ACCESS;
	ea[1].grfInheritance = NO_INHERITANCE;
	ea[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
	ea[1].Trustee.TrusteeType = TRUSTEE_IS_USER;
	ea[1].Trustee.ptstrName = (LPTSTR)pCurrentUserSID;

	if (SetEntriesInAcl(2, ea, NULL, &pAcl) != ERROR_SUCCESS) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		goto Cleanup;
	}

	psd = (PSECURITY_DESCRIPTOR)LocalAlloc(LPTR, SECURITY_DESCRIPTOR_MIN_LENGTH);
	if (psd == NULL) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		goto Cleanup;
	}

	if (!InitializeSecurityDescriptor(psd, SECURITY_DESCRIPTOR_REVISION)) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		goto Cleanup;
	}

	if (!SetSecurityDescriptorDacl(psd, TRUE, pAcl, FALSE)) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		goto Cleanup;
	}

	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = psd;
	sa.bInheritHandle = FALSE;

	stream->hSharedMemory = CreateFileMapping(INVALID_HANDLE_VALUE, &sa, PAGE_READWRITE, 0, size, name);

	if (stream->hSharedMemory == NULL) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		goto Cleanup;
	}

	stream->data = (BYTE*)MapViewOfFile(stream->hSharedMemory, FILE_MAP_ALL_ACCESS, 0, 0, size);

	if (stream->data == NULL) {
		CloseHandle(stream->hSharedMemory);
		stream->hSharedMemory = NULL;
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		goto Cleanup;
	}

	MEMORY_BASIC_INFORMATION info;
	stream->alloc = VirtualQuery(stream->data, &info, sizeof(MEMORY_BASIC_INFORMATION));

	if (stream->alloc == 0) {

		UnmapViewOfFile(stream->data);
		stream->data = NULL;
		CloseHandle(stream->hSharedMemory);
		stream->hSharedMemory = NULL;

		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		goto Cleanup;
	}
	else {
		stream->alloc = info.RegionSize;
		stream->len = info.RegionSize;
		lua_pushnil(L);
	}

Cleanup:

	if (pEveryoneSID) 
		FreeSid(pEveryoneSID);
	if (pCurrentUserSID) 
		FreeSid(pCurrentUserSID);
	if (pAcl) 
		LocalFree(pAcl);
	if (psd) 
		LocalFree(psd);

	return 2;
}

LuaStream* lua_pushluastream(lua_State* L) {

	LuaStream* stream = (LuaStream*)lua_newuserdata(L, sizeof(LuaStream));

	if (stream == NULL)
		luaL_error(L, "Unable to push stream");

	luaL_getmetatable(L, STREAM);
	lua_setmetatable(L, -2);

	memset(stream, 0, sizeof(LuaStream));
	stream->allocfunc = LUA_NOREF;

	return stream;
}

LuaStream* lua_toluastream(lua_State* L, int index) {

	LuaStream* pipe = (LuaStream*)luaL_checkudata(L, index, STREAM);

	if (pipe == NULL)
		luaL_error(L, "parameter is not a %s", STREAM);

	return pipe;
}

int luastream_gc(lua_State* L) {

	LuaStream* pipe = lua_toluastream(L, 1);

	if (pipe->hSharedMemory) {
		UnmapViewOfFile(pipe->data);
		CloseHandle(pipe->hSharedMemory);
		pipe->hSharedMemory = NULL;
	}
	else if (pipe->data) {
		gff_free(pipe->data);
	}

	if (pipe->tempBuffer) {
		gff_free(pipe->tempBuffer);
	}

	pipe->data = NULL;
	pipe->tempBuffer = NULL;

	if (pipe->allocfunc != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, pipe->allocfunc);
		pipe->allocfunc = LUA_NOREF;
	}

	return 0;
}

int luastream_tostring(lua_State* L) {
	char tim[100];
	sprintf(tim, "Stream: 0x%08X", lua_toluastream(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}