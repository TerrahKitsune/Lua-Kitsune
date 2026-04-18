#pragma once
#ifdef KITSUNE_IMGUI

#include "ImguiRenderer.h"
#include "ResourceCache.h"
#include "KitsuneEngine.h"

// Shared core used by OpenGL.ResolveTexture (Lua) and the markdown image renderer.
// Cache hit (live or sentinel) returns immediately. On miss, calls ctx->resourceLoader,
// decodes, uploads, and calls ctx->postLoader if set.
// Returns nullptr only if the source is completely unknown (no loader / loader returned nil).
const ImguiTexture* resolve_texture(ImguiWindowContext* ctx,
	const char* source, int sourceLen);

// Returns the raw GL texture id for a luaId, or 0 if not found / sentinel.
// Used by renderer:Image and renderer:ImageFrame.
unsigned int ResolveTextureGlId(int luaId);

// Registers all OpenGL.* Lua functions.
void RegisterOpenGLFunctions();

// OpenGL.* Lua function declarations
int OpenGL_SetResourceLoader(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int OpenGL_ResolveTexture(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int OpenGL_LoadTexture(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int OpenGL_UnloadTexture(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int OpenGL_DestroyTexture(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int OpenGL_DestroyAllTextures(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int OpenGL_ResizeTexture(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int OpenGL_CopyTexture(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int OpenGL_GetId(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int OpenGL_GetData(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int OpenGL_IsLoaded(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int OpenGL_GetTextureCount(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int OpenGL_GetFrameUVs(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int OpenGL_GetAllLoadedTextures(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);

#endif // KITSUNE_IMGUI
