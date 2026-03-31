#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <Windows.h>
#include "lua_main_incl.h"
#include <conio.h>
#include <io.h>
#include "Shellapi.h"
#include "mem.h"
#include "luawchar.h"

#define HI_PART(x)  ((x>>4) & 0x0F)
#define LO_PART(x)  ((x) & 0x0F)

// _isatty/_fileno are Windows CRT names; POSIX uses isatty/fileno without the underscore.
#ifdef _WIN32
static inline bool is_stdin_tty() { return _isatty(_fileno(stdin)) != 0; }
#else
#include <unistd.h>
static inline bool is_stdin_tty() { return isatty(fileno(stdin)) != 0; }
#endif

int L_kbhit(lua_State *L) {
	if (is_stdin_tty()) {
		lua_pushboolean(L, _kbhit());
	} else if (feof(stdin)) {
		lua_pushboolean(L, 1);
	} else {
		lua_pushboolean(L, 0);
	}
	return 1;
}

int L_getch(lua_State *L) {
	if (is_stdin_tty()) {
		lua_pushinteger(L, _getch());
	} else {
		char data;
		if (fread(&data, sizeof(char), 1, stdin) == 1) {
			lua_pushinteger(L, data);
		} else {
			lua_pushinteger(L, -1);
		}
	}
	return 1;
}

int L_GetTextColor(lua_State *L) {
	WORD data;
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
		data = csbi.wAttributes;
		lua_pushinteger(L, HI_PART(data));
		lua_pushinteger(L, LO_PART(data));
	} else {
		lua_pushnil(L);
		lua_pushnil(L);
	}
	return 2;
}

int L_SetTextColor(lua_State *L) {
	int BackC = (int)luaL_checknumber(L, 1);
	int ForgC = (int)luaL_checknumber(L, 2);
	lua_pop(L, 2);
	WORD wColor = ((BackC & 0x0F) << 4) + (ForgC & 0x0F);
	SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), wColor);
	return 0;
}

int L_cls(lua_State *L) {
	HANDLE hStdOut;
	CONSOLE_SCREEN_BUFFER_INFO csbi;
	DWORD count, cellCount;
	COORD homeCoords = { 0, 0 };
	hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hStdOut == INVALID_HANDLE_VALUE)
		return 0;
	if (!GetConsoleScreenBufferInfo(hStdOut, &csbi))
		return 0;
	cellCount = csbi.dwSize.X * csbi.dwSize.Y;
	if (!FillConsoleOutputCharacter(hStdOut, (TCHAR)' ', cellCount, homeCoords, &count))
		return 0;
	if (!FillConsoleOutputAttribute(hStdOut, csbi.wAttributes, cellCount, homeCoords, &count))
		return 0;
	SetConsoleCursorPosition(hStdOut, homeCoords);
	return 0;
}

int L_put(lua_State *L) {
	size_t len;
	const char* text = luaL_tolstring(L, 1, &len);
	if (len > 0) {
		for (unsigned int n = 0; n < len; n++) {
			if (text[n] == 13)
				printf("\n");
			else if (text[n] == 8)
				printf("\b \b");
			else
				printf("%c", text[n]);
		}
	}
	lua_pop(L, 1);
	return 0;
}

int L_GetMemory(lua_State *L) {
	lua_pop(L, lua_gettop(L));
	int mem = lua_gc(L, LUA_GCCOUNT, 0);
	mem = mem * 1024;
	mem += lua_gc(L, LUA_GCCOUNTB, 0);
	lua_pushinteger(L, mem);
	return 1;
}

int L_ShellExecute(lua_State *L) {
	INT_PTR ok = (INT_PTR)ShellExecute(NULL, "open", luaL_checkstring(L, 1), luaL_checkstring(L, 2), NULL, SW_SHOW);
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, ok > 32);
	return 1;
}

int L_GetReg(lua_State *L) {
	HKEY key = HKEY_LOCAL_MACHINE;
	switch (lua_tointeger(L, 1)) {
	case 1: key = HKEY_CLASSES_ROOT; break;
	case 2: key = HKEY_CURRENT_CONFIG; break;
	case 3: key = HKEY_CURRENT_USER; break;
	case 4: key = HKEY_PERFORMANCE_DATA; break;
	case 5: key = HKEY_PERFORMANCE_NLSTEXT; break;
	case 6: key = HKEY_PERFORMANCE_TEXT; break;
	case 7: key = HKEY_USERS; break;
	default: break;
	}

	DWORD max = 1048576;
	char* buffer = (char*)gff_malloc(max);
	if (!buffer) {
		luaL_error(L, "Unable to allocate memory for readbuffer in GetReg");
		return 0;
	}
	memset(buffer, 0, max);

	LSTATUS status = RegGetValue(key, luaL_checkstring(L, 2), luaL_checkstring(L, 3), RRF_RT_ANY, nullptr, buffer, &max);
	if (status == ERROR_SUCCESS) {
		lua_pop(L, lua_gettop(L));
		lua_pushstring(L, buffer);
		gff_free(buffer);
	} else {
		lua_pop(L, lua_gettop(L));
		char* err;
		if (!FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
			NULL, status, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)&err, 0, NULL)) {
			lua_pushnil(L);
			lua_pushstring(L, "unable to format error message!");
		} else {
			lua_pushnil(L);
			lua_pushstring(L, err);
			LocalFree(err);
		}
		gff_free(buffer);
		return 2;
	}
	return 1;
}

int L_ToggleConsole(lua_State *L) {
	bool toggle = lua_toboolean(L, 1) > 0;
	HWND console = GetConsoleWindow();
	if (toggle)
		ShowWindow(console, SW_RESTORE);
	else
		ShowWindow(console, SW_HIDE);
	lua_pop(L, 1);
	return 0;
}

int L_SetTitle(lua_State *L) {
	SetConsoleTitle(luaL_checkstring(L, 1));
	lua_pop(L, 1);
	return 0;
}

// ── Session functions (Session.Console and Session.Clipboard) ────────────────────
// All session functions are static; they are only referenced by luaopen_session.

static int lua_SetClipboard(lua_State *L) {
	size_t len;
	const char* data = lua_tolstring(L, -1, &len);
	if (!OpenClipboard(NULL)) {
		lua_pushboolean(L, false);
		return 1;
	}
	if (!data || len == 0) {
		lua_pushboolean(L, EmptyClipboard() ? true : false);
		CloseClipboard();
		return 1;
	}
	HGLOBAL hGlobal = GlobalAlloc(GMEM_MOVEABLE, len + 1);
	if (hGlobal == NULL) {
		CloseClipboard();
		lua_pushboolean(L, false);
		return 1;
	}
	char* pGlobal = (char*)GlobalLock(hGlobal);
	if (pGlobal == NULL) {
		GlobalFree(hGlobal);
		CloseClipboard();
		lua_pushboolean(L, false);
		return 1;
	}
	memcpy(pGlobal, data, len);
	GlobalUnlock(hGlobal);
	if (SetClipboardData(CF_TEXT, hGlobal) == NULL) {
		GlobalFree(hGlobal);
		lua_pushboolean(L, false);
	} else {
		lua_pushboolean(L, true);
	}
	CloseClipboard();
	return 1;
}

static int lua_GetClipboard(lua_State *L) {
	if (!OpenClipboard(NULL)) {
		lua_pushnil(L);
		return 1;
	}
	HANDLE hData = GetClipboardData(CF_UNICODETEXT);
	if (hData == NULL) {
		CloseClipboard();
		lua_pushnil(L);
		return 1;
	}
	wchar_t* pszText = (wchar_t*)GlobalLock(hData);
	if (pszText == NULL) {
		CloseClipboard();
		lua_pushnil(L);
		return 1;
	}
	lua_pushwchar(L, pszText);
	GlobalUnlock(hData);
	CloseClipboard();
	return 1;
}

static int L_SetConsoleCoords(lua_State *L) {
	int x = (int)luaL_checkinteger(L, 1);
	int y = (int)luaL_checkinteger(L, 2);
	HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hStdOut == INVALID_HANDLE_VALUE)
		return 0;
	COORD homeCoords = { (SHORT)x, (SHORT)y };
	SetConsoleCursorPosition(hStdOut, homeCoords);
	return 0;
}

static int L_GetConsoleCoords(lua_State *L) {
	HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO info;
	lua_pop(L, lua_gettop(L));
	if (hStdOut == INVALID_HANDLE_VALUE)
		return 0;
	if (!GetConsoleScreenBufferInfo(hStdOut, &info)) {
		lua_pushnil(L);
		return 1;
	}
	lua_pushinteger(L, info.dwCursorPosition.X);
	lua_pushinteger(L, info.dwCursorPosition.Y);
	lua_pushinteger(L, info.dwSize.X);
	lua_pushinteger(L, info.dwSize.Y);
	lua_pushinteger(L, info.dwMaximumWindowSize.X);
	lua_pushinteger(L, info.dwMaximumWindowSize.Y);
	return 6;
}

static int L_ConsoleCreate(lua_State *L) {
	lua_pop(L, lua_gettop(L));
	BOOL ok = AllocConsole();
	lua_pushboolean(L, ok > 0);
	return 1;
}

static int L_ConsoleDestroy(lua_State *L) {
	lua_pop(L, lua_gettop(L));
	BOOL ok = FreeConsole();
	lua_pushboolean(L, ok > 0);
	return 1;
}

static int L_ConsoleWrite(lua_State *L) {
	HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hStdOut == INVALID_HANDLE_VALUE)
		return 0;
	size_t len;
	const char* data;
	if (lua_isstring(L, 1)) {
		data = lua_tolstring(L, 1, &len);
	} else {
		data = luaL_tolstring(L, 1, &len);
	}
	DWORD written;
	WriteConsole(hStdOut, data, (DWORD)len, &written, NULL);
	lua_pop(L, lua_gettop(L));
	lua_pushinteger(L, written);
	return 1;
}

static int L_ConsolePrint(lua_State *L) {
	HANDLE hStdOut = GetStdHandle(STD_OUTPUT_HANDLE);
	if (hStdOut == INVALID_HANDLE_VALUE)
		return 0;
	DWORD written, total = 0;
	size_t len;
	const char* data;
	for (int n = 1; n <= lua_gettop(L); n++) {
		data = luaL_tolstring(L, n, &len);
		lua_pop(L, 1);
		if (!data) {
			data = "";
			len  = 0;
		}
		WriteConsole(hStdOut, data, (DWORD)len, &written, NULL);
		total += written;
		if (n < lua_gettop(L)) {
			data = "\t";
			len  = 1;
		} else {
			data = "\n";
			len  = 1;
		}
		WriteConsole(hStdOut, data, (DWORD)len, &written, NULL);
		total += written;
	}
	lua_pop(L, lua_gettop(L));
	lua_pushinteger(L, total);
	return 1;
}

static int L_ConsoleReadKey(lua_State *L) {
	HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);
	if (hStdIn == INVALID_HANDLE_VALUE)
		return 0;
	lua_pop(L, lua_gettop(L));
	bool keydown = false;
	if (is_stdin_tty()) {
		keydown = _kbhit() > 0;
	} else {
		keydown = !feof(stdin);
	}
	if (keydown) {
		lua_pushinteger(L, _getch());
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static int L_AttachConsole(lua_State *L) {
	DWORD processId = (DWORD)luaL_optinteger(L, 1, ATTACH_PARENT_PROCESS);
	lua_pop(L, lua_gettop(L));
	BOOL ok = AttachConsole(processId);
	lua_pushboolean(L, ok > 0);
	return 1;
}

static int L_GetKeyState(lua_State *L) {
	SHORT state = GetAsyncKeyState((int)luaL_checkinteger(L, 1));
	lua_pushboolean(L, (state & 0x8000) == 0x8000);
	return 1;
}

typedef struct { int count; HMONITOR search; int idx; } DisplayEnumData;

static BOOL CALLBACK MonitorEnumProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM dwData) {
	DisplayEnumData* cb = (DisplayEnumData*)dwData;
	cb->count++;
	if (cb->search == hMonitor)
		cb->idx = cb->count;
	return TRUE;
}

static int GetMonitorIdx(HMONITOR hMonitor) {
	DisplayEnumData data = { 0, hMonitor, 0 };
	if (EnumDisplayMonitors(NULL, NULL, MonitorEnumProc, (LPARAM)&data))
		return data.idx;
	return 0;
}

static int L_GetCursorPosition(lua_State *L) {
	POINT cursorPos;
	HMONITOR hMonitor;
	MONITORINFO monitorInfo;
	if (GetCursorPos(&cursorPos)) {
		hMonitor = MonitorFromPoint(cursorPos, MONITOR_DEFAULTTONEAREST);
		monitorInfo.cbSize = sizeof(MONITORINFO);
		if (GetMonitorInfo(hMonitor, &monitorInfo)) {
			lua_pushinteger(L, cursorPos.x - monitorInfo.rcMonitor.left);
			lua_pushinteger(L, cursorPos.y - monitorInfo.rcMonitor.top);
			lua_pushinteger(L, GetMonitorIdx(hMonitor));
			return 3;
		}
	}
	lua_pushnil(L);
	return 1;
}

static int L_GetCursorPoint(lua_State *L) {
	POINT point;
	GetCursorPos(&point);
	lua_pushinteger(L, point.x);
	lua_pushinteger(L, point.y);
	return 2;
}

static int L_GetScreenSize(lua_State *L) {
	lua_pushinteger(L, GetSystemMetrics(SM_CXSCREEN));
	lua_pushinteger(L, GetSystemMetrics(SM_CYSCREEN));
	return 2;
}

int luaopen_session(lua_State *L) {
	lua_newtable(L);

	// Session.Console
	lua_newtable(L);
	lua_pushcfunction(L, L_put);                  lua_setfield(L, -2, "Put");
	lua_pushcfunction(L, L_ConsoleWrite);         lua_setfield(L, -2, "Write");
	lua_pushcfunction(L, L_ConsolePrint);         lua_setfield(L, -2, "Print");
	lua_pushcfunction(L, L_ConsoleReadKey);       lua_setfield(L, -2, "ReadKey");
	lua_pushcfunction(L, L_getch);                lua_setfield(L, -2, "GetKey");
	lua_pushcfunction(L, L_kbhit);                lua_setfield(L, -2, "HasKeyDown");
	lua_pushcfunction(L, L_GetKeyState);          lua_setfield(L, -2, "GetKeyState");
	lua_pushcfunction(L, L_SetTextColor);         lua_setfield(L, -2, "SetColor");
	lua_pushcfunction(L, L_GetTextColor);         lua_setfield(L, -2, "GetColor");
	lua_pushcfunction(L, L_ToggleConsole);        lua_setfield(L, -2, "SetVisible");
	lua_pushcfunction(L, L_SetTitle);             lua_setfield(L, -2, "SetTitle");
	lua_pushcfunction(L, L_ConsoleCreate);        lua_setfield(L, -2, "Create");
	lua_pushcfunction(L, L_ConsoleDestroy);       lua_setfield(L, -2, "Destroy");
	lua_pushcfunction(L, L_AttachConsole);        lua_setfield(L, -2, "Attach");
	lua_pushcfunction(L, L_cls);                  lua_setfield(L, -2, "Clear");
	lua_pushcfunction(L, L_GetConsoleCoords);     lua_setfield(L, -2, "GetInfo");
	lua_pushcfunction(L, L_SetConsoleCoords);     lua_setfield(L, -2, "SetCursorPosition");
	lua_setfield(L, -2, "Console");

	// Session.Display
	lua_newtable(L);
	lua_pushcfunction(L, L_GetScreenSize);        lua_setfield(L, -2, "GetScreenSize");
	lua_pushcfunction(L, L_GetCursorPosition);    lua_setfield(L, -2, "GetCursorPosition");
	lua_pushcfunction(L, L_GetCursorPoint);       lua_setfield(L, -2, "GetCursorPoint");
	lua_setfield(L, -2, "Display");

	// Session.Clipboard
	lua_newtable(L);
	lua_pushcfunction(L, lua_SetClipboard);       lua_setfield(L, -2, "Set");
	lua_pushcfunction(L, lua_GetClipboard);       lua_setfield(L, -2, "Get");
	lua_setfield(L, -2, "Clipboard");

	lua_setglobal(L, "Session");
	return 0;
}