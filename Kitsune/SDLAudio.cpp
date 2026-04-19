#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

#include "SDLAudio.h"
#include "ImguiSession.h"
#include "ResourceCache.h"
#include <SDL_mixer.h>
#include <SDL.h>
#include <cstdlib>
#include <cstring>

// ---------------------------------------------------------------------------
// Shared error helper
// ---------------------------------------------------------------------------

// luaId of the LuaMusic resource currently playing; 0 = none.
static int s_currentMusicId = 0;

static int sdlaudio_error(const kitsune_ResultSetter setter, const char* msg) {
    KitsuneVariable e = {};
    e.type = KITSUNE_TERROR;
    e.data = (unsigned char*)msg;
    e.length = strlen(msg);
    setter(&e);
    return 1;
}

// ---------------------------------------------------------------------------
// Heap-copies a source string onto a Resource. Returns false on OOM.
// ---------------------------------------------------------------------------

static bool sdlaudio_set_source(Resource* res, const char* source, int sourceLen) {
    if (!source || sourceLen <= 0) {
        res->source = nullptr;
        return true;
    }
    res->source = (char*)malloc(sourceLen + 1);
    if (!res->source)
        return false;
    memcpy(res->source, source, sourceLen);
    res->source[sourceLen] = '\0';
    return true;
}

// ---------------------------------------------------------------------------
// Finalizers — called by ResourceCache on remove/clear/shutdown
// ---------------------------------------------------------------------------

static void sfx_finalizer(Resource* res) {
    LuaAudio* a = (LuaAudio*)res;
    if (a->chunk)
        Mix_FreeChunk(a->chunk);
    free(a->resource.source);
    free(a);
}

static void music_finalizer(Resource* res) {
    LuaMusic* m = (LuaMusic*)res;
    if (m->music) {
        if (m->resource.luaId == s_currentMusicId) {
            Mix_HaltMusic();
            s_currentMusicId = 0;
        }
        Mix_FreeMusic(m->music);
    }
    free(m->buffer);
    free(m->resource.source);
    free(m);
}

// ---------------------------------------------------------------------------
// Allocators
// ---------------------------------------------------------------------------

static LuaAudio* sdlaudio_alloc_sfx() {
    LuaAudio* a = (LuaAudio*)calloc(1, sizeof(LuaAudio));
    if (!a)
        return nullptr;
    a->resource.type = RESOURCE_AUDIO_SFX;
    a->resource.fn = sfx_finalizer;
    a->channel = -1;
    return a;
}

static LuaMusic* sdlaudio_alloc_music() {
    LuaMusic* m = (LuaMusic*)calloc(1, sizeof(LuaMusic));
    if (!m)
        return nullptr;
    m->resource.type = RESOURCE_AUDIO_MUSIC;
    m->resource.fn = music_finalizer;
    return m;
}

// ---------------------------------------------------------------------------
// Read raw bytes from a Kitsune stream variable
// ---------------------------------------------------------------------------

static KitsuneVariable* sdlaudio_read_stream(const KitsuneVariable* streamVar) {
    KitsuneVariable seekArg = {};
    seekArg.type = KITSUNE_TINTEGER;
    seekArg.integer = 0;
    KitsuneVariable* r = KitsuneCallMethod(streamVar, "Seek", 1, &seekArg);
    bool ok = r && r->type != KITSUNE_TERROR;
    KitsuneVariableFree(r);
    if (!ok)
        return nullptr;
    KitsuneVariable* readResult = KitsuneCallMethod(streamVar, "Read", 0, nullptr);
    if (!readResult || readResult->type != KITSUNE_TSTRING || readResult->length == 0) {
        KitsuneVariableFree(readResult);
        return nullptr;
    }
    return readResult;
}

// ---------------------------------------------------------------------------
// Decode helpers — SDL_RWops over in-memory bytes so no temp files are needed
// ---------------------------------------------------------------------------

static Mix_Chunk* sdlaudio_decode_chunk(const unsigned char* data, int dataLen) {
    SDL_RWops* rw = SDL_RWFromConstMem(data, dataLen);
    if (!rw)
        return nullptr;
    return Mix_LoadWAV_RW(rw, 1);
}

// Music streams from the RWops for its entire lifetime, so we heap-copy the
// buffer and pass freesrc=1 so SDL_mixer owns and frees the RWops — but we
// keep our own copy alive in LuaMusic::buffer until the finalizer runs.
static Mix_Music* sdlaudio_decode_music(void* heapBuffer, int dataLen) {
    SDL_RWops* rw = SDL_RWFromMem(heapBuffer, dataLen);
    if (!rw)
        return nullptr;
    return Mix_LoadMUS_RW(rw, 1);
}

// ---------------------------------------------------------------------------
// SDLAudioInit / SDLAudioShutdown — called from RenderLoop
// ---------------------------------------------------------------------------

bool SDLAudioInit() {
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        fprintf(stderr, "Mix_OpenAudio failed: %s\n", Mix_GetError());
        return false;
    }
    Mix_Init(MIX_INIT_OGG | MIX_INIT_MP3 | MIX_INIT_FLAC);
    return true;
}

void SDLAudioShutdown() {
    s_currentMusicId = 0;
    ResourceCacheClearType(RESOURCE_AUDIO_SFX);
    ResourceCacheClearType(RESOURCE_AUDIO_MUSIC);
    Mix_CloseAudio();
    Mix_Quit();
}

// ---------------------------------------------------------------------------
// SDL.Audio.Load(stream [, source]) -> luaId
// Loads a sound effect from a Kitsune stream, decoded by SDL_mixer.
// ---------------------------------------------------------------------------

int SDLAudioLoad(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 1 || argv[0].type != KITSUNE_TUSERDATA)
        return sdlaudio_error(setter, "SDL.Audio.Load(stream [, source]): stream expected");

    const char* source = nullptr;
    int sourceLen = 0;
    if (argc > 1 && argv[1].type == KITSUNE_TSTRING) {
        source = (const char*)argv[1].data;
        sourceLen = (int)argv[1].length;
    }

    KitsuneVariable* readResult = sdlaudio_read_stream(&argv[0]);
    if (!readResult)
        return sdlaudio_error(setter, "SDL.Audio.Load: stream read failed");

    Mix_Chunk* chunk = sdlaudio_decode_chunk(readResult->data, (int)readResult->length);
    KitsuneVariableFree(readResult);
    if (!chunk)
        return sdlaudio_error(setter, "SDL.Audio.Load: Mix_LoadWAV_RW failed — unsupported format or corrupt data");

    LuaAudio* a = sdlaudio_alloc_sfx();
    if (!a) {
        Mix_FreeChunk(chunk);
        return sdlaudio_error(setter, "SDL.Audio.Load: out of memory");
    }
    if (!sdlaudio_set_source(&a->resource, source, sourceLen)) {
        Mix_FreeChunk(chunk);
        free(a);
        return sdlaudio_error(setter, "SDL.Audio.Load: out of memory");
    }
    a->chunk = chunk;

    if (!ResourceCacheUpsert(&a->resource)) {
        sfx_finalizer(&a->resource);
        return sdlaudio_error(setter, "SDL.Audio.Load: out of memory");
    }

    KitsuneVariable r = {};
    r.type = KITSUNE_TINTEGER;
    r.integer = a->resource.luaId;
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// SDL.Audio.LoadRaw(data [, source]) -> luaId
// Loads a sound effect from a raw byte string (e.g. already-read file data).
// ---------------------------------------------------------------------------

int SDLAudioLoadRaw(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 1 || argv[0].type != KITSUNE_TSTRING || argv[0].length == 0)
        return sdlaudio_error(setter, "SDL.Audio.LoadRaw(data [, source]): non-empty string expected");

    const char* source = nullptr;
    int sourceLen = 0;
    if (argc > 1 && argv[1].type == KITSUNE_TSTRING) {
        source = (const char*)argv[1].data;
        sourceLen = (int)argv[1].length;
    }

    Mix_Chunk* chunk = sdlaudio_decode_chunk(argv[0].data, (int)argv[0].length);
    if (!chunk)
        return sdlaudio_error(setter, "SDL.Audio.LoadRaw: Mix_LoadWAV_RW failed — unsupported format or corrupt data");

    LuaAudio* a = sdlaudio_alloc_sfx();
    if (!a) {
        Mix_FreeChunk(chunk);
        return sdlaudio_error(setter, "SDL.Audio.LoadRaw: out of memory");
    }
    if (!sdlaudio_set_source(&a->resource, source, sourceLen)) {
        Mix_FreeChunk(chunk);
        free(a);
        return sdlaudio_error(setter, "SDL.Audio.LoadRaw: out of memory");
    }
    a->chunk = chunk;

    if (!ResourceCacheUpsert(&a->resource)) {
        sfx_finalizer(&a->resource);
        return sdlaudio_error(setter, "SDL.Audio.LoadRaw: out of memory");
    }

    KitsuneVariable r = {};
    r.type = KITSUNE_TINTEGER;
    r.integer = a->resource.luaId;
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// SDL.Audio.LoadMusic(stream [, source]) -> luaId
// Loads streaming music (OGG, MP3, FLAC, etc.) from a Kitsune stream.
// ---------------------------------------------------------------------------

int SDLAudioLoadMusic(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 1 || argv[0].type != KITSUNE_TUSERDATA)
        return sdlaudio_error(setter, "SDL.Audio.LoadMusic(stream [, source]): stream expected");

    const char* source = nullptr;
    int sourceLen = 0;
    if (argc > 1 && argv[1].type == KITSUNE_TSTRING) {
        source = (const char*)argv[1].data;
        sourceLen = (int)argv[1].length;
    }

    KitsuneVariable* readResult = sdlaudio_read_stream(&argv[0]);
    if (!readResult)
        return sdlaudio_error(setter, "SDL.Audio.LoadMusic: stream read failed");

    int dataLen = (int)readResult->length;
    void* buffer = malloc(dataLen);
    if (!buffer) {
        KitsuneVariableFree(readResult);
        return sdlaudio_error(setter, "SDL.Audio.LoadMusic: out of memory");
    }
    memcpy(buffer, readResult->data, dataLen);
    KitsuneVariableFree(readResult);

    Mix_Music* music = sdlaudio_decode_music(buffer, dataLen);
    if (!music) {
        free(buffer);
        return sdlaudio_error(setter, "SDL.Audio.LoadMusic: Mix_LoadMUS_RW failed - unsupported format or corrupt data");
    }

    LuaMusic* m = sdlaudio_alloc_music();
    if (!m) {
        Mix_FreeMusic(music);
        free(buffer);
        return sdlaudio_error(setter, "SDL.Audio.LoadMusic: out of memory");
    }
    if (!sdlaudio_set_source(&m->resource, source, sourceLen)) {
        Mix_FreeMusic(music);
        free(buffer);
        free(m);
        return sdlaudio_error(setter, "SDL.Audio.LoadMusic: out of memory");
    }
    m->music = music;
    m->buffer = buffer;
    m->bufferLen = dataLen;

    if (!ResourceCacheUpsert(&m->resource)) {
        music_finalizer(&m->resource);
        return sdlaudio_error(setter, "SDL.Audio.LoadMusic: out of memory");
    }

    KitsuneVariable r = {};
    r.type = KITSUNE_TINTEGER;
    r.integer = m->resource.luaId;
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// SDL.Audio.Unload(luaId)
// Frees the Mix_Chunk but keeps the cache slot as a sentinel.
// ---------------------------------------------------------------------------

int SDLAudioUnload(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 1)
        return 0;
    int luaId = (int)KitsuneAsInt(&argv[0], 0);
    if (luaId <= 0)
        return 0;
    LuaAudio* a = (LuaAudio*)ResourceCacheGetById(luaId, RESOURCE_AUDIO_SFX);
    if (a && a->chunk) {
        Mix_FreeChunk(a->chunk);
        a->chunk = nullptr;
        a->channel = -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// SDL.Audio.Destroy(luaId) — removes and frees a sound-effect resource
// ---------------------------------------------------------------------------

int SDLAudioDestroy(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 1)
        return 0;
    int luaId = (int)KitsuneAsInt(&argv[0], 0);
    ResourceCacheRemoveById(luaId, RESOURCE_AUDIO_SFX);
    return 0;
}

// ---------------------------------------------------------------------------
// SDL.Audio.DestroyMusic(luaId) — removes and frees a music resource
// ---------------------------------------------------------------------------

int SDLAudioDestroyMusic(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 1)
        return 0;
    int luaId = (int)KitsuneAsInt(&argv[0], 0);
    ResourceCacheRemoveById(luaId, RESOURCE_AUDIO_MUSIC);
    return 0;
}

// ---------------------------------------------------------------------------
// SDL.Audio.DestroyAll() — halts all playback and clears all audio resources
// ---------------------------------------------------------------------------

int SDLAudioDestroyAll(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    Mix_HaltChannel(-1);
    Mix_HaltMusic();
    s_currentMusicId = 0;
    ResourceCacheClearType(RESOURCE_AUDIO_SFX);
    ResourceCacheClearType(RESOURCE_AUDIO_MUSIC);
    return 0;
}

// ---------------------------------------------------------------------------
// SDL.Audio.Play(luaId [, loops]) -> channel | nil
// loops: 0 = play once (default), -1 = infinite, n = repeat n extra times
// ---------------------------------------------------------------------------

int SDLAudioPlay(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 1)
        return sdlaudio_error(setter, "SDL.Audio.Play(luaId [, loops])");
    int luaId = (int)KitsuneAsInt(&argv[0], 0);
    int loops = argc > 1 ? (int)KitsuneAsInt(&argv[1], 0) : 0;

    LuaAudio* a = (LuaAudio*)ResourceCacheGetById(luaId, RESOURCE_AUDIO_SFX);
    if (!a || !a->chunk) {
        KitsuneVariable r = {};
        r.type = KITSUNE_TNIL;
        setter(&r);
        return 1;
    }

    int channel = Mix_PlayChannel(-1, a->chunk, loops);
    a->channel = channel;

    if (channel < 0) {
        KitsuneVariable r = {};
        r.type = KITSUNE_TNIL;
        setter(&r);
        return 1;
    }

    KitsuneVariable r = {};
    r.type = KITSUNE_TINTEGER;
    r.integer = channel;
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// SDL.Audio.PlayMusic(luaId [, loops]) -> bool
// loops: -1 = infinite (default)
// ---------------------------------------------------------------------------

int SDLAudioPlayMusic(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 1)
        return sdlaudio_error(setter, "SDL.Audio.PlayMusic(luaId [, loops])");
    int luaId = (int)KitsuneAsInt(&argv[0], 0);
    int loops = argc > 1 ? (int)KitsuneAsInt(&argv[1], -1) : -1;

    LuaMusic* m = (LuaMusic*)ResourceCacheGetById(luaId, RESOURCE_AUDIO_MUSIC);
    KitsuneVariable r = {};
    r.type = KITSUNE_TBOOLEAN;
    if (m && m->music && Mix_PlayMusic(m->music, loops) == 0) {
        s_currentMusicId = luaId;
        r.boolean = true;
    }
    else {
        r.boolean = false;
    }
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// SDL.Audio.Stop([luaId]) — stops the channel the sfx is playing on, or all
// ---------------------------------------------------------------------------

int SDLAudioStop(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 1) {
        Mix_HaltChannel(-1);
        return 0;
    }
    int luaId = (int)KitsuneAsInt(&argv[0], 0);
    if (luaId <= 0) {
        Mix_HaltChannel(-1);
        return 0;
    }
    LuaAudio* a = (LuaAudio*)ResourceCacheGetById(luaId, RESOURCE_AUDIO_SFX);
    if (a && a->channel >= 0) {
        Mix_HaltChannel(a->channel);
        a->channel = -1;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// SDL.Audio.StopMusic()
// ---------------------------------------------------------------------------

int SDLAudioStopMusic(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    Mix_HaltMusic();
    s_currentMusicId = 0;
    return 0;
}

// ---------------------------------------------------------------------------
// SDL.Audio.SetVolume(luaId, volume) — volume 0..128
// ---------------------------------------------------------------------------

int SDLAudioSetVolume(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 2)
        return sdlaudio_error(setter, "SDL.Audio.SetVolume(luaId, volume): volume 0..128");
    int luaId = (int)KitsuneAsInt(&argv[0], 0);
    int vol = (int)KitsuneAsInt(&argv[1], MIX_MAX_VOLUME);
    if (vol < 0)
        vol = 0;
    if (vol > MIX_MAX_VOLUME)
        vol = MIX_MAX_VOLUME;
    LuaAudio* a = (LuaAudio*)ResourceCacheGetById(luaId, RESOURCE_AUDIO_SFX);
    if (a && a->chunk)
        Mix_VolumeChunk(a->chunk, vol);
    return 0;
}

// ---------------------------------------------------------------------------
// SDL.Audio.SetMusicVolume(volume) — volume 0..128
// ---------------------------------------------------------------------------

int SDLAudioSetMusicVolume(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    int vol = argc > 0 ? (int)KitsuneAsInt(&argv[0], MIX_MAX_VOLUME) : MIX_MAX_VOLUME;
    if (vol < 0)
        vol = 0;
    if (vol > MIX_MAX_VOLUME)
        vol = MIX_MAX_VOLUME;
    Mix_VolumeMusic(vol);
    return 0;
}

// ---------------------------------------------------------------------------
// SDL.Audio.IsPlaying(luaId) -> bool
// ---------------------------------------------------------------------------

int SDLAudioIsPlaying(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    KitsuneVariable r = {};
    r.type = KITSUNE_TBOOLEAN;
    r.boolean = false;
    if (argc >= 1) {
        int luaId = (int)KitsuneAsInt(&argv[0], 0);
        LuaAudio* a = (LuaAudio*)ResourceCacheGetById(luaId, RESOURCE_AUDIO_SFX);
        if (a && a->channel >= 0)
            r.boolean = Mix_Playing(a->channel) != 0;
    }
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// SDL.Audio.IsMusicPlaying() -> bool
// ---------------------------------------------------------------------------

int SDLAudioIsMusicPlaying(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    KitsuneVariable r = {};
    r.type = KITSUNE_TBOOLEAN;
    r.boolean = Mix_PlayingMusic() != 0;
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// SDL.Audio.GetId(source) -> luaId | nil
// Searches sfx first, then music.
// ---------------------------------------------------------------------------

int SDLAudioGetId(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 1 || argv[0].type != KITSUNE_TSTRING)
        return sdlaudio_error(setter, "SDL.Audio.GetId(source): string expected");

    char key[2048];
    int keyLen = (int)argv[0].length < (int)(sizeof(key) - 1) ? (int)argv[0].length : (int)(sizeof(key) - 1);
    memcpy(key, argv[0].data, keyLen);
    key[keyLen] = '\0';

    Resource* res = ResourceCacheGetBySource(key, RESOURCE_AUDIO_SFX);
    if (!res)
        res = ResourceCacheGetBySource(key, RESOURCE_AUDIO_MUSIC);

    if (res) {
        KitsuneVariable r = {};
        r.type = KITSUNE_TINTEGER;
        r.integer = res->luaId;
        setter(&r);
        return 1;
    }

    KitsuneVariable r = {};
    r.type = KITSUNE_TNIL;
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// SDL.Audio.GetData(luaId) -> { source, isLoaded, type } | nil
// type is "sfx" or "music"
// ---------------------------------------------------------------------------

int SDLAudioGetData(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 1)
        return sdlaudio_error(setter, "SDL.Audio.GetData(luaId)");
    int luaId = (int)KitsuneAsInt(&argv[0], 0);

    Resource* res = ResourceCacheGetById(luaId, RESOURCE_AUDIO_SFX);
    bool isSfx = res != nullptr;
    if (!res)
        res = ResourceCacheGetById(luaId, RESOURCE_AUDIO_MUSIC);

    if (!res) {
        KitsuneVariable r = {};
        r.type = KITSUNE_TNIL;
        setter(&r);
        return 1;
    }

    KitsuneVariable tableVar = {};
    tableVar.type = KITSUNE_TTABLECONTENTS;
    tableVar.table = nullptr;
    KitsuneVariable* tbl = KitsuneAnchorVariable(&tableVar);
    if (!tbl)
        return sdlaudio_error(setter, "SDL.Audio.GetData: out of memory");

    KitsuneVariable sourceKey = {};
    sourceKey.type = KITSUNE_TSTRING;
    sourceKey.data = (unsigned char*)"source";
    sourceKey.length = 6;

    KitsuneVariable isLoadedKey = {};
    isLoadedKey.type = KITSUNE_TSTRING;
    isLoadedKey.data = (unsigned char*)"isLoaded";
    isLoadedKey.length = 8;

    KitsuneVariable typeKey = {};
    typeKey.type = KITSUNE_TSTRING;
    typeKey.data = (unsigned char*)"type";
    typeKey.length = 4;

    KitsuneVariable sourceVal = {};
    if (res->source) {
        sourceVal.type = KITSUNE_TSTRING;
        sourceVal.data = (unsigned char*)res->source;
        sourceVal.length = strlen(res->source);
    }
    else {
        sourceVal.type = KITSUNE_TNIL;
    }

    KitsuneVariable isLoadedVal = {};
    isLoadedVal.type = KITSUNE_TBOOLEAN;
    if (isSfx)
        isLoadedVal.boolean = ((LuaAudio*)res)->chunk != nullptr;
    else
        isLoadedVal.boolean = ((LuaMusic*)res)->music != nullptr;

    const char* typeStr = isSfx ? "sfx" : "music";
    KitsuneVariable typeVal = {};
    typeVal.type = KITSUNE_TSTRING;
    typeVal.data = (unsigned char*)typeStr;
    typeVal.length = strlen(typeStr);

    KitsuneSetIndex(tbl, &sourceKey, &sourceVal);
    KitsuneSetIndex(tbl, &isLoadedKey, &isLoadedVal);
    KitsuneSetIndex(tbl, &typeKey, &typeVal);

    setter(tbl);
    KitsuneVariableFree(tbl);
    return 1;
}

// ---------------------------------------------------------------------------
// SDL.Audio.FadeIn(luaId, ms [, loops]) -> channel | nil
// ---------------------------------------------------------------------------

int SDLAudioFadeIn(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 2)
        return sdlaudio_error(setter, "SDL.Audio.FadeIn(luaId, ms [, loops])");
    int luaId = (int)KitsuneAsInt(&argv[0], 0);
    int ms = (int)KitsuneAsInt(&argv[1], 500);
    int loops = argc > 2 ? (int)KitsuneAsInt(&argv[2], 0) : 0;

    LuaAudio* a = (LuaAudio*)ResourceCacheGetById(luaId, RESOURCE_AUDIO_SFX);
    if (!a || !a->chunk) {
        KitsuneVariable r = {};
        r.type = KITSUNE_TNIL;
        setter(&r);
        return 1;
    }

    int channel = Mix_FadeInChannel(-1, a->chunk, loops, ms);
    a->channel = channel;

    if (channel < 0) {
        KitsuneVariable r = {};
        r.type = KITSUNE_TNIL;
        setter(&r);
        return 1;
    }

    KitsuneVariable r = {};
    r.type = KITSUNE_TINTEGER;
    r.integer = channel;
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// SDL.Audio.FadeOut(luaId, ms)
// ---------------------------------------------------------------------------

int SDLAudioFadeOut(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 2)
        return sdlaudio_error(setter, "SDL.Audio.FadeOut(luaId, ms)");
    int luaId = (int)KitsuneAsInt(&argv[0], 0);
    int ms = (int)KitsuneAsInt(&argv[1], 500);
    LuaAudio* a = (LuaAudio*)ResourceCacheGetById(luaId, RESOURCE_AUDIO_SFX);
    if (a && a->channel >= 0)
        Mix_FadeOutChannel(a->channel, ms);
    return 0;
}

// ---------------------------------------------------------------------------
// SDL.Audio.PauseMusic() / SDL.Audio.ResumeMusic()
// ---------------------------------------------------------------------------

int SDLAudioPauseMusic(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    Mix_PauseMusic();
    return 0;
}

int SDLAudioResumeMusic(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    Mix_ResumeMusic();
    return 0;
}

// ---------------------------------------------------------------------------
// SDL.Audio.IsMusicPaused() -> bool
// ---------------------------------------------------------------------------

int SDLAudioIsMusicPaused(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    KitsuneVariable r = {};
    r.type = KITSUNE_TBOOLEAN;
    r.boolean = Mix_PausedMusic() != 0;
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// SDL.Audio.GetCurrentMusic() -> luaId | nil
// ---------------------------------------------------------------------------

int SDLAudioGetCurrentMusic(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (s_currentMusicId > 0 && Mix_PlayingMusic()) {
        KitsuneVariable r = {};
        r.type = KITSUNE_TINTEGER;
        r.integer = s_currentMusicId;
        setter(&r);
        return 1;
    }
    // Music finished naturally or was never started
    s_currentMusicId = 0;
    KitsuneVariable r = {};
    r.type = KITSUNE_TNIL;
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// RegisterSDLAudioFunctions
// ---------------------------------------------------------------------------

void RegisterSDLAudioFunctions() {
    KitsuneRegisterFunction("SDL.Audio.Load",              SDLAudioLoad,              nullptr);
    KitsuneRegisterFunction("SDL.Audio.LoadMusic",         SDLAudioLoadMusic,         nullptr);
    KitsuneRegisterFunction("SDL.Audio.Unload",            SDLAudioUnload,            nullptr);
    KitsuneRegisterFunction("SDL.Audio.Destroy",           SDLAudioDestroy,           nullptr);
    KitsuneRegisterFunction("SDL.Audio.DestroyMusic",      SDLAudioDestroyMusic,      nullptr);
    KitsuneRegisterFunction("SDL.Audio.DestroyAll",        SDLAudioDestroyAll,        nullptr);
    KitsuneRegisterFunction("SDL.Audio.Play",              SDLAudioPlay,              nullptr);
    KitsuneRegisterFunction("SDL.Audio.PlayMusic",         SDLAudioPlayMusic,         nullptr);
    KitsuneRegisterFunction("SDL.Audio.Stop",              SDLAudioStop,              nullptr);
    KitsuneRegisterFunction("SDL.Audio.StopMusic",         SDLAudioStopMusic,         nullptr);
    KitsuneRegisterFunction("SDL.Audio.SetVolume",         SDLAudioSetVolume,         nullptr);
    KitsuneRegisterFunction("SDL.Audio.SetMusicVolume",    SDLAudioSetMusicVolume,    nullptr);
    KitsuneRegisterFunction("SDL.Audio.IsPlaying",         SDLAudioIsPlaying,         nullptr);
    KitsuneRegisterFunction("SDL.Audio.IsMusicPlaying",    SDLAudioIsMusicPlaying,    nullptr);
    KitsuneRegisterFunction("SDL.Audio.GetId",             SDLAudioGetId,             nullptr);
    KitsuneRegisterFunction("SDL.Audio.GetData",           SDLAudioGetData,           nullptr);
    KitsuneRegisterFunction("SDL.Audio.FadeIn",            SDLAudioFadeIn,            nullptr);
    KitsuneRegisterFunction("SDL.Audio.FadeOut",           SDLAudioFadeOut,           nullptr);
    KitsuneRegisterFunction("SDL.Audio.PauseMusic",        SDLAudioPauseMusic,        nullptr);
    KitsuneRegisterFunction("SDL.Audio.ResumeMusic",       SDLAudioResumeMusic,       nullptr);
    KitsuneRegisterFunction("SDL.Audio.IsMusicPaused",     SDLAudioIsMusicPaused,     nullptr);
    KitsuneRegisterFunction("SDL.Audio.GetCurrentMusic",   SDLAudioGetCurrentMusic,   nullptr);
}

#endif // KITSUNE_IMGUI
