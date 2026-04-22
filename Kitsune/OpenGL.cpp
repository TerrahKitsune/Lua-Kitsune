#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "OpenGL.h"
#include "ImguiSession.h"
#include "ResourceCache.h"
#include "Imgui/imgui.h"
#include <SDL_opengl.h>
#include <SDL.h>
#include <cstdlib>
#include <cstring>

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
// Texture finalizer — called by ResourceCacheRemoveById/BySource/ClearType
// ---------------------------------------------------------------------------

static void texture_finalizer(Resource* res) {
	ImguiTexture* tex = (ImguiTexture*)res;
	if (tex->frameGlIds && tex->frameCount > 0) {
		for (int i = 0; i < tex->frameCount; i++) {
			if (tex->frameGlIds[i] != 0) {
				GLuint id = (GLuint)tex->frameGlIds[i];
				glDeleteTextures(1, &id);
			}
		}
		free(tex->frameGlIds);
		free(tex->frameDelays);
	}
	else if (tex->glId != 0) {
		GLuint id = (GLuint)tex->glId;
		glDeleteTextures(1, &id);
	}
	free(tex->resource.source);
	free(tex);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static int terror(const kitsune_ResultSetter setter, const char* msg) {
	KitsuneVariable e = {};
	e.type = KITSUNE_TERROR;
	e.data = (unsigned char*)msg;
	e.length = strlen(msg);
	setter(&e);
	return 1;
}

// Allocates and zero-initialises a new ImguiTexture with the finalizer set.
static ImguiTexture* alloc_texture() {
	ImguiTexture* tex = (ImguiTexture*)calloc(1, sizeof(ImguiTexture));
	if (!tex)
		return nullptr;
	tex->resource.type = RESOURCE_TEXTURE;
	tex->resource.fn = texture_finalizer;
	return tex;
}

// Heap-copies a source string onto a Resource. Returns false on OOM.
static bool set_source(Resource* res, const char* source, int sourceLen) {
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

static bool upload_gif_from_bytes(const unsigned char* data, int dataLen,
	unsigned int** outGlIds, int** outDelays, int* outFrameCount,
	int* outW, int* outH) {
	int  w = 0;
	int  h = 0;
	int  frameCount = 0;
	int  channels = 0;
	int* delays = nullptr;

	unsigned char* pixels = stbi_load_gif_from_memory(
		(const stbi_uc*)data, dataLen, &delays, &w, &h, &frameCount, &channels, 4);
	if (!pixels || frameCount <= 0) {
		stbi_image_free(pixels);
		free(delays);
		return false;
	}

	unsigned int* glIds = (unsigned int*)calloc(frameCount, sizeof(unsigned int));
	int*          msCopy = (int*)calloc(frameCount, sizeof(int));
	if (!glIds || !msCopy) {
		free(glIds);
		free(msCopy);
		stbi_image_free(pixels);
		free(delays);
		return false;
	}

	size_t frameBytes = (size_t)w * h * 4;
	for (int i = 0; i < frameCount; i++) {
		GLuint id = 0;
		glGenTextures(1, &id);
		glBindTexture(GL_TEXTURE_2D, id);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
			pixels + (size_t)i * frameBytes);
		glIds[i] = (unsigned int)id;
		// stbi stores delay in 1/1000ths of a second (already ms)
		msCopy[i] = delays ? delays[i] : 100;
		if (msCopy[i] <= 0)
			msCopy[i] = 100;
	}

	stbi_image_free(pixels);
	free(delays);

	*outGlIds = glIds;
	*outDelays = msCopy;
	*outFrameCount = frameCount;
	*outW = w;
	*outH = h;
	return true;
}

static KitsuneVariable* read_stream(const KitsuneVariable* streamVar) {
	KitsuneVariable seekArg = {};
	seekArg.type = KITSUNE_TINTEGER;
	seekArg.integer = 0;
	KitsuneVariable* r = KitsuneCallMethod(streamVar, "Seek", 1, &seekArg);
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

// Calls the post-loader with RESOURCE_TEXTURE type.
// Re-looks up by luaId after the call — post-loader may have mutated the cache.
static ImguiTexture* call_post_loader(int luaId, const char* source, int sourceLen) {
	ResourceCacheCallPostLoader(RESOURCE_TEXTURE, luaId, source, sourceLen);
	return (ImguiTexture*)ResourceCacheGetById(luaId, RESOURCE_TEXTURE);
}

static bool is_gif(const unsigned char* data, int len) {
	return len >= 6 &&
		data[0] == 'G' && data[1] == 'I' && data[2] == 'F' &&
		data[3] == '8' && (data[4] == '7' || data[4] == '9') && data[5] == 'a';
}

static const char* detect_format(const unsigned char* data, int len) {
	if (len >= 6 &&
		data[0] == 'G' && data[1] == 'I' && data[2] == 'F' &&
		data[3] == '8' && (data[4] == '7' || data[4] == '9') && data[5] == 'a')
		return "gif";
	if (len >= 4 &&
		data[0] == 0x89 && data[1] == 'P' && data[2] == 'N' && data[3] == 'G')
		return "png";
	if (len >= 3 &&
		data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF)
		return "jpeg";
	if (len >= 2 && data[0] == 'B' && data[1] == 'M')
		return "bmp";
	return "unknown";
}

// Decodes bytes into tex, auto-detecting GIF vs static image.
// On success tex is fully populated (glId or frameGlIds). Returns false on failure.
static bool load_texture_from_bytes(const unsigned char* data, int dataLen, ImguiTexture* tex) {
	tex->format = detect_format(data, dataLen);
	if (is_gif(data, dataLen)) {
		unsigned int* glIds = nullptr;
		int*          delays = nullptr;
		int           frameCount = 0;
		int           w = 0;
		int           h = 0;
		if (!upload_gif_from_bytes(data, dataLen, &glIds, &delays, &frameCount, &w, &h))
			return false;
		tex->glId = glIds[0];
		tex->width = w;
		tex->height = h;
		tex->frameGlIds = glIds;
		tex->frameDelays = delays;
		tex->frameCount = frameCount;
		tex->currentFrame = 0;
		tex->frameTimer = 0.0;
	}
	else {
		int    w = 0;
		int    h = 0;
		GLuint glId = upload_texture_from_bytes(data, dataLen, &w, &h);
		if (!glId)
			return false;
		tex->glId = (unsigned int)glId;
		tex->width = w;
		tex->height = h;
	}
	return true;
}

// ---------------------------------------------------------------------------
// FBO function pointers
// ---------------------------------------------------------------------------

typedef void   (APIENTRY* PFNGLGENFRAMEBUFFERS_T)         (GLsizei, GLuint*);
typedef void   (APIENTRY* PFNGLDELETEFRAMEBUFFERS_T)      (GLsizei, const GLuint*);
typedef void   (APIENTRY* PFNGLBINDFRAMEBUFFER_T)         (GLenum, GLuint);
typedef void   (APIENTRY* PFNGLFRAMEBUFFERTEXTURE2D_T)    (GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum(APIENTRY* PFNGLCHECKFRAMEBUFFERSTATUS_T)  (GLenum);
typedef void   (APIENTRY* PFNGLBLITFRAMEBUFFER_T)         (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);

static PFNGLGENFRAMEBUFFERS_T        s_glGenFramebuffers = nullptr;
static PFNGLDELETEFRAMEBUFFERS_T     s_glDeleteFramebuffers = nullptr;
static PFNGLBINDFRAMEBUFFER_T        s_glBindFramebuffer = nullptr;
static PFNGLFRAMEBUFFERTEXTURE2D_T   s_glFramebufferTexture2D = nullptr;
static PFNGLCHECKFRAMEBUFFERSTATUS_T s_glCheckFramebufferStatus = nullptr;
static PFNGLBLITFRAMEBUFFER_T        s_glBlitFramebuffer = nullptr;
static bool                          s_fboLoaded = false;

static bool load_fbo_functions() {
	if (s_fboLoaded)
		return s_glGenFramebuffers != nullptr;
	s_fboLoaded = true;
	s_glGenFramebuffers = (PFNGLGENFRAMEBUFFERS_T)SDL_GL_GetProcAddress("glGenFramebuffers");
	s_glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERS_T)SDL_GL_GetProcAddress("glDeleteFramebuffers");
	s_glBindFramebuffer = (PFNGLBINDFRAMEBUFFER_T)SDL_GL_GetProcAddress("glBindFramebuffer");
	s_glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2D_T)SDL_GL_GetProcAddress("glFramebufferTexture2D");
	s_glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUS_T)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
	s_glBlitFramebuffer = (PFNGLBLITFRAMEBUFFER_T)SDL_GL_GetProcAddress("glBlitFramebuffer");
	return s_glGenFramebuffers != nullptr;
}

static GLuint blit_texture(GLuint srcGlId, int srcW, int srcH, int dstW, int dstH) {
	if (!srcGlId || srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0)
		return 0;
	if (!load_fbo_functions())
		return 0;

	GLuint dstId = 0;
	glGenTextures(1, &dstId);
	glBindTexture(GL_TEXTURE_2D, dstId);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, dstW, dstH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

	GLuint fbos[2] = { 0, 0 };
	s_glGenFramebuffers(2, fbos);

	s_glBindFramebuffer(GL_READ_FRAMEBUFFER, fbos[0]);
	s_glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, srcGlId, 0);

	s_glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbos[1]);
	s_glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, dstId, 0);

	GLenum readStatus = s_glCheckFramebufferStatus(GL_READ_FRAMEBUFFER);
	GLenum drawStatus = s_glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);

	if (readStatus == GL_FRAMEBUFFER_COMPLETE && drawStatus == GL_FRAMEBUFFER_COMPLETE) {
		s_glBlitFramebuffer(
			0, 0, srcW, srcH,
			0, 0, dstW, dstH,
			GL_COLOR_BUFFER_BIT, GL_LINEAR);
	}

	s_glBindFramebuffer(GL_FRAMEBUFFER, 0);
	s_glDeleteFramebuffers(2, fbos);

	if (readStatus != GL_FRAMEBUFFER_COMPLETE || drawStatus != GL_FRAMEBUFFER_COMPLETE) {
		glDeleteTextures(1, &dstId);
		return 0;
	}

	return dstId;
}

// ---------------------------------------------------------------------------
// resolve_texture — used by OpenGL.ResolveTexture and markdown renderer
// ---------------------------------------------------------------------------

// Cache hit on live or sentinel -> return it (sentinel means "tried, don't retry").
// No hit -> call Resource.SetLoader -> decode -> upload -> store (sentinel on failure).
const ImguiTexture* resolve_texture(ImguiWindowContext* ctx,
	const char* source, int sourceLen) {
	if (!ctx || !source || sourceLen <= 0 || !ResourceCacheLoaderIsSet())
		return nullptr;

	char key[2048];
	int  keyLen = sourceLen < (int)(sizeof(key) - 1) ? sourceLen : (int)(sizeof(key) - 1);
	memcpy(key, source, keyLen);
	key[keyLen] = '\0';

	// Cache hit (live or sentinel)
	Resource* existing = ResourceCacheGetBySource(key, RESOURCE_TEXTURE);
	if (existing)
		return (ImguiTexture*)existing;

	KitsuneVariable* streamVar = ResourceCacheCallLoader(RESOURCE_TEXTURE, key, keyLen);
	if (!streamVar)
		return nullptr;

	if (streamVar->type != KITSUNE_TUSERDATA ||
		!streamVar->userdata ||
		!streamVar->userdata->name ||
		strcmp(streamVar->userdata->name, "STREAM") != 0) {
		fprintf(stderr, "Resource.SetLoader for type %d '%.*s' did not return a stream\n",
			RESOURCE_TEXTURE, keyLen, key);
		KitsuneVariableFree(streamVar);
		return nullptr;
	}

	KitsuneVariable* readResult = read_stream(streamVar);
	KitsuneVariableFree(streamVar);

	ImguiTexture* tex = alloc_texture();
	if (!tex) {
		KitsuneVariableFree(readResult);
		return nullptr;
	}
	if (!set_source(&tex->resource, key, keyLen)) {
		free(tex);
		KitsuneVariableFree(readResult);
		return nullptr;
	}

	if (readResult) {
		load_texture_from_bytes(readResult->data, (int)readResult->length, tex);
		KitsuneVariableFree(readResult);
	}
	// glId==0 here means sentinel — load was attempted but failed

	if (!ResourceCacheAdd(&tex->resource)) {
		texture_finalizer(&tex->resource);
		return nullptr;
	}

	if (tex->glId != 0) {
		ImguiTexture* live = call_post_loader(tex->resource.luaId, key, keyLen);
		return live;
	}

	return tex;
}

// ---------------------------------------------------------------------------
// ResolveTextureGlId — used by renderer:Image / renderer:ImageFrame
// Advances GIF animation by the current frame's delta time before returning.
// ---------------------------------------------------------------------------

unsigned int ResolveTextureGlId(int luaId) {
	ImguiTexture* tex = (ImguiTexture*)ResourceCacheGetById(luaId, RESOURCE_TEXTURE);
	if (!tex)
		return 0;
	if (tex->frameGlIds && tex->frameCount > 0) {
		double elapsedMs = (double)ImGui::GetIO().DeltaTime * 1000.0;
		tex->frameTimer += elapsedMs;
		while (tex->frameTimer >= tex->frameDelays[tex->currentFrame]) {
			tex->frameTimer -= tex->frameDelays[tex->currentFrame];
			tex->currentFrame = (tex->currentFrame + 1) % tex->frameCount;
		}
		tex->glId = tex->frameGlIds[tex->currentFrame];
	}
	return tex->glId;
}

// ---------------------------------------------------------------------------
// OpenGL.SetResourceLoader(loader [, postLoader])
// ---------------------------------------------------------------------------

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
	r.type = KITSUNE_TINTEGER;
	r.integer = tex->resource.luaId;
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// OpenGL.LoadTexture(stream [, source]) -> integer
// ---------------------------------------------------------------------------

int OpenGL_LoadTexture(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx)
		return terror(setter, "OpenGL.LoadTexture: no active session");
	if (argc < 1 || argv[0].type != KITSUNE_TUSERDATA)
		return terror(setter, "OpenGL.LoadTexture(stream[, source]): stream expected");

	const char* source = nullptr;
	int         sourceLen = 0;
	if (argc > 1 && argv[1].type == KITSUNE_TSTRING) {
		source = (const char*)argv[1].data;
		sourceLen = (int)argv[1].length;
	}

	KitsuneVariable* readResult = read_stream(&argv[0]);
	if (!readResult)
		return terror(setter, "OpenGL.LoadTexture: stream read failed");

	ImguiTexture* tex = alloc_texture();
	if (!tex) {
		KitsuneVariableFree(readResult);
		return terror(setter, "OpenGL.LoadTexture: out of memory");
	}
	if (!set_source(&tex->resource, source, sourceLen)) {
		KitsuneVariableFree(readResult);
		free(tex);
		return terror(setter, "OpenGL.LoadTexture: out of memory");
	}

	bool ok = load_texture_from_bytes(readResult->data, (int)readResult->length, tex);
	KitsuneVariableFree(readResult);
	if (!ok) {
		texture_finalizer(&tex->resource);
		return terror(setter, "OpenGL.LoadTexture: image decode failed");
	}

	if (!ResourceCacheUpsert(&tex->resource)) {
		texture_finalizer(&tex->resource);
		return terror(setter, "OpenGL.LoadTexture: out of memory");
	}

	int luaId = tex->resource.luaId;
	ResourceCacheCallPostLoader(RESOURCE_TEXTURE, luaId, source ? source : "", sourceLen);

	KitsuneVariable r = {};
	r.type = KITSUNE_TINTEGER;
	r.integer = luaId;
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// OpenGL.UnloadTexture(id)
// ---------------------------------------------------------------------------

int OpenGL_UnloadTexture(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx || argc < 1)
		return 0;
	int luaId = (int)KitsuneAsInt(&argv[0], 0);
	if (luaId == 0)
		return 0;
	ImguiTexture* tex = (ImguiTexture*)ResourceCacheGetById(luaId, RESOURCE_TEXTURE);
	if (tex) {
		if (tex->frameGlIds && tex->frameCount > 0) {
			for (int i = 0; i < tex->frameCount; i++) {
				if (tex->frameGlIds[i] != 0) {
					GLuint id = (GLuint)tex->frameGlIds[i];
					glDeleteTextures(1, &id);
					tex->frameGlIds[i] = 0;
				}
			}
		}
		else if (tex->glId != 0) {
			GLuint id = (GLuint)tex->glId;
			glDeleteTextures(1, &id);
			tex->glId = 0;
			tex->width = 0;
			tex->height = 0;
		}
	}
	return 0;
}

// ---------------------------------------------------------------------------
// OpenGL.DestroyTexture(id)
// ---------------------------------------------------------------------------

int OpenGL_DestroyTexture(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx || argc < 1)
		return 0;
	int luaId = (int)KitsuneAsInt(&argv[0], 0);
	ResourceCacheRemoveById(luaId, RESOURCE_TEXTURE);
	return 0;
}

// ---------------------------------------------------------------------------
// OpenGL.DestroyAll()
// ---------------------------------------------------------------------------

int OpenGL_DestroyAllTextures(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	ResourceCacheClearType(RESOURCE_TEXTURE);
	return 0;
}

// ---------------------------------------------------------------------------
// OpenGL.ResizeTexture(id, newW, newH [, source]) -> integer | nil
// ---------------------------------------------------------------------------

int OpenGL_ResizeTexture(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx)
		return terror(setter, "OpenGL.ResizeTexture: no active session");
	if (argc < 3)
		return terror(setter, "OpenGL.ResizeTexture(id, newW, newH [, source])");

	int luaId = (int)KitsuneAsInt(&argv[0], 0);
	int newW = (int)KitsuneAsInt(&argv[1], 0);
	int newH = (int)KitsuneAsInt(&argv[2], 0);

	if (newW <= 0 || newH <= 0)
		return terror(setter, "OpenGL.ResizeTexture: newW and newH must be positive");

	const char* source = nullptr;
	int         sourceLen = 0;
	if (argc > 3 && argv[3].type == KITSUNE_TSTRING) {
		source = (const char*)argv[3].data;
		sourceLen = (int)argv[3].length;
	}

	// Source already cached — return existing id immediately
	if (source && sourceLen > 0) {
		Resource* existing = ResourceCacheGetBySource(source, RESOURCE_TEXTURE);
		if (existing) {
			KitsuneVariable r = {};
			r.type = KITSUNE_TINTEGER;
			r.integer = existing->luaId;
			setter(&r);
			return 1;
		}
	}

	ImguiTexture* src = (ImguiTexture*)ResourceCacheGetById(luaId, RESOURCE_TEXTURE);
	if (!src)
		return terror(setter, "OpenGL.ResizeTexture: invalid texture id");
	if (src->glId == 0)
		return terror(setter, "OpenGL.ResizeTexture: texture is unloaded (sentinel); reload before resizing");

	if (source && sourceLen > 0) {
		// New named resource — blit current frame (glId) as a static texture
		GLuint dstGlId = blit_texture((GLuint)src->glId, src->width, src->height, newW, newH);
		if (!dstGlId)
			return terror(setter, "OpenGL.ResizeTexture: framebuffer blit failed");

		ImguiTexture* dst = alloc_texture();
		if (!dst) {
			glDeleteTextures(1, &dstGlId);
			return terror(setter, "OpenGL.ResizeTexture: out of memory");
		}
		if (!set_source(&dst->resource, source, sourceLen)) {
			glDeleteTextures(1, &dstGlId);
			free(dst);
			return terror(setter, "OpenGL.ResizeTexture: out of memory");
		}
		dst->glId = (unsigned int)dstGlId;
		dst->width = newW;
		dst->height = newH;
		if (!ResourceCacheAdd(&dst->resource)) {
			texture_finalizer(&dst->resource);
			return terror(setter, "OpenGL.ResizeTexture: out of memory");
		}
		KitsuneVariable r = {};
		r.type = KITSUNE_TINTEGER;
		r.integer = dst->resource.luaId;
		setter(&r);
		return 1;
	}

	// In-place resize — handle all GIF frames
	if (src->frameGlIds && src->frameCount > 0) {
		for (int i = 0; i < src->frameCount; i++) {
			if (src->frameGlIds[i] == 0)
				continue;
			GLuint newId = blit_texture((GLuint)src->frameGlIds[i], src->width, src->height, newW, newH);
			GLuint old = (GLuint)src->frameGlIds[i];
			glDeleteTextures(1, &old);
			src->frameGlIds[i] = newId ? (unsigned int)newId : 0;
		}
		src->glId = src->frameGlIds[src->currentFrame];
	}
	else {
		GLuint dstGlId = blit_texture((GLuint)src->glId, src->width, src->height, newW, newH);
		if (!dstGlId)
			return terror(setter, "OpenGL.ResizeTexture: framebuffer blit failed");
		GLuint old = (GLuint)src->glId;
		glDeleteTextures(1, &old);
		src->glId = (unsigned int)dstGlId;
	}
	src->width = newW;
	src->height = newH;
	KitsuneVariable r = {};
	r.type = KITSUNE_TINTEGER;
	r.integer = luaId;
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// OpenGL.CopyTexture(id, newSource [, frame]) -> integer | nil
// Copies a texture into a new static resource under newSource.
// For GIFs: if frame is given (1-based), copies that specific frame.
//           if frame is nil/omitted, copies the current animated frame (src->glId).
// ---------------------------------------------------------------------------

int OpenGL_CopyTexture(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx)
		return terror(setter, "OpenGL.CopyTexture: no active session");
	if (argc < 2 || argv[1].type != KITSUNE_TSTRING)
		return terror(setter, "OpenGL.CopyTexture(id, newSource [, frame]): string newSource expected");

	int         luaId = (int)KitsuneAsInt(&argv[0], 0);
	const char* newSource = (const char*)argv[1].data;
	int         newLen = (int)argv[1].length;

	ImguiTexture* src = (ImguiTexture*)ResourceCacheGetById(luaId, RESOURCE_TEXTURE);
	if (!src)
		return terror(setter, "OpenGL.CopyTexture: invalid texture id");
	if (src->glId == 0)
		return terror(setter, "OpenGL.CopyTexture: texture is unloaded (sentinel); reload before copying");

	// Can't copy to its own source
	if (src->resource.source &&
		(int)strlen(src->resource.source) == newLen &&
		strncmp(src->resource.source, newSource, newLen) == 0)
		return terror(setter, "OpenGL.CopyTexture: cannot copy texture to its own source");

	// Resolve which GL id to blit — default to current frame (src->glId)
	GLuint srcGlId = (GLuint)src->glId;
	if (argc > 2 && argv[2].type == KITSUNE_TINTEGER) {
		int frame = (int)argv[2].integer;
		if (src->frameGlIds && src->frameCount > 0) {
			if (frame < 1 || frame > src->frameCount)
				return terror(setter, "OpenGL.CopyTexture: frame out of range");
			srcGlId = (GLuint)src->frameGlIds[frame - 1];
		}
		else if (frame != 1) {
			return terror(setter, "OpenGL.CopyTexture: frame out of range");
		}
	}

	if (!srcGlId)
		return terror(setter, "OpenGL.CopyTexture: source frame is not loaded");

	GLuint dstGlId = blit_texture(srcGlId, src->width, src->height, src->width, src->height);
	if (!dstGlId) {
		KitsuneVariable r = {};
		r.type = KITSUNE_TNIL;
		setter(&r);
		return 1;
	}

	ImguiTexture* dst = alloc_texture();
	if (!dst) {
		glDeleteTextures(1, &dstGlId);
		return terror(setter, "OpenGL.CopyTexture: out of memory");
	}
	if (!set_source(&dst->resource, newSource, newLen)) {
		glDeleteTextures(1, &dstGlId);
		free(dst);
		return terror(setter, "OpenGL.CopyTexture: out of memory");
	}
	dst->glId = (unsigned int)dstGlId;
	dst->width = src->width;
	dst->height = src->height;

	// Upsert: if newSource exists, replace its GL texture
	if (!ResourceCacheUpsert(&dst->resource)) {
		texture_finalizer(&dst->resource);
		return terror(setter, "OpenGL.CopyTexture: out of memory");
	}

	KitsuneVariable r = {};
	r.type = KITSUNE_TINTEGER;
	r.integer = dst->resource.luaId;
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// OpenGL.GetId(source) -> integer | nil
// ---------------------------------------------------------------------------

int OpenGL_GetId(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx)
		return terror(setter, "OpenGL.GetId: no active session");
	if (argc < 1 || argv[0].type != KITSUNE_TSTRING)
		return terror(setter, "OpenGL.GetId(source): string expected");

	const char* source = (const char*)argv[0].data;
	int         sourceLen = (int)argv[0].length;

	// Need null-terminated key for GetBySource
	char key[2048];
	int  keyLen = sourceLen < (int)(sizeof(key) - 1) ? sourceLen : (int)(sizeof(key) - 1);
	memcpy(key, source, keyLen);
	key[keyLen] = '\0';

	Resource* res = ResourceCacheGetBySource(key, RESOURCE_TEXTURE);
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
// OpenGL.GetData(id) -> table | nil
// ---------------------------------------------------------------------------

int OpenGL_GetData(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx)
		return terror(setter, "OpenGL.GetData: no active session");
	if (argc < 1)
		return terror(setter, "OpenGL.GetData(id): id expected");

	int           luaId = (int)KitsuneAsInt(&argv[0], 0);
	ImguiTexture* tex = (ImguiTexture*)ResourceCacheGetById(luaId, RESOURCE_TEXTURE);

	if (!tex) {
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
		return terror(setter, "OpenGL.GetData: out of memory");

	KitsuneVariable widthKey = {};
	widthKey.type = KITSUNE_TSTRING;
	widthKey.data = (unsigned char*)"width";
	widthKey.length = 5;

	KitsuneVariable heightKey = {};
	heightKey.type = KITSUNE_TSTRING;
	heightKey.data = (unsigned char*)"height";
	heightKey.length = 6;

	KitsuneVariable sourceKey = {};
	sourceKey.type = KITSUNE_TSTRING;
	sourceKey.data = (unsigned char*)"source";
	sourceKey.length = 6;

	KitsuneVariable isLoadedKey = {};
	isLoadedKey.type = KITSUNE_TSTRING;
	isLoadedKey.data = (unsigned char*)"isLoaded";
	isLoadedKey.length = 8;

	KitsuneVariable frameCountKey = {};
	frameCountKey.type = KITSUNE_TSTRING;
	frameCountKey.data = (unsigned char*)"frameCount";
	frameCountKey.length = 10;

	KitsuneVariable formatKey = {};
	formatKey.type = KITSUNE_TSTRING;
	formatKey.data = (unsigned char*)"format";
	formatKey.length = 6;

	KitsuneVariable widthVal = {};
	widthVal.type = KITSUNE_TINTEGER;
	widthVal.integer = tex->width;

	KitsuneVariable heightVal = {};
	heightVal.type = KITSUNE_TINTEGER;
	heightVal.integer = tex->height;

	KitsuneVariable sourceVal = {};
	if (tex->resource.source) {
		sourceVal.type = KITSUNE_TSTRING;
		sourceVal.data = (unsigned char*)tex->resource.source;
		sourceVal.length = strlen(tex->resource.source);
	}
	else {
		sourceVal.type = KITSUNE_TNIL;
	}

	KitsuneVariable isLoadedVal = {};
	isLoadedVal.type = KITSUNE_TBOOLEAN;
	isLoadedVal.boolean = tex->glId != 0;

	KitsuneVariable frameCountVal = {};
	frameCountVal.type = KITSUNE_TINTEGER;
	frameCountVal.integer = tex->frameCount > 0 ? tex->frameCount : 1;

	KitsuneVariable formatVal = {};
	if (tex->format) {
		formatVal.type = KITSUNE_TSTRING;
		formatVal.data = (unsigned char*)tex->format;
		formatVal.length = strlen(tex->format);
	}
	else {
		formatVal.type = KITSUNE_TNIL;
	}

	KitsuneSetIndex(tbl, &widthKey, &widthVal);
	KitsuneSetIndex(tbl, &heightKey, &heightVal);
	KitsuneSetIndex(tbl, &sourceKey, &sourceVal);
	KitsuneSetIndex(tbl, &isLoadedKey, &isLoadedVal);
	KitsuneSetIndex(tbl, &frameCountKey, &frameCountVal);
	KitsuneSetIndex(tbl, &formatKey, &formatVal);

	setter(tbl);
	KitsuneVariableFree(tbl);
	return 1;
}

// ---------------------------------------------------------------------------
// OpenGL.IsLoaded(id) -> bool
// ---------------------------------------------------------------------------

int OpenGL_IsLoaded(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TBOOLEAN;
	r.boolean = false;
	if (!g_imguiCtx || argc < 1) {
		setter(&r);
		return 1;
	}
	int           luaId = (int)KitsuneAsInt(&argv[0], 0);
	ImguiTexture* tex = (ImguiTexture*)ResourceCacheGetById(luaId, RESOURCE_TEXTURE);
	r.boolean = tex != nullptr && tex->glId != 0;
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// OpenGL.GetTextureCount() -> count, bytes
// ---------------------------------------------------------------------------

struct CountState { int count; long long bytes; };

static bool count_iter(Resource* res, const void* ud) {
	if (res->type != RESOURCE_TEXTURE)
		return true;
	CountState* s = (CountState*)ud;
	s->count++;
	ImguiTexture* tex = (ImguiTexture*)res;
	if (tex->glId != 0) {
		int frames = tex->frameCount > 0 ? tex->frameCount : 1;
		s->bytes += (long long)tex->width * tex->height * 4 * frames;
	}
	return true;
}

int OpenGL_GetTextureCount(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	CountState s = { 0, 0 };
	ResourceCacheIterate(count_iter, &s);
	KitsuneVariable rc = {};
	rc.type = KITSUNE_TINTEGER;
	rc.integer = s.count;
	setter(&rc);
	KitsuneVariable rb = {};
	rb.type = KITSUNE_TINTEGER;
	rb.integer = s.bytes;
	setter(&rb);
	return 2;
}

// ---------------------------------------------------------------------------
// OpenGL.GetAllLoadedTextures() -> table
// ---------------------------------------------------------------------------

struct CollectState { KitsuneVariable* tbl; int seq; };

static bool collect_iter(Resource* res, const void* ud) {
	if (res->type != RESOURCE_TEXTURE)
		return true;
	CollectState* s = (CollectState*)ud;
	KitsuneVariable k = {};
	k.type = KITSUNE_TINTEGER;
	k.integer = s->seq++;
	KitsuneVariable v = {};
	v.type = KITSUNE_TINTEGER;
	v.integer = res->luaId;
	KitsuneSetIndex(s->tbl, &k, &v);
	return true;
}

int OpenGL_GetAllLoadedTextures(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx)
		return terror(setter, "OpenGL.GetAllLoadedTextures: no active session");

	KitsuneVariable tableVar = {};
	tableVar.type = KITSUNE_TTABLECONTENTS;
	tableVar.table = nullptr;
	KitsuneVariable* tbl = KitsuneAnchorVariable(&tableVar);
	if (!tbl)
		return terror(setter, "OpenGL.GetAllLoadedTextures: out of memory");

	CollectState s = { tbl, 1 };
	ResourceCacheIterate(collect_iter, &s);

	setter(tbl);
	KitsuneVariableFree(tbl);
	return 1;
}

// ---------------------------------------------------------------------------
// OpenGL.GetFrameUVs(id, frameIndex, cols, rows) -> u0, v0, u1, v1
// ---------------------------------------------------------------------------

int OpenGL_GetFrameUVs(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx)
		return terror(setter, "OpenGL.GetFrameUVs: no active session");
	if (argc < 4)
		return terror(setter, "OpenGL.GetFrameUVs(id, frameIndex, cols, rows)");

	int luaId = (int)KitsuneAsInt(&argv[0], 0);
	int frameIndex = (int)KitsuneAsInt(&argv[1], 1);
	int cols = (int)KitsuneAsInt(&argv[2], 1);
	int rows = (int)KitsuneAsInt(&argv[3], 1);

	if (cols <= 0 || rows <= 0)
		return terror(setter, "OpenGL.GetFrameUVs: cols and rows must be positive");
	if (!ResourceCacheGetById(luaId, RESOURCE_TEXTURE))
		return terror(setter, "OpenGL.GetFrameUVs: unknown texture id");
	if (frameIndex < 1 || frameIndex > cols * rows)
		return terror(setter, "OpenGL.GetFrameUVs: frameIndex out of range");

	int col = (frameIndex - 1) % cols;
	int row = (frameIndex - 1) / cols;

	float u0 = (float)col / (float)cols;
	float v0 = (float)row / (float)rows;
	float u1 = (float)(col + 1) / (float)cols;
	float v1 = (float)(row + 1) / (float)rows;

	KitsuneVariable ru0 = {}; ru0.type = KITSUNE_TNUMBER; ru0.number = (double)u0;
	KitsuneVariable rv0 = {}; rv0.type = KITSUNE_TNUMBER; rv0.number = (double)v0;
	KitsuneVariable ru1 = {}; ru1.type = KITSUNE_TNUMBER; ru1.number = (double)u1;
	KitsuneVariable rv1 = {}; rv1.type = KITSUNE_TNUMBER; rv1.number = (double)v1;
	setter(&ru0);
	setter(&rv0);
	setter(&ru1);
	setter(&rv1);
	return 4;
}

// ---------------------------------------------------------------------------
// RegisterOpenGLFunctions
// ---------------------------------------------------------------------------

void RegisterOpenGLFunctions() {
	KitsuneRegisterFunction("OpenGL.LoadTexture",          OpenGL_LoadTexture,          nullptr);
	KitsuneRegisterFunction("OpenGL.UnloadTexture",        OpenGL_UnloadTexture,        nullptr);
	KitsuneRegisterFunction("OpenGL.DestroyTexture",       OpenGL_DestroyTexture,       nullptr);
	KitsuneRegisterFunction("OpenGL.DestroyAllTextures",   OpenGL_DestroyAllTextures,   nullptr);
	KitsuneRegisterFunction("OpenGL.ResolveTexture",       OpenGL_ResolveTexture,       nullptr);
	KitsuneRegisterFunction("OpenGL.ResizeTexture",        OpenGL_ResizeTexture,        nullptr);
	KitsuneRegisterFunction("OpenGL.CopyTexture",          OpenGL_CopyTexture,          nullptr);
	KitsuneRegisterFunction("OpenGL.GetId",                OpenGL_GetId,                nullptr);
	KitsuneRegisterFunction("OpenGL.GetData",              OpenGL_GetData,              nullptr);
	KitsuneRegisterFunction("OpenGL.IsLoaded",             OpenGL_IsLoaded,             nullptr);
	KitsuneRegisterFunction("OpenGL.GetTextureCount",      OpenGL_GetTextureCount,      nullptr);
	KitsuneRegisterFunction("OpenGL.GetFrameUVs",          OpenGL_GetFrameUVs,          nullptr);
	KitsuneRegisterFunction("OpenGL.GetAllLoadedTextures", OpenGL_GetAllLoadedTextures, nullptr);
}

#endif // KITSUNE_IMGUI
