#include "alivetoken/LuaAliveToken.h"
#include "alivetoken/LuaAliveTokenMain.h"

static const struct luaL_Reg alivetokenFunctions[] = {
	{ "New",         AliveToken_New           },
	{ "IsAlive",     AliveToken_IsAlive },
	{ "Dispose",     AliveToken_Dispose       },
	{ "ErrorIfDead", AliveToken_ErrorIfDead  },
	{ "Link",        AliveToken_Link          },
	{ NULL, NULL }
};

static const struct luaL_Reg alivetokenMeta[] = {
	{ "__gc",       AliveToken_GC       },
	{ "__tostring", AliveToken_ToString },
	{ NULL, NULL }
};

int luaopen_alivetoken(lua_State* L) {
	luaL_newlibtable(L, alivetokenFunctions);
	luaL_setfuncs(L, alivetokenFunctions, 0);

	luaL_newmetatable(L, LUAALIVETOKEN);
	luaL_setfuncs(L, alivetokenMeta, 0);

	// __index = module table so all functions are reachable as both
	// AliveToken.Xxx() and token:Xxx() — the standard wchar pattern.
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);   // pop metatable — module table is now on top

	// Create the app-level token, store it in the registry and expose it as AliveToken.App.
	AliveToken_New(L);   // pushes a new token onto the stack
	lua_pushvalue(L, -1);    // dup for registry
	lua_rawsetp(L, LUA_REGISTRYINDEX, AliveTokenAppRegistryKey());
	lua_pushliteral(L, "App");
	lua_insert(L, -2);       // swap: key below value
	lua_rawset(L, -3);       // module["App"] = token

	return 1;        // return module table
}
