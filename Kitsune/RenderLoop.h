#pragma once
#ifdef KITSUNE_IMGUI

// Registers Imgui.Start and Imgui.Schedule.
void RegisterRenderLoopFunctions();

// Starts the SDL2/OpenGL/ImGui render loop. Blocks until the window closes.
// Called by RunImguiSession in the host after RegisterImguiFunctions().
void RunImguiSession();

#endif // KITSUNE_IMGUI
