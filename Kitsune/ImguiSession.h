#pragma once
#ifdef KITSUNE_IMGUI

#include "ImguiRenderer.h"

// Global session pointer. Set by Imgui.Run when a session is requested.
// Checked by Program.cpp after the startup script finishes.
// Set back to nullptr by __gc on teardown.
extern ImguiWindowContext* g_imguiCtx;

// Registers Imgui.Run, Imgui.GetEnums, Imgui.Console, Imgui.Schedule
// and the ImguiRenderer userdata type. Called alongside RegisterSessionFunctions().
void RegisterImguiFunctions();

// Runs the SDL2+OpenGL render loop using g_imguiCtx.
// Blocks until the window is closed or renderFn returns false.
// g_imguiCtx is left non-null on exit; __gc frees it during KitsuneCleanup.
void RunImguiSession();

#endif // KITSUNE_IMGUI
