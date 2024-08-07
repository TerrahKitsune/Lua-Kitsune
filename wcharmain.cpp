#include "luawchar.h"
#include "wcharmain.h"

static const struct luaL_Reg wcharfunctions[] = {
	{ "Find", WcharFind},
	{ "FromAnsi",  FromAnsi },
	{ "ToAnsi", ToAnsi },
	{ "FromUtf8", FromUtf8 },
	{ "ToUtf8", ToUtf8 },
	{ "Substring", FromSubstring },
	{ "ToWide", ToWide },
	{ "ToLower", FromToLower },
	{ "ToUpper", FromToUpper },
	{ "ToBytes", ToBytes },
	{ "FromBytes", FromBytes },
	{ "Codepoints", GetCodepoints },
	{ "At", GetCharacterAt },
	{ "len", wchar_len },
	{ "Setlocale", SetLocale },
	{ NULL, NULL }
};

static const luaL_Reg wcharmeta[] = {
	{ "__len", wchar_len },
	{ "__eq", wchar_eq },
	{ "__concat", wchar_concat },
	{ "__gc",  wchar_gc },
	{ "__tostring",  wchar_tostring },
	{ NULL, NULL }
};

int luaopen_wchar(lua_State* L) {

	luaL_newlibtable(L, wcharfunctions);
	luaL_setfuncs(L, wcharfunctions, 0);

	luaL_newmetatable(L, LUAWCHAR);
	luaL_setfuncs(L, wcharmeta, 0);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}