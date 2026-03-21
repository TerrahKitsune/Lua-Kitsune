#include "PostgresMain.h"
#include "LuaPostgres.h"

static const struct luaL_Reg postgresfunctions[] = {

{ "IsBusy", PostgresIsBusy },
{ "GetResultRow", PostgresGetResultRow },
{ "GetResult", PostgresGetResult },
{ "EscapeValue", PostgresEscapeValue },
{ "Query", PostgresQuery },
{ "QueryParams", PostgresQueryParams },
{ "GetResultFields", PostgresGetResultFields },
{ "Connect", PostgresConnect },
{ "Close", luapostgres_gc },
{ NULL, NULL }
};

static const luaL_Reg postgresmeta[] = {
{ "__gc", luapostgres_gc },
{ "__tostring", luapostgres_tostring },
{ NULL, NULL }
};

int luaopen_postgres(lua_State* L) {

luaL_newlibtable(L, postgresfunctions);
luaL_setfuncs(L, postgresfunctions, 0);

luaL_newmetatable(L, LUAPOSTGRES);
luaL_setfuncs(L, postgresmeta, 0);

lua_pushliteral(L, "__index");
lua_pushvalue(L, -3);
lua_rawset(L, -3);
lua_pushliteral(L, "__metatable");
lua_pushvalue(L, -3);
lua_rawset(L, -3);

lua_pop(L, 1);

return 1;
}