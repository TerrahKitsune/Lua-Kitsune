#include "luatts.h"
#include "LuaTTSMain.h"

static const struct luaL_Reg ttsfunctions[] = {

	{ "GetIsSpeaking", GetIsSpeaking },
	{ "Speak", Speak },
	{ "Skip", Skip },
	{ "PlayPause", PlayPause },
	{ "GetIsPaused", GetIsPaused },
	{ "GetVolume", GetVolume },
	{ "SetVolume", SetVolume },
	{ "GetRate", GetRate },
	{ "SetRate", SetRate },
	{ "Create", CreateTTS },
	{ "Dispose", tts_gc },
	{ "GetVoices", GetVoices },
	{ "SetVoice", SetVoice },
	{ NULL, NULL }
};

static const luaL_Reg ttsmeta[] = {
	{ "__gc",  tts_gc },
	{ "__tostring",  tts_tostring },
{ NULL, NULL }
};

int luaopen_tts(lua_State* L) {

	luaL_newlibtable(L, ttsfunctions);
	luaL_setfuncs(L, ttsfunctions, 0);

	luaL_newmetatable(L, TTS);
	luaL_setfuncs(L, ttsmeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}