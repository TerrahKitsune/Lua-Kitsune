#include "sqlite/LuaSQLiteMain.h"
#include "sqlite/LuaSQLite.h"

static const struct luaL_Reg lasqlitefunctions[] = {
	{ "Finish" , SQLite_Finish },
	{ "Open", SQLite_Open },
	{ "Close", SQLite_Close },
	{ "Query", SQLite_Query },	
	{ "Fetch", SQLite_Fetch },
	{ "GetRow", SQLite_GetRow },
	{ "GetColumns", SQLite_GetColumns },
	{ "SetBusyHandler", SQLite_SetBusyHandler },
	{ "RegisterFunction", SQLite_RegisterFunction },
	{ "RegisterAggregateFunction", SQLite_RegisterAggregateFunction },
	{ NULL, NULL }
};

static const luaL_Reg luasqlmeta[] = {
	{ "__gc", SQLite_Close },
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

void * SqliteMalloc(int size) {
	return kitsune_malloc(size);
}

void SqliteFree(void * ptr) {
	kitsune_free(ptr);
}

void * SqliteRealloc(void * ptr, int size) {
	return kitsune_realloc(ptr, size);
}
#endif

int luaopen_sqlite(lua_State *L) {

	sqlite3_config(SQLITE_CONFIG_MEMSTATUS, 0);
#ifdef _WIN32
	sqlite3_config(SQLITE_CONFIG_GETMALLOC, &sqlitemalloc);
	sqlitemalloc.xMalloc = SqliteMalloc;
	sqlitemalloc.xFree = SqliteFree;
	sqlitemalloc.xRealloc = SqliteRealloc;
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