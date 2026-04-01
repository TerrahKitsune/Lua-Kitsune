#include "streamfile.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>

struct InFileStream {
	FILE* file;
	BYTE  caps;
	char  mode[16];
	char  filename[512];
};

// Derive capability flags from an fopen-style mode string.
//   'r'          -> READ | SEEK | PEEK
//   'w'          -> WRITE | SEEK
//   'a'          -> WRITE  (append forces writes to EOF; seek would mislead)
//   any '+' form -> adds both READ and WRITE
static BYTE mode_to_caps(const char* mode) {
	bool has_read   = false;
	bool has_write  = false;
	bool has_append = false;

	for (const char* p = mode; *p; p++) {
		if (*p == 'r')
			has_read = true;
		else if (*p == 'w')
			has_write = true;
		else if (*p == 'a') {
			has_write  = true;
			has_append = true;
		} else if (*p == '+') {
			has_read  = true;
			has_write = true;
		}
	}

	BYTE caps = 0;
	if (has_read)
		caps |= STREAM_CAP_READ;
	if (has_write)
		caps |= STREAM_CAP_WRITE;
	if (!has_append) {
		caps |= STREAM_CAP_SEEK;
		if (has_read)
			caps |= STREAM_CAP_PEEK;
	}
	return caps;
}

static int pushfile_backend(lua_State* L) {
	int type = (int)lua_tointeger(L, 1);
	InFileStream* stream = (InFileStream*)lua_touserdata(L, lua_upvalueindex(1));

	if (!stream || !stream->file) {
		lua_pushboolean(L, false);
		return 1;
	}

	switch (type) {
	case STREAM_OP_OPEN:
		lua_pushinteger(L, stream->caps);
		return 1;

	case STREAM_OP_CLOSE:
		fclose(stream->file);
		stream->file = NULL;
		gff_free(stream);
		lua_pushboolean(L, true);
		return 1;

	case STREAM_OP_READ: {
		size_t requested = (size_t)luaL_optinteger(L, 2, 0);
		if (requested == 0) {
			long cur = ftell(stream->file);
			fseek(stream->file, 0, SEEK_END);
			long end = ftell(stream->file);
			fseek(stream->file, cur, SEEK_SET);
			if (end <= cur) {
				lua_pushlstring(L, NULL, 0);
				return 1;
			}
			requested = (size_t)(end - cur);
		}
		BYTE* buf = (BYTE*)gff_malloc(requested);
		if (!buf) {
			lua_pushboolean(L, false);
			lua_pushstring(L, "out of memory");
			return 2;
		}
		size_t got = fread(buf, 1, requested, stream->file);
		if (got == 0) {
			gff_free(buf);
			lua_pushlstring(L, NULL, 0);
			return 1;
		}
		lua_pushlstring(L, (const char*)buf, got);
		gff_free(buf);
		return 1;
	}

	case STREAM_OP_WRITE: {
		size_t len;
		const char* data = lua_tolstring(L, 2, &len);
		if (!data || len == 0) {
			lua_pushboolean(L, true);
			return 1;
		}
		size_t written = fwrite(data, 1, len, stream->file);
		lua_pushboolean(L, written == len);
		return 1;
	}

	case STREAM_OP_SETPOS: {
		lua_Integer pos = luaL_checkinteger(L, 2);
		if (pos < 0)
			pos = 0;
		lua_pushboolean(L, fseek(stream->file, (long)pos, SEEK_SET) == 0);
		return 1;
	}

	case STREAM_OP_CURPOS: {
		long pos = ftell(stream->file);
		if (pos < 0) {
			lua_pushnil(L);
		} else {
			lua_pushinteger(L, (lua_Integer)pos);
		}
		return 1;
	}

	case STREAM_OP_LEN: {
		long cur = ftell(stream->file);
		fseek(stream->file, 0, SEEK_END);
		long end = ftell(stream->file);
		fseek(stream->file, cur, SEEK_SET);
		lua_pushinteger(L, (lua_Integer)end);
		return 1;
	}

	case STREAM_OP_INFO: {
		long cur = ftell(stream->file);
		fseek(stream->file, 0, SEEK_END);
		long end = ftell(stream->file);
		fseek(stream->file, cur, SEEK_SET);
		lua_createtable(L, 0, 5);
		lua_pushinteger(L, (lua_Integer)cur);
		lua_setfield(L, -2, "pos");
		lua_pushinteger(L, (lua_Integer)end);
		lua_setfield(L, -2, "len");
		lua_pushstring(L, stream->mode);
		lua_setfield(L, -2, "mode");
		lua_pushstring(L, stream->filename);
		lua_setfield(L, -2, "name");
		lua_pushstring(L, "file");
		lua_setfield(L, -2, "type");
		return 1;
	}

	default:
		lua_pushboolean(L, false);
		return 1;
	}
}

LuaStream* lua_pushfilestream(lua_State* L, const char* filename, const char* mode) {
	FILE* f = NULL;
	fopen_s(&f, filename, mode);
	if (!f) {
		char errbuf[256];
		strerror_s(errbuf, sizeof(errbuf), errno);
		luaL_error(L, "Stream.Open: cannot open '%s' (%s)", filename, errbuf);
		return NULL;
	}

	InFileStream* fs = (InFileStream*)gff_malloc(sizeof(InFileStream));
	if (!fs) {
		fclose(f);
		luaL_error(L, "Stream.Open: out of memory");
		return NULL;
	}
	ZeroMemory(fs, sizeof(InFileStream));
	fs->file = f;
	fs->caps = mode_to_caps(mode);
	strncpy_s(fs->mode, sizeof(fs->mode), mode, _TRUNCATE);
	strncpy_s(fs->filename, sizeof(fs->filename), filename, _TRUNCATE);

	LuaStream* stream = (LuaStream*)lua_newuserdata(L, sizeof(LuaStream));
	luaL_getmetatable(L, STREAM);
	lua_setmetatable(L, -2);
	memset(stream, 0, sizeof(LuaStream));
	stream->backendRef = LUA_NOREF;

	lua_pushlightuserdata(L, fs);
	lua_pushcclosure(L, pushfile_backend, 1);
	stream->backendRef = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_rawgeti(L, LUA_REGISTRYINDEX, stream->backendRef);
	lua_pushinteger(L, STREAM_OP_OPEN);
	lua_call_nohook(L, 1, 1);
	stream->Caps = (BYTE)lua_tointeger(L, -1);
	lua_pop(L, 1);

	return stream;
}
