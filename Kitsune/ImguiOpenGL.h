#pragma once
#ifdef KITSUNE_IMGUI

#include "ImguiRenderer.h"
#include "KitsuneEngine.h"

// Frees all live GL textures in ctx->textures and releases the slot array.
// Must be called while the GL context is current (i.e. before GL teardown in imgui_gc).
void FreeTextureCache(ImguiWindowContext* ctx);

// Resolves a Lua handle to its raw GL texture id. Returns 0 if not found.
unsigned int ResolveTextureById(const ImguiTextureCache* cache, int luaId);

// Resolves a Lua handle to its full slot (glId, width, height, source).
// Returns nullptr if not found.
const ImguiTexture* ResolveTextureSlot(const ImguiTextureCache* cache, int luaId);

// Shared core used by OpenGL.ResolveTexture (Lua) and the markdown image renderer.
// Looks up source in the cache and returns the existing slot if found.
// If not found, calls ctx->resourceLoader(source) -> stream, decodes, and uploads to GL.
// Returns nullptr if no loader is set, the loader returns nil/error, or any step fails.
// Must be called while the GL context is current.
const ImguiTexture* resolve_texture(ImguiWindowContext* ctx,
    const char* source, int sourceLen);

// OpenGL.SetResourceLoader(fn | nil)
// Registers a Lua function as the session-wide resource loader.
// Called with the source string; must return a readable stream or nil.
// Passing nil clears the loader. Only one loader is active at a time.
int OpenGL_SetResourceLoader(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud);

// OpenGL.ResolveTexture(source) -> integer | nil
// Returns the cached luaId for source, loading it via the resource loader if needed.
// Returns nil if no loader is set, the loader returns nil, or any step fails.
int OpenGL_ResolveTexture(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud);

// OpenGL.LoadTexture(stream [, source]) -> integer
// Decodes image data from stream and uploads it as a GL texture.
// If source is provided and already cached, replaces the GL texture keeping the same luaId.
int OpenGL_LoadTexture(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud);

// OpenGL.UnloadTexture(handle)
// Deletes the GL texture and tombstones the slot. No-op on invalid handle.
int OpenGL_UnloadTexture(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud);

// renderer:Image(handle, width, height [, uv0x, uv0y, uv1x, uv1y])
// Calls ImGui::Image with the resolved GL texture id.
// Renders a blank/white quad silently if the handle is invalid (stale handle safety).
int ImguiRenderer_Image(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud);

#endif // KITSUNE_IMGUI
