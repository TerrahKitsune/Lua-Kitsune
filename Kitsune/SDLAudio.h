#pragma once
#ifdef KITSUNE_IMGUI

#include "ResourceCache.h"
#include "KitsuneEngine.h"
#include <SDL_mixer.h>

// Sound-effect resource. Resource must be the first field.
struct LuaAudio {
    Resource   resource;  // type=RESOURCE_AUDIO_SFX; luaId and source live here
    Mix_Chunk* chunk;     // decoded PCM; nullptr = sentinel (load failed / freed)
    int        channel;   // last SDL_mixer channel played on; -1 = none
};

// Music resource. Resource must be the first field.
struct LuaMusic {
    Resource   resource;  // type=RESOURCE_AUDIO_MUSIC; luaId and source live here
    Mix_Music* music;     // streamed music handle; nullptr = sentinel
    void*      buffer;    // heap copy of the raw file bytes — kept alive for streaming
    int        bufferLen;
};

// Registers all SDL.Audio.* Lua functions. Called from RegisterImguiFunctions().
void RegisterSDLAudioFunctions();

// Initialises SDL_mixer. Called from RunImguiSession after SDL_Init.
// Returns true on success.
bool SDLAudioInit();

// Shuts down SDL_mixer and frees all cached audio resources.
// Called from RunImguiSession before ResourceCacheShutdown.
void SDLAudioShutdown();

// SDL.Audio.* Lua function declarations
int SDLAudioLoad(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioLoadRaw(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioLoadMusic(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioUnload(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioDestroy(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioDestroyMusic(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioDestroyAll(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioPlay(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioPlayMusic(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioStop(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioStopMusic(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioSetVolume(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioSetMusicVolume(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioIsPlaying(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioIsMusicPlaying(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioGetId(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioGetData(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioFadeIn(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioFadeOut(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioPauseMusic(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioResumeMusic(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioIsMusicPaused(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int SDLAudioGetCurrentMusic(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);

#endif // KITSUNE_IMGUI
