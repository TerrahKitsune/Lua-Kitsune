#include "LuaSoundMain.h"
#include "LuaSound.h"

static const struct luaL_Reg soundfunctions[] = {
	{ "New",       Sound_New },
	{ "Tone",      Sound_Tone },
	{ "Noise",     Sound_Noise },
	{ "Open",      Sound_Open },
	{ "FromBytes", Sound_FromBytes },

	{ "GetSampleRate", Sound_GetSampleRate },
	{ "GetChannels",   Sound_GetChannels },
	{ "GetFrameCount", Sound_GetFrameCount },
	{ "GetDuration",   Sound_GetDuration },
	{ "GetSample",     Sound_GetSample },
	{ "SetSample",     Sound_SetSample },

	{ "Clone",    Sound_Clone },
	{ "Slice",    Sound_Slice },
	{ "Concat",   Sound_Concat },
	{ "Resample", Sound_Resample },

	{ "Mix",       Sound_Mix },
	{ "ApplyGain", Sound_ApplyGain },
	{ "Fade",      Sound_Fade },
	{ "Normalize", Sound_Normalize },
	{ "Reverse",   Sound_Reverse },

	{ "GetPeak", Sound_GetPeak },
	{ "GetRMS",  Sound_GetRMS },

	{ "Save",    Sound_Save },
	{ "ToBytes", Sound_ToBytes },
	{ NULL, NULL }
};

static const luaL_Reg soundmeta[] = {
	{ "__gc", sound_gc },
	{ "__tostring", sound_tostring },
	{ NULL, NULL }
};

int luaopen_sound(lua_State *L) {

	luaL_newlibtable(L, soundfunctions);
	luaL_setfuncs(L, soundfunctions, 0);

	luaL_newmetatable(L, LUASOUND);
	luaL_setfuncs(L, soundmeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}
