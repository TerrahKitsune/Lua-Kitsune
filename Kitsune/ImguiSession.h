#pragma once
#ifdef KITSUNE_IMGUI

#include "ImguiRenderer.h"
#include "Scheduler.h"

// Global session pointer. Set by Imgui.Run when a session is requested.
// Checked by Program.cpp after the startup script finishes.
// Set back to nullptr by __gc on teardown.
extern ImguiWindowContext* g_imguiCtx;

// Registers Imgui.Run, Imgui.GetEnums, Imgui.Console, Imgui.Schedule
// and the ImguiRenderer userdata type. Called alongside RegisterSessionFunctions().
void RegisterImguiFunctions();

// Runs the SDL2+OpenGL render loop using g_imguiCtx.
// scheduler is the program-level SchedulerState; ctx->scheduler is pointed at it
// so scheduled calls from the main script keep running inside the imgui loop.
// Blocks until the window is closed or renderFn returns false.
void RunImguiSession(SchedulerState* scheduler);

#endif // KITSUNE_IMGUI
