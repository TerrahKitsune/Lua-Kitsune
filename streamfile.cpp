#include "streamfile.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <io.h>

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
	if (!has_append)
		caps |= STREAM_CAP_SEEK;
	return caps;
}

// ── vtable implementations ────────────────────────────────────────────────────

static const BYTE* file_read(void* native, lua_State* L, size_t len, size_t* outLen) {
	InFileStream* f = (InFileStream*)native;
	if (!f->file) {
		lua_pushboolean(L, false);
		if (outLen)
			*outLen = 0;
		return NULL;
	}
	if (len == 0) {
		__int64 cur      = _ftelli64(f->file);
		__int64 fileSize = _filelengthi64(_fileno(f->file));
		if (fileSize < 0 || fileSize <= cur) {
			lua_pushlstring(L, "", 0);
			if (outLen)
				*outLen = 0;
			return NULL;
		}
		len = (size_t)(fileSize - cur);
	}
	BYTE* buf = (BYTE*)gff_malloc(len);
	if (!buf) {
		lua_pushboolean(L, false);
		if (outLen)
			*outLen = 0;
		return NULL;
	}
	size_t got = fread(buf, 1, len, f->file);
	if (got == 0) {
		gff_free(buf);
		lua_pushlstring(L, "", 0);
		if (outLen)
			*outLen = 0;
		return NULL;
	}
	lua_pushlstring(L, (const char*)buf, got);
	gff_free(buf);
	if (outLen)
		*outLen = got;
	return (const BYTE*)lua_tolstring(L, -1, NULL);
}

static bool file_write(void* native, const BYTE* data, size_t len) {
	InFileStream* f = (InFileStream*)native;
	if (!f->file || !data || len == 0)
		return true;
	return fwrite(data, 1, len, f->file) == len;
}

static bool file_setpos(void* native, lua_Integer pos) {
	InFileStream* f = (InFileStream*)native;
	if (pos < 0)
		pos = 0;
	return _fseeki64(f->file, (__int64)pos, SEEK_SET) == 0;
}

static lua_Integer file_curpos(void* native) {
	__int64 pos = _ftelli64(((InFileStream*)native)->file);
	return pos < 0 ? 0 : (lua_Integer)pos;
}

static lua_Integer file_getlen(void* native) {
	InFileStream* f    = (InFileStream*)native;
	__int64       size = _filelengthi64(_fileno(f->file));
	return size < 0 ? 0 : (lua_Integer)size;
}

static void file_close(void* native) {
	InFileStream* f = (InFileStream*)native;
	if (f->file)
		fclose(f->file);
	gff_free(f);
}

static int file_info(void* native, lua_State* L) {
	InFileStream* f  = (InFileStream*)native;
	__int64 cur      = _ftelli64(f->file);
	__int64 fileSize = _filelengthi64(_fileno(f->file));
	lua_createtable(L, 0, 5);
	lua_pushinteger(L, (lua_Integer)cur);
	lua_setfield(L, -2, "pos");
	lua_pushinteger(L, fileSize < 0 ? 0 : (lua_Integer)fileSize);
	lua_setfield(L, -2, "len");
	lua_pushstring(L, f->mode);
	lua_setfield(L, -2, "mode");
	lua_pushstring(L, f->filename);
	lua_setfield(L, -2, "name");
	lua_pushstring(L, "file");
	lua_setfield(L, -2, "type");
	return 1;
}

static const LuaStreamVtable g_file_vtbl = {
	file_read,
	file_write,
	file_setpos,
	file_curpos,
	file_getlen,
	file_close,
	file_info,
};

// ── Public constructor helper ─────────────────────────────────────────────────

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
	strncpy_s(fs->mode,     sizeof(fs->mode),     mode,     _TRUNCATE);
	strncpy_s(fs->filename, sizeof(fs->filename), filename, _TRUNCATE);

	LuaStream* stream = (LuaStream*)lua_newuserdata(L, sizeof(LuaStream));
	luaL_getmetatable(L, STREAM);
	lua_setmetatable(L, -2);
	memset(stream, 0, sizeof(LuaStream));
	stream->backendRef = LUA_NOREF;
	stream->vtbl       = &g_file_vtbl;
	stream->native     = fs;
	stream->Caps       = fs->caps;
	return stream;
}
