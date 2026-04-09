#if defined(_WIN32) && defined(_DEBUG)
#define _CRTDBG_MAP_ALLOC
#endif
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#define KITSUNE_ALL

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>
#include "platform.h"
#ifdef _WIN32
#include <conio.h>
#include <io.h>
#else
#include <unistd.h>
#include <sys/ioctl.h>
#endif
#if defined(_WIN32) && defined(_DEBUG)
#include <crtdbg.h>
#endif
#include "KitsuneEngine.h"

#ifdef _DEBUG
int Test(int argc, KitsuneVariable* argv, const kitsune_ResultSetter resultSetter, void* userdata) {

	for (int n = 0; n < argc; n++) {
		if (argv[n].type == KITSUNE_TSTRING && argv[n].data && argv[n].length > 0)
			printf("arg %d: %.*s\n", n, (int)argv[n].length, (char*)argv[n].data);
		else if (argv[n].type == KITSUNE_TNUMBER)
			printf("arg %d: %f\n", n, argv[n].number);
		else if(argv[n].type == KITSUNE_TBOOLEAN)
			printf("arg %d: %s\n", n, argv[n].boolean ? "true" : "false");
		else
			printf("arg %d: (type %d)\n", n, argv[n].type);
	}

	const char* testResult = "This is a test result";

	KitsuneVariable* result = (KitsuneVariable*)malloc(sizeof(KitsuneVariable));
	if (!result) {
		return 0;
	}

	result->type = KITSUNE_TSTRING;
	result->length = strlen(testResult);
	result->data = (unsigned char*)malloc(result->length + 1);
	strcpy((char*)result->data, testResult);

	resultSetter(result);

	if (result->data) {
		free(result->data);
	}
	free(result);

	return 1;
}
#endif

// ── Session functions ──────────────────────────────────────────────────────────
// Registered via KitsuneRegisterFunction as Session.Console/Display/Clipboard.
// kitsune_ResultSetter may be called multiple times to return multiple values.

static inline bool prog_is_stdin_tty() {
#ifdef _WIN32
	return _isatty(_fileno(stdin)) != 0;
#else
	return isatty(fileno(stdin)) != 0;
#endif
}

// Session.Console.Put — print a string to stdout with CR/BS handling.
static int SessionConsolePut(int argc, KitsuneVariable* argv, const kitsune_ResultSetter, void*) {
	if (argc > 0 && argv[0].type == KITSUNE_TSTRING && argv[0].data && argv[0].length > 0) {
		for (size_t n = 0; n < argv[0].length; n++) {
			char c = (char)argv[0].data[n];
			if (c == '\r')       printf("\n");
			else if (c == '\b')  printf("\b \b");
			else                 printf("%c", c);
		}
	}
	return 1;
}

// Session.Console.GetKey — read one byte/key from stdin; returns integer.
static int SessionConsoleGetKey(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	long long ch;
	if (prog_is_stdin_tty()) {
#ifdef _WIN32
		ch = _getch();
#else
		ch = -1;
#endif
	} else {
		char data;
		ch = (fread(&data, sizeof(char), 1, stdin) == 1) ? (unsigned char)data : -1;
	}
	KitsuneVariable r{}; r.type = KITSUNE_TINTEGER; r.integer = ch;
	setter(&r);
	return 1;
}

// Session.Console.HasKeyDown — true if stdin has input waiting.
static int SessionConsoleHasKeyDown(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	bool has;
	if (prog_is_stdin_tty()) {
#ifdef _WIN32
		has = _kbhit() > 0;
#else
		has = false;
#endif
	} else {
		has = !feof(stdin);
	}
	KitsuneVariable r{}; r.type = KITSUNE_TBOOLEAN; r.boolean = has;
	setter(&r);
	return 1;
}

// Session.Display.GetScreenSize — returns width, height (two values).
static int SessionGetScreenSize(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	long long w = 0, h = 0;
#ifdef _WIN32
	w = GetSystemMetrics(SM_CXSCREEN);
	h = GetSystemMetrics(SM_CYSCREEN);
#else
	struct winsize ws{};
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
		w = ws.ws_col;
		h = ws.ws_row;
	}
#endif
	KitsuneVariable vw{}, vh{};
	vw.type = KITSUNE_TINTEGER; vw.integer = w;
	vh.type = KITSUNE_TINTEGER; vh.integer = h;
	setter(&vw);
	setter(&vh);
	return 1;
}

// Session.Display.GetCursorPosition — returns x, y (and monitor index on Windows).
static int SessionGetCursorPosition(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
#ifdef _WIN32
	POINT cur;
	if (GetCursorPos(&cur)) {
		HMONITOR hMon = MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi{}; mi.cbSize = sizeof(mi);
		if (GetMonitorInfo(hMon, &mi)) {
			KitsuneVariable vx{}, vy{}, vm{};
			vx.type = KITSUNE_TINTEGER; vx.integer = cur.x - mi.rcMonitor.left;
			vy.type = KITSUNE_TINTEGER; vy.integer = cur.y - mi.rcMonitor.top;
			setter(&vx); setter(&vy);
			return 1;
		}
	}
	KitsuneVariable n{}; n.type = KITSUNE_TNIL;
	setter(&n);
#else
	KitsuneVariable vx{}, vy{};
	vx.type = KITSUNE_TINTEGER; vx.integer = 0;
	vy.type = KITSUNE_TINTEGER; vy.integer = 0;
	setter(&vx); setter(&vy);
#endif
	return 1;
}

// Session.Display.GetCursorPoint — raw screen cursor coordinates (pixels).
static int SessionGetCursorPoint(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
#ifdef _WIN32
	POINT p{};
	GetCursorPos(&p);
	KitsuneVariable vx{}, vy{};
	vx.type = KITSUNE_TINTEGER; vx.integer = p.x;
	vy.type = KITSUNE_TINTEGER; vy.integer = p.y;
	setter(&vx); setter(&vy);
#else
	KitsuneVariable vx{}, vy{};
	vx.type = KITSUNE_TINTEGER; vx.integer = 0;
	vy.type = KITSUNE_TINTEGER; vy.integer = 0;
	setter(&vx); setter(&vy);
#endif
	return 1;
}

// Session.Clipboard.Set — copy a string to the clipboard; returns boolean.
static int SessionClipboardSet(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	bool ok = false;
#ifdef _WIN32
	if (OpenClipboard(NULL)) {
		if (argc > 0 && argv[0].type == KITSUNE_TSTRING && argv[0].data && argv[0].length > 0) {
			HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, argv[0].length + 1);
			if (hg) {
				char* p = (char*)GlobalLock(hg);
				if (p) {
					memcpy(p, argv[0].data, argv[0].length);
					p[argv[0].length] = '\0';
					GlobalUnlock(hg);
					ok = SetClipboardData(CF_TEXT, hg) != NULL;
					if (!ok)
						GlobalFree(hg);
				} else {
					GlobalFree(hg);
				}
			}
		} else {
			ok = EmptyClipboard() != FALSE;
		}
		CloseClipboard();
	}
#endif
	KitsuneVariable r{}; r.type = KITSUNE_TBOOLEAN; r.boolean = ok;
	setter(&r);
	return 1;
}

// Session.Clipboard.Get — returns the clipboard text as a Wchar, or nil.
static int SessionClipboardGet(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
#ifdef _WIN32
	if (OpenClipboard(NULL)) {
		HANDLE hData = GetClipboardData(CF_UNICODETEXT);
		if (hData) {
			wchar_t* text = (wchar_t*)GlobalLock(hData);
			if (text) {
				size_t wlen = wcslen(text);
				// Pack as KITSUNE_TCHAR16: length = number of char16_t code units.
				KitsuneVariable r{};
				r.type = KITSUNE_TCHAR16;
				r.length = wlen;
				r.char16data = (char16_t*)text; // wchar_t == char16_t on Windows (both 2 bytes)
				setter(&r);
				GlobalUnlock(hData);
				CloseClipboard();
				return 1;
			}
		}
		CloseClipboard();
	}
#endif
	KitsuneVariable n{}; n.type = KITSUNE_TNIL;
	setter(&n);
	return 1;
}

#ifdef _WIN32
// Session.Console.Write — WriteConsole with a single string; returns bytes written.
static int SessionConsoleWrite(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD written = 0;
	if (h != INVALID_HANDLE_VALUE && argc > 0 && argv[0].type == KITSUNE_TSTRING && argv[0].data)
		WriteConsole(h, argv[0].data, (DWORD)argv[0].length, &written, NULL);
	KitsuneVariable r{}; r.type = KITSUNE_TINTEGER; r.integer = written;
	setter(&r);
	return 1;
}

// Session.Console.ReadKey — returns key code or nil if no key is ready.
static int SessionConsoleReadKey(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	bool has = prog_is_stdin_tty() ? (_kbhit() > 0) : !feof(stdin);
	if (has) {
		KitsuneVariable r{}; r.type = KITSUNE_TINTEGER; r.integer = _getch();
		setter(&r);
	} else {
		KitsuneVariable n{}; n.type = KITSUNE_TNIL;
		setter(&n);
	}
	return 1;
}

// Session.Console.GetKeyState — returns true if the given virtual key is held.
static int SessionConsoleGetKeyState(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	bool down = false;
	if (argc > 0 && argv[0].type == KITSUNE_TINTEGER)
		down = (GetAsyncKeyState((int)argv[0].integer) & 0x8000) != 0;
	KitsuneVariable r{}; r.type = KITSUNE_TBOOLEAN; r.boolean = down;
	setter(&r);
	return 1;
}

// Session.Console.SetColor — set console text attributes from (background, foreground).
static int SessionConsoleSetColor(int argc, KitsuneVariable* argv, const kitsune_ResultSetter, void*) {
	if (argc >= 2 && argv[0].type == KITSUNE_TINTEGER && argv[1].type == KITSUNE_TINTEGER) {
		WORD attr = (WORD)(((argv[0].integer & 0x0F) << 4) + (argv[1].integer & 0x0F));
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), attr);
	}
	return 1;
}

// Session.Console.GetColor — returns (background, foreground) attribute nibbles.
static int SessionConsoleGetColor(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	CONSOLE_SCREEN_BUFFER_INFO csbi{};
	if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
		KitsuneVariable vbg{}, vfg{};
		vbg.type = KITSUNE_TINTEGER; vbg.integer = (csbi.wAttributes >> 4) & 0x0F;
		vfg.type = KITSUNE_TINTEGER; vfg.integer = csbi.wAttributes & 0x0F;
		setter(&vbg); setter(&vfg);
	} else {
		KitsuneVariable n{}; n.type = KITSUNE_TNIL;
		setter(&n); setter(&n);
	}
	return 1;
}

// Session.Console.SetVisible — show/hide the console window.
static int SessionConsoleSetVisible(int argc, KitsuneVariable* argv, const kitsune_ResultSetter, void*) {
	bool show = argc > 0 && argv[0].type == KITSUNE_TBOOLEAN && argv[0].boolean;
	HWND hwnd = GetConsoleWindow();
	ShowWindow(hwnd, show ? SW_RESTORE : SW_HIDE);
	return 1;
}

// Session.Console.SetTitle — set the console window title.
static int SessionConsoleSetTitle(int argc, KitsuneVariable* argv, const kitsune_ResultSetter, void*) {
	if (argc > 0 && argv[0].type == KITSUNE_TSTRING && argv[0].data)
		SetConsoleTitleA((const char*)argv[0].data);
	return 1;
}

// Session.Console.Create / Destroy / Attach — AllocConsole / FreeConsole / AttachConsole.
static int SessionConsoleCreate(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	KitsuneVariable r{}; r.type = KITSUNE_TBOOLEAN; r.boolean = AllocConsole() != FALSE;
	setter(&r); return 1;
}
static int SessionConsoleDestroy(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	KitsuneVariable r{}; r.type = KITSUNE_TBOOLEAN; r.boolean = FreeConsole() != FALSE;
	setter(&r); return 1;
}
static int SessionConsoleAttach(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	DWORD pid = (argc > 0 && argv[0].type == KITSUNE_TINTEGER)
		? (DWORD)argv[0].integer : ATTACH_PARENT_PROCESS;
	KitsuneVariable r{}; r.type = KITSUNE_TBOOLEAN; r.boolean = AttachConsole(pid) != FALSE;
	setter(&r); return 1;
}

// Session.Console.Clear — clear the console screen buffer.
static int SessionConsoleClear(int argc, KitsuneVariable* argv, const kitsune_ResultSetter, void*) {
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO csbi{};
	if (h == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(h, &csbi))
		return 1;
	DWORD cells = csbi.dwSize.X * csbi.dwSize.Y, written;
	COORD origin{};
	FillConsoleOutputCharacter(h, ' ', cells, origin, &written);
	FillConsoleOutputAttribute(h, csbi.wAttributes, cells, origin, &written);
	SetConsoleCursorPosition(h, origin);
	return 1;
}

// Session.Console.GetInfo — returns x, y, width, height, maxW, maxH (six values).
static int SessionConsoleGetInfo(int argc, KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO csbi{};
	if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &csbi)) {
		long long vals[6] = {
			csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y,
			csbi.dwSize.X,           csbi.dwSize.Y,
			csbi.dwMaximumWindowSize.X, csbi.dwMaximumWindowSize.Y
		};
		for (int i = 0; i < 6; i++) {
			KitsuneVariable v{}; v.type = KITSUNE_TINTEGER; v.integer = vals[i];
			setter(&v);
		}
	} else {
		KitsuneVariable n{}; n.type = KITSUNE_TNIL;
		setter(&n);
	}
	return 1;
}

// Session.Console.SetCursorPosition — move cursor to (x, y).
static int SessionConsoleSetCursorPosition(int argc, KitsuneVariable* argv, const kitsune_ResultSetter, void*) {
	if (argc >= 2 && argv[0].type == KITSUNE_TINTEGER && argv[1].type == KITSUNE_TINTEGER) {
		HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
		COORD c = { (SHORT)argv[0].integer, (SHORT)argv[1].integer };
		SetConsoleCursorPosition(h, c);
	}
	return 1;
}
#endif // _WIN32

// Registers Session.Console, Session.Display and Session.Clipboard into Lua.
// Intermediate tables are created automatically by KitsuneRegisterFunction.
static void RegisterSessionFunctions() {
	KitsuneRegisterFunction("Session.Console.Put",          SessionConsolePut);
	KitsuneRegisterFunction("Session.Console.GetKey",       SessionConsoleGetKey);
	KitsuneRegisterFunction("Session.Console.HasKeyDown",   SessionConsoleHasKeyDown);
	KitsuneRegisterFunction("Session.Display.GetScreenSize",     SessionGetScreenSize);
	KitsuneRegisterFunction("Session.Display.GetCursorPosition", SessionGetCursorPosition);
	KitsuneRegisterFunction("Session.Display.GetCursorPoint",    SessionGetCursorPoint);
	KitsuneRegisterFunction("Session.Clipboard.Set",        SessionClipboardSet);
	KitsuneRegisterFunction("Session.Clipboard.Get",        SessionClipboardGet);
#ifdef _WIN32
	KitsuneRegisterFunction("Session.Console.Write",              SessionConsoleWrite);
	KitsuneRegisterFunction("Session.Console.ReadKey",            SessionConsoleReadKey);
	KitsuneRegisterFunction("Session.Console.GetKeyState",        SessionConsoleGetKeyState);
	KitsuneRegisterFunction("Session.Console.SetColor",           SessionConsoleSetColor);
	KitsuneRegisterFunction("Session.Console.GetColor",           SessionConsoleGetColor);
	KitsuneRegisterFunction("Session.Console.SetVisible",         SessionConsoleSetVisible);
	KitsuneRegisterFunction("Session.Console.SetTitle",           SessionConsoleSetTitle);
	KitsuneRegisterFunction("Session.Console.Create",             SessionConsoleCreate);
	KitsuneRegisterFunction("Session.Console.Destroy",            SessionConsoleDestroy);
	KitsuneRegisterFunction("Session.Console.Attach",             SessionConsoleAttach);
	KitsuneRegisterFunction("Session.Console.Clear",              SessionConsoleClear);
	KitsuneRegisterFunction("Session.Console.GetInfo",            SessionConsoleGetInfo);
	KitsuneRegisterFunction("Session.Console.SetCursorPosition",  SessionConsoleSetCursorPosition);
#endif
}

static std::atomic<long> g_exitSignaled{0};

#ifdef _WIN32
BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
	switch (ctrlType) {
	case CTRL_C_EVENT:
	case CTRL_BREAK_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_SHUTDOWN_EVENT:
		g_exitSignaled.store(1);
		KitsuneInterrupt();
		return TRUE;
	}
	return FALSE;
}
#else
static void SigIntHandler(int) {
	g_exitSignaled.store(1);
	KitsuneInterrupt();
}
#endif

int main(int argc, char* argv[]) {

#if defined(_WIN32) && defined(_DEBUG)
	_CrtMemState sOld;
	_CrtMemState sNew;
	_CrtMemState sDiff;
	_CrtMemCheckpoint(&sOld);
#endif

	#ifdef _WIN32
		SetConsoleOutputCP(65001);
	#endif

	if (!KitsuneInit()) {
		fprintf(stderr, "KitsuneInit failed\n");
		return -1;
	}
	else {
		RegisterSessionFunctions();
	}

	#ifdef _WIN32
		SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
	#else
		signal(SIGINT, SigIntHandler);
		signal(SIGTERM, SigIntHandler);
	#endif

	const char* file = argc > 1 ? argv[1] : "main.lua";

	// Convert extra command-line args (argv[2..]) to KitsuneVariable array.
	int extraArgc = argc > 2 ? argc - 2 : 0;
	KitsuneVariable* vars = nullptr;
	if (extraArgc > 0) {
		vars = new KitsuneVariable[extraArgc]();
		for (int i = 0; i < extraArgc; i++) {
			vars[i].type = KITSUNE_TSTRING;
			vars[i].length = strlen(argv[i + 2]);
			vars[i].data = (unsigned char*)argv[i + 2];
		}
	}

#ifdef _DEBUG
	KitsuneRegisterFunction("Test", Test);
#endif

	int id = KitsuneExecuteFileAsync(file, extraArgc, vars);
	delete[] vars;

	if (id < 0) {
		fprintf(stderr, "Failed to start %s\n", file);
		KitsuneCleanup();
		return -1;
	}

	// Block until the coroutine finishes or an exit signal is received.
	while (!KitsuneHasResult(id, nullptr) && !g_exitSignaled.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(1));

	// If the signal fired before the coroutine reported done, wait for the
	// interrupt to propagate so KitsuneGetError / KitsuneGetResult are valid.
	if (!KitsuneHasResult(id, nullptr))
		KitsuneWait();

	size_t errLen = KitsuneGetError(id, nullptr, 0);
	int ret = errLen > 0 ? 1 : 0;
	if (errLen > 0) {
		char* errBuf = new char[errLen + 1];
		KitsuneGetError(id, errBuf, errLen + 1);
		fprintf(stderr, "%s\n", errBuf);
		delete[] errBuf;
	}

	KitsuneVariable* result = KitsuneGetResult(id);
	if (result) {
		if (result->type == KITSUNE_TSTRING && result->data && result->length > 0)
			printf("%.*s\n", (int)result->length, (char*)result->data);
		else if (result->type == KITSUNE_TNUMBER)
			printf("%f\n", result->number);
		else if (result->type == KITSUNE_TBOOLEAN)
			printf("%s\n", result->boolean ? "true" : "false");
		else if(result->type != KITSUNE_TNIL || result->type != KITSUNE_TNONE)
			printf("(type %d)\n", result->type);
		KitsuneVariableFree(result);
	}

	KitsuneCleanup();

#if defined(_WIN32) && defined(_DEBUG)
	_CrtMemCheckpoint(&sNew);
	if (_CrtMemDifference(&sDiff, &sOld, &sNew)) {
		OutputDebugString("-----------_CrtMemDumpStatistics ---------");
		_CrtMemDumpStatistics(&sDiff);
		OutputDebugString("-----------_CrtMemDumpAllObjectsSince ---------");
		_CrtMemDumpAllObjectsSince(&sOld);
		OutputDebugString("-----------_CrtDumpMemoryLeaks ---------");
		_CrtDumpMemoryLeaks();
		DebugBreak();
	}
#endif

	return ret;
}
