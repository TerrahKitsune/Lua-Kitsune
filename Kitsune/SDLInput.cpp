#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

#include "SDLInput.h"
#include "ImguiSession.h"
#include "KitsuneEngine.h"

#include <SDL.h>

// ---------------------------------------------------------------------------
// SDL.GetKeyState(scancode) -> bool
// ---------------------------------------------------------------------------

static int SDLGetKeyState(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TBOOLEAN;
	r.boolean = false;
	if (argc < 1) {
		setter(&r);
		return 1;
	}
	int scancode = (int)KitsuneAsInt(&argv[0], -1);
	if (scancode < 0 || scancode >= SDL_NUM_SCANCODES) {
		setter(&r);
		return 1;
	}
	const Uint8* state = SDL_GetKeyboardState(nullptr);
	r.boolean = state[scancode] != 0;
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// SDL.GetModState() -> table
// ---------------------------------------------------------------------------

static int SDLGetModState(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	SDL_Keymod mods = SDL_GetModState();

	KitsuneVariable tableVar = {};
	tableVar.type = KITSUNE_TTABLECONTENTS;
	tableVar.table = nullptr;
	KitsuneVariable* tbl = KitsuneAnchorVariable(&tableVar);
	if (!tbl)
		return 0;

	struct { const char* name; size_t len; bool val; } entries[] = {
		{ "shift",    5, (mods & KMOD_SHIFT) != 0 },
		{ "ctrl",     4, (mods & KMOD_CTRL) != 0 },
		{ "alt",      3, (mods & KMOD_ALT) != 0 },
		{ "gui",      3, (mods & KMOD_GUI) != 0 },
		{ "capslock", 8, (mods & KMOD_CAPS) != 0 },
		{ "numlock",  7, (mods & KMOD_NUM) != 0 },
	};
	for (int i = 0; i < (int)(sizeof(entries) / sizeof(entries[0])); i++) {
		KitsuneVariable k = {};
		k.type = KITSUNE_TSTRING;
		k.data = (unsigned char*)entries[i].name;
		k.length = entries[i].len;
		KitsuneVariable v = {};
		v.type = KITSUNE_TBOOLEAN;
		v.boolean = entries[i].val;
		KitsuneSetIndex(tbl, &k, &v);
	}
	setter(tbl);
	KitsuneVariableFree(tbl);
	return 1;
}

// ---------------------------------------------------------------------------
// SDL.GetMouseState() -> x, y, buttons
// ---------------------------------------------------------------------------

static int SDLGetMouseState(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	int    x;
	int    y;
	Uint32 buttons = SDL_GetMouseState(&x, &y);
	KitsuneVariable vx = {};
	vx.type = KITSUNE_TINTEGER;
	vx.integer = x;
	KitsuneVariable vy = {};
	vy.type = KITSUNE_TINTEGER;
	vy.integer = y;
	KitsuneVariable vb = {};
	vb.type = KITSUNE_TINTEGER;
	vb.integer = (long long)buttons;
	setter(&vx);
	setter(&vy);
	setter(&vb);
	return 3;
}

// ---------------------------------------------------------------------------
// SDL.GetRelativeMouseState() -> dx, dy, buttons
// ---------------------------------------------------------------------------

static int SDLGetRelativeMouseState(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	int    dx;
	int    dy;
	Uint32 buttons = SDL_GetRelativeMouseState(&dx, &dy);
	KitsuneVariable vdx = {};
	vdx.type = KITSUNE_TINTEGER;
	vdx.integer = dx;
	KitsuneVariable vdy = {};
	vdy.type = KITSUNE_TINTEGER;
	vdy.integer = dy;
	KitsuneVariable vb = {};
	vb.type = KITSUNE_TINTEGER;
	vb.integer = (long long)buttons;
	setter(&vdx);
	setter(&vdy);
	setter(&vb);
	return 3;
}

// ---------------------------------------------------------------------------
// SDL.SetRelativeMouseMode(bool)
// ---------------------------------------------------------------------------

static int SDLSetRelativeMouseMode(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	bool enable = argc > 0 && KitsuneAsBool(&argv[0]);
	SDL_SetRelativeMouseMode(enable ? SDL_TRUE : SDL_FALSE);
	return 0;
}

// ---------------------------------------------------------------------------
// SDL.WarpMouse(x, y)
// ---------------------------------------------------------------------------

static int SDLWarpMouse(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	if (!g_imguiCtx || !g_imguiCtx->window || argc < 2)
		return 0;
	SDL_WarpMouseInWindow(g_imguiCtx->window,
		(int)KitsuneAsInt(&argv[0], 0),
		(int)KitsuneAsInt(&argv[1], 0));
	return 0;
}

// ---------------------------------------------------------------------------
// SDL.GetNumJoysticks() -> integer
// ---------------------------------------------------------------------------

static int SDLGetNumJoysticks(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TINTEGER;
	r.integer = SDL_NumJoysticks();
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// SDL.GetGamepadAxis(index, axis) -> float
// ---------------------------------------------------------------------------

static int SDLGetGamepadAxis(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TNUMBER;
	r.number = 0.0;
	if (argc < 2) {
		setter(&r);
		return 1;
	}
	int index = (int)KitsuneAsInt(&argv[0], 0);
	int axis = (int)KitsuneAsInt(&argv[1], 0);
	SDL_GameController* ctrl = SDL_GameControllerOpen(index);
	if (ctrl) {
		Sint16 raw = SDL_GameControllerGetAxis(ctrl, (SDL_GameControllerAxis)axis);
		r.number = raw >= 0 ? (double)raw / 32767.0 : (double)raw / 32768.0;
		SDL_GameControllerClose(ctrl);
	}
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// SDL.GetGamepadButton(index, button) -> bool
// ---------------------------------------------------------------------------

static int SDLGetGamepadButton(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TBOOLEAN;
	r.boolean = false;
	if (argc < 2) {
		setter(&r);
		return 1;
	}
	int index = (int)KitsuneAsInt(&argv[0], 0);
	int button = (int)KitsuneAsInt(&argv[1], 0);
	SDL_GameController* ctrl = SDL_GameControllerOpen(index);
	if (ctrl) {
		r.boolean = SDL_GameControllerGetButton(ctrl, (SDL_GameControllerButton)button) != 0;
		SDL_GameControllerClose(ctrl);
	}
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// SDL.Time.GetTicks() -> integer (milliseconds)
// ---------------------------------------------------------------------------

static int SDLGetTicks(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TINTEGER;
	r.integer = (long long)SDL_GetTicks64();
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// SDL.Time.GetPerformanceCounter() / SDL.Time.GetPerformanceFrequency()
// ---------------------------------------------------------------------------

static int SDLGetPerformanceCounter(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TINTEGER;
	r.integer = (long long)SDL_GetPerformanceCounter();
	setter(&r);
	return 1;
}

static int SDLGetPerformanceFrequency(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TINTEGER;
	r.integer = (long long)SDL_GetPerformanceFrequency();
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// Frame timing state — shared by SDL.GetDeltaTime and SDL.GetFPS
// ---------------------------------------------------------------------------

static Uint64 s_lastCounter = 0;
static double s_deltaTime = 0.0;
static double s_fps = 0.0;

void UpdateFrameTiming() {
	Uint64 now = SDL_GetPerformanceCounter();
	Uint64 freq = SDL_GetPerformanceFrequency();
	if (s_lastCounter == 0 || freq == 0) {
		s_lastCounter = now;
		s_deltaTime = 0.0;
		s_fps = 0.0;
		return;
	}
	s_deltaTime = (double)(now - s_lastCounter) / (double)freq;
	s_lastCounter = now;
	if (s_deltaTime > 0.0)
		s_fps = 0.9 * s_fps + 0.1 * (1.0 / s_deltaTime);
}

// ---------------------------------------------------------------------------
// SDL.Time.GetFrameTime() -> dt, fps
// ---------------------------------------------------------------------------

static int SDLGetFrameTime(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable rdt = {};
	rdt.type = KITSUNE_TNUMBER;
	rdt.number = s_deltaTime;
	setter(&rdt);
	KitsuneVariable rfps = {};
	rfps.type = KITSUNE_TNUMBER;
	rfps.number = s_fps;
	setter(&rfps);
	return 2;
}

// ---------------------------------------------------------------------------
// RegisterSDLInputFunctions
// ---------------------------------------------------------------------------

void RegisterSDLInputFunctions() {
	KitsuneRegisterFunction("SDL.Input.GetKeyState", SDLGetKeyState, nullptr);
	KitsuneRegisterFunction("SDL.Input.GetModState", SDLGetModState, nullptr);
	KitsuneRegisterFunction("SDL.Input.GetMouseState", SDLGetMouseState, nullptr);
	KitsuneRegisterFunction("SDL.Input.GetRelativeMouseState", SDLGetRelativeMouseState, nullptr);
	KitsuneRegisterFunction("SDL.Input.SetRelativeMouseMode", SDLSetRelativeMouseMode, nullptr);
	KitsuneRegisterFunction("SDL.Input.WarpMouse", SDLWarpMouse, nullptr);
	KitsuneRegisterFunction("SDL.Input.GetNumJoysticks", SDLGetNumJoysticks, nullptr);
	KitsuneRegisterFunction("SDL.Input.GetGamepadAxis", SDLGetGamepadAxis, nullptr);
	KitsuneRegisterFunction("SDL.Input.GetGamepadButton", SDLGetGamepadButton, nullptr);
	KitsuneRegisterFunction("SDL.Time.GetTicks",                SDLGetTicks,                nullptr);
	KitsuneRegisterFunction("SDL.Time.GetPerformanceCounter",   SDLGetPerformanceCounter,   nullptr);
	KitsuneRegisterFunction("SDL.Time.GetPerformanceFrequency", SDLGetPerformanceFrequency, nullptr);
	KitsuneRegisterFunction("SDL.Time.GetFrameTime",            SDLGetFrameTime,            nullptr);
}

#endif // KITSUNE_IMGUI
