#include "postgres/LuaPostgresMain.h"
#include "postgres/LuaPostgres.h"
#include "alivetoken/LuaAliveToken.h"

static const luaL_Reg postgresfunctions[] = {
	{ "Connect",        Postgres_Connect        },
	{ "IsBusy",         Postgres_IsBusy         },
	{ "EscapeValue",    Postgres_EscapeValue    },
	{ "Query",          Postgres_Query          },
	{ "NonQuery",       Postgres_NonQuery       },
	{ "Scalar",         Postgres_Scalar         },
	{ "QueryAll",       Postgres_QueryAll       },
	{ "SetAliveToken",  Postgres_SetAliveToken  },
	{ "Close",          Postgres_Close          },
	{ NULL, NULL }
};

static const luaL_Reg postgresmeta[] = {
	{ "__gc",       Postgres_Close       },
	{ "__tostring", Postgres_ToString },
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
