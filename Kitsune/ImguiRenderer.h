#pragma once
#ifdef KITSUNE_IMGUI

#include "KitsuneEngine.h"
#include <SDL.h>
#include <SDL_opengl.h>

struct ImguiScheduledCall {
    KitsuneVariable*    fn;
    int                 argc;
    KitsuneVariable**   argv;   // heap array of anchored KitsuneVariable* pointers, length = argc
    ImguiScheduledCall* next;
};

struct ImguiWindowContext {
    SDL_Window*                window;
    SDL_GLContext               glContext;
    void*                       imguiContext;  // ImGuiContext* — typed as void* to avoid pulling imgui.h into all headers
    char*                       title;         // heap copy of window title
    int                         width;
    int                         height;
    char*                       inputBuf;      // heap-allocated; reused by every InputText call
    size_t                      inputBufSize;  // current allocated capacity; grown on demand, never shrunk
    KitsuneVariable*            renderFn;
    KitsuneVariable*            context;
    KitsuneVariable*            onError;
    ImguiScheduledCall*         scheduledHead; // linked list of pending scheduled calls
    KitsuneUserDataRegistration reg;           // renderer userdata registration; nodes freed on teardown
};

// Populates reg->Functions with heap-allocated KitsuneNamedFunction nodes for
// all auto-generated ImGui bindings. Called once in RunImguiSession before the
// render loop starts. After KitsuneRegisterUserdata returns the nodes are kept
// alive in ctx->reg and freed in __gc.
void add_imgui_bindings(KitsuneUserDataRegistration* reg);

// Adds the __gc and __tostring metamethods and hand-written override functions
// (InputText, PlotLines, Combo, ListBox, etc.) into reg.
void add_imgui_meta_bindings(KitsuneUserDataRegistration* reg);

#endif // KITSUNE_IMGUI
