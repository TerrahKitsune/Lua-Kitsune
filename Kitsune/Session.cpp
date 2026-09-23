#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "Session.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#ifdef _WIN32
#include <conio.h>
#include <io.h>
#include "kitsuneconsole.h"
#else
#include <unistd.h>
#include <sys/ioctl.h>
#endif

// kitsune_ResultSetter may be called multiple times to return multiple values.

#ifdef _WIN32
// Lua strings are UTF-8; Windows console APIs are used in their UTF-16 (W) form so text
// is correct regardless of the console code page.
static std::wstring session_utf8_to_wide(const char* s, size_t len) {
	std::wstring w;
	if (!s || len == 0)
		return w;
	int n = MultiByteToWideChar(CP_UTF8, 0, s, (int)len, NULL, 0);
	if (n <= 0)
		return w;
	w.resize((size_t)n);
	MultiByteToWideChar(CP_UTF8, 0, s, (int)len, &w[0], n);
	return w;
}

// A console created or attached at runtime starts in the OEM code page.
static void session_console_utf8() {
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
}
#endif

static inline bool session_is_stdin_tty() {
#ifdef _WIN32
	return _isatty(_fileno(stdin)) != 0;
#else
	return isatty(fileno(stdin)) != 0;
#endif
}

// Session.Console.Put -- print a string to stdout with CR/BS handling.
static int SessionConsolePut(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter, void*) {
	if (argc > 0 && argv[0].type == KITSUNE_TSTRING && argv[0].data && argv[0].length > 0) {
		for (size_t n = 0; n < argv[0].length; n++) {
			char c = (char)argv[0].data[n];
			if (c == '\r')
				printf("\n");
			else if (c == '\b')
				printf("\b \b");
			else
				printf("%c", c);
		}
	}
	return 1;
}

// Session.Console.GetKey -- read one key from stdin; returns its Unicode code point
// (utf8.char(key) gives the character) or, when stdin is redirected, the next byte.
static int SessionConsoleGetKey(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	long long ch;
	if (session_is_stdin_tty()) {
#ifdef _WIN32
		ch = kitsune_getwch_codepoint();
#else
		ch = -1;
#endif
	}
	else {
		char data;
		ch = (fread(&data, sizeof(char), 1, stdin) == 1) ? (unsigned char)data : -1;
	}
	KitsuneVariable r = {}; r.type = KITSUNE_TINTEGER; r.integer = ch;
	setter(&r);
	return 1;
}

// Session.Console.HasKeyDown -- true if stdin has input waiting.
static int SessionConsoleHasKeyDown(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	bool has;
	if (session_is_stdin_tty()) {
#ifdef _WIN32
		has = _kbhit() > 0;
#else
		has = false;
#endif
	}
	else {
		has = !feof(stdin);
	}
	KitsuneVariable r = {}; r.type = KITSUNE_TBOOLEAN; r.boolean = has;
	setter(&r);
	return 1;
}

// Session.Display.GetScreenSize -- returns width, height (two values).
static int SessionGetScreenSize(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	long long w = 0, h = 0;
#ifdef _WIN32
	w = GetSystemMetrics(SM_CXSCREEN);
	h = GetSystemMetrics(SM_CYSCREEN);
#else
	struct winsize ws = {};
	if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
		w = ws.ws_col;
		h = ws.ws_row;
	}
#endif
	KitsuneVariable vw = {}, vh = {};
	vw.type = KITSUNE_TINTEGER; vw.integer = w;
	vh.type = KITSUNE_TINTEGER; vh.integer = h;
	setter(&vw);
	setter(&vh);
	return 1;
}

// Session.Display.GetCursorPosition -- returns x, y.
static int SessionGetCursorPosition(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
#ifdef _WIN32
	POINT cur;
	if (GetCursorPos(&cur)) {
		HMONITOR hMon = MonitorFromPoint(cur, MONITOR_DEFAULTTONEAREST);
		MONITORINFO mi = {}; mi.cbSize = sizeof(mi);
		if (GetMonitorInfo(hMon, &mi)) {
			KitsuneVariable vx = {}, vy = {};
			vx.type = KITSUNE_TINTEGER; vx.integer = cur.x - mi.rcMonitor.left;
			vy.type = KITSUNE_TINTEGER; vy.integer = cur.y - mi.rcMonitor.top;
			setter(&vx); setter(&vy);
			return 1;
		}
	}
	KitsuneVariable n = {}; n.type = KITSUNE_TNIL;
	setter(&n);
#else
	KitsuneVariable vx = {}, vy = {};
	vx.type = KITSUNE_TINTEGER; vx.integer = 0;
	vy.type = KITSUNE_TINTEGER; vy.integer = 0;
	setter(&vx); setter(&vy);
#endif
	return 1;
}

// Session.Display.GetCursorPoint -- raw screen cursor coordinates (pixels).
static int SessionGetCursorPoint(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
#ifdef _WIN32
	POINT p = {};
	GetCursorPos(&p);
	KitsuneVariable vx = {}, vy = {};
	vx.type = KITSUNE_TINTEGER; vx.integer = p.x;
	vy.type = KITSUNE_TINTEGER; vy.integer = p.y;
	setter(&vx); setter(&vy);
#else
	KitsuneVariable vx = {}, vy = {};
	vx.type = KITSUNE_TINTEGER; vx.integer = 0;
	vy.type = KITSUNE_TINTEGER; vy.integer = 0;
	setter(&vx); setter(&vy);
#endif
	return 1;
}

// Session.Clipboard.Set -- copy a string to the clipboard; returns boolean.
static int SessionClipboardSet(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	bool ok = false;
#ifdef _WIN32
	if (OpenClipboard(NULL)) {
		if (argc > 0 && argv[0].type == KITSUNE_TSTRING && argv[0].data && argv[0].length > 0) {
			// Lua strings are UTF-8; CF_TEXT would be read back in the ANSI code page.
			const char* src = (const char*)argv[0].data;
			int srclen = (int)argv[0].length;
			int wlen = MultiByteToWideChar(CP_UTF8, 0, src, srclen, NULL, 0);
			HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, ((size_t)wlen + 1) * sizeof(wchar_t));
			if (hg) {
				wchar_t* p = (wchar_t*)GlobalLock(hg);
				if (p) {
					MultiByteToWideChar(CP_UTF8, 0, src, srclen, p, wlen);
					p[wlen] = L'\0';
					GlobalUnlock(hg);
					ok = SetClipboardData(CF_UNICODETEXT, hg) != NULL;
					if (!ok)
						GlobalFree(hg);
				}
				else {
					GlobalFree(hg);
				}
			}
		}
		else {
			ok = EmptyClipboard() != FALSE;
		}
		CloseClipboard();
	}
#endif
	KitsuneVariable r = {}; r.type = KITSUNE_TBOOLEAN; r.boolean = ok;
	setter(&r);
	return 1;
}

// Session.Clipboard.Get -- returns the clipboard text as a UTF-8 string, or nil.
static int SessionClipboardGet(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
#ifdef _WIN32
	if (OpenClipboard(NULL)) {
		HANDLE hData = GetClipboardData(CF_UNICODETEXT);
		if (hData) {
			wchar_t* text = (wchar_t*)GlobalLock(hData);
			if (text) {
				int wlen = (int)wcslen(text);
				int bytes = WideCharToMultiByte(CP_UTF8, 0, text, wlen, NULL, 0, NULL, NULL);
				char* utf8 = (char*)malloc((size_t)bytes + 1);
				bool pushed = false;
				if (utf8) {
					if (bytes > 0)
						WideCharToMultiByte(CP_UTF8, 0, text, wlen, utf8, bytes, NULL, NULL);
					KitsuneVariable r = {};
					r.type = KITSUNE_TSTRING;
					r.length = (size_t)bytes;
					r.data = (unsigned char*)utf8;
					setter(&r);
					free(utf8);
					pushed = true;
				}
				GlobalUnlock(hData);
				CloseClipboard();
				if (pushed)
					return 1;
				KitsuneVariable n = {};
				n.type = KITSUNE_TNIL;
				setter(&n);
				return 1;
			}
		}
		CloseClipboard();
	}
#endif
	KitsuneVariable n = {}; n.type = KITSUNE_TNIL;
	setter(&n);
	return 1;
}

#ifdef _WIN32
// Session.Console.Write -- WriteConsole with a single string; returns bytes written.
static int SessionConsoleWrite(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	long long written = 0;
	if (h != INVALID_HANDLE_VALUE && argc > 0 && argv[0].type == KITSUNE_TSTRING && argv[0].data) {
		std::wstring w = session_utf8_to_wide((const char*)argv[0].data, argv[0].length);
		DWORD chars = 0;
		// Reports the UTF-8 byte count, as before, when the whole string was written.
		if (!w.empty() && WriteConsoleW(h, w.data(), (DWORD)w.size(), &chars, NULL) && chars == (DWORD)w.size())
			written = (long long)argv[0].length;
	}
	KitsuneVariable r = {}; r.type = KITSUNE_TINTEGER; r.integer = written;
	setter(&r);
	return 1;
}

// Session.Console.ReadKey -- returns key code or nil if no key is ready.
static int SessionConsoleReadKey(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	bool has = session_is_stdin_tty() ? (_kbhit() > 0) : !feof(stdin);
	if (has) {
		KitsuneVariable r = {}; r.type = KITSUNE_TINTEGER; r.integer = kitsune_getwch_codepoint();
		setter(&r);
	}
	else {
		KitsuneVariable n = {}; n.type = KITSUNE_TNIL;
		setter(&n);
	}
	return 1;
}

// Session.Console.GetKeyState -- returns true if the given virtual key is held.
static int SessionConsoleGetKeyState(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	bool down = false;
	if (argc > 0 && argv[0].type == KITSUNE_TINTEGER)
		down = (GetAsyncKeyState((int)argv[0].integer) & 0x8000) != 0;
	KitsuneVariable r = {}; r.type = KITSUNE_TBOOLEAN; r.boolean = down;
	setter(&r);
	return 1;
}

// Session.Console.SetColor -- set console text attributes from (background, foreground).
static int SessionConsoleSetColor(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter, void*) {
	if (argc >= 2 && argv[0].type == KITSUNE_TINTEGER && argv[1].type == KITSUNE_TINTEGER) {
		WORD attr = (WORD)(((argv[0].integer & 0x0F) << 4) + (argv[1].integer & 0x0F));
		SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), attr);
	}
	return 1;
}

// Session.Console.GetColor -- returns (background, foreground) attribute nibbles.
static int SessionConsoleGetColor(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	CONSOLE_SCREEN_BUFFER_INFO csbi = {};
	if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
		KitsuneVariable vbg = {}, vfg = {};
		vbg.type = KITSUNE_TINTEGER; vbg.integer = (csbi.wAttributes >> 4) & 0x0F;
		vfg.type = KITSUNE_TINTEGER; vfg.integer = csbi.wAttributes & 0x0F;
		setter(&vbg); setter(&vfg);
	}
	else {
		KitsuneVariable n = {}; n.type = KITSUNE_TNIL;
		setter(&n); setter(&n);
	}
	return 1;
}

// Session.Console.SetVisible -- show/hide the console window.
static int SessionConsoleSetVisible(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter, void*) {
	bool show = argc > 0 && argv[0].type == KITSUNE_TBOOLEAN && argv[0].boolean;
	HWND hwnd = GetConsoleWindow();
	ShowWindow(hwnd, show ? SW_RESTORE : SW_HIDE);
	return 1;
}

// Session.Console.SetTitle -- set the console window title.
static int SessionConsoleSetTitle(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter, void*) {
	if (argc > 0 && argv[0].type == KITSUNE_TSTRING && argv[0].data)
		SetConsoleTitleW(session_utf8_to_wide((const char*)argv[0].data, argv[0].length).c_str());
	return 1;
}

// Session.Console.Create / Destroy / Attach -- AllocConsole / FreeConsole / AttachConsole.
static int SessionConsoleCreate(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	bool ok = AllocConsole() != FALSE;
	if (ok)
		session_console_utf8();
	KitsuneVariable r = {}; r.type = KITSUNE_TBOOLEAN; r.boolean = ok;
	setter(&r);
	return 1;
}
static int SessionConsoleDestroy(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	KitsuneVariable r = {}; r.type = KITSUNE_TBOOLEAN; r.boolean = FreeConsole() != FALSE;
	setter(&r);
	return 1;
}
static int SessionConsoleAttach(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	DWORD pid = (argc > 0 && argv[0].type == KITSUNE_TINTEGER)
		? (DWORD)argv[0].integer : ATTACH_PARENT_PROCESS;
	bool ok = AttachConsole(pid) != FALSE;
	if (ok)
		session_console_utf8();
	KitsuneVariable r = {}; r.type = KITSUNE_TBOOLEAN; r.boolean = ok;
	setter(&r);
	return 1;
}

// Session.Console.Clear -- clear the console screen buffer.
static int SessionConsoleClear(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter, void*) {
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO csbi = {};
	if (h == INVALID_HANDLE_VALUE || !GetConsoleScreenBufferInfo(h, &csbi))
		return 1;
	DWORD cells = csbi.dwSize.X * csbi.dwSize.Y, written;
	COORD origin = {};
	FillConsoleOutputCharacter(h, ' ', cells, origin, &written);
	FillConsoleOutputAttribute(h, csbi.wAttributes, cells, origin, &written);
	SetConsoleCursorPosition(h, origin);
	return 1;
}

// Session.Console.GetInfo -- returns x, y, width, height, maxW, maxH (six values).
static int SessionConsoleGetInfo(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter setter, void*) {
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO csbi = {};
	if (h != INVALID_HANDLE_VALUE && GetConsoleScreenBufferInfo(h, &csbi)) {
		long long vals[6] = {
			csbi.dwCursorPosition.X, csbi.dwCursorPosition.Y,
			csbi.dwSize.X,           csbi.dwSize.Y,
			csbi.dwMaximumWindowSize.X, csbi.dwMaximumWindowSize.Y
		};
		for (int i = 0; i < 6; i++) {
			KitsuneVariable v = {}; v.type = KITSUNE_TINTEGER; v.integer = vals[i];
			setter(&v);
		}
	}
	else {
		KitsuneVariable n = {}; n.type = KITSUNE_TNIL;
		setter(&n);
	}
	return 1;
}

// Session.Console.SetCursorPosition -- move cursor to (x, y).
static int SessionConsoleSetCursorPosition(int argc, const KitsuneVariable* argv, const kitsune_ResultSetter, void*) {
	if (argc >= 2 && argv[0].type == KITSUNE_TINTEGER && argv[1].type == KITSUNE_TINTEGER) {
		HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
		COORD c = { (SHORT)argv[0].integer, (SHORT)argv[1].integer };
		SetConsoleCursorPosition(h, c);
	}
	return 1;
}
#endif // _WIN32

void RegisterSessionFunctions() {
	KitsuneRegisterFunction("Session.Console.Put", SessionConsolePut);
	KitsuneRegisterFunction("Session.Console.GetKey", SessionConsoleGetKey);
	KitsuneRegisterFunction("Session.Console.HasKeyDown", SessionConsoleHasKeyDown);
	KitsuneRegisterFunction("Session.Display.GetScreenSize", SessionGetScreenSize);
	KitsuneRegisterFunction("Session.Display.GetCursorPosition", SessionGetCursorPosition);
	KitsuneRegisterFunction("Session.Display.GetCursorPoint", SessionGetCursorPoint);
	KitsuneRegisterFunction("Session.Clipboard.Set", SessionClipboardSet);
	KitsuneRegisterFunction("Session.Clipboard.Get", SessionClipboardGet);
#ifdef _WIN32
	KitsuneRegisterFunction("Session.Console.Write", SessionConsoleWrite);
	KitsuneRegisterFunction("Session.Console.ReadKey", SessionConsoleReadKey);
	KitsuneRegisterFunction("Session.Console.GetKeyState", SessionConsoleGetKeyState);
	KitsuneRegisterFunction("Session.Console.SetColor", SessionConsoleSetColor);
	KitsuneRegisterFunction("Session.Console.GetColor", SessionConsoleGetColor);
	KitsuneRegisterFunction("Session.Console.SetVisible", SessionConsoleSetVisible);
	KitsuneRegisterFunction("Session.Console.SetTitle", SessionConsoleSetTitle);
	KitsuneRegisterFunction("Session.Console.Create", SessionConsoleCreate);
	KitsuneRegisterFunction("Session.Console.Destroy", SessionConsoleDestroy);
	KitsuneRegisterFunction("Session.Console.Attach", SessionConsoleAttach);
	KitsuneRegisterFunction("Session.Console.Clear", SessionConsoleClear);
	KitsuneRegisterFunction("Session.Console.GetInfo", SessionConsoleGetInfo);
	KitsuneRegisterFunction("Session.Console.SetCursorPosition", SessionConsoleSetCursorPosition);
#endif
}
