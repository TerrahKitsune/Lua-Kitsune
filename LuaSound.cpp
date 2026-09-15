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

#include "LuaSoundVorbis.h"

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

// White noise is flat across the whole spectrum -- every sample independent,
// no correlation to its neighbors. Band-limiting it with Filter() changes
// how *bright* it sounds but not its underlying grain, so it still reads as
// "static" rather than a soft hiss no matter the cutoff. Pink ("Paul
// Kellet's economy" 3-pole approximation, the standard cheap trick for
// this) and brown (a simple leaky integrator) noise have naturally more
// energy at low frequencies and less at high, which is what actual tape
// hiss/steam/wind sound like even unfiltered.
// Box-Muller transform: two independent uniform (0,1] samples -> one
// standard-normal (Gaussian) sample. Naively using a *uniform* random value
// as a "noise sample" (every value between -1 and 1 equally likely) gives a
// harsher, more "digital/buzzy" edge than real-world analog noise, which is
// Gaussian-distributed (values near zero are far more common than the
// extremes) -- this alone is a big part of why a naive noise generator
// reads as "static" no matter how it's spectrally shaped afterwards.
static double GaussianSample() {
	double u1 = ((double)rand() + 1.0) / ((double)RAND_MAX + 1.0); // avoid log(0)
	double u2 = (double)rand() / RAND_MAX;
	return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

int Sound_Noise(lua_State* L) {
	int sampleRate = (int)luaL_checkinteger(L, 1);
	int channels = (int)luaL_checkinteger(L, 2);
	int frameCount = (int)luaL_checkinteger(L, 3);
	double amplitude = luaL_optnumber(L, 4, 1.0);
	const char* noiseType = luaL_optstring(L, 5, "white");
	CheckArgs(L, sampleRate, channels, frameCount, "Sound.Noise");
	if (amplitude < 0.0) amplitude = 0.0;
	if (amplitude > 1.0) amplitude = 1.0;

	bool isWhite = strcmp(noiseType, "white") == 0;
	bool isPink = strcmp(noiseType, "pink") == 0;
	bool isBrown = strcmp(noiseType, "brown") == 0;
	if (!isWhite && !isPink && !isBrown)
		return luaL_error(L, "Sound.Noise: noiseType must be \"white\", \"pink\", or \"brown\"");

	LuaSound* snd = PushBlankSound(L, sampleRate, channels, frameCount);

	// All three types carry per-channel state (pink/brown's filters; even
	// "white" now needs a per-sample Gaussian draw rather than a bulk fill),
	// so every channel is generated independently -- otherwise channel 2's
	// "previous sample" would really be channel 1's.
	double* raw = (double*)kitsune_malloc((size_t)frameCount * sizeof(double));
	for (int c = 0; c < channels; c++) {
		double peak = 0.0;

		if (isWhite) {
			for (int i = 0; i < frameCount; i++) {
				double v = GaussianSample();
				raw[i] = v;
				double a = fabs(v);
				if (a > peak) peak = a;
			}
		}
		else if (isPink) {
			double b0 = 0.0, b1 = 0.0, b2 = 0.0;
			for (int i = 0; i < frameCount; i++) {
				double w = GaussianSample();
				b0 = 0.99886 * b0 + w * 0.0555179;
				b1 = 0.99332 * b1 + w * 0.0750759;
				b2 = 0.96900 * b2 + w * 0.1538520;
				double v = b0 + b1 + b2 + w * 0.5362;
				raw[i] = v;
				double a = fabs(v);
				if (a > peak) peak = a;
			}
		}
		else { // brown
			double acc = 0.0;
			for (int i = 0; i < frameCount; i++) {
				double w = GaussianSample();
				acc = (acc + w * 0.02) / 1.02; // leaky integrator, bounded by construction
				raw[i] = acc;
				double a = fabs(acc);
				if (a > peak) peak = a;
			}
		}

		// A Gaussian draw is technically unbounded (just increasingly
		// unlikely at the extremes), so -- same as pink/brown already did --
		// normalize per channel to the actual peak reached rather than
		// assuming a fixed range, guaranteeing the requested amplitude is
		// hit exactly regardless of type.
		double scale = peak > 0.0 ? (amplitude / peak) : 0.0;
		for (int i = 0; i < frameCount; i++)
			snd->samples[(size_t)i * channels + c] = ClampSample(raw[i] * scale);
	}
	kitsune_free(raw);
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

	if (IsOggData(buf, (size_t)size)) {
		LuaSound tmp; memset(&tmp, 0, sizeof(tmp));
		const char* errMsg = NULL;
		bool ok = DecodeOgg(buf, (size_t)size, &tmp, &errMsg);
		kitsune_free(buf);
		if (!ok)
			return luaL_error(L, "Sound.Open: failed to decode '%s' (%s)", path, errMsg);
		*lua_pushsound(L) = tmp;
		return 1;
	}

	unsigned int channels, sampleRate;
	drwav_uint64 frameCount;
	float* samples = drwav_open_memory_and_read_pcm_frames_f32(buf, (size_t)size, &channels, &sampleRate, &frameCount, NULL);
	kitsune_free(buf);
	if (!samples)
		return luaL_error(L, "Sound.Open: failed to decode '%s' (not a valid WAV or OGG file)", path);

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

	if (IsOggData((const unsigned char*)data, len)) {
		LuaSound tmp; memset(&tmp, 0, sizeof(tmp));
		const char* errMsg = NULL;
		if (!DecodeOgg((const unsigned char*)data, len, &tmp, &errMsg))
			return luaL_error(L, "Sound.FromBytes: failed to decode (%s)", errMsg);
		*lua_pushsound(L) = tmp;
		return 1;
	}

	unsigned int channels, sampleRate;
	drwav_uint64 frameCount;
	float* samples = drwav_open_memory_and_read_pcm_frames_f32(data, len, &channels, &sampleRate, &frameCount, NULL);
	if (!samples)
		return luaL_error(L, "Sound.FromBytes: failed to decode (not a valid WAV or OGG buffer)");

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

// Downmixes to mono (average of the source channels) and broadcasts that to
// n output channels; n == channels is just an independent copy, same as
// Clone. Shared by Sound_ToMono/Sound_ToChannels.
static LuaSound* ToChannelsImpl(lua_State* L, const LuaSound* snd, int n) {
	if (n == snd->channels) {
		LuaSound* out = PushBlankSound(L, snd->sampleRate, snd->channels, snd->frameCount);
		memcpy(out->samples, snd->samples, (size_t)snd->frameCount * snd->channels * sizeof(float));
		return out;
	}

	LuaSound* out = PushBlankSound(L, snd->sampleRate, n, snd->frameCount);
	for (int i = 0; i < snd->frameCount; i++) {
		double mono = 0.0;
		for (int c = 0; c < snd->channels; c++)
			mono += snd->samples[(size_t)i * snd->channels + c];
		mono /= snd->channels;
		float v = ClampSample(mono);
		for (int c = 0; c < n; c++)
			out->samples[(size_t)i * n + c] = v;
	}
	return out;
}

int Sound_ToMono(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	ToChannelsImpl(L, snd, 1);
	return 1;
}

int Sound_ToChannels(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	int n = (int)luaL_checkinteger(L, 2);
	if (n <= 0)
		return luaL_error(L, "Sound:ToChannels: channels must be positive");
	ToChannelsImpl(L, snd, n);
	return 1;
}

// ===========================================================================
// In-place editing (Mix/ApplyGain/Fade/Normalize/Reverse/Filter)
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

// Standard biquad (2nd-order IIR) -- the same "Audio EQ Cookbook" (Robert
// Bristow-Johnson) formulas used by the Web Audio API's BiquadFilterNode and
// most audio engines. Operates on the whole buffer (not a sub-range like
// ApplyGain/Fade): a filter carries state between samples, so filtering only
// part of a buffer would leave an unprimed discontinuity (an audible click)
// at the boundary for no practical benefit.
int Sound_Filter(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	const char* type = luaL_checkstring(L, 2);
	double cutoffHz = luaL_checknumber(L, 3);
	double q = luaL_optnumber(L, 4, 0.7071067811865476);

	bool isLowpass = strcmp(type, "lowpass") == 0;
	bool isHighpass = strcmp(type, "highpass") == 0;
	bool isBandpass = strcmp(type, "bandpass") == 0;
	bool isNotch = strcmp(type, "notch") == 0;
	if (!isLowpass && !isHighpass && !isBandpass && !isNotch)
		return luaL_error(L, "Sound:Filter: type must be \"lowpass\", \"highpass\", \"bandpass\", or \"notch\"");
	if (cutoffHz <= 0.0 || cutoffHz >= snd->sampleRate / 2.0)
		return luaL_error(L, "Sound:Filter: cutoffHz must be between 0 and sampleRate/2 (Nyquist)");
	if (q <= 0.0)
		return luaL_error(L, "Sound:Filter: Q must be positive");

	double w0 = 2.0 * M_PI * cutoffHz / snd->sampleRate;
	double alpha = sin(w0) / (2.0 * q);
	double cosw0 = cos(w0);

	double b0, b1, b2, a0, a1, a2;
	if (isLowpass) {
		b0 = (1.0 - cosw0) / 2.0; b1 = 1.0 - cosw0;    b2 = (1.0 - cosw0) / 2.0;
	}
	else if (isHighpass) {
		b0 = (1.0 + cosw0) / 2.0; b1 = -(1.0 + cosw0); b2 = (1.0 + cosw0) / 2.0;
	}
	else if (isBandpass) {
		b0 = alpha; b1 = 0.0; b2 = -alpha;
	}
	else { // notch
		b0 = 1.0; b1 = -2.0 * cosw0; b2 = 1.0;
	}
	a0 = 1.0 + alpha; a1 = -2.0 * cosw0; a2 = 1.0 - alpha;
	b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;

	// Independent filter state per channel -- a stereo signal's left/right
	// history must not bleed into each other.
	int channels = snd->channels;
	double* x1 = (double*)kitsune_calloc(channels, sizeof(double));
	double* x2 = (double*)kitsune_calloc(channels, sizeof(double));
	double* y1 = (double*)kitsune_calloc(channels, sizeof(double));
	double* y2 = (double*)kitsune_calloc(channels, sizeof(double));

	for (int i = 0; i < snd->frameCount; i++) {
		for (int c = 0; c < channels; c++) {
			double x0 = snd->samples[(size_t)i * channels + c];
			double y0 = b0 * x0 + b1 * x1[c] + b2 * x2[c] - a1 * y1[c] - a2 * y2[c];
			x2[c] = x1[c]; x1[c] = x0;
			y2[c] = y1[c]; y1[c] = y0;
			snd->samples[(size_t)i * channels + c] = ClampSample(y0);
		}
	}

	kitsune_free(x1); kitsune_free(x2); kitsune_free(y1); kitsune_free(y2);
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

// Shared by Save/ToBytes: validates the format arg and encodes accordingly.
// Returns a kitsune_malloc'd buffer (caller frees) or NULL on failure, in
// which case *errMsg points at a static description.
static unsigned char* EncodeSound(lua_State* L, const LuaSound* snd, int formatArgIndex, int qualityArgIndex, size_t* outLen, const char** errMsg) {
	const char* fmt = luaL_optstring(L, formatArgIndex, "wav");
	if (strcmp(fmt, "wav") == 0)
		return EncodeToWav(snd, outLen);
	if (strcmp(fmt, "ogg") == 0) {
		double quality = luaL_optnumber(L, qualityArgIndex, 0.6);
		unsigned char* data = EncodeOgg(snd, quality, outLen);
		if (!data)
			*errMsg = "libvorbis encode failed";
		return data;
	}
	*errMsg = "format must be \"wav\" or \"ogg\"";
	return NULL;
}

int Sound_Save(lua_State* L) {
	LuaSound* snd = lua_tosound(L, 1);
	const char* path = luaL_checkstring(L, 2);

	size_t len;
	const char* errMsg = "failed to encode";
	unsigned char* data = EncodeSound(L, snd, 3, 4, &len, &errMsg);
	if (!data)
		return luaL_error(L, "Sound:Save: %s", errMsg);

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
	const char* errMsg = "failed to encode";
	unsigned char* data = EncodeSound(L, snd, 2, 3, &len, &errMsg);
	if (!data)
		return luaL_error(L, "Sound:ToBytes: %s", errMsg);
	lua_pushlstring(L, (const char*)data, len);
	kitsune_free(data);
	return 1;
}
