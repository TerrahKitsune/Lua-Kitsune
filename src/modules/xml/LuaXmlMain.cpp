#include "xml/LuaXml.h"
#include "xml/LuaXmlMain.h"

static const struct luaL_Reg xmlFunctions[] = {
	{ "New",     Xml_New    },  // Xml.New([indent])
	{ "Decode",  Xml_Decode },  // xml:Decode(string)
	{ "Encode",  Xml_Encode },  // xml:Encode(table)
	{ "Dispose", Xml_Dispose     },  // xml:Dispose()
	{ NULL, NULL }
};

static const struct luaL_Reg xmlMeta[] = {
	{ "__gc",       Xml_Dispose       },
	{ "__tostring", Xml_ToString },
	{ NULL, NULL }
};

int luaopen_xml(lua_State* L) {
	EnsureAllocator();
	luaL_newlibtable(L, xmlFunctions);
	luaL_setfuncs(L, xmlFunctions, 0);

	// Instance metatable: __gc, __tostring, and __index = module table so all
	// xmlFunctions are reachable as both Xml.Xxx() and xml:Xxx().
	luaL_newmetatable(L, LUAXML);
	luaL_setfuncs(L, xmlMeta, 0);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);   // module table
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);   // module table
	lua_rawset(L, -3);

	lua_pop(L, 1);   // pop metatable
	return 1;        // return module table
}
