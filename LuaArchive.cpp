#include "LuaArchive.h"
#include "luawchar.h"

int OpenReadArchive(lua_State* L) {

	size_t len;
	const char* file = luaL_checklstring(L, 1, &len);
	int useWchar = lua_toboolean(L, 2);

	struct archive* a;
	struct archive_entry* entry;
	int r;

	a = archive_read_new();

	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);

	r = archive_read_open_filename(a, file, 10240);

	if (r != ARCHIVE_OK) {
		
		lua_pushnil(L);
		lua_pushstring(L, archive_error_string(a));
		archive_read_free(a);
		return 2;
	}

	archive_read_free(a);

	LuaArchive* arc = lua_pusharchive(L);


	arc->useWchar = useWchar != 0;

	arc->isRead = true;
	arc->file = (char*)gff_malloc(len+1);

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
	struct archive_entry* entry;
	int r;
	int nth = 0;

	a = archive_read_new();

	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);

	r = archive_read_open_filename(a, arc->file, 10240);

	if (r != ARCHIVE_OK) {

		lua_pushnil(L);
		lua_pushstring(L, archive_error_string(a));
		archive_read_free(a);
		return 2;
	}

	r = archive_read_next_header(a, &entry);

	lua_newtable(L);

	while (r == ARCHIVE_OK) {

		lua_createtable(L, 0, 2);

		lua_pushstring(L, "Name");
		if (arc->useWchar) {
			lua_pushwchar(L, archive_entry_pathname_w(entry));
		}
		else {
			lua_pushstring(L, archive_entry_pathname(entry));
		}
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
		lua_pushstring(L, archive_error_string(a));
		archive_read_free(a);
		return 2;
	}

	archive_read_free(a);

	return 1;
}

int ReadEntry(lua_State* L) {

	LuaArchive* arc = lua_toarchive(L, 1);
	int buffer = luaL_optinteger(L, 2, 1024);
	size_t size;

	if (arc->buff) {
		gff_free(arc->buff);
		arc->buff = NULL;
	}

	if (!arc || !arc->file || !arc->isRead || !arc->a || !arc->entry || buffer <= 0) {

		luaL_error(L, "Invalid params or file not open");
		return 0;
	}

	arc->buff = gff_malloc(buffer);

	if (!arc->buff) {
		luaL_error(L, "Not enough memory to allocate buffer");
		return 0;
	}

	size = archive_read_data(arc->a, arc->buff, buffer);

	if (size > 0) {
		lua_pushlstring(L, (const char*)arc->buff, size);
	}
	else {
		lua_pushnil(L);
	}

	gff_free(arc->buff);
	arc->buff = NULL;

	return 1;
}

int SetReadEntry(lua_State* L) {

	LuaArchive* arc = lua_toarchive(L, 1);
	int target = luaL_checkinteger(L, 2);

	if (!arc || !arc->file || !arc->isRead) {

		luaL_error(L, "Archive not open for read");
		return 0;
	}

	if (arc->a) {
		archive_read_free(arc->a);
		arc->a = NULL;
		arc->entry = NULL;
	}

	int r;
	int nth = 0;

	arc->a = archive_read_new();

	archive_read_support_filter_all(arc->a);
	archive_read_support_format_all(arc->a);

	r = archive_read_open_filename(arc->a, arc->file, 10240);

	if (r != ARCHIVE_OK) {

		lua_pushnil(L);
		lua_pushstring(L, archive_error_string(arc->a));
		archive_read_free(arc->a);
		return 2;
	}

	r = archive_read_next_header(arc->a, &arc->entry);

	while (r == ARCHIVE_OK) {

		if (target == ++nth) {
			if (arc->useWchar) {
				lua_pushwchar(L, archive_entry_pathname_w(arc->entry));
			}
			else {
				lua_pushstring(L, archive_entry_pathname(arc->entry));
			}
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
		lua_pushstring(L, archive_error_string(arc->a));

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

		gff_free(arc->file);
		arc->file = NULL;
	}

	if (arc->buff) {
		gff_free(arc->buff);
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
	sprintf(tim, "Archive: 0x%08X", lua_toarchive(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}