#pragma once
#ifdef KITSUNE_IMGUI

#include "ResourceCache.h"
#include "KitsuneEngine.h"
#include "Imgui/imgui.h"

// ---------------------------------------------------------------------------
// FontResource
// Font resources are owned by the ResourceCache (type=RESOURCE_FONT).
// The raw TTF bytes must outlive the ImGui font atlas — ImGui does not copy them.
// imFont is nullptr until the atlas rebuild completes (the frame after Resolve).
// ---------------------------------------------------------------------------

struct FontResource {
    Resource  resource;    // type=RESOURCE_FONT; source="FaceName:size:style"
    uint8_t*  ttfData;     // heap-owned TTF bytes; must outlive ImGui atlas
    size_t    ttfLength;
    float     size;        // pixel size
    int       style;       // FONT_STYLE_* bitmask
    ImFont*   imFont;      // nullptr until next atlas build
};

#define FONT_STYLE_REGULAR  0
#define FONT_STYLE_BOLD     1
#define FONT_STYLE_ITALIC   2

// ---------------------------------------------------------------------------
// Atlas rebuild flag
// Set when a new font has been registered since the last build.
// Checked by RenderLoop before ImGui::NewFrame(); cleared after rebuild.
// ---------------------------------------------------------------------------

bool FontAtlasRebuildPending();
void FontClearRebuildFlag();

// ---------------------------------------------------------------------------
// Internal C helpers
// Used by KitsuneHtmlContainer and any future renderer that needs font access.
// FontResolveInternal may return nullptr for the one transitional frame before
// the first atlas build — callers must handle nullptr (FontPush treats it as
// the ImGui default font, which is always safe).
// ---------------------------------------------------------------------------

ImFont* FontResolveInternal(const char* face, float size, int style);
void    FontPush(ImFont* font);   // nullptr = default font; always safe
void    FontPop();

// ---------------------------------------------------------------------------
// Module registration
// Called from RegisterImguiFunctions() in ImguiSession.cpp.
// ---------------------------------------------------------------------------

void RegisterFontFunctions();

// ---------------------------------------------------------------------------
// Lua function declarations (Font.*)
// ---------------------------------------------------------------------------

int Font_Resolve(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int Font_Destroy(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int Font_GetData(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int Font_GetId(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);
int Font_SetDefault(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void* ud);

#endif // KITSUNE_IMGUI
