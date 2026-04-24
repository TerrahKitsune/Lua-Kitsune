#include "MySQLMain.h"
#include "LuaMySQL.h"
#include "luaalivetoken.h"

static const luaL_Reg connfunctions[] = {
	{ "Connect",        MySqlConnect        },
	{ "IsBusy",         MySqlIsBusy         },
	{ "EscapeValue",    MySqlEscapeValue    },
	{ "Query",          MySqlQuery          },
	{ "NonQuery",       MySqlNonQuery       },
	{ "Scalar",         MySqlScalar         },
	{ "QueryAll",       MySqlQueryAll       },
	{ "SetAliveToken",  MySqlSetAliveToken  },
	{ "Close",          luamysql_gc         },
	{ NULL, NULL }
};

static const luaL_Reg connmeta[] = {
	{ "__gc",       luamysql_gc       },
	{ "__tostring", luamysql_tostring },
	{ NULL, NULL }
};

int luaopen_mysql(lua_State* L) {

	luaL_newlibtable(L, connfunctions);
	luaL_setfuncs(L, connfunctions, 0);

	luaL_newmetatable(L, LUAMYSQL);
	luaL_setfuncs(L, connmeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);

	return 1;
}