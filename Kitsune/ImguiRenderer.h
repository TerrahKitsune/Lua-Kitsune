#pragma once
#ifdef KITSUNE_IMGUI

#include "KitsuneEngine.h"
#include "ResourceCache.h"
#include <SDL.h>
#include <SDL_opengl.h>

struct ImguiScheduledCall {
	KitsuneVariable*    fn;
	int                 argc;
	KitsuneVariable**   argv;
	ImguiScheduledCall* next;
};

// Texture resource — Resource must be first field so Resource* casts work.
// glId==0 with luaId!=0 means sentinel (load attempted, failed or unloaded).
struct ImguiTexture {
	Resource     resource;  // type=RESOURCE_TEXTURE; luaId and source live here
	unsigned int glId;      // GL texture object; 0 = sentinel
	int          width;
	int          height;
};

struct ImguiWindowContext {
	SDL_Window*                 window;
	SDL_GLContext               glContext;
	void*                       imguiContext;
	char*                       title;
	int                         width;
	int                         height;
	char*                       inputBuf;
	size_t                      inputBufSize;
	KitsuneVariable*            renderFn;
	KitsuneVariable*            context;
	KitsuneVariable*            onError;
	ImguiScheduledCall*         scheduledHead;
	KitsuneUserDataRegistration reg;
	// Markdown cache
	uint64_t                    mdCacheId;
	char*                       mdContent;
	size_t                      mdContentLen;
	struct MarkdownNode*        mdNodes;
	int                         mdNodeCount;
	int                         mdNodeAlloc;
	const char*                 mdCacheError;
};

// Populates reg->Functions with heap-allocated KitsuneNamedFunction nodes for
// all auto-generated ImGui bindings. Called once in RunImguiSession before the
// render loop starts. After KitsuneRegisterUserdata returns the nodes are kept
// alive in ctx->reg and freed in __gc.
void add_imgui_bindings(KitsuneUserDataRegistration* reg);

// Adds the __gc and __tostring metamethods and hand-written override functions
// (InputText, PlotLines, Combo, ListBox, etc.) into reg.
void add_imgui_meta_bindings(KitsuneUserDataRegistration* reg);

// renderer:Image(handle, width, height [, uv0x, uv0y, uv1x, uv1y])
int ImguiRenderer_Image(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud);

// renderer:ImageFrame(id, w, h, frameIndex, cols, rows [, flipX [, flipY]])
// flipX mirrors horizontally; flipY mirrors vertically. Both default to false.
int ImguiRenderer_ImageFrame(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud);

#endif // KITSUNE_IMGUI
