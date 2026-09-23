#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "platform.h"
#include "lua_main_incl.h"
#include "luatext.h"
#ifdef _WIN32
#include "Shellapi.h"
#endif
#include "mem.h"

int L_GetMemory(lua_State *L) {
	lua_pop(L, lua_gettop(L));
	// 64-bit so totals above 2 GB don't overflow.
	lua_Integer mem = (lua_Integer)lua_gc(L, LUA_GCCOUNT, 0);
	mem = mem * 1024;
	mem += (lua_Integer)lua_gc(L, LUA_GCCOUNTB, 0);
	lua_pushinteger(L, mem);
	return 1;
}

#ifdef _WIN32
int L_ShellExecute(lua_State *L) {
	// W form so non-ASCII paths, URLs and arguments are passed intact.
	wchar_t* file = kitsune_utf8_to_wide_alloc(luaL_checkstring(L, 1));
	wchar_t* params = kitsune_utf8_to_wide_alloc(luaL_checkstring(L, 2));
	INT_PTR ok = 0;
	if (file && params)
		ok = (INT_PTR)ShellExecuteW(NULL, L"open", file, params, NULL, SW_SHOW);
	kitsune_free(file);
	kitsune_free(params);
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, ok > 32);
	return 1;
}
#endif // _WIN32
