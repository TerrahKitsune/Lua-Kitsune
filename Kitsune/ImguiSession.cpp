#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

// Prevent SDL from redefining main() as SDL_main on Windows.
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

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

        if (call->argc > 0) {
            KitsuneVariable* values = (KitsuneVariable*)malloc(call->argc * sizeof(KitsuneVariable));
            if (values) {
                for (int i = 0; i < call->argc; i++)
                    values[i] = *call->argv[i];
                KitsuneExecuteVariableAsync(call->fn, call->argc, values, true);
                free(values);
            }
        } else {
            KitsuneExecuteVariableAsync(call->fn, 0, nullptr, true);
        }

        for (int i = 0; i < call->argc; i++)
            KitsuneVariableFree(call->argv[i]);
        KitsuneVariableFree(call->fn);
        free(call->argv);
        free(call);
    }
}

// Free remaining scheduled calls without dispatching them.
// Used after the render loop exits so callbacks that captured the renderer
// do not run against a window/GL context that is being torn down.
static void free_scheduled_calls(ImguiWindowContext* ctx) {
    while (ctx->scheduledHead) {
        ImguiScheduledCall* call = ctx->scheduledHead;
        ctx->scheduledHead = call->next;
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
// Imgui.Console / Imgui.OwnsConsole
// ---------------------------------------------------------------------------

// Cached at registration time: true if this process was the sole owner of
// its console when it started (launched standalone or from VS), false if a
// parent shell (cmd, PowerShell, Windows Terminal) was also attached.
// Stays true even after FreeConsole() so the toggle button remains usable.
static bool g_ownsConsole = false;

static int ImguiOwnsConsole(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    KitsuneVariable r = {}; r.type = KITSUNE_TBOOLEAN; r.boolean = g_ownsConsole;
    setter(&r);
    return 1;
}

static int ImguiConsole(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
#ifdef _WIN32
    HWND hwnd = GetConsoleWindow();
    if (hwnd) {
        bool show = argc > 0 && argv[0].type == KITSUNE_TBOOLEAN && argv[0].boolean;
        ShowWindow(hwnd, show ? SW_SHOWNOACTIVATE : SW_MINIMIZE);
        bool isShown = IsWindowVisible(hwnd) && !IsIconic(hwnd);
        KitsuneVariable r = {}; r.type = KITSUNE_TBOOLEAN; r.boolean = isShown;
        setter(&r);
        return 1;
    }
#endif
    return 0;
}

// Permanently destroys the console window. Only valid when this process owns
// its console (g_ownsConsole == true).
static int ImguiDestroyConsole(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    bool ok = false;
#ifdef _WIN32
    if (g_ownsConsole && GetConsoleWindow())
        ok = FreeConsole() != 0;
#endif
    KitsuneVariable r = {}; r.type = KITSUNE_TBOOLEAN; r.boolean = ok;
    setter(&r);
    return 1;
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
    free(ctx->inputBuf);  ctx->inputBuf  = nullptr; ctx->inputBufSize = 0;
    free(ctx->title);     ctx->title     = nullptr;
    // These early-exit paths are reached before the renderer userdata is ever
    // anchored, so imgui_gc will never fire. Free ctx itself here.
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

    // lua_registerkitsuneuserdata copies fn->func and fn->userdata as light-userdata
    // upvalues into Lua closures — the NamedKitsuneFunction nodes are never touched by
    // Lua again after this point. Free them now rather than carrying them until imgui_gc.
    auto free_reg_nodes = [](KitsuneNamedFunction*& head) {
        KitsuneNamedFunction* n = head;
        while (n) { KitsuneNamedFunction* next = n->Next; free(n); n = next; }
        head = nullptr;
    };
    free_reg_nodes(ctx->reg.Functions);
    free_reg_nodes(ctx->reg.MetaTableFunctions);

    // ImGui init
    IMGUI_CHECKVERSION();
    ImGuiContext* imCtx = ImGui::CreateContext();
    ctx->imguiContext = imCtx;
    ImGui::SetCurrentContext(imCtx);
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplSDL2_InitForOpenGL(ctx->window, ctx->glContext);
    ImGui_ImplOpenGL3_Init("#version 130");

    // Disable asserts on recoverable errors (missing End() etc.) so the Lua error handler
    // can keep the loop alive instead of crashing. Errors are still logged.
    ImGui::GetIO().ConfigErrorRecoveryEnableAssert   = false;
    ImGui::GetIO().ConfigErrorRecoveryEnableDebugLog = false;

    // Create the renderer userdata once and anchor it in the Lua registry so
    // imgui_gc fires exactly once (when KitsuneCleanup runs) and not once per frame.
    KitsuneUserData rendererUD = {};
    rendererUD.name     = (char*)"ImguiRenderer";
    rendererUD.ref      = 0;
    rendererUD.userdata = ctx;
    KitsuneVariable rendererVar = {};
    rendererVar.type     = KITSUNE_TUSERDATA;
    rendererVar.length   = strlen(rendererUD.name);
    rendererVar.userdata = &rendererUD;
    KitsuneVariable* anchoredRenderer = KitsuneAnchorVariable(&rendererVar);

    // Render loop
    bool running = (anchoredRenderer != nullptr);
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

        // Pass the anchored renderer userdata and context to the render function
        KitsuneVariable args[2] = {};
        args[0] = *anchoredRenderer;
        if (ctx->context) {
            args[1] = *ctx->context;
        } else {
            args[1].type = KITSUNE_TNIL;
        }

        KitsuneVariable* result = KitsuneExecuteVariable(ctx->renderFn, 2, args);

        if (result && result->type == KITSUNE_TERROR) {

            if (ctx->onError) {
                KitsuneVariable* keepRunning = KitsuneExecuteVariable(ctx->onError, 1, result);
                if (!keepRunning || keepRunning->type == KITSUNE_TERROR) {
                    fprintf(stderr, "Imgui render error: %.*s\n",
                        (int)result->length, (char*)result->data);
                    fprintf(stderr, "Imgui onError handler failed: %.*s\n",
                        (int)(keepRunning ? keepRunning->length : 0),
                        keepRunning ? (char*)keepRunning->data : "");
                    KitsuneVariableFree(result);
                    KitsuneVariableFree(keepRunning);
                    running = false;
                }
                else {
                    bool cont = keepRunning->type == KITSUNE_TBOOLEAN && keepRunning->boolean;
                    if (!cont)
                        fprintf(stderr, "Imgui render error: %.*s\n",
                            (int)result->length, (char*)result->data);
                    KitsuneVariableFree(result);
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

    // Free any remaining scheduled calls without dispatching them — the render
    // loop has exited so any callbacks that captured the renderer would run
    // against a window/GL context that is about to be torn down.
    free_scheduled_calls(ctx);

    // Release the anchored renderer — this triggers imgui_gc exactly once via Lua GC.
    if (anchoredRenderer) {
        KitsuneVariableFree(anchoredRenderer);
    } else {
        // Anchoring failed: imgui_gc will never fire, so clean up manually.
        if (ctx->imguiContext) {
            ImGui::SetCurrentContext((ImGuiContext*)ctx->imguiContext);
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplSDL2_Shutdown();
            ImGui::DestroyContext((ImGuiContext*)ctx->imguiContext);
            ctx->imguiContext = nullptr;
        }
        if (ctx->glContext) { SDL_GL_DeleteContext(ctx->glContext); ctx->glContext = nullptr; }
        if (ctx->window)    { SDL_DestroyWindow(ctx->window);       ctx->window    = nullptr; }
        SDL_Quit();
        free_ctx(ctx);
    }
}

// ---------------------------------------------------------------------------
// RegisterImguiFunctions
// ---------------------------------------------------------------------------

void RegisterImguiFunctions() {
#ifdef _WIN32
    DWORD pids[2];
    g_ownsConsole = (GetConsoleProcessList(pids, 2) == 1);
#endif
    KitsuneRegisterFunction("Imgui.Start",           ImguiStart,           nullptr);
    KitsuneRegisterFunction("Imgui.Schedule",        ImguiSchedule,        nullptr);

    // SDL.* — window and display functions (SDL owns these)
    KitsuneRegisterFunction("SDL.GetWindowWidth",    ImguiGetWindowWidth,  nullptr);
    KitsuneRegisterFunction("SDL.GetWindowHeight",   ImguiGetWindowHeight, nullptr);
    KitsuneRegisterFunction("SDL.GetWindowX",        ImguiGetWindowX,      nullptr);
    KitsuneRegisterFunction("SDL.GetWindowY",        ImguiGetWindowY,      nullptr);
    KitsuneRegisterFunction("SDL.SetWindowSize",     ImguiSetWindowSize,   nullptr);
    KitsuneRegisterFunction("SDL.SetWindowPosition", ImguiSetWindowPosition, nullptr);
    KitsuneRegisterFunction("SDL.SetWindowTitle",    ImguiSetWindowTitle,  nullptr);
    KitsuneRegisterFunction("SDL.IsMinimized",       ImguiIsMinimized,     nullptr);
    KitsuneRegisterFunction("SDL.IsFocused",         ImguiIsFocused,       nullptr);
    KitsuneRegisterFunction("SDL.SetFullscreen",     ImguiSetFullscreen,   nullptr);
    KitsuneRegisterFunction("SDL.GetMonitor",        ImguiGetMonitor,      nullptr);

    // Win32.* — console window functions (Win32 owns these; impls are no-op on Linux via #ifdef _WIN32)
    KitsuneRegisterFunction("Win32.Console",         ImguiConsole,         nullptr);
    KitsuneRegisterFunction("Win32.OwnsConsole",     ImguiOwnsConsole,     nullptr);
    KitsuneRegisterFunction("Win32.DestroyConsole",  ImguiDestroyConsole,  nullptr);
    register_imgui_enums();
}

#endif // KITSUNE_IMGUI
