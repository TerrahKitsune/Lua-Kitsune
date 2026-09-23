#include "luatextmain.h"
#include "luatext.h"

static const struct luaL_Reg textfunctions[] = {
	{ "ToUtf16",      TextToUtf16 },
	{ "FromUtf16",    TextFromUtf16 },
	{ "FromCodepage", TextFromCodepage },
	{ "ToCodepage",   TextToCodepage },
	{ "Lower",        TextLower },
	{ "Upper",        TextUpper },
	{ NULL, NULL }
};

int luaopen_text(lua_State* L) {

	luaL_newlibtable(L, textfunctions);
	luaL_setfuncs(L, textfunctions, 0);
	return 1;
}
