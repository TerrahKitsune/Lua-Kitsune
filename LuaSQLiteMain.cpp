#include "LuaSQLiteMain.h"
#include "LuaSQLite.h"

static const struct luaL_Reg lasqlitefunctions[] = {
	{ "Finish" , SQLiteFinish },
	{ "Open", SQLiteConnect },
	{ "Close", SQLite_GC },
	{ "Query", SQLiteExecute },	
	{ "Fetch", SQLiteFetch },
	{ "GetRow", SQLiteGetRow },
	{ "SetBusyHandler", SQLiteSetBusyHandler },
	{ "ToggleWidechar", SQLiteSetUseWidechar },
	{ "RegisterFunction", SQLiteRegisterFunction },
	{ "RegisterAggregateFunction", SQLiteRegisterAggregateFunction },
	{ NULL, NULL }
};

static const luaL_Reg luasqlmeta[] = {
	{ "__gc", SQLite_GC },
	{ "__tostring", SQLite_ToString },
	{ NULL, NULL }
};

#ifdef _WIN32
// On Windows the gff_* allocators may use a custom heap (HeapAlloc / custom
// pool), so redirect SQLite's internal allocator to match.  The xSize field
// is deliberately kept from the default (SQLITE_CONFIG_GETMALLOC) because
// the default sqlite3MemSize reads an 8-byte header written by sqlite3MemMalloc,
// not by our wrappers.  On Windows this works by layout accident (HeapAlloc
// header padding); the only safe cross-platform approach is a full replacement
// of all 8 fields including xSize.  For the Linux/bare-bones build we just let
// SQLite use its own built-in malloc/free which are already the same functions.
static struct sqlite3_mem_methods sqlitemalloc;

void * sqlite_malloc(int size) {
	return kitsune_malloc(size);
}

void sqlite_free(void * ptr) {
	kitsune_free(ptr);
}

void * sqlite_realloc(void * ptr, int size) {
	return kitsune_realloc(ptr, size);
}
#endif

int luaopen_sqlite(lua_State *L) {

	sqlite3_config(SQLITE_CONFIG_MEMSTATUS, 0);
#ifdef _WIN32
	sqlite3_config(SQLITE_CONFIG_GETMALLOC, &sqlitemalloc);
	sqlitemalloc.xMalloc = sqlite_malloc;
	sqlitemalloc.xFree = sqlite_free;
	sqlitemalloc.xRealloc = sqlite_realloc;
	sqlite3_config(SQLITE_CONFIG_MALLOC, &sqlitemalloc);
#endif

	luaL_newlibtable(L, lasqlitefunctions);
	luaL_setfuncs(L, lasqlitefunctions, 0);

	luaL_newmetatable(L, LUASQLITE);
	luaL_setfuncs(L, luasqlmeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}