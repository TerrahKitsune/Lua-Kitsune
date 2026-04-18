#pragma once
#ifdef KITSUNE_IMGUI

void RegisterSDLInputFunctions();

// Called once per frame by the render loop to advance delta time and FPS.
// Must be called before any SDL.GetFrameTime() Lua calls this frame.
void UpdateFrameTiming();

#endif // KITSUNE_IMGUI