#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

#include "Font.h"
#include "ResourceCache.h"
#include "KitsuneEngine.h"
#include "Imgui/imgui.h"
#include "Imgui/imgui_impl_opengl3.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// Atlas rebuild flag
// ---------------------------------------------------------------------------

static bool s_rebuildPending = false;

bool FontAtlasRebuildPending() {
    return s_rebuildPending;
}

void FontClearRebuildFlag() {
    s_rebuildPending = false;
}

// ---------------------------------------------------------------------------
// Error helper
// ---------------------------------------------------------------------------

static int font_error(const kitsune_ResultSetter setter, const char* msg) {
    KitsuneVariable e = {};
    e.type = KITSUNE_TERROR;
    e.data = (unsigned char*)msg;
    e.length = strlen(msg);
    setter(&e);
    return 1;
}

// ---------------------------------------------------------------------------
// Finalizer
// ---------------------------------------------------------------------------

static void font_free(Resource* node) {
    FontResource* f = (FontResource*)node;
    // Do NOT free f->ttfData here — ImGui holds a live pointer into it until
    // the atlas is rebuilt without this font. Safe to free only after the atlas
    // no longer references it. For simplicity we leak on destroy; the atlas
    // rebuild path (via Font.SetDefault(nil) + forced rebuild) is the safe exit.
    // In practice fonts are session-lived and freed at ResourceCacheShutdown
    // after ImGui has been torn down.
    free(f->resource.source);
    free(f);
}

// ---------------------------------------------------------------------------
// Source key helper — "FaceName:size:style"
// ---------------------------------------------------------------------------

static int make_font_key(char* buf, int bufLen, const char* face, float size, int style) {
    return snprintf(buf, bufLen, "%s:%.1f:%d", face, size, style);
}

// ---------------------------------------------------------------------------
// FontResolveInternal
// Looks up or loads a FontResource by face/size/style.
// Returns imFont* (may be nullptr for one transitional frame before first build).
// Sets s_rebuildPending when a new font is registered.
// ---------------------------------------------------------------------------

ImFont* FontResolveInternal(const char* face, float size, int style) {
    if (!face || size <= 0.0f)
        return nullptr;

    char key[256];
    int keyLen = make_font_key(key, sizeof(key), face, size, style);
    if (keyLen <= 0 || keyLen >= (int)sizeof(key))
        return nullptr;

    FontResource* existing = (FontResource*)ResourceCacheGetBySource(key, RESOURCE_FONT);
    if (existing)
        return existing->imFont;

    // Not cached — call the loader to get TTF bytes.
    KitsuneVariable* streamVar = ResourceCacheCallLoader(RESOURCE_FONT, key, keyLen);
    if (!streamVar)
        return nullptr;

    // Read bytes from stream or string.
    uint8_t* ttfData = nullptr;
    size_t ttfLength = 0;

    if (streamVar->type == KITSUNE_TSTRING) {
        if (streamVar->length > 0) {
            ttfData = (uint8_t*)malloc(streamVar->length);
            if (ttfData) {
                memcpy(ttfData, streamVar->data, streamVar->length);
                ttfLength = streamVar->length;
            }
        }
    }
    else if (streamVar->type == KITSUNE_TUSERDATA) {
        KitsuneVariable seekArg = {};
        seekArg.type = KITSUNE_TINTEGER;
        seekArg.integer = 0;
        KitsuneVariable* r = KitsuneCallMethod(streamVar, "Seek", 1, &seekArg);
        bool ok = r && r->type != KITSUNE_TERROR;
        KitsuneVariableFree(r);
        if (ok) {
            KitsuneVariable* readResult = KitsuneCallMethod(streamVar, "Read", 0, nullptr);
            if (readResult && readResult->type == KITSUNE_TSTRING && readResult->length > 0) {
                ttfData = (uint8_t*)malloc(readResult->length);
                if (ttfData) {
                    memcpy(ttfData, readResult->data, readResult->length);
                    ttfLength = readResult->length;
                }
            }
            KitsuneVariableFree(readResult);
        }
    }

    KitsuneVariableFree(streamVar);

    if (!ttfData) {
        // Store a sentinel (ttfData=null, imFont=null) so we never hit the loader
        // again for this key. The sentinel is a valid cache entry that returns nullptr.
        FontResource* sentinel = (FontResource*)calloc(1, sizeof(FontResource));
        if (sentinel) {
            sentinel->resource.type = RESOURCE_FONT;
            sentinel->resource.source = (char*)malloc(keyLen + 1);
            if (sentinel->resource.source) {
                memcpy(sentinel->resource.source, key, keyLen);
                sentinel->resource.source[keyLen] = '\0';
            }
            sentinel->resource.fn = font_free;
            sentinel->ttfData = nullptr;
            sentinel->imFont  = nullptr;
            ResourceCacheAdd(&sentinel->resource);
        }
        return nullptr;
    }

    FontResource* f = (FontResource*)calloc(1, sizeof(FontResource));
    if (!f) {
        free(ttfData);
        return nullptr;
    }

    f->resource.type = RESOURCE_FONT;
    f->resource.source = (char*)malloc(keyLen + 1);
    if (f->resource.source) {
        memcpy(f->resource.source, key, keyLen);
        f->resource.source[keyLen] = '\0';
    }
    f->resource.fn = font_free;
    f->ttfData = ttfData;
    f->ttfLength = ttfLength;
    f->size = size;
    f->style = style;
    f->imFont = nullptr; // set after atlas rebuild

    // Register into ImGui atlas. ImGui keeps a pointer into ttfData — do not free.
    ImFontConfig cfg = {};
    cfg.FontDataOwnedByAtlas = false; // we own the memory
    f->imFont = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
        ttfData, (int)ttfLength, size, &cfg);

    if (!ResourceCacheAdd(&f->resource)) {
        // Atlas already has the font registered — can't undo that, but at least
        // free the node. The atlas entry will be a no-op orphan until next rebuild.
        free(f->resource.source);
        free(f);
        s_rebuildPending = true;
        return nullptr;
    }

    s_rebuildPending = true;
    return f->imFont; // valid after next NewFrame atlas rebuild
}

// ---------------------------------------------------------------------------
// FontPush / FontPop
// ---------------------------------------------------------------------------

void FontPush(ImFont* font) {
    ImGui::PushFont(font); // nullptr = ImGui default font; always safe
}

void FontPop() {
    ImGui::PopFont();
}

// ---------------------------------------------------------------------------
// Font.Resolve(face, size, opt bold, opt italic) -> luaId | nil
// ---------------------------------------------------------------------------

int Font_Resolve(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    KitsuneVariable r = {};
    r.type = KITSUNE_TNIL;
    if (argc < 2) {
        setter(&r);
        return 1;
    }
    if (argv[0].type != KITSUNE_TSTRING || argv[0].length == 0) {
        setter(&r);
        return 1;
    }
    const char* face = (const char*)argv[0].data;
    float size = KitsuneAsFloat(&argv[1], 0.0f);
    if (size <= 0.0f) {
        setter(&r);
        return 1;
    }
    int style = FONT_STYLE_REGULAR;
    if (argc >= 3 && KitsuneAsBool(&argv[2]))
        style |= FONT_STYLE_BOLD;
    if (argc >= 4 && KitsuneAsBool(&argv[3]))
        style |= FONT_STYLE_ITALIC;

    char key[256];
    int keyLen = make_font_key(key, sizeof(key), face, size, style);
    if (keyLen <= 0 || keyLen >= (int)sizeof(key)) {
        setter(&r);
        return 1;
    }

    FontResource* existing = (FontResource*)ResourceCacheGetBySource(key, RESOURCE_FONT);
    if (existing) {
        r.type = KITSUNE_TINTEGER;
        r.integer = existing->resource.luaId;
        setter(&r);
        return 1;
    }

    FontResolveInternal(face, size, style);

    existing = (FontResource*)ResourceCacheGetBySource(key, RESOURCE_FONT);
    if (existing) {
        r.type = KITSUNE_TINTEGER;
        r.integer = existing->resource.luaId;
    }
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// Font.Destroy(luaId)
// ---------------------------------------------------------------------------

int Font_Destroy(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 1)
        return 0;
    int luaId = (int)KitsuneAsInt(&argv[0], 0);
    if (luaId > 0)
        ResourceCacheRemoveById(luaId, RESOURCE_FONT);
    return 0;
}

// ---------------------------------------------------------------------------
// Font.GetData(luaId) -> { face, size, style, isBuilt } | nil
// ---------------------------------------------------------------------------

int Font_GetData(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    KitsuneVariable r = {};
    r.type = KITSUNE_TNIL;
    if (argc < 1) {
        setter(&r);
        return 1;
    }
    int luaId = (int)KitsuneAsInt(&argv[0], 0);
    FontResource* f = (FontResource*)ResourceCacheGetById(luaId, RESOURCE_FONT);
    if (!f) {
        setter(&r);
        return 1;
    }

    KitsuneVariable tableVar = {};
    tableVar.type = KITSUNE_TTABLECONTENTS;
    tableVar.table = nullptr;
    KitsuneVariable* tbl = KitsuneAnchorVariable(&tableVar);
    if (!tbl) {
        setter(&r);
        return 1;
    }

    // Extract face name from source key ("FaceName:size:style")
    const char* src = f->resource.source ? f->resource.source : "";
    const char* colon = strchr(src, ':');
    int faceLen = colon ? (int)(colon - src) : (int)strlen(src);

    KitsuneVariable faceKey = {};
    faceKey.type = KITSUNE_TSTRING;
    faceKey.data = (unsigned char*)"face";
    faceKey.length = 4;

    KitsuneVariable faceVal = {};
    faceVal.type = KITSUNE_TSTRING;
    faceVal.data = (unsigned char*)src;
    faceVal.length = faceLen;

    KitsuneVariable sizeKey = {};
    sizeKey.type = KITSUNE_TSTRING;
    sizeKey.data = (unsigned char*)"size";
    sizeKey.length = 4;

    KitsuneVariable sizeVal = {};
    sizeVal.type = KITSUNE_TNUMBER;
    sizeVal.number = (double)f->size;

    KitsuneVariable styleKey = {};
    styleKey.type = KITSUNE_TSTRING;
    styleKey.data = (unsigned char*)"style";
    styleKey.length = 5;

    KitsuneVariable styleVal = {};
    styleVal.type = KITSUNE_TINTEGER;
    styleVal.integer = f->style;

    KitsuneVariable builtKey = {};
    builtKey.type = KITSUNE_TSTRING;
    builtKey.data = (unsigned char*)"isBuilt";
    builtKey.length = 7;

    KitsuneVariable builtVal = {};
    builtVal.type = KITSUNE_TBOOLEAN;
    builtVal.boolean = (f->imFont != nullptr);

    KitsuneSetIndex(tbl, &faceKey, &faceVal);
    KitsuneSetIndex(tbl, &sizeKey, &sizeVal);
    KitsuneSetIndex(tbl, &styleKey, &styleVal);
    KitsuneSetIndex(tbl, &builtKey, &builtVal);

    setter(tbl);
    KitsuneVariableFree(tbl);
    return 1;
}

// ---------------------------------------------------------------------------
// Font.GetId(face, size, opt bold, opt italic) -> luaId | nil
// Lookup without triggering a load.
// ---------------------------------------------------------------------------

int Font_GetId(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    KitsuneVariable r = {};
    r.type = KITSUNE_TNIL;
    if (argc < 2 || argv[0].type != KITSUNE_TSTRING || argv[0].length == 0) {
        setter(&r);
        return 1;
    }
    const char* face = (const char*)argv[0].data;
    float size = KitsuneAsFloat(&argv[1], 0.0f);
    int style = FONT_STYLE_REGULAR;
    if (argc >= 3 && KitsuneAsBool(&argv[2]))
        style |= FONT_STYLE_BOLD;
    if (argc >= 4 && KitsuneAsBool(&argv[3]))
        style |= FONT_STYLE_ITALIC;

    char key[256];
    int keyLen = make_font_key(key, sizeof(key), face, size, style);
    if (keyLen <= 0 || keyLen >= (int)sizeof(key)) {
        setter(&r);
        return 1;
    }

    FontResource* f = (FontResource*)ResourceCacheGetBySource(key, RESOURCE_FONT);
    if (f) {
        r.type = KITSUNE_TINTEGER;
        r.integer = f->resource.luaId;
    }
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// Font.SetDefault(opt luaId)
// Sets the ImGui default font to the given FontResource.
// Pass 0 or nil to restore the built-in default.
// Takes effect after the next atlas rebuild.
// ---------------------------------------------------------------------------

int Font_SetDefault(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    if (argc < 1 || argv[0].type == KITSUNE_TNIL || argv[0].type == KITSUNE_TNONE) {
        ImGui::GetIO().FontDefault = nullptr;
        return 0;
    }
    int luaId = (int)KitsuneAsInt(&argv[0], 0);
    if (luaId <= 0) {
        ImGui::GetIO().FontDefault = nullptr;
        return 0;
    }
    FontResource* f = (FontResource*)ResourceCacheGetById(luaId, RESOURCE_FONT);
    if (f && f->imFont)
        ImGui::GetIO().FontDefault = f->imFont;
    return 0;
}

// ---------------------------------------------------------------------------
// RegisterFontFunctions
// ---------------------------------------------------------------------------

void RegisterFontFunctions() {
    KitsuneRegisterFunction("Font.Resolve", Font_Resolve, nullptr);
    KitsuneRegisterFunction("Font.Destroy", Font_Destroy, nullptr);
    KitsuneRegisterFunction("Font.GetData", Font_GetData, nullptr);
    KitsuneRegisterFunction("Font.GetId", Font_GetId, nullptr);
    KitsuneRegisterFunction("Font.SetDefault", Font_SetDefault, nullptr);
}

#endif // KITSUNE_IMGUI
