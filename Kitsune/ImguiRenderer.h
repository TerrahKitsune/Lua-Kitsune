#pragma once
#ifdef KITSUNE_IMGUI

#include "KitsuneEngine.h"
#include "ResourceCache.h"
#include <SDL.h>
#include <SDL_opengl.h>

// Texture resource — Resource must be first field so Resource* casts work.
// glId==0 with luaId!=0 means sentinel (load attempted, failed or unloaded).
struct ImguiTexture {
	Resource     resource;  // type=RESOURCE_TEXTURE; luaId and source live here
	unsigned int glId;      // GL texture object; 0 = sentinel (or use frameGlIds[currentFrame] for GIFs)
	int          width;
	int          height;
	const char*  format;    // static string literal: "gif", "png", "jpeg", "bmp", or "unknown"
	// GIF animation — nullptr/0 when not a GIF
	unsigned int* frameGlIds;   // heap array of per-frame GL texture ids
	int*          frameDelays;  // per-frame delay in ms (from stbi)
	int           frameCount;
	int           currentFrame; // 0-based index of the frame currently shown
	double        frameTimer;   // accumulated time in ms since last frame advance
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
	KitsuneUserDataRegistration reg;
	// Optional bold/italic fonts for markdown rendering.
	// Set via renderer:SetBoldFont(luaId) / renderer:SetItalicFont(luaId).
	// nullptr = fall back to colour stand-in.
	struct ImFont*              font_bold;
	struct ImFont*              font_italic;
	// ImguiStack — linked list of push/pop entries; drained before each frame
	struct ImguiStackEntry*     stackHead;
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

// renderer:ImageButton(label, id, w, h [, uv0x, uv0y, uv1x, uv1y])
int ImguiRenderer_ImageButton(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud);

// renderer:ImageFrame(id, w, h, frameIndex, cols, rows [, flipX [, flipY]])
// flipX mirrors horizontally; flipY mirrors vertically. Both default to false.
int ImguiRenderer_ImageFrame(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud);

// Stack IDs for bold/italic entries — used by ImguiPushFontStyle and ImguiMarkdown.
#define IMGUI_STACK_BOLD   1
#define IMGUI_STACK_ITALIC 2

// Fallback text colors used when the bold/italic font variant is not loaded.
// Applied as PushStyleColor(ImGuiCol_Text) stand-ins in both the renderer
// functions and the markdown renderer so they always match.
#define IMGUI_BOLD_FALLBACK_R   1.0f
#define IMGUI_BOLD_FALLBACK_G   0.85f
#define IMGUI_BOLD_FALLBACK_B   0.4f
#define IMGUI_BOLD_FALLBACK_A   1.0f
#define IMGUI_ITALIC_FALLBACK_R 0.75f
#define IMGUI_ITALIC_FALLBACK_G 0.75f
#define IMGUI_ITALIC_FALLBACK_B 0.75f
#define IMGUI_ITALIC_FALLBACK_A 1.0f

// Pushes a font style (bold or italic) onto the ImguiStack.
// Tries to resolve the current font + styleBit; falls back to a color if the
// styled font variant has not been loaded. r/g/b/a are the fallback text color.
void ImguiPushFontStyle(int stackId, int styleBit, float r, float g, float b, float a);

// Pops the innermost entry with the given stackId, calling its finalizer.
void ImguiPopFontStyle(int stackId);

// Sets the mutable fallback color for the given style bit (FONT_STYLE_BOLD or
// FONT_STYLE_ITALIC) used when the styled font variant is not loaded.
void ImguiSetFontStyleFallback(int styleBit, float r, float g, float b, float a);

#endif // KITSUNE_IMGUI
