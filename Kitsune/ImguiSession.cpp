#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

// Prevent SDL from redefining main() as SDL_main on Windows.
#define SDL_MAIN_HANDLED

#include "ImguiSession.h"
#include "ImguiRenderer.h"
#include "ImguiEnums.h"

#include "Imgui/imgui.h"
#include "Imgui/imgui_internal.h"
#include "Imgui/imgui_impl_sdl2.h"
#include "Imgui/imgui_impl_opengl3.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef _WIN32
#include <malloc.h>
#else
#include <alloca.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

// ---------------------------------------------------------------------------
// Global session pointer
// ---------------------------------------------------------------------------

ImguiWindowContext* g_imguiCtx = nullptr;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static void drain_scheduled_calls(ImguiWindowContext* ctx) {
    while (ctx->scheduledHead) {
        ImguiScheduledCall* call = ctx->scheduledHead;
        ctx->scheduledHead = call->next;

        KitsuneVariable* values = call->argc > 0
            ? (KitsuneVariable*)alloca(call->argc * sizeof(KitsuneVariable)) : nullptr;
        for (int i = 0; i < call->argc; i++)
            values[i] = *call->argv[i];

        KitsuneExecuteVariableAsync(call->fn, call->argc, values, true);

        for (int i = 0; i < call->argc; i++)
            KitsuneVariableFree(call->argv[i]);
        KitsuneVariableFree(call->fn);
        free(call->argv);
        free(call);
    }
}

// ---------------------------------------------------------------------------
// Imgui.Run
// ---------------------------------------------------------------------------

static int ImguiStart(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {

    if (g_imguiCtx) {
        KitsuneVariable err = {};
        const char* msg = "Imgui.Start already called";
        err.type = KITSUNE_TERROR;
        err.data = (unsigned char*)msg;
        err.length = strlen(msg);
        setter(&err);
        return 1;
    }

    // Validate required args: title(0), width(1), height(2), renderFn(3)
    if (argc < 4
        || argv[0].type != KITSUNE_TSTRING
        || argv[3].type != KITSUNE_TFUNCTION) {
        KitsuneVariable err = {};
        const char* msg = "Imgui.Start(title, width, height, renderFn, [context], [onError])";
        err.type = KITSUNE_TERROR;
        err.data = (unsigned char*)msg;
        err.length = strlen(msg);
        setter(&err);
        return 1;
    }

    ImguiWindowContext* ctx = (ImguiWindowContext*)calloc(1, sizeof(ImguiWindowContext));
    if (!ctx)
        return 0;

    // Title
    ctx->title = (char*)malloc(argv[0].length + 1);
    if (!ctx->title) {
        free(ctx);
        return 0;
    }
    memcpy(ctx->title, argv[0].data, argv[0].length);
    ctx->title[argv[0].length] = '\0';

    ctx->width  = argc > 1 ? (int)(argv[1].type == KITSUNE_TNUMBER ? (int)argv[1].number : (int)argv[1].integer) : 800;
    ctx->height = argc > 2 ? (int)(argv[2].type == KITSUNE_TNUMBER ? (int)argv[2].number : (int)argv[2].integer) : 600;

    // Anchor renderFn
    ctx->renderFn = KitsuneAnchorVariable(&argv[3]);

    // Anchor context — must be a live KITSUNE_TTABLE, not a snapshot.
    // If not provided, leave ctx->context null and pass nil each frame.
    if (argc > 4 && argv[4].type == KITSUNE_TTABLE) {
        ctx->context = KitsuneAnchorVariable(&argv[4]);
    }

    // Anchor onError if provided
    if (argc > 5 && argv[5].type == KITSUNE_TFUNCTION)
        ctx->onError = KitsuneAnchorVariable(&argv[5]);

    // Initial inputBuf
    ctx->inputBufSize = 256;
    ctx->inputBuf = (char*)malloc(ctx->inputBufSize);
    if (ctx->inputBuf)
        ctx->inputBuf[0] = '\0';

    g_imguiCtx = ctx;

    // Prevent Imgui.Start from being called a second time
    KitsuneVariable nilVar = {};
    nilVar.type = KITSUNE_TNIL;
    KitsuneSetVariable("Imgui.Start", &nilVar);

    return 0;
}

// ---------------------------------------------------------------------------
// Imgui.Console
// ---------------------------------------------------------------------------

static int ImguiConsole(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
#ifdef _WIN32
    HWND hwnd = GetConsoleWindow();
    if (hwnd) {
        bool visible = argc > 0 && argv[0].boolean;
        ShowWindow(hwnd, visible ? SW_SHOW : SW_HIDE);
    }
#endif
    // No-op on Linux
    return 0;
}

// ---------------------------------------------------------------------------
// Imgui.Schedule
// ---------------------------------------------------------------------------

static int ImguiSchedule(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    if (!g_imguiCtx || argc < 1 || argv[0].type != KITSUNE_TFUNCTION)
        return 0;

    ImguiScheduledCall* call = (ImguiScheduledCall*)calloc(1, sizeof(ImguiScheduledCall));
    if (!call)
        return 0;

    call->fn   = KitsuneAnchorVariable(&argv[0]);
    call->argc = argc - 1;

    if (call->argc > 0) {
        call->argv = (KitsuneVariable**)malloc(call->argc * sizeof(KitsuneVariable*));
        if (!call->argv) {
            KitsuneVariableFree(call->fn);
            free(call);
            return 0;
        }
        for (int i = 0; i < call->argc; i++)
            call->argv[i] = KitsuneAnchorVariable(&argv[i + 1]);
    }

    call->next = g_imguiCtx->scheduledHead;
    g_imguiCtx->scheduledHead = call;
    return 0;
}

// ---------------------------------------------------------------------------
// Window utility functions
// ---------------------------------------------------------------------------

static int ImguiGetWindowWidth(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    if (!g_imguiCtx || !g_imguiCtx->window)
        return 0;
    int w, h;
    SDL_GetWindowSize(g_imguiCtx->window, &w, &h);
    KitsuneVariable r = {}; r.type = KITSUNE_TINTEGER; r.integer = w;
    setter(&r);
    return 1;
}

static int ImguiGetWindowHeight(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    if (!g_imguiCtx || !g_imguiCtx->window)
        return 0;
    int w, h;
    SDL_GetWindowSize(g_imguiCtx->window, &w, &h);
    KitsuneVariable r = {}; r.type = KITSUNE_TINTEGER; r.integer = h;
    setter(&r);
    return 1;
}

static int ImguiGetWindowX(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    if (!g_imguiCtx || !g_imguiCtx->window)
        return 0;
    int x, y;
    SDL_GetWindowPosition(g_imguiCtx->window, &x, &y);
    KitsuneVariable r = {}; r.type = KITSUNE_TINTEGER; r.integer = x;
    setter(&r);
    return 1;
}

static int ImguiGetWindowY(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    if (!g_imguiCtx || !g_imguiCtx->window)
        return 0;
    int x, y;
    SDL_GetWindowPosition(g_imguiCtx->window, &x, &y);
    KitsuneVariable r = {}; r.type = KITSUNE_TINTEGER; r.integer = y;
    setter(&r);
    return 1;
}

static int ImguiSetWindowSize(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    if (!g_imguiCtx || !g_imguiCtx->window || argc < 2)
        return 0;
    SDL_SetWindowSize(g_imguiCtx->window, (int)argv[0].integer, (int)argv[1].integer);
    return 0;
}

static int ImguiSetWindowPosition(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    if (!g_imguiCtx || !g_imguiCtx->window || argc < 2)
        return 0;
    SDL_SetWindowPosition(g_imguiCtx->window, (int)argv[0].integer, (int)argv[1].integer);
    return 0;
}

static int ImguiSetWindowTitle(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    if (!g_imguiCtx || !g_imguiCtx->window || argc < 1 || argv[0].type != KITSUNE_TSTRING)
        return 0;
    // SDL_SetWindowTitle copies the string internally — no need to keep argv[0].data alive.
    SDL_SetWindowTitle(g_imguiCtx->window, (const char*)argv[0].data);
    return 0;
}

static int ImguiIsMinimized(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    if (!g_imguiCtx || !g_imguiCtx->window)
        return 0;
    Uint32 flags = SDL_GetWindowFlags(g_imguiCtx->window);
    KitsuneVariable r = {}; r.type = KITSUNE_TBOOLEAN;
    r.boolean = (flags & SDL_WINDOW_MINIMIZED) != 0;
    setter(&r);
    return 1;
}

static int ImguiIsFocused(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    if (!g_imguiCtx || !g_imguiCtx->window)
        return 0;
    Uint32 flags = SDL_GetWindowFlags(g_imguiCtx->window);
    KitsuneVariable r = {}; r.type = KITSUNE_TBOOLEAN;
    r.boolean = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
    setter(&r);
    return 1;
}

static int ImguiSetFullscreen(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    if (!g_imguiCtx || !g_imguiCtx->window)
        return 0;
    bool fullscreen = argc > 0 && argv[0].boolean;
    SDL_SetWindowFullscreen(g_imguiCtx->window,
        fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    return 0;
}

static int ImguiGetMonitor(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    if (!g_imguiCtx || !g_imguiCtx->window)
        return 0;
    int idx = SDL_GetWindowDisplayIndex(g_imguiCtx->window);
    if (idx < 0)
        return 0;
    SDL_Rect bounds = {};
    SDL_GetDisplayBounds(idx, &bounds);
    SDL_DisplayMode mode = {};
    SDL_GetCurrentDisplayMode(idx, &mode);
    const char* name = SDL_GetDisplayName(idx);

    KitsuneVariable v = {};
    v.type = KITSUNE_TINTEGER;

    // Return: index, name, x, y, width, height, refreshRate
    v.integer = idx;                            setter(&v);
    v.type = KITSUNE_TSTRING;
    v.data = (unsigned char*)(name ? name : "");
    v.length = name ? strlen(name) : 0;         setter(&v);
    v.type = KITSUNE_TINTEGER;
    v.integer = bounds.x;                       setter(&v);
    v.integer = bounds.y;                       setter(&v);
    v.integer = bounds.w;                       setter(&v);
    v.integer = bounds.h;                       setter(&v);
    v.integer = mode.refresh_rate;              setter(&v);
    return 7;
}

// ---------------------------------------------------------------------------
// RunImguiSession
// ---------------------------------------------------------------------------

static void free_ctx(ImguiWindowContext* ctx) {
    if (!ctx)
        return;
    if (ctx->renderFn) { KitsuneVariableFree(ctx->renderFn); ctx->renderFn = nullptr; }
    if (ctx->context)  { KitsuneVariableFree(ctx->context);  ctx->context  = nullptr; }
    if (ctx->onError)  { KitsuneVariableFree(ctx->onError);  ctx->onError  = nullptr; }
    free(ctx->inputBuf);
    free(ctx->title);
    free(ctx);
    g_imguiCtx = nullptr;
}

void RunImguiSession() {
    ImguiWindowContext* ctx = g_imguiCtx;
    if (!ctx)
        return;

    // SDL2 + OpenGL init — do this before registering userdata so
    // early failures don't leave a registered type with no backing window.
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        drain_scheduled_calls(ctx);
        free_ctx(ctx);
        return;
    }

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    Uint32 windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
    ctx->window = SDL_CreateWindow(ctx->title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        ctx->width, ctx->height, windowFlags);
    if (!ctx->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        drain_scheduled_calls(ctx);
        free_ctx(ctx);
        return;
    }

    ctx->glContext = SDL_GL_CreateContext(ctx->window);
    if (!ctx->glContext) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(ctx->window);
        ctx->window = nullptr;
        SDL_Quit();
        drain_scheduled_calls(ctx);
        free_ctx(ctx);
        return;
    }
    SDL_GL_MakeCurrent(ctx->window, ctx->glContext);
    SDL_GL_SetSwapInterval(1); // vsync

    // Register ImGui userdata type now that we have a valid window and GL context
    add_imgui_meta_bindings(&ctx->reg);
    add_imgui_bindings(&ctx->reg);
    KitsuneRegisterUserdata("ImguiRenderer", &ctx->reg);

    // ImGui init
    IMGUI_CHECKVERSION();
    ImGuiContext* imCtx = ImGui::CreateContext();
    ctx->imguiContext = imCtx;
    ImGui::SetCurrentContext(imCtx);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplSDL2_InitForOpenGL(ctx->window, ctx->glContext);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Disable asserts on recoverable errors (missing End() etc.) so the error handler
    // can keep the loop alive instead of crashing. Errors are still logged.
    ImGui::GetIO().ConfigErrorRecoveryEnableAssert = false;

    // Render loop
    bool running = true;
    while (running) {
        // Process SDL events
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL2_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                running = false;
        }

        if (!running)
            break;

        // New frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();

        // Build renderer userdata pointing directly to ctx
        KitsuneUserData ud = {};
        ud.name     = (char*)"ImguiRenderer";
        ud.ref      = 0;
        ud.userdata = ctx;

        KitsuneVariable args[2] = {};
        args[0].type     = KITSUNE_TUSERDATA;
        args[0].length   = 0;
        args[0].userdata = &ud;
        if (ctx->context) {
            args[1] = *ctx->context;
        } else {
            args[1].type = KITSUNE_TNIL;
        }

        KitsuneVariable* result = KitsuneExecuteVariable(ctx->renderFn, 2, args);

        if (result && result->type == KITSUNE_TERROR) {

            if (ctx->onError) {
                KitsuneVariable* keepRunning = KitsuneExecuteVariable(ctx->onError, 1, result);
                fprintf(stderr, "Imgui render error: %.*s\n",
                    (int)result->length, (char*)result->data);
                KitsuneVariableFree(result);
                if (!keepRunning || keepRunning->type == KITSUNE_TERROR) {
                    fprintf(stderr, "Imgui onError handler failed: %.*s\n",
                        (int)(keepRunning ? keepRunning->length : 0),
                        keepRunning ? (char*)keepRunning->data : "");
                    KitsuneVariableFree(keepRunning);
                    running = false;
                }
                else {
                    bool cont = keepRunning->type == KITSUNE_TBOOLEAN && keepRunning->boolean;
                    if (!cont)
                        fprintf(stderr, "Imgui render error (handled): %.*s\n",
                            (int)result->length, (char*)result->data);
                    KitsuneVariableFree(keepRunning);
                    if (!cont)
                        running = false;
                }
            }
            else {
                fprintf(stderr, "Imgui render error: %.*s\n",
                    (int)result->length, (char*)result->data);
                KitsuneVariableFree(result);
                running = false;
            }
        }
        else if (result && result->type == KITSUNE_TBOOLEAN && !result->boolean) {
            KitsuneVariableFree(result);
            running = false;
        }
        else {
            KitsuneVariableFree(result);
        }

        // Drain any scheduled calls queued this frame
        drain_scheduled_calls(ctx);

        // Render
        ImGui::EndFrame();
        ImGui::Render();
        int displayW, displayH;
        SDL_GL_GetDrawableSize(ctx->window, &displayW, &displayH);
        glViewport(0, 0, displayW, displayH);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        SDL_GL_SwapWindow(ctx->window);
    }

    // Drain any remaining scheduled calls without executing them
    drain_scheduled_calls(ctx);
}

// ---------------------------------------------------------------------------
// RegisterImguiFunctions
// ---------------------------------------------------------------------------

void RegisterImguiFunctions() {
    KitsuneRegisterFunction("Imgui.Start",           ImguiStart,           nullptr);
    KitsuneRegisterFunction("Imgui.Console",         ImguiConsole,         nullptr);
    KitsuneRegisterFunction("Imgui.Schedule",        ImguiSchedule,        nullptr);
    KitsuneRegisterFunction("Imgui.GetWindowWidth",  ImguiGetWindowWidth,  nullptr);
    KitsuneRegisterFunction("Imgui.GetWindowHeight", ImguiGetWindowHeight, nullptr);
    KitsuneRegisterFunction("Imgui.GetWindowX",      ImguiGetWindowX,      nullptr);
    KitsuneRegisterFunction("Imgui.GetWindowY",      ImguiGetWindowY,      nullptr);
    KitsuneRegisterFunction("Imgui.SetWindowSize",   ImguiSetWindowSize,   nullptr);
    KitsuneRegisterFunction("Imgui.SetWindowPosition", ImguiSetWindowPosition, nullptr);
    KitsuneRegisterFunction("Imgui.SetWindowTitle",  ImguiSetWindowTitle,  nullptr);
    KitsuneRegisterFunction("Imgui.IsMinimized",     ImguiIsMinimized,     nullptr);
    KitsuneRegisterFunction("Imgui.IsFocused",       ImguiIsFocused,       nullptr);
    KitsuneRegisterFunction("Imgui.SetFullscreen",   ImguiSetFullscreen,   nullptr);
    KitsuneRegisterFunction("Imgui.GetMonitor",      ImguiGetMonitor,      nullptr);
    register_imgui_enums();
}

#endif // KITSUNE_IMGUI
