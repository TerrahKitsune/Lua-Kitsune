#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

#include "ImguiRenderer.h"

#include "Imgui/imgui.h"
#include "Imgui/imgui_impl_sdl2.h"
#include "Imgui/imgui_impl_opengl3.h"
#include <SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

// argv[0] is always the renderer userdata (self) from Lua colon-call syntax.
// Use these macros in every kitsune_CFunction in this file.
#define IMGUI_ARGC  (argc - 1)
#define IMGUI_ARGV  (argc > 0 ? argv + 1 : argv)

// ---------------------------------------------------------------------------
// Metamethods
// ---------------------------------------------------------------------------

static int imgui_gc(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    ImguiWindowContext* ctx = (ImguiWindowContext*)ud;
    if (!ctx)
        return 0;

    // Drain any remaining scheduled calls
    while (ctx->scheduledHead) {
        ImguiScheduledCall* call = ctx->scheduledHead;
        ctx->scheduledHead = call->next;
        for (int i = 0; i < call->argc; i++)
            KitsuneVariableFree(call->argv[i]);
        KitsuneVariableFree(call->fn);
        free(call->argv);
        free(call);
    }

    // Release anchored Lua variables
    if (ctx->renderFn) { KitsuneVariableFree(ctx->renderFn); ctx->renderFn = nullptr; }
    if (ctx->context)  { KitsuneVariableFree(ctx->context);  ctx->context  = nullptr; }
    if (ctx->onError)  { KitsuneVariableFree(ctx->onError);  ctx->onError  = nullptr; }

    // Free NamedKitsuneFunction nodes in reg.Functions
    NamedKitsuneFunction* fn = ctx->reg.Functions;
    while (fn) {
        NamedKitsuneFunction* next = fn->Next;
        free(fn);
        fn = next;
    }
    ctx->reg.Functions = nullptr;

    // Free NamedKitsuneFunction nodes in reg.MetaTableFunctions
    fn = ctx->reg.MetaTableFunctions;
    while (fn) {
        NamedKitsuneFunction* next = fn->Next;
        free(fn);
        fn = next;
    }
    ctx->reg.MetaTableFunctions = nullptr;

    // SDL2 / ImGui teardown
    if (ctx->imguiContext) {
        ImGui::SetCurrentContext((ImGuiContext*)ctx->imguiContext);
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext((ImGuiContext*)ctx->imguiContext);
        ctx->imguiContext = nullptr;
    }

    if (ctx->glContext) {
        SDL_GL_DeleteContext(ctx->glContext);
        ctx->glContext = nullptr;
    }

    if (ctx->window) {
        SDL_DestroyWindow(ctx->window);
        ctx->window = nullptr;
    }

    SDL_Quit();

    // Free heap buffers
    free(ctx->inputBuf);
    free(ctx->title);
    free(ctx);

    extern ImguiWindowContext* g_imguiCtx;
    g_imguiCtx = nullptr;

    return 0;
}

static int imgui_tostring(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    char buf[64];
    snprintf(buf, sizeof(buf), "OpenGL3 / Dear ImGui %s", ImGui::GetVersion());
    KitsuneVariable r = {};
    r.type   = KITSUNE_TSTRING;
    r.data   = (unsigned char*)buf;
    r.length = strlen(buf);
    setter(&r);
    return 1;
}

// ---------------------------------------------------------------------------
// Hand-written overrides: Text variants
// These take a format string + varargs in C++ but in Lua we just take the
// already-formatted string and pass it to the Unformatted variants.
// ---------------------------------------------------------------------------

static int ImguiRenderer_Text(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    const int _argc = IMGUI_ARGC;
    const KitsuneVariable* _argv = IMGUI_ARGV;
    if (_argc < 1) return 0;
    KitsuneVariable* owned = nullptr;
    const char* str = nullptr;
    if (_argv[0].type == KITSUNE_TSTRING) {
        str = (const char*)_argv[0].data;
    } else {
        owned = KitsuneToString(&_argv[0]);
        str = owned ? (const char*)owned->data : "";
    }
    ImGui::TextUnformatted(str);
    if (owned) KitsuneVariableFree(owned);
    return 0;
}

static int ImguiRenderer_TextColored(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    const int _argc = IMGUI_ARGC;
    const KitsuneVariable* _argv = IMGUI_ARGV;
    if (_argc < 5) return 0;
    ImVec4 col(KitsuneAsFloat(&_argv[0], 1.0f), KitsuneAsFloat(&_argv[1], 1.0f),
               KitsuneAsFloat(&_argv[2], 1.0f), KitsuneAsFloat(&_argv[3], 1.0f));
    KitsuneVariable* owned = nullptr;
    const char* str = nullptr;
    if (_argv[4].type == KITSUNE_TSTRING) {
        str = (const char*)_argv[4].data;
    } else {
        owned = KitsuneToString(&_argv[4]);
        str = owned ? (const char*)owned->data : "";
    }
    ImGui::TextColored(col, "%s", str);
    if (owned) KitsuneVariableFree(owned);
    return 0;
}

static int ImguiRenderer_TextDisabled(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    const int _argc = IMGUI_ARGC;
    const KitsuneVariable* _argv = IMGUI_ARGV;
    if (_argc < 1) return 0;
    KitsuneVariable* owned = nullptr;
    const char* str = nullptr;
    if (_argv[0].type == KITSUNE_TSTRING) {
        str = (const char*)_argv[0].data;
    } else {
        owned = KitsuneToString(&_argv[0]);
        str = owned ? (const char*)owned->data : "";
    }
    ImGui::TextDisabled("%s", str);
    if (owned) KitsuneVariableFree(owned);
    return 0;
}

static int ImguiRenderer_TextWrapped(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    const int _argc = IMGUI_ARGC;
    const KitsuneVariable* _argv = IMGUI_ARGV;
    if (_argc < 1) return 0;
    KitsuneVariable* owned = nullptr;
    const char* str = nullptr;
    if (_argv[0].type == KITSUNE_TSTRING) {
        str = (const char*)_argv[0].data;
    } else {
        owned = KitsuneToString(&_argv[0]);
        str = owned ? (const char*)owned->data : "";
    }
    ImGui::TextWrapped("%s", str);
    if (owned) KitsuneVariableFree(owned);
    return 0;
}

// ---------------------------------------------------------------------------
// Hand-written override: InputText
// ---------------------------------------------------------------------------

static int ImguiRenderer_InputText(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    ImguiWindowContext* ctx = (ImguiWindowContext*)ud;
    const int _argc = IMGUI_ARGC;
    const KitsuneVariable* _argv = IMGUI_ARGV;

    if (_argc < 2) {
        KitsuneVariable err = {};
        const char* msg = "InputText requires label and value arguments";
        err.type = KITSUNE_TERROR; err.data = (unsigned char*)msg; err.length = strlen(msg);
        setter(&err);
        return 1;
    }

    size_t needed = (_argc > 2 ? (size_t)_argv[2].integer : 256) + 1;
    if (_argv[1].length + 1 > needed)
        needed = _argv[1].length + 1;

    if (needed > ctx->inputBufSize) {
        char* grown = (char*)realloc(ctx->inputBuf, needed);
        if (!grown)
            return 0;
        ctx->inputBuf     = grown;
        ctx->inputBufSize = needed;
    }

    size_t len = _argv[1].length < ctx->inputBufSize - 1
        ? _argv[1].length : ctx->inputBufSize - 1;
    memcpy(ctx->inputBuf, _argv[1].data, len);
    ctx->inputBuf[len] = '\0';

    const char* label = _argv[0].type == KITSUNE_TSTRING
        ? (const char*)_argv[0].data : "";
    KitsuneVariable* labelOwned = nullptr;
    if (_argv[0].type != KITSUNE_TSTRING) {
        labelOwned = KitsuneToString(&_argv[0]);
        label = labelOwned ? (const char*)labelOwned->data : "";
    }

    bool changed = ImGui::InputText(label, ctx->inputBuf, ctx->inputBufSize);

    if (labelOwned) KitsuneVariableFree(labelOwned);

    KitsuneVariable r1 = {}; r1.type = KITSUNE_TBOOLEAN; r1.boolean = changed;
    KitsuneVariable r2 = {}; r2.type = KITSUNE_TSTRING;
    r2.data   = (unsigned char*)ctx->inputBuf;
    r2.length = strlen(ctx->inputBuf);
    setter(&r1);
    setter(&r2);
    return 2;
}

// ---------------------------------------------------------------------------
// Hand-written override: InputTextMultiline
// ---------------------------------------------------------------------------

static int ImguiRenderer_InputTextMultiline(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    ImguiWindowContext* ctx = (ImguiWindowContext*)ud;
    const int _argc = IMGUI_ARGC;
    const KitsuneVariable* _argv = IMGUI_ARGV;

    if (_argc < 2) {
        KitsuneVariable err = {};
        const char* msg = "InputTextMultiline requires label and value arguments";
        err.type = KITSUNE_TERROR; err.data = (unsigned char*)msg; err.length = strlen(msg);
        setter(&err);
        return 1;
    }

    size_t needed = (_argc > 4 ? (size_t)_argv[4].integer : 256) + 1;
    if (_argv[1].length + 1 > needed)
        needed = _argv[1].length + 1;

    if (needed > ctx->inputBufSize) {
        char* grown = (char*)realloc(ctx->inputBuf, needed);
        if (!grown)
            return 0;
        ctx->inputBuf     = grown;
        ctx->inputBufSize = needed;
    }

    size_t len = _argv[1].length < ctx->inputBufSize - 1
        ? _argv[1].length : ctx->inputBufSize - 1;
    memcpy(ctx->inputBuf, _argv[1].data, len);
    ctx->inputBuf[len] = '\0';

    float w = _argc > 2 ? (float)_argv[2].number : 0.0f;
    float h = _argc > 3 ? (float)_argv[3].number : 0.0f;

    const char* label = nullptr;
    KitsuneVariable* labelOwned = nullptr;
    if (_argv[0].type == KITSUNE_TSTRING) {
        label = (const char*)_argv[0].data;
    }
    else {
        labelOwned = KitsuneToString(&_argv[0]);
        label = labelOwned ? (const char*)labelOwned->data : "";
    }

    bool changed = ImGui::InputTextMultiline(label, ctx->inputBuf, ctx->inputBufSize,
        ImVec2(w, h));

    if (labelOwned) KitsuneVariableFree(labelOwned);

    KitsuneVariable r1 = {}; r1.type = KITSUNE_TBOOLEAN; r1.boolean = changed;
    KitsuneVariable r2 = {}; r2.type = KITSUNE_TSTRING;
    r2.data   = (unsigned char*)ctx->inputBuf;
    r2.length = strlen(ctx->inputBuf);
    setter(&r1);
    setter(&r2);
    return 2;
}

// ---------------------------------------------------------------------------
// Hand-written override: PlotLines
// ---------------------------------------------------------------------------

static int ImguiRenderer_PlotLines(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    const int _argc = IMGUI_ARGC;
    const KitsuneVariable* _argv = IMGUI_ARGV;
    if (_argc < 2)
        return 0;

    const char* label = nullptr;
    KitsuneVariable* labelOwned = nullptr;
    if (_argv[0].type == KITSUNE_TSTRING) {
        label = (const char*)_argv[0].data;
    }
    else {
        labelOwned = KitsuneToString(&_argv[0]);
        label = labelOwned ? (const char*)labelOwned->data : "";
    }

    // _argv[1] must be a table of floats
    int values_count = 0;
    float* values = nullptr;
    if (_argv[1].type == KITSUNE_TTABLE) {
        KitsuneVariable* lenVar = KitsuneGetLength(&_argv[1]);
        values_count = lenVar ? (int)lenVar->integer : 0;
        KitsuneVariableFree(lenVar);
        if (values_count > 0) {
            values = (float*)alloca(values_count * sizeof(float));
            for (int k = 0; k < values_count; k++) {
                KitsuneVariable ki = {}; ki.type = KITSUNE_TINTEGER; ki.integer = k + 1;
                KitsuneVariable* kv = KitsuneGetIndex(&_argv[1], &ki);
                values[k] = kv ? (kv->type == KITSUNE_TINTEGER ? (float)kv->integer : (float)kv->number) : 0.0f;
                KitsuneVariableFree(kv);
            }
        }
    }

    const char* overlay = _argc > 2 && _argv[2].type == KITSUNE_TSTRING ? (const char*)_argv[2].data : nullptr;
    ImGui::PlotLines(label, values, values_count, 0, overlay);

    if (labelOwned) KitsuneVariableFree(labelOwned);
    return 0;
}

// ---------------------------------------------------------------------------
// Hand-written override: Combo (string list variant)
// ---------------------------------------------------------------------------

static int ImguiRenderer_Combo(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    const int _argc = IMGUI_ARGC;
    const KitsuneVariable* _argv = IMGUI_ARGV;
    if (_argc < 3)
        return 0;

    const char* label = nullptr;
    KitsuneVariable* labelOwned = nullptr;
    if (_argv[0].type == KITSUNE_TSTRING) {
        label = (const char*)_argv[0].data;
    }
    else {
        labelOwned = KitsuneToString(&_argv[0]);
        label = labelOwned ? (const char*)labelOwned->data : "";
    }

    int currentItem = (int)_argv[1].integer;

    const KeyValuePairKitsuneVariableNode* node = _argv[2].table;
    int count = (int)_argv[2].length;

    const char** items = count > 0 ? (const char**)alloca(count * sizeof(const char*)) : nullptr;
    int idx = 0;
    while (node && idx < count) {
        items[idx++] = (node->value.type == KITSUNE_TSTRING)
            ? (const char*)node->value.data : "";
        node = node->next;
    }

    bool changed = ImGui::Combo(label, &currentItem, items, idx);

    if (labelOwned)
        KitsuneVariableFree(labelOwned);

    KitsuneVariable r1 = {}; r1.type = KITSUNE_TBOOLEAN; r1.boolean = changed;
    KitsuneVariable r2 = {}; r2.type = KITSUNE_TINTEGER;  r2.integer = currentItem;
    setter(&r1);
    setter(&r2);
    return 2;
}

// ---------------------------------------------------------------------------
// Hand-written override: ListBox (string list variant)
// ---------------------------------------------------------------------------

static int ImguiRenderer_ListBox(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* ud) {
    const int _argc = IMGUI_ARGC;
    const KitsuneVariable* _argv = IMGUI_ARGV;
    if (_argc < 3)
        return 0;

    const char* label = nullptr;
    KitsuneVariable* labelOwned = nullptr;
    if (_argv[0].type == KITSUNE_TSTRING) {
        label = (const char*)_argv[0].data;
    }
    else {
        labelOwned = KitsuneToString(&_argv[0]);
        label = labelOwned ? (const char*)labelOwned->data : "";
    }

    int currentItem = (int)_argv[1].integer;

    const KeyValuePairKitsuneVariableNode* node = _argv[2].table;
    int count = (int)_argv[2].length;

    const char** items = count > 0 ? (const char**)alloca(count * sizeof(const char*)) : nullptr;
    int idx = 0;
    while (node && idx < count) {
        items[idx++] = (node->value.type == KITSUNE_TSTRING)
            ? (const char*)node->value.data : "";
        node = node->next;
    }

    bool changed = ImGui::ListBox(label, &currentItem, items, idx);

    if (labelOwned) KitsuneVariableFree(labelOwned);

    KitsuneVariable r1 = {}; r1.type = KITSUNE_TBOOLEAN; r1.boolean = changed;
    KitsuneVariable r2 = {}; r2.type = KITSUNE_TINTEGER;  r2.integer = currentItem;
    setter(&r1);
    setter(&r2);
    return 2;
}

// ---------------------------------------------------------------------------
// Registration helpers
// ---------------------------------------------------------------------------

static void prepend_meta(KitsuneUserDataRegistration* reg,
    const char* name, kitsune_CFunction func) {
    NamedKitsuneFunction* node = (NamedKitsuneFunction*)malloc(sizeof(NamedKitsuneFunction));
    if (!node) return;
    node->name     = (char*)name;
    node->func     = func;
    node->userdata = nullptr;
    node->Next     = reg->MetaTableFunctions;
    reg->MetaTableFunctions = node;
}

static void prepend_fn(KitsuneUserDataRegistration* reg,
    const char* name, kitsune_CFunction func) {
    NamedKitsuneFunction* node = (NamedKitsuneFunction*)malloc(sizeof(NamedKitsuneFunction));
    if (!node) return;
    node->name     = (char*)name;
    node->func     = func;
    node->userdata = nullptr;
    node->Next     = reg->Functions;
    reg->Functions = node;
}

void add_imgui_meta_bindings(KitsuneUserDataRegistration* reg) {
    prepend_meta(reg, "__gc",       imgui_gc);
    prepend_meta(reg, "__tostring", imgui_tostring);

    prepend_fn(reg, "Text",               ImguiRenderer_Text);
    prepend_fn(reg, "TextColored",        ImguiRenderer_TextColored);
    prepend_fn(reg, "TextDisabled",       ImguiRenderer_TextDisabled);
    prepend_fn(reg, "TextWrapped",        ImguiRenderer_TextWrapped);
    prepend_fn(reg, "InputText",          ImguiRenderer_InputText);
    prepend_fn(reg, "InputTextMultiline", ImguiRenderer_InputTextMultiline);
    prepend_fn(reg, "PlotLines",          ImguiRenderer_PlotLines);
    prepend_fn(reg, "Combo",              ImguiRenderer_Combo);
    prepend_fn(reg, "ListBox",            ImguiRenderer_ListBox);
}

#endif // KITSUNE_IMGUI
