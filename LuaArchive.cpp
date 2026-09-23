#include "LuaArchive.h"
#include "luatext.h"
#include <errno.h>

// Entry names are always returned as UTF-8 strings. archive_entry_pathname() uses the
// locale's code page on Windows, so the UTF-8 and wide forms are tried first.
static void push_entry_name(lua_State* L, struct archive_entry* entry) {

	const char* utf8 = archive_entry_pathname_utf8(entry);
	if (utf8) {
		lua_pushstring(L, utf8);
		return;
	}

	const wchar_t* wide = archive_entry_pathname_w(entry);
	if (wide) {
		lua_pushwideasutf8(L, wide);
		return;
	}

	const char* name = archive_entry_pathname(entry);
	lua_pushstring(L, name ? name : "");
}

// Pushes libarchive's error message for a, or a generic one when it has none.
static void push_archive_error(lua_State* L, struct archive* a, const char* fallback) {

	const char* err = a ? archive_error_string(a) : NULL;
	lua_pushstring(L, err ? err : fallback);
}

// Opens a UTF-8 path. archive_read_open_filename() reads the path in the ANSI code
// page on Windows, so the wide-char variant is used there.
static int open_archive_file(struct archive* a, const char* path) {
#ifdef _WIN32
	wchar_t* wpath = kitsune_utf8_to_wide_alloc(path);
	if (!wpath) {
		archive_set_error(a, ENOMEM, "out of memory");
		return ARCHIVE_FATAL;
	}

	int r = archive_read_open_filename_w(a, wpath, 10240);
	kitsune_free(wpath);
	return r;
#else
	return archive_read_open_filename(a, path, 10240);
#endif
}

// The optional second argument (usewchar) is accepted for compatibility and ignored.
int OpenReadArchive(lua_State* L) {

	size_t len;
	const char* file = luaL_checklstring(L, 1, &len);

	struct archive* a;
	struct archive_entry* entry = NULL;
	int r;

	a = archive_read_new();
	if (!a) {
		lua_pushnil(L);
		lua_pushstring(L, "Out of memory");
		return 2;
	}

	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);

	r = open_archive_file(a, file);

	if (r != ARCHIVE_OK) {

		lua_pushnil(L);
		const char* err = archive_error_string(a);
		lua_pushstring(L, err ? err : "Unable to open archive");
		archive_read_free(a);
		return 2;
	}

	archive_read_free(a);

	LuaArchive* arc = lua_pusharchive(L);

	arc->isRead = true;
	arc->file = (char*)kitsune_malloc(len+1);

	if (!arc->file) {

		lua_pushnil(L);
		lua_pushstring(L, "Out of memory");
		return 2;
	}
	else {
		memcpy(arc->file, file, len);
		arc->file[len] = '\0';
	}

	return 1;
}

int ReadArchiveEntries(lua_State* L) {

	LuaArchive* arc = lua_toarchive(L, 1);

	if (!arc || !arc->file || !arc->isRead) {

		luaL_error(L, "Archive not open for read");
		return 0;
	}

	struct archive* a;
	struct archive_entry* entry = NULL;
	int r;
	int nth = 0;

	a = archive_read_new();
	if (!a) {
		lua_pushnil(L);
		lua_pushstring(L, "Out of memory");
		return 2;
	}

	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);

	r = open_archive_file(a, arc->file);

	if (r != ARCHIVE_OK) {

		lua_pushnil(L);
		push_archive_error(L, a, "error reading archive");
		archive_read_free(a);
		return 2;
	}

	r = archive_read_next_header(a, &entry);

	lua_newtable(L);

	while (r == ARCHIVE_OK) {

		lua_createtable(L, 0, 2);

		lua_pushstring(L, "Name");
		push_entry_name(L, entry);
		lua_settable(L, -3);

		lua_pushstring(L, "Size");
		lua_pushinteger(L, archive_entry_size(entry));
		lua_settable(L, -3);

		lua_rawseti(L, -2, ++nth);

		archive_read_data_skip(a);
		r = archive_read_next_header(a, &entry);
	}

	if (r != ARCHIVE_EOF) {

		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		push_archive_error(L, a, "error reading archive");
		archive_read_free(a);
		return 2;
	}

	archive_read_free(a);

	return 1;
}

int ReadEntry(lua_State* L) {

	LuaArchive* arc = lua_toarchive(L, 1);
	int buffer = (int)luaL_optinteger(L, 2, 1024);
	long long size;

	if (arc->buff) {
		kitsune_free(arc->buff);
		arc->buff = NULL;
	}

	if (!arc || !arc->file || !arc->isRead || !arc->a || !arc->entry || buffer <= 0) {

		luaL_error(L, "Invalid params or file not open");
		return 0;
	}

	arc->buff = kitsune_malloc(buffer);

	if (!arc->buff) {
		luaL_error(L, "Not enough memory to allocate buffer");
		return 0;
	}

	size = archive_read_data(arc->a, arc->buff, buffer);

	if (size > 0) {
		lua_pushlstring(L, (const char*)arc->buff, size);
		kitsune_free(arc->buff);
		arc->buff = NULL;
	}
	else if (size < 0) {
		kitsune_free(arc->buff);
		arc->buff = NULL;
		const char* error = archive_error_string(arc->a);
		luaL_error(L, error ? error : "error reading archive data");
		return 0;
	}
	else {
		kitsune_free(arc->buff);
		arc->buff = NULL;
		lua_pushnil(L);
	}

	return 1;
}

int ReadAllEntry(lua_State* L) {

	LuaArchive* arc = lua_toarchive(L, 1);

	if (!arc || !arc->file || !arc->isRead || !arc->a || !arc->entry) {

		luaL_error(L, "Invalid params or file not open");
		return 0;
	}

	const size_t CHUNK = 65536;
	size_t capacity = CHUNK;
	size_t total = 0;

	char* buf = (char*)kitsune_malloc(capacity);

	if (!buf) {
		luaL_error(L, "Not enough memory to allocate buffer");
		return 0;
	}

	for (;;) {
		if (total == capacity) {
			size_t newcap = capacity * 2;
			char* newbuf = (char*)kitsune_realloc(buf, newcap);
			if (!newbuf) {
				kitsune_free(buf);
				luaL_error(L, "Not enough memory to allocate buffer");
				return 0;
			}
			buf = newbuf;
			capacity = newcap;
		}
		long long n = archive_read_data(arc->a, buf + total, capacity - total);
		if (n == 0)
			break;
		if (n < 0) {
			const char* err = archive_error_string(arc->a);
			kitsune_free(buf);
			luaL_error(L, err ? err : "error reading archive entry");
			return 0;
		}
		total += (size_t)n;
	}

	lua_pushlstring(L, buf, total);
	kitsune_free(buf);
	return 1;
}

int SetReadEntry(lua_State* L) {

	LuaArchive* arc = lua_toarchive(L, 1);
	int target = (int)luaL_checkinteger(L, 2);

	if (!arc || !arc->file || !arc->isRead) {

		luaL_error(L, "Archive not open for read");
		return 0;
	}

	if (arc->a) {
		archive_read_free(arc->a);
		arc->a = NULL;
		arc->entry = NULL;
	}

	long long r;
	int nth = 0;

	arc->a = archive_read_new();
	if (!arc->a) {
		lua_pushnil(L);
		lua_pushstring(L, "Out of memory");
		return 2;
	}

	archive_read_support_filter_all(arc->a);
	archive_read_support_format_all(arc->a);

	r = open_archive_file(arc->a, arc->file);

	if (r != ARCHIVE_OK) {

		lua_pushnil(L);
		const char* err = archive_error_string(arc->a);
		lua_pushstring(L, err ? err : "Unable to open archive");
		archive_read_free(arc->a);
		arc->a = NULL;
		arc->entry = NULL;
		return 2;
	}

	r = archive_read_next_header(arc->a, &arc->entry);

	while (r == ARCHIVE_OK) {

		if (target == ++nth) {
			push_entry_name(L, arc->entry);
			lua_pushinteger(L, archive_entry_size(arc->entry));
			return 2;
		}

		archive_read_data_skip(arc->a);
		r = archive_read_next_header(arc->a, &arc->entry);
	}

	arc->entry = NULL;

	if (r != ARCHIVE_EOF) {

		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		push_archive_error(L, arc->a, "error reading archive");

		return 2;
	}

	lua_pushnil(L);
	lua_pushstring(L, "EOF");
	return 2;
}

LuaArchive* lua_pusharchive(lua_State* L) {
	
	LuaArchive* archive = (LuaArchive*)lua_newuserdata(L, sizeof(LuaArchive));
	if (!archive) {
		luaL_error(L, "Unable to push archive");
		return NULL;
	}
	luaL_getmetatable(L, ARCHIVE);
	lua_setmetatable(L, -2);
	memset(archive, 0, sizeof(LuaArchive));

	return archive;
}

LuaArchive* lua_toarchive(lua_State* L, int index) {
	
	LuaArchive* archive = (LuaArchive*)luaL_checkudata(L, index, ARCHIVE);
	if (!archive) {
		luaL_error(L, "parameter is not a %s", ARCHIVE);
		return NULL;
	}

	return archive;
}

int archive_gc(lua_State* L) {

	LuaArchive* arc = lua_toarchive(L, 1);

	if (arc->file) {

		kitsune_free(arc->file);
		arc->file = NULL;
	}

	if (arc->buff) {
		kitsune_free(arc->buff);
		arc->buff = NULL;
	}

	if (arc->a) {
		archive_read_free(arc->a);
		arc->a = NULL;
		arc->entry = NULL;
	}

	return 0;
}

int archive_tostring(lua_State* L) {
	char tim[100];
	sprintf(tim, "Archive: %p", (void*)lua_toarchive(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}