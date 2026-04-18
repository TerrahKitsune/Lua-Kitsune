#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include "stb_image.h"

#include "ImguiOpenGL.h"
#include "ImguiSession.h"
#include "Imgui/imgui.h"
#include <SDL_opengl.h>
#include <cstdlib>
#include <cstring>

// argv[0] is always the renderer userdata (self) — identical pattern to ImguiRenderer.cpp.
#define IMGUI_ARGC  (argc - 1)
#define IMGUI_ARGV  (argc > 0 ? argv + 1 : argv)
#define IMGUI_CTX   ((ImguiWindowContext*)(argc > 0 && argv[0].type == KITSUNE_TUSERDATA && argv[0].userdata ? argv[0].userdata->userdata : ud))
#define IMGUI_REQUIRE_CTX(ctx) \
    do { if (!(ctx)) { \
        KitsuneVariable _e = {}; \
        const char* _m = "renderer: invalid context"; \
        _e.type = KITSUNE_TERROR; \
        _e.data = (unsigned char*)_m; \
        _e.length = strlen(_m); \
        setter(&_e); \
        return 1; \
    } } while(0)

// ---------------------------------------------------------------------------
// FreeTextureCache
// ---------------------------------------------------------------------------

void FreeTextureCache(ImguiWindowContext* ctx) {
    if (!ctx->textures.slots)
        return;
    for (int i = 0; i < ctx->textures.count; i++) {
        if (ctx->textures.slots[i].luaId != 0) {
            GLuint id = (GLuint)ctx->textures.slots[i].glId;
            glDeleteTextures(1, &id);
            free(ctx->textures.slots[i].source);
        }
    }
    free(ctx->textures.slots);
    ctx->textures.slots  = nullptr;
    ctx->textures.count  = 0;
    ctx->textures.alloc  = 0;
    ctx->textures.nextId = 1;
}

// ---------------------------------------------------------------------------
// ResolveTextureById — resolves a Lua handle to its GL id
// ---------------------------------------------------------------------------

unsigned int ResolveTextureById(const ImguiTextureCache* cache, int luaId) {
    if (luaId == 0 || !cache->slots)
        return 0;
    for (int i = 0; i < cache->count; i++) {
        if (cache->slots[i].luaId == luaId)
            return cache->slots[i].glId;
    }
    return 0;
}

// ---------------------------------------------------------------------------
// ResolveTextureSlot — resolves a Lua handle to its full slot
// ---------------------------------------------------------------------------

const ImguiTexture* ResolveTextureSlot(const ImguiTextureCache* cache, int luaId) {
    if (luaId == 0 || !cache->slots)
        return nullptr;
    for (int i = 0; i < cache->count; i++) {
        if (cache->slots[i].luaId == luaId)
            return &cache->slots[i];
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static int acquire_slot(ImguiTextureCache* cache) {
    for (int i = 0; i < cache->count; i++) {
        if (cache->slots[i].luaId == 0)
            return i;
    }
    if (cache->count >= cache->alloc) {
        int newAlloc = cache->alloc == 0 ? 16 : cache->alloc * 2;
        ImguiTexture* grown = (ImguiTexture*)realloc(
            cache->slots, (size_t)newAlloc * sizeof(ImguiTexture));
        if (!grown)
            return -1;
        cache->slots = grown;
        cache->alloc = newAlloc;
    }
    return cache->count++;
}

static int terror(const kitsune_ResultSetter setter, const char* msg) {
    KitsuneVariable e = {};
    e.type   = KITSUNE_TERROR;
    e.data   = (unsigned char*)msg;
    e.length = strlen(msg);
    setter(&e);
    return 1;
}

// Decodes pixels from raw bytes, uploads to a new GL texture, and returns the GLuint.
// Returns 0 on decode failure. Caller owns the GLuint and must call glDeleteTextures on failure.
static GLuint upload_texture_from_bytes(const unsigned char* data, int dataLen,
    int* outW, int* outH) {
    int            channels;
    unsigned char* pixels = stbi_load_from_memory(
        (const stbi_uc*)data, dataLen, outW, outH, &channels, 4);
    if (!pixels)
        return 0;
    GLuint glId = 0;
    glGenTextures(1, &glId);
    glBindTexture(GL_TEXTURE_2D, glId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, *outW, *outH, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    stbi_image_free(pixels);
    return glId;
}

// Reads a stream (Seek(0) + Read()) into a heap buffer. Returns nullptr on failure.
// Caller must KitsuneVariableFree the returned variable.
static KitsuneVariable* read_stream(const KitsuneVariable* streamVar) {
    KitsuneVariable seekArg = {};
    seekArg.type    = KITSUNE_TINTEGER;
    seekArg.integer = 0;
    KitsuneVariable* r  = KitsuneCallMethod(streamVar, "Seek", 1, &seekArg);
    bool             ok = r && r->type != KITSUNE_TERROR;
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
// resolve_texture — shared core used by OpenGL.ResolveTexture and markdown
// ---------------------------------------------------------------------------

// Looks up source in the cache. If found, returns the existing slot.
// If not found, calls ctx->resourceLoader(source) -> stream, decodes, and uploads.
// Returns nullptr if no loader is set, the loader returns nil, or any step fails.
const ImguiTexture* resolve_texture(ImguiWindowContext* ctx,
    const char* source, int sourceLen) {
    if (!ctx || !source || sourceLen <= 0)
        return nullptr;
    if (!ctx->resourceLoader)
        return nullptr;

    // Clamp and null-terminate for use as cache key and loader arg.
    char key[2048];
    int  keyLen = sourceLen < (int)(sizeof(key) - 1) ? sourceLen : (int)(sizeof(key) - 1);
    memcpy(key, source, keyLen);
    key[keyLen] = '\0';

    // Deduplicate: return existing slot if this source was already loaded.
    ImguiTextureCache* cache = &ctx->textures;
    if (cache->slots) {
        for (int i = 0; i < cache->count; i++) {
            if (cache->slots[i].luaId != 0 &&
                cache->slots[i].source != nullptr &&
                strcmp(cache->slots[i].source, key) == 0) {
                return &cache->slots[i];
            }
        }
    }

    // Call resourceLoader(source) -> stream | nil
    KitsuneVariable sourceArg = {};
    sourceArg.type   = KITSUNE_TSTRING;
    sourceArg.data   = (unsigned char*)key;
    sourceArg.length = (unsigned int)keyLen;

    KitsuneVariable* streamVar = KitsuneExecuteVariable(ctx->resourceLoader, 1, &sourceArg);
    if (!streamVar ||
        streamVar->type == KITSUNE_TNIL  ||
        streamVar->type == KITSUNE_TNONE) {
        KitsuneVariableFree(streamVar);
        return nullptr;
    }
    if (streamVar->type == KITSUNE_TERROR) {
        fprintf(stderr, "OpenGL resource loader error for '%.*s': %.*s\n",
            keyLen, key, (int)streamVar->length, (char*)streamVar->data);
        KitsuneVariableFree(streamVar);
        return nullptr;
    }
    if (streamVar->type != KITSUNE_TUSERDATA ||
        !streamVar->userdata ||
        !streamVar->userdata->name ||
        strcmp(streamVar->userdata->name, "STREAM") != 0) {
        fprintf(stderr, "OpenGL resource loader for '%.*s' did not return a stream\n",
            keyLen, key);
        KitsuneVariableFree(streamVar);
        return nullptr;
    }

    KitsuneVariable* readResult = read_stream(streamVar);
    KitsuneVariableFree(streamVar);
    if (!readResult)
        return nullptr;

    int    w;
    int    h;
    GLuint glId = upload_texture_from_bytes(
        readResult->data, (int)readResult->length, &w, &h);
    KitsuneVariableFree(readResult);
    if (!glId)
        return nullptr;

    int slot = acquire_slot(cache);
    if (slot < 0) {
        glDeleteTextures(1, &glId);
        return nullptr;
    }

    char* sourceCopy = (char*)malloc(keyLen + 1);
    if (!sourceCopy) {
        glDeleteTextures(1, &glId);
        return nullptr;
    }
    memcpy(sourceCopy, key, keyLen + 1);

    if (cache->nextId <= 0)
        cache->nextId = 1;

    cache->slots[slot].glId    = (unsigned int)glId;
    cache->slots[slot].luaId   = cache->nextId++;
    cache->slots[slot].width   = w;
    cache->slots[slot].height  = h;
    cache->slots[slot].source  = sourceCopy;

    return &cache->slots[slot];
}

// ---------------------------------------------------------------------------
// OpenGL.SetResourceLoader(fn | nil)
// ---------------------------------------------------------------------------

int OpenGL_SetResourceLoader(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (!g_imguiCtx)
        return 0;
    if (g_imguiCtx->resourceLoader) {
        KitsuneVariableFree(g_imguiCtx->resourceLoader);
        g_imguiCtx->resourceLoader = nullptr;
    }
    if (argc > 0 && argv[0].type == KITSUNE_TFUNCTION)
        g_imguiCtx->resourceLoader = KitsuneAnchorVariable(&argv[0]);
    return 0;
}

// ---------------------------------------------------------------------------
// OpenGL.ResolveTexture(source) -> integer | nil
// ---------------------------------------------------------------------------

int OpenGL_ResolveTexture(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (!g_imguiCtx)
        return terror(setter, "OpenGL.ResolveTexture: no active session");
    if (argc < 1 || argv[0].type != KITSUNE_TSTRING)
        return terror(setter, "OpenGL.ResolveTexture(source): string expected");

    const ImguiTexture* tex = resolve_texture(
        g_imguiCtx,
        (const char*)argv[0].data, (int)argv[0].length);

    if (!tex) {
        KitsuneVariable r = {};
        r.type = KITSUNE_TNIL;
        setter(&r);
        return 1;
    }

    KitsuneVariable r = {};
    r.type    = KITSUNE_TINTEGER;
    r.integer = tex->luaId;
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// OpenGL.LoadTexture(stream [, source]) -> integer
// ---------------------------------------------------------------------------
// If source is provided and already exists in the cache, the existing GL texture
// is replaced with the new stream data and the same luaId is returned.

int OpenGL_LoadTexture(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (!g_imguiCtx)
        return terror(setter, "OpenGL.LoadTexture: no active session");
    if (argc < 1 || argv[0].type != KITSUNE_TUSERDATA)
        return terror(setter, "OpenGL.LoadTexture(stream[, source]): stream expected");

    // Optional source string
    const char* source    = nullptr;
    int         sourceLen = 0;
    if (argc > 1 && argv[1].type == KITSUNE_TSTRING) {
        source    = (const char*)argv[1].data;
        sourceLen = (int)argv[1].length;
    }

    KitsuneVariable* readResult = read_stream(&argv[0]);
    if (!readResult)
        return terror(setter, "OpenGL.LoadTexture: stream read failed");

    int    w;
    int    h;
    GLuint glId = upload_texture_from_bytes(
        readResult->data, (int)readResult->length, &w, &h);
    KitsuneVariableFree(readResult);
    if (!glId)
        return terror(setter, "OpenGL.LoadTexture: image decode failed");

    ImguiTextureCache* cache = &g_imguiCtx->textures;

    // If a source was given and already exists, replace the GL texture in-place.
    if (source && sourceLen > 0 && cache->slots) {
        for (int i = 0; i < cache->count; i++) {
            if (cache->slots[i].luaId != 0 &&
                cache->slots[i].source != nullptr &&
                (int)strlen(cache->slots[i].source) == sourceLen &&
                strncmp(cache->slots[i].source, source, sourceLen) == 0) {
                GLuint old = (GLuint)cache->slots[i].glId;
                glDeleteTextures(1, &old);
                cache->slots[i].glId   = (unsigned int)glId;
                cache->slots[i].width  = w;
                cache->slots[i].height = h;
                KitsuneVariable r = {};
                r.type    = KITSUNE_TINTEGER;
                r.integer = cache->slots[i].luaId;
                setter(&r);
                return 1;
            }
        }
    }

    // New slot
    int slot = acquire_slot(cache);
    if (slot < 0) {
        glDeleteTextures(1, &glId);
        return terror(setter, "OpenGL.LoadTexture: out of memory");
    }

    char* sourceCopy = nullptr;
    if (source && sourceLen > 0) {
        sourceCopy = (char*)malloc(sourceLen + 1);
        if (!sourceCopy) {
            glDeleteTextures(1, &glId);
            return terror(setter, "OpenGL.LoadTexture: out of memory");
        }
        memcpy(sourceCopy, source, sourceLen);
        sourceCopy[sourceLen] = '\0';
    }

    if (cache->nextId <= 0)
        cache->nextId = 1;
    int luaId = cache->nextId++;

    cache->slots[slot].glId    = (unsigned int)glId;
    cache->slots[slot].luaId   = luaId;
    cache->slots[slot].width   = w;
    cache->slots[slot].height  = h;
    cache->slots[slot].source  = sourceCopy;

    KitsuneVariable r = {};
    r.type    = KITSUNE_TINTEGER;
    r.integer = luaId;
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// OpenGL.UnloadTexture(handle)
// ---------------------------------------------------------------------------

int OpenGL_UnloadTexture(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (!g_imguiCtx || argc < 1)
        return 0;
    int luaId = (int)KitsuneAsInt(&argv[0], 0);
    if (luaId == 0)
        return 0;
    ImguiTextureCache* cache = &g_imguiCtx->textures;
    for (int i = 0; i < cache->count; i++) {
        if (cache->slots[i].luaId == luaId) {
            GLuint glId = (GLuint)cache->slots[i].glId;
            glDeleteTextures(1, &glId);
            free(cache->slots[i].source);
            cache->slots[i].source = nullptr;
            cache->slots[i].glId   = 0;
            cache->slots[i].luaId  = 0;
            return 0;
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// renderer:Image(handle, width, height [, uv0x, uv0y, uv1x, uv1y])
// ---------------------------------------------------------------------------

int ImguiRenderer_Image(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    const int                _argc = IMGUI_ARGC;
    const KitsuneVariable*   _argv = IMGUI_ARGV;
    ImguiWindowContext*       ctx   = IMGUI_CTX;
    IMGUI_REQUIRE_CTX(ctx);
    if (_argc < 3)
        return 0;

    int   luaId = (int)KitsuneAsInt(&_argv[0], 0);
    float w     = KitsuneAsFloat(&_argv[1], 0.0f);
    float h     = KitsuneAsFloat(&_argv[2], 0.0f);

    unsigned int glId = ResolveTextureById(&ctx->textures, luaId);
    ImVec2 uv0(
        _argc > 4 ? KitsuneAsFloat(&_argv[3], 0.0f) : 0.0f,
        _argc > 4 ? KitsuneAsFloat(&_argv[4], 0.0f) : 0.0f);
    ImVec2 uv1(
        _argc > 6 ? KitsuneAsFloat(&_argv[5], 1.0f) : 1.0f,
        _argc > 6 ? KitsuneAsFloat(&_argv[6], 1.0f) : 1.0f);
    ImGui::Image((ImTextureID)(uintptr_t)glId, ImVec2(w, h), uv0, uv1);
    return 0;
}

#endif // KITSUNE_IMGUI
