#include "mysql/LuaMySQLMain.h"
#include "mysql/LuaMySQL.h"
#include "alivetoken/LuaAliveToken.h"

static const luaL_Reg connfunctions[] = {
	{ "Connect",        MySQL_Connect        },
	{ "IsBusy",         MySQL_IsBusy         },
	{ "EscapeValue",    MySQL_EscapeValue    },
	{ "Query",          MySQL_Query          },
	{ "NonQuery",       MySQL_NonQuery       },
	{ "Scalar",         MySQL_Scalar         },
	{ "QueryAll",       MySQL_QueryAll       },
	{ "SetAliveToken",  MySQL_SetAliveToken  },
	{ "Close",          MySQL_Close         },
	{ NULL, NULL }
};

static const luaL_Reg connmeta[] = {
	{ "__gc",       MySQL_Close       },
	{ "__tostring", MySQL_ToString },
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