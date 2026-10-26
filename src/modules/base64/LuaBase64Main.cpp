#include "base64/LuaBase64.h"
#include "base64/LuaBase64Main.h"

int luaopen_base64(lua_State *L) {

	build_decoding_table();
	lua_newtable(L);

	lua_pushstring(L, "Encode");
	lua_pushcfunction(L, lua_base64encode);
	lua_settable(L, -3);

	lua_pushstring(L, "Decode");
	lua_pushcfunction(L, lua_base64decode);
	lua_settable(L, -3);

	lua_pushstring(L, "GetEncodeTable");
	lua_pushcfunction(L, lua_base64getencodingtable);
	lua_settable(L, -3);

	lua_pushstring(L, "SetEncodeTable");
	lua_pushcfunction(L, lua_base64setencodingtable);
	lua_settable(L, -3);

	return 1;
}
