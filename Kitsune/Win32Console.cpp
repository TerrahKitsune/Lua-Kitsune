#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#ifdef KITSUNE_IMGUI

#include "Win32Console.h"
#include "KitsuneEngine.h"

#ifdef _WIN32
#include <windows.h>
#endif

// Cached at registration time: true if this process owned its console on startup.
// Stays true even after FreeConsole() so toggle buttons remain usable.
static bool g_ownsConsole = false;

// ---------------------------------------------------------------------------
// Win32.OwnsConsole() -> bool
// ---------------------------------------------------------------------------

static int Win32OwnsConsole(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	KitsuneVariable r = {};
	r.type = KITSUNE_TBOOLEAN;
	r.boolean = g_ownsConsole;
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// Win32.Console(show) -> bool
// ---------------------------------------------------------------------------

static int Win32Console(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
#ifdef _WIN32
	HWND hwnd = GetConsoleWindow();
	if (hwnd) {
		bool show = argc > 0 && argv[0].type == KITSUNE_TBOOLEAN && argv[0].boolean;
		ShowWindow(hwnd, show ? SW_SHOWNOACTIVATE : SW_MINIMIZE);
		bool isShown = IsWindowVisible(hwnd) && !IsIconic(hwnd);
		KitsuneVariable r = {};
		r.type = KITSUNE_TBOOLEAN;
		r.boolean = isShown;
		setter(&r);
		return 1;
	}
#endif
	return 0;
}

// ---------------------------------------------------------------------------
// Win32.DestroyConsole() -> bool
// ---------------------------------------------------------------------------

static int Win32DestroyConsole(int argc, const KitsuneVariable* argv,
	const kitsune_ResultSetter setter, void* ud) {
	bool ok = false;
#ifdef _WIN32
	if (g_ownsConsole && GetConsoleWindow())
		ok = FreeConsole() != 0;
#endif
	KitsuneVariable r = {};
	r.type = KITSUNE_TBOOLEAN;
	r.boolean = ok;
	setter(&r);
	return 1;
}

// ---------------------------------------------------------------------------
// RegisterWin32ConsoleFunctions
// ---------------------------------------------------------------------------

void RegisterWin32ConsoleFunctions() {
#ifdef _WIN32
	DWORD pids[2];
	g_ownsConsole = (GetConsoleProcessList(pids, 2) == 1);
#endif
	KitsuneRegisterFunction("Win32.OwnsConsole", Win32OwnsConsole, nullptr);
	KitsuneRegisterFunction("Win32.Console", Win32Console, nullptr);
	KitsuneRegisterFunction("Win32.DestroyConsole", Win32DestroyConsole, nullptr);
}

#endif // KITSUNE_IMGUI
