#include "LuaArchive.h"

int OpenArchive(lua_State* L) {

	const char* file = luaL_checkstring(L, 1);

	struct archive* a;
	struct archive_entry* entry = NULL;
	int r;

	a = archive_read_new();
	archive_read_support_filter_all(a);
	archive_read_support_format_all(a);

	r = archive_read_open_filename(a, file, 102400);

	if (r != ARCHIVE_OK) {
		r = archive_read_free(a);
		lua_pushnil(L);
		return 1;
	}
	
	r = archive_read_next_header(a, &entry);
	
	if (r != ARCHIVE_OK) {
		puts(archive_error_string(a));
	}

	while (r == ARCHIVE_OK) {
		printf("%s\n", archive_entry_pathname(entry));

		r = archive_read_next_header(a, &entry);
	}
	
	if (r != ARCHIVE_OK) {
		r = archive_read_free(a);
		lua_pushnil(L);
		return 1;
	}

	r = archive_read_free(a);

	LuaArchive* archive = lua_pusharchive(L);

	return 1;
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

	LuaArchive* pipe = lua_toarchive(L, 1);

	return 0;
}

int archive_tostring(lua_State* L) {
	char tim[100];
	sprintf(tim, "Archive: 0x%08X", lua_toarchive(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}