#include "LuaDuckDBMain.h"
#include "LuaDuckDB.h"

static const struct luaL_Reg laduckdbfunctions[] = {
{ "Open", DuckDB_Open },
{ "Close", DuckDB_GC },
{ "Execute", DuckDB_Execute },
{ "Query", DuckDB_Execute },
{ "Fetch", DuckDB_Fetch },
{ "GetRow", DuckDB_GetRow },
{ "Finish", DuckDB_Finish },
{ NULL, NULL }
};

static const luaL_Reg luaduckdbmeta[] = {
{ "__gc", DuckDB_GC },
{ "__tostring", DuckDB_ToString },
{ NULL, NULL }
};

int luaopen_duckdb(lua_State *L) {
luaL_newlibtable(L, laduckdbfunctions);
luaL_setfuncs(L, laduckdbfunctions, 0);

luaL_newmetatable(L, LUADUCKDB);
luaL_setfuncs(L, luaduckdbmeta, 0);

lua_pushliteral(L, "__index");
lua_pushvalue(L, -3);
lua_rawset(L, -3);
lua_pushliteral(L, "__metatable");
lua_pushvalue(L, -3);
lua_rawset(L, -3);

lua_pop(L, 1);
return 1;
}
