#pragma once
#include "LuaSound.h"

// True if the buffer starts with the Ogg capture pattern ("OggS").
bool IsOggData(const unsigned char* data, size_t len);

// Decodes an in-memory Ogg Vorbis stream into snd's sampleRate/channels/
// frameCount/samples fields (samples is kitsune_malloc'd). On failure
// returns false and leaves *errMsg pointing at a static description; snd is
// left untouched.
bool DecodeOgg(const unsigned char* data, size_t len, LuaSound* snd, const char** errMsg);

// Encodes snd to an in-memory Ogg Vorbis stream at the given VBR quality
// (0.0-1.0, per vorbis_encode_init_vbr's own scale). Returns a
// kitsune_malloc'd buffer (caller frees with kitsune_free) and sets *outLen,
// or returns NULL on failure.
unsigned char* EncodeOgg(const LuaSound* snd, double quality, size_t* outLen);
