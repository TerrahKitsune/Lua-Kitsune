#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

#include "SDLWindow.h"
#include "ImguiSession.h"
#include "KitsuneEngine.h"

#include <SDL.h>
#include <cstring>

// ---------------------------------------------------------------------------
// SDL.GetWindowWidth / GetWindowHeight
// ---------------------------------------------------------------------------

static int SDLGetWindowWidth(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx || !g_imguiCtx->window)
		return 0;
	int w;
	int h;
	SDL_GetWindowSize(g_imguiCtx->window, &w, &h);
	KitsuneVariable r = {};
	r.type = KITSUNE_TINTEGER;
	r.integer = w;
	setter(&r);
	return 1;
}

static int SDLGetWindowHeight(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx || !g_imguiCtx->window)
		return 0;
	int w;
	int h;
	SDL_GetWindowSize(g_imguiCtx->window, &w, &h);
	KitsuneVariable r = {};
	r.type = KITSUNE_TINTEGER;
	r.integer = h;
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// SDL.GetWindowX / GetWindowY
// ---------------------------------------------------------------------------

static int SDLGetWindowX(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx || !g_imguiCtx->window)
		return 0;
	int x;
	int y;
	SDL_GetWindowPosition(g_imguiCtx->window, &x, &y);
	KitsuneVariable r = {};
	r.type = KITSUNE_TINTEGER;
	r.integer = x;
	setter(&r);
	return 1;
}

static int SDLGetWindowY(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx || !g_imguiCtx->window)
		return 0;
	int x;
	int y;
	SDL_GetWindowPosition(g_imguiCtx->window, &x, &y);
	KitsuneVariable r = {};
	r.type = KITSUNE_TINTEGER;
	r.integer = y;
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// SDL.SetWindowSize / SetWindowPosition / SetWindowTitle
// ---------------------------------------------------------------------------

static int SDLSetWindowSize(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx || !g_imguiCtx->window || argc < 2)
		return 0;
	SDL_SetWindowSize(g_imguiCtx->window,
		(int)KitsuneAsInt(&argv[0], 0),
		(int)KitsuneAsInt(&argv[1], 0));
	return 0;
}

static int SDLSetWindowPosition(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx || !g_imguiCtx->window || argc < 2)
		return 0;
	SDL_SetWindowPosition(g_imguiCtx->window,
		(int)KitsuneAsInt(&argv[0], 0),
		(int)KitsuneAsInt(&argv[1], 0));
	return 0;
}

static int SDLSetWindowTitle(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx || !g_imguiCtx->window || argc < 1 || argv[0].type != KITSUNE_TSTRING)
		return 0;
	SDL_SetWindowTitle(g_imguiCtx->window, (const char*)argv[0].data);
	return 0;
}

// ---------------------------------------------------------------------------
// SDL.IsMinimized / IsFocused / SetFullscreen
// ---------------------------------------------------------------------------

static int SDLIsMinimized(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx || !g_imguiCtx->window)
		return 0;
	Uint32 flags = SDL_GetWindowFlags(g_imguiCtx->window);
	KitsuneVariable r = {};
	r.type = KITSUNE_TBOOLEAN;
	r.boolean = (flags & SDL_WINDOW_MINIMIZED) != 0;
	setter(&r);
	return 1;
}

static int SDLIsFocused(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx || !g_imguiCtx->window)
		return 0;
	Uint32 flags = SDL_GetWindowFlags(g_imguiCtx->window);
	KitsuneVariable r = {};
	r.type = KITSUNE_TBOOLEAN;
	r.boolean = (flags & SDL_WINDOW_INPUT_FOCUS) != 0;
	setter(&r);
	return 1;
}

static int SDLSetFullscreen(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx || !g_imguiCtx->window)
		return 0;
	bool fullscreen = argc > 0 && KitsuneAsBool(&argv[0]);
	SDL_SetWindowFullscreen(g_imguiCtx->window,
		fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
	return 0;
}

// ---------------------------------------------------------------------------
// SDL.GetMonitor
// ---------------------------------------------------------------------------

static int SDLGetMonitor(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx || !g_imguiCtx->window)
		return 0;
	int idx = SDL_GetWindowDisplayIndex(g_imguiCtx->window);
	if (idx < 0)
		return 0;
	SDL_Rect        bounds = {};
	SDL_DisplayMode mode = {};
	SDL_GetDisplayBounds(idx, &bounds);
	SDL_GetCurrentDisplayMode(idx, &mode);
	const char* name = SDL_GetDisplayName(idx);

	KitsuneVariable vi = {};
	vi.type = KITSUNE_TINTEGER;
	vi.integer = idx;
	setter(&vi);

	KitsuneVariable vs = {};
	vs.type = KITSUNE_TSTRING;
	vs.data = (unsigned char*)(name ? name : "");
	vs.length = name ? strlen(name) : 0;
	setter(&vs);

	vi.integer = bounds.x;          setter(&vi);
	vi.integer = bounds.y;          setter(&vi);
	vi.integer = bounds.w;          setter(&vi);
	vi.integer = bounds.h;          setter(&vi);
	vi.integer = mode.refresh_rate; setter(&vi);
	return 7;
}

// ---------------------------------------------------------------------------
// RegisterSDLWindowFunctions
// ---------------------------------------------------------------------------

void RegisterSDLWindowFunctions() {
	KitsuneRegisterFunction("SDL.Window.GetWindowWidth", SDLGetWindowWidth, nullptr);
	KitsuneRegisterFunction("SDL.Window.GetWindowHeight", SDLGetWindowHeight, nullptr);
	KitsuneRegisterFunction("SDL.Window.GetWindowX", SDLGetWindowX, nullptr);
	KitsuneRegisterFunction("SDL.Window.GetWindowY", SDLGetWindowY, nullptr);
	KitsuneRegisterFunction("SDL.Window.SetWindowSize", SDLSetWindowSize, nullptr);
	KitsuneRegisterFunction("SDL.Window.SetWindowPosition", SDLSetWindowPosition, nullptr);
	KitsuneRegisterFunction("SDL.Window.SetWindowTitle", SDLSetWindowTitle, nullptr);
	KitsuneRegisterFunction("SDL.Window.IsMinimized", SDLIsMinimized, nullptr);
	KitsuneRegisterFunction("SDL.Window.IsFocused", SDLIsFocused, nullptr);
	KitsuneRegisterFunction("SDL.Window.SetFullscreen", SDLSetFullscreen, nullptr);
	KitsuneRegisterFunction("SDL.Window.GetMonitor", SDLGetMonitor, nullptr);
}

#endif // KITSUNE_IMGUI
