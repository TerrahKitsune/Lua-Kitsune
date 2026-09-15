#include "LuaSound.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Route dr_wav's internal allocations through the engine's tracked allocator
// so Sound buffers show up in the same GetMemory()/CollectGarbage()
// accounting as every other module (same convention as LuaImage.cpp's
// STBI_MALLOC routing).
#define DRWAV_MALLOC(sz)         kitsune_malloc(sz)
#define DRWAV_REALLOC(p, sz)     kitsune_realloc(p, sz)
#define DRWAV_FREE(p)            kitsune_free(p)
#define DR_WAV_IMPLEMENTATION
#include "dr_libs/dr_wav.h"

// ===========================================================================
// Userdata plumbing (mirrors LuaImage.cpp)
// ===========================================================================

LuaSound* lua_tosound(lua_State* L, int index) {
	// luaL_checkudata already raises a Lua error (and never returns) on mismatch.
	return (LuaSound*)luaL_checkudata(L, index, LUASOUND);
}

LuaSound* lua_pushsound(lua_State* L) {
	LuaSound* snd = (LuaSound*)lua_newuserdata(L, sizeof(LuaSound));
	if (!snd)
		luaL_error(L, "Unable to create sound userdata");
	luaL_getmetatable(L, LUASOUND);
	lua_setmetatable(L, -2);
	memset(snd, 0, sizeof(LuaSound));
	return snd;
}

int sound_tostring(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	lua_pushfstring(L, "Sound: %dch %dHz %dframes", snd->channels, snd->sampleRate, snd->frameCount);
	return 1;
}

int sound_gc(lua_State* L) {
	LuaSound* snd = (LuaSound*)lua_touserdata(L, 1);
	if (!snd)
		return 0;
	if (snd->samples) {
		kitsune_free(snd->samples);
		snd->samples = NULL;
	}
	return 0;
}

// ===========================================================================
// Small internal helpers
// ===========================================================================

static inline float ClampSample(double v) {
	return (float)(v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v));
}

static LuaSound* PushBlankSound(lua_State* L, int sampleRate, int channels, int frameCount) {
	LuaSound* snd = lua_pushsound(L);
	snd->sampleRate = sampleRate;
	snd->channels = channels;
	snd->frameCount = frameCount;
	snd->samples = (float*)kitsune_calloc((size_t)frameCount * channels, sizeof(float));
	return snd;
}

static void CheckArgs(lua_State* L, int sampleRate, int channels, int frameCount, const char* who) {
	if (sampleRate <= 0)
		luaL_error(L, "%s: sampleRate must be positive", who);
	if (channels <= 0)
		luaL_error(L, "%s: channels must be positive", who);
	if (frameCount <= 0)
		luaL_error(L, "%s: frameCount must be positive", who);
}

// ===========================================================================
// Module-level constructors
// ===========================================================================

int Sound_New(lua_State* L) {
	int sampleRate = (int)luaL_checkinteger(L, 1);
	int channels = (int)luaL_checkinteger(L, 2);
	int frameCount = (int)luaL_checkinteger(L, 3);
	CheckArgs(L, sampleRate, channels, frameCount, "Sound.New");
	PushBlankSound(L, sampleRate, channels, frameCount);
	return 1;
}

// Generates a fixed-frequency waveform on every channel identically.
// waveform: "sine" (default), "square", "triangle", "saw".
int Sound_Tone(lua_State* L) {
	int sampleRate = (int)luaL_checkinteger(L, 1);
	int channels = (int)luaL_checkinteger(L, 2);
	int frameCount = (int)luaL_checkinteger(L, 3);
	double frequency = luaL_checknumber(L, 4);
	const char* waveform = luaL_optstring(L, 5, "sine");
	double amplitude = luaL_optnumber(L, 6, 1.0);
	CheckArgs(L, sampleRate, channels, frameCount, "Sound.Tone");
	if (frequency <= 0.0)
		return luaL_error(L, "Sound.Tone: frequency must be positive");
	if (amplitude < 0.0) amplitude = 0.0;
	if (amplitude > 1.0) amplitude = 1.0;

	bool isSquare = strcmp(waveform, "square") == 0;
	bool isTriangle = strcmp(waveform, "triangle") == 0;
	bool isSaw = strcmp(waveform, "saw") == 0;
	bool isSine = strcmp(waveform, "sine") == 0;
	if (!isSine && !isSquare && !isTriangle && !isSaw)
		return luaL_error(L, "Sound.Tone: waveform must be \"sine\", \"square\", \"triangle\", or \"saw\"");

	LuaSound* snd = PushBlankSound(L, sampleRate, channels, frameCount);
	double phaseStep = frequency / sampleRate;
	for (int i = 0; i < frameCount; i++) {
		double phase = fmod(i * phaseStep, 1.0);
		double v;
		if (isSine)          v = sin(2.0 * M_PI * phase);
		else if (isSquare)   v = phase < 0.5 ? 1.0 : -1.0;
		else if (isTriangle) v = 2.0 * fabs(2.0 * (phase - floor(phase + 0.5))) - 1.0;
		else /* saw */       v = 2.0 * (phase - floor(phase + 0.5));
		float sample = ClampSample(v * amplitude);
		for (int c = 0; c < channels; c++)
			snd->samples[(size_t)i * channels + c] = sample;
	}
	return 1;
}

int Sound_Noise(lua_State* L) {
	int sampleRate = (int)luaL_checkinteger(L, 1);
	int channels = (int)luaL_checkinteger(L, 2);
	int frameCount = (int)luaL_checkinteger(L, 3);
	double amplitude = luaL_optnumber(L, 4, 1.0);
	CheckArgs(L, sampleRate, channels, frameCount, "Sound.Noise");
	if (amplitude < 0.0) amplitude = 0.0;
	if (amplitude > 1.0) amplitude = 1.0;

	LuaSound* snd = PushBlankSound(L, sampleRate, channels, frameCount);
	size_t count = (size_t)frameCount * channels;
	for (size_t i = 0; i < count; i++) {
		double v = ((double)rand() / RAND_MAX) * 2.0 - 1.0;
		snd->samples[i] = ClampSample(v * amplitude);
	}
	return 1;
}

int Sound_Open(lua_State* L) {
	const char* path = luaL_checkstring(L, 1);

	FILE* f = fopen(path, "rb");
	if (!f)
		return luaL_error(L, "Sound.Open: cannot open '%s'", path);
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (size <= 0) {
		fclose(f);
		return luaL_error(L, "Sound.Open: '%s' is empty or unreadable", path);
	}
	unsigned char* buf = (unsigned char*)kitsune_malloc((size_t)size);
	size_t read = fread(buf, 1, (size_t)size, f);
	fclose(f);
	if (read != (size_t)size) {
		kitsune_free(buf);
		return luaL_error(L, "Sound.Open: failed reading '%s'", path);
	}

	unsigned int channels, sampleRate;
	drwav_uint64 frameCount;
	float* samples = drwav_open_memory_and_read_pcm_frames_f32(buf, (size_t)size, &channels, &sampleRate, &frameCount, NULL);
	kitsune_free(buf);
	if (!samples)
		return luaL_error(L, "Sound.Open: failed to decode '%s' (not a valid WAV file)", path);

	LuaSound* snd = lua_pushsound(L);
	snd->sampleRate = (int)sampleRate;
	snd->channels = (int)channels;
	snd->frameCount = (int)frameCount;
	snd->samples = samples;
	return 1;
}

int Sound_FromBytes(lua_State* L) {
	size_t len;
	const char* data = luaL_checklstring(L, 1, &len);

	unsigned int channels, sampleRate;
	drwav_uint64 frameCount;
	float* samples = drwav_open_memory_and_read_pcm_frames_f32(data, len, &channels, &sampleRate, &frameCount, NULL);
	if (!samples)
		return luaL_error(L, "Sound.FromBytes: failed to decode (not a valid WAV buffer)");

	LuaSound* snd = lua_pushsound(L);
	snd->sampleRate = (int)sampleRate;
	snd->channels = (int)channels;
	snd->frameCount = (int)frameCount;
	snd->samples = samples;
	return 1;
}

// ===========================================================================
// Accessors
// ===========================================================================

int Sound_GetSampleRate(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	lua_pushinteger(L, snd->sampleRate);
	return 1;
}

int Sound_GetChannels(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	lua_pushinteger(L, snd->channels);
	return 1;
}

int Sound_GetFrameCount(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	lua_pushinteger(L, snd->frameCount);
	return 1;
}

int Sound_GetDuration(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	lua_pushnumber(L, (double)snd->frameCount / snd->sampleRate);
	return 1;
}

int Sound_GetSample(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	int frame = (int)luaL_checkinteger(L, 2);
	int channel = (int)luaL_checkinteger(L, 3);
	if (frame < 0 || frame >= snd->frameCount || channel < 0 || channel >= snd->channels)
		return luaL_error(L, "Sound:GetSample: (%d,%d) out of bounds for %d frames / %d channels", frame, channel, snd->frameCount, snd->channels);
	lua_pushnumber(L, snd->samples[(size_t)frame * snd->channels + channel]);
	return 1;
}

int Sound_SetSample(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	int frame = (int)luaL_checkinteger(L, 2);
	int channel = (int)luaL_checkinteger(L, 3);
	double value = luaL_checknumber(L, 4);
	if (frame < 0 || frame >= snd->frameCount || channel < 0 || channel >= snd->channels)
		return luaL_error(L, "Sound:SetSample: (%d,%d) out of bounds for %d frames / %d channels", frame, channel, snd->frameCount, snd->channels);
	snd->samples[(size_t)frame * snd->channels + channel] = ClampSample(value);
	return 0;
}

// ===========================================================================
// Non-mutating transforms (Clone/Slice/Concat/Resample)
// ===========================================================================

int Sound_Clone(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	LuaSound* out = PushBlankSound(L, snd->sampleRate, snd->channels, snd->frameCount);
	memcpy(out->samples, snd->samples, (size_t)snd->frameCount * snd->channels * sizeof(float));
	return 1;
}

int Sound_Slice(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	int startFrame = (int)luaL_checkinteger(L, 2);
	int frameCount = (int)luaL_checkinteger(L, 3);
	if (frameCount <= 0 || startFrame < 0 || startFrame + frameCount > snd->frameCount)
		return luaL_error(L, "Sound:Slice: range (%d,%d) out of bounds for %d frames", startFrame, frameCount, snd->frameCount);

	LuaSound* out = PushBlankSound(L, snd->sampleRate, snd->channels, frameCount);
	memcpy(out->samples, snd->samples + (size_t)startFrame * snd->channels, (size_t)frameCount * snd->channels * sizeof(float));
	return 1;
}

int Sound_Concat(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	LuaSound* other = lua_tosound(L, 2);
	if (snd->channels != other->channels)
		return luaL_error(L, "Sound:Concat: channel count mismatch (%d vs %d)", snd->channels, other->channels);
	if (snd->sampleRate != other->sampleRate)
		return luaL_error(L, "Sound:Concat: sample rate mismatch (%d vs %d) -- Resample one first", snd->sampleRate, other->sampleRate);

	int frameCount = snd->frameCount + other->frameCount;
	LuaSound* out = PushBlankSound(L, snd->sampleRate, snd->channels, frameCount);
	memcpy(out->samples, snd->samples, (size_t)snd->frameCount * snd->channels * sizeof(float));
	memcpy(out->samples + (size_t)snd->frameCount * snd->channels, other->samples, (size_t)other->frameCount * other->channels * sizeof(float));
	return 1;
}

int Sound_Resample(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	int newSampleRate = (int)luaL_checkinteger(L, 2);
	if (newSampleRate <= 0)
		return luaL_error(L, "Sound:Resample: sampleRate must be positive");

	double ratio = (double)newSampleRate / snd->sampleRate;
	int newFrameCount = MAX(1, (int)(snd->frameCount * ratio + 0.5));
	LuaSound* out = PushBlankSound(L, newSampleRate, snd->channels, newFrameCount);

	double step = (double)snd->frameCount / newFrameCount;
	for (int i = 0; i < newFrameCount; i++) {
		double srcPos = i * step;
		int f0 = (int)floor(srcPos);
		double w = srcPos - f0;
		int f1 = MIN(f0 + 1, snd->frameCount - 1);
		f0 = MIN(f0, snd->frameCount - 1);
		for (int c = 0; c < snd->channels; c++) {
			double a = snd->samples[(size_t)f0 * snd->channels + c];
			double b = snd->samples[(size_t)f1 * snd->channels + c];
			out->samples[(size_t)i * snd->channels + c] = ClampSample(a * (1.0 - w) + b * w);
		}
	}
	return 1;
}

// ===========================================================================
// In-place editing (Mix/ApplyGain/Fade/Normalize/Reverse)
// ===========================================================================

// Additive mix, clamped -- clips silently at the buffer's edges the same way
// Image:Composite clips off-canvas pixels, rather than growing self.
int Sound_Mix(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	LuaSound* other = lua_tosound(L, 2);
	int atFrame = (int)luaL_checkinteger(L, 3);
	double gain = luaL_optnumber(L, 4, 1.0);
	if (snd->channels != other->channels)
		return luaL_error(L, "Sound:Mix: channel count mismatch (%d vs %d)", snd->channels, other->channels);

	for (int i = 0; i < other->frameCount; i++) {
		int destFrame = atFrame + i;
		if (destFrame < 0 || destFrame >= snd->frameCount)
			continue;
		for (int c = 0; c < snd->channels; c++) {
			float* dst = &snd->samples[(size_t)destFrame * snd->channels + c];
			double v = *dst + other->samples[(size_t)i * other->channels + c] * gain;
			*dst = ClampSample(v);
		}
	}
	return 0;
}

int Sound_ApplyGain(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	double gain = luaL_checknumber(L, 2);
	int startFrame = (int)luaL_optinteger(L, 3, 0);
	int frameCount = (int)luaL_optinteger(L, 4, snd->frameCount - startFrame);
	int x0 = MAX(startFrame, 0), x1 = MIN(startFrame + frameCount, snd->frameCount);

	for (int i = x0; i < x1; i++)
		for (int c = 0; c < snd->channels; c++) {
			float* p = &snd->samples[(size_t)i * snd->channels + c];
			*p = ClampSample(*p * gain);
		}
	return 0;
}

// Applies a linear gain envelope from fromGain to toGain across the given
// range -- call twice (e.g. 0..1 then 1..0) to build a fade-in/fade-out.
int Sound_Fade(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	int startFrame = (int)luaL_checkinteger(L, 2);
	int frameCount = (int)luaL_checkinteger(L, 3);
	double fromGain = luaL_checknumber(L, 4);
	double toGain = luaL_checknumber(L, 5);
	if (frameCount <= 0 || startFrame < 0 || startFrame + frameCount > snd->frameCount)
		return luaL_error(L, "Sound:Fade: range (%d,%d) out of bounds for %d frames", startFrame, frameCount, snd->frameCount);

	for (int i = 0; i < frameCount; i++) {
		double t = frameCount > 1 ? (double)i / (frameCount - 1) : 0.0;
		double gain = fromGain + (toGain - fromGain) * t;
		for (int c = 0; c < snd->channels; c++) {
			float* p = &snd->samples[(size_t)(startFrame + i) * snd->channels + c];
			*p = ClampSample(*p * gain);
		}
	}
	return 0;
}

int Sound_Normalize(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	double targetPeak = luaL_optnumber(L, 2, 1.0);
	if (targetPeak <= 0.0)
		return luaL_error(L, "Sound:Normalize: targetPeak must be positive");

	size_t count = (size_t)snd->frameCount * snd->channels;
	float peak = 0.0f;
	for (size_t i = 0; i < count; i++) {
		float a = fabsf(snd->samples[i]);
		if (a > peak) peak = a;
	}
	if (peak <= 0.0f)
		return 0; // silence stays silence

	double scale = targetPeak / peak;
	for (size_t i = 0; i < count; i++)
		snd->samples[i] = ClampSample(snd->samples[i] * scale);
	return 0;
}

int Sound_Reverse(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	int channels = snd->channels;
	for (int i = 0, j = snd->frameCount - 1; i < j; i++, j--) {
		for (int c = 0; c < channels; c++) {
			float tmp = snd->samples[(size_t)i * channels + c];
			snd->samples[(size_t)i * channels + c] = snd->samples[(size_t)j * channels + c];
			snd->samples[(size_t)j * channels + c] = tmp;
		}
	}
	return 0;
}

// ===========================================================================
// Analysis (read-only)
// ===========================================================================

int Sound_GetPeak(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	size_t count = (size_t)snd->frameCount * snd->channels;
	float mn = 0.0f, mx = 0.0f;
	for (size_t i = 0; i < count; i++) {
		float v = snd->samples[i];
		if (v < mn) mn = v;
		if (v > mx) mx = v;
	}
	lua_pushnumber(L, mn);
	lua_pushnumber(L, mx);
	return 2;
}

int Sound_GetRMS(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	size_t count = (size_t)snd->frameCount * snd->channels;
	if (count == 0) {
		lua_pushnumber(L, 0.0);
		return 1;
	}
	double sumSq = 0.0;
	for (size_t i = 0; i < count; i++) {
		double v = snd->samples[i];
		sumSq += v * v;
	}
	lua_pushnumber(L, sqrt(sumSq / count));
	return 1;
}

// ===========================================================================
// Encode / persist -- always emits 16-bit PCM WAV, the most broadly
// compatible subformat and the one SDL_mixer's Mix_LoadWAV_RW already
// expects, so a Sound saved here is directly playable via SDL.Audio.LoadRaw.
// ===========================================================================

static unsigned char* EncodeToWav(const LuaSound* snd, size_t* outLen) {
	drwav_data_format format;
	format.container = drwav_container_riff;
	format.format = DR_WAVE_FORMAT_PCM;
	format.channels = (drwav_uint32)snd->channels;
	format.sampleRate = (drwav_uint32)snd->sampleRate;
	format.bitsPerSample = 16;

	size_t count = (size_t)snd->frameCount * snd->channels;
	int16_t* pcm = (int16_t*)kitsune_malloc(count * sizeof(int16_t));
	for (size_t i = 0; i < count; i++) {
		float v = snd->samples[i];
		if (v < -1.0f) v = -1.0f;
		if (v > 1.0f) v = 1.0f;
		double scaled = v * 32767.0;
		pcm[i] = (int16_t)(scaled >= 0.0 ? scaled + 0.5 : scaled - 0.5);
	}

	drwav wav;
	void* pData = NULL;
	size_t dataSize = 0;
	if (!drwav_init_memory_write(&wav, &pData, &dataSize, &format, NULL)) {
		kitsune_free(pcm);
		return NULL;
	}
	drwav_write_pcm_frames(&wav, (drwav_uint64)snd->frameCount, pcm);
	drwav_uninit(&wav);
	kitsune_free(pcm);

	*outLen = dataSize;
	return (unsigned char*)pData;
}

int Sound_Save(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	const char* path = luaL_checkstring(L, 2);

	size_t len;
	unsigned char* data = EncodeToWav(snd, &len);
	if (!data)
		return luaL_error(L, "Sound:Save: failed to encode WAV");

	FILE* f = fopen(path, "wb");
	if (!f) {
		kitsune_free(data);
		return luaL_error(L, "Sound:Save: cannot open '%s' for writing", path);
	}
	size_t written = fwrite(data, 1, len, f);
	fclose(f);
	kitsune_free(data);
	if (written != len)
		return luaL_error(L, "Sound:Save: short write to '%s'", path);

	lua_pushinteger(L, (lua_Integer)written);
	return 1;
}

int Sound_ToBytes(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	size_t len;
	unsigned char* data = EncodeToWav(snd, &len);
	if (!data)
		return luaL_error(L, "Sound:ToBytes: failed to encode WAV");
	lua_pushlstring(L, (const char*)data, len);
	kitsune_free(data);
	return 1;
}
