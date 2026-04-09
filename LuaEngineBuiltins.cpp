#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "platform.h"
#include "lua_main_incl.h"
#ifdef _WIN32
#include <conio.h>
#include <io.h>
#include "Shellapi.h"
#endif
#include "mem.h"
#include "luawchar.h"

#define HI_PART(x)  ((x>>4) & 0x0F)
#define LO_PART(x)  ((x) & 0x0F)

// _isatty/_fileno are Windows CRT names; POSIX uses isatty/fileno without the underscore.
#ifdef _WIN32
static inline bool is_stdin_tty() { return _isatty(_fileno(stdin)) != 0; }
#else
#include <unistd.h>
#include <sys/ioctl.h>
static inline bool is_stdin_tty() { return isatty(fileno(stdin)) != 0; }
#endif

int L_kbhit(lua_State *L) {
	if (is_stdin_tty()) {
#ifdef _WIN32
		lua_pushboolean(L, _kbhit());
#else
		lua_pushboolean(L, 0);
#endif
	} else if (feof(stdin)) {
		lua_pushboolean(L, 1);
	} else {
		lua_pushboolean(L, 0);
	}
	return 1;
}

int L_getch(lua_State *L) {
	if (is_stdin_tty()) {
#ifdef _WIN32
		lua_pushinteger(L, _getch());
#else
		lua_pushinteger(L, -1);
#endif
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

#ifdef _WIN32
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
#endif // _WIN32

int L_GetMemory(lua_State *L) {
	lua_pop(L, lua_gettop(L));
	int mem = lua_gc(L, LUA_GCCOUNT, 0);
	mem = mem * 1024;
	mem += lua_gc(L, LUA_GCCOUNTB, 0);
	lua_pushinteger(L, mem);
	return 1;
}

#ifdef _WIN32
int L_ShellExecute(lua_State *L) {
	INT_PTR ok = (INT_PTR)ShellExecute(NULL, "open", luaL_checkstring(L, 1), luaL_checkstring(L, 2), NULL, SW_SHOW);
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, ok > 32);
	return 1;
}
#endif // _WIN32
