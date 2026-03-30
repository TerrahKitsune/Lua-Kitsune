#include "luacsv.h"
#include "LuaCsvMain.h"

static const struct luaL_Reg csvfunctions[] = {
	{ "Decode",              LuaDecodeCsv          },
	{ "Encode",              LuaEncodeCsv          },
	{ "DecodeFromFunction",  LuaDecodeFromFunction  },
	{ "New",                 LuaCsvNew              },
	{ NULL, NULL }
};

int luaopen_csv(lua_State* L) {
	luaL_newlibtable(L, csvfunctions);
	luaL_setfuncs(L, csvfunctions, 0);
	return 1;
}
