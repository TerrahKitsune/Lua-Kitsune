#pragma once
#include "lua_main_incl.h"

static const char* LUASOUND = "LuaSound";

// Plain-old-data userdata payload: safe to lua_newuserdata + memset(0), no
// C++ constructors involved (mirrors LuaImage's own POD + memset pattern).
typedef struct LuaSound {
	int sampleRate;
	int channels;
	int frameCount;
	float* samples;   // interleaved, -1..1, frameCount*channels floats, kitsune_malloc'd
} LuaSound;

LuaSound* lua_tosound(lua_State* L, int index);
LuaSound* lua_pushsound(lua_State* L);

// -- module-level (Sound.X) --
int Sound_New(lua_State* L);
int Sound_Tone(lua_State* L);
int Sound_Noise(lua_State* L);
int Sound_Open(lua_State* L);
int Sound_FromBytes(lua_State* L);

// -- methods (snd:X) --
int Sound_GetSampleRate(lua_State* L);
int Sound_GetChannels(lua_State* L);
int Sound_GetFrameCount(lua_State* L);
int Sound_GetDuration(lua_State* L);
int Sound_GetSample(lua_State* L);
int Sound_SetSample(lua_State* L);

int Sound_Clone(lua_State* L);
int Sound_Slice(lua_State* L);
int Sound_Concat(lua_State* L);
int Sound_Resample(lua_State* L);
int Sound_ToMono(lua_State* L);
int Sound_ToChannels(lua_State* L);

int Sound_Mix(lua_State* L);
int Sound_ApplyGain(lua_State* L);
int Sound_Fade(lua_State* L);
int Sound_Normalize(lua_State* L);
int Sound_Reverse(lua_State* L);
int Sound_Filter(lua_State* L);

int Sound_GetPeak(lua_State* L);
int Sound_GetRMS(lua_State* L);

int Sound_Save(lua_State* L);
int Sound_ToBytes(lua_State* L);

int sound_gc(lua_State* L);
int sound_tostring(lua_State* L);
