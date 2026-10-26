#include "text/LuaTextMain.h"
#include "text/LuaText.h"

static const struct luaL_Reg textfunctions[] = {
	{ "ToUtf16",      Text_ToUtf16 },
	{ "FromUtf16",    Text_FromUtf16 },
	{ "FromCodepage", Text_FromCodepage },
	{ "ToCodepage",   Text_ToCodepage },
	{ "Lower",        Text_Lower },
	{ "Upper",        Text_Upper },
	{ NULL, NULL }
};

int luaopen_text(lua_State* L) {

	luaL_newlibtable(L, textfunctions);
	luaL_setfuncs(L, textfunctions, 0);
	return 1;
}
