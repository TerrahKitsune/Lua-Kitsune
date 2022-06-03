#include "LuaArchive.h"

int OpenReadArchive(lua_State* L) {

	size_t len;
	const char* file = luaL_checklstring(L, 1, &len);

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

	return 0;
}

int archive_tostring(lua_State* L) {
	char tim[100];
	sprintf(tim, "Archive: 0x%08X", lua_toarchive(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}