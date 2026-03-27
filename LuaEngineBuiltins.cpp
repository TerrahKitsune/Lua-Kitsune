#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <Windows.h>
#include "lua_main_incl.h"
#include <conio.h>
#include <io.h>
#include "Shellapi.h"
#include "mem.h"

#define HI_PART(x)  ((x>>4) & 0x0F)
#define LO_PART(x)  ((x) & 0x0F)

int L_kbhit(lua_State *L) {
	if (_isatty(_fileno(stdin))) {
		lua_pushboolean(L, _kbhit());
	} else if (feof(stdin)) {
		lua_pushboolean(L, 1);
	} else {
		lua_pushboolean(L, 0);
	}
	return 1;
}

int L_getch(lua_State *L) {
	if (_isatty(_fileno(stdin))) {
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
	if (hStdOut == INVALID_HANDLE_VALUE) return 0;
	if (!GetConsoleScreenBufferInfo(hStdOut, &csbi)) return 0;
	cellCount = csbi.dwSize.X * csbi.dwSize.Y;
	if (!FillConsoleOutputCharacter(hStdOut, (TCHAR)' ', cellCount, homeCoords, &count)) return 0;
	if (!FillConsoleOutputAttribute(hStdOut, csbi.wAttributes, cellCount, homeCoords, &count)) return 0;
	SetConsoleCursorPosition(hStdOut, homeCoords);
	return 0;
}

int L_put(lua_State *L) {
	size_t len;
	const char* text = luaL_tolstring(L, 1, &len);
	if (len > 0) {
		for (unsigned int n = 0; n < len; n++) {
			if (text[n] == 13) printf("\n");
			else if (text[n] == 8) printf("\b \b");
			else printf("%c", text[n]);
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
	if (toggle) ShowWindow(console, SW_RESTORE);
	else ShowWindow(console, SW_HIDE);
	lua_pop(L, 1);
	return 0;
}

int L_SetTitle(lua_State *L) {
	SetConsoleTitle(luaL_checkstring(L, 1));
	lua_pop(L, 1);
	return 0;
}
