#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif

#include "ImguiSession.h"
#include "RenderLoop.h"
#include "SDLWindow.h"
#include "SDLInput.h"
#include "Win32Console.h"
#include "ImguiEnums.h"
#include "OpenGL.h"

// ---------------------------------------------------------------------------
// Global session pointer — shared across all Imgui* / SDL* / OpenGL* files
// ---------------------------------------------------------------------------

ImguiWindowContext* g_imguiCtx = nullptr;

// ---------------------------------------------------------------------------
// RegisterImguiFunctions
// ---------------------------------------------------------------------------

void RegisterImguiFunctions() {
	RegisterRenderLoopFunctions();
	RegisterSDLWindowFunctions();
	RegisterSDLInputFunctions();
	RegisterWin32ConsoleFunctions();
	RegisterOpenGLFunctions();
	register_imgui_enums();
}

#endif // KITSUNE_IMGUI
