#include "base64/LuaBase64.h"
#include "base64/LuaBase64Main.h"

int luaopen_base64(lua_State *L) {

	BuildDecodingTable();
	lua_newtable(L);

	lua_pushstring(L, "Encode");
	lua_pushcfunction(L, Base64_Encode);
	lua_settable(L, -3);

	lua_pushstring(L, "Decode");
	lua_pushcfunction(L, Base64_Decode);
	lua_settable(L, -3);

	lua_pushstring(L, "GetEncodeTable");
	lua_pushcfunction(L, Base64_GetEncodeTable);
	lua_settable(L, -3);

	lua_pushstring(L, "SetEncodeTable");
	lua_pushcfunction(L, Base64_SetEncodeTable);
	lua_settable(L, -3);

	return 1;
}
