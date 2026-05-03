#pragma once
#include "xp_lua_incl.h"
#include "Kitsune/KitsuneLuaDebug.h"
#include "mem.h"
#ifndef _WIN32
#include <stdint.h>
#endif

#define MIN(a,b) (((a)<(b))?(a):(b))
#define MAX(a,b) (((a)>(b))?(a):(b))

// Drop-in replacements for lua_call / lua_pcall that suppress the scheduler
// Ticker hook for the duration of the call.  Both lua_call and lua_pcall use
// luaD_callnoyield internally (non-yieldable boundary), so if the Ticker fires
// inside the called function and attempts lua_yield it raises "attempt to yield
// across a C-call boundary".  Suppressing the hook prevents that error whenever
// a user-supplied Lua callback is invoked from within a C processing loop.
// The hook is always restored before returning or re-raising, so a Lua-level
// pcall/xpcall catching the error cannot leave the coroutine hookless.
static inline void lua_call_nohook(lua_State* L, int nargs, int nresults) {
	lua_Hook savedHook  = kitsune_gethook(L);
	int      savedMask  = kitsune_gethookmask(L);
	int      savedCount = kitsune_gethookcount(L);
	kitsune_sethook(L, NULL, 0, 0);
	int rc = lua_pcall(L, nargs, nresults, 0);
	kitsune_sethook(L, savedHook, savedMask, savedCount);
	if (rc != LUA_OK)
		lua_error(L);
}

static inline int lua_pcall_nohook(lua_State* L, int nargs, int nresults, int msgh) {
	lua_Hook savedHook  = kitsune_gethook(L);
	int      savedMask  = kitsune_gethookmask(L);
	int      savedCount = kitsune_gethookcount(L);
	kitsune_sethook(L, NULL, 0, 0);
	int rc = lua_pcall(L, nargs, nresults, msgh);
	kitsune_sethook(L, savedHook, savedMask, savedCount);
	return rc;
}

static void DumpStack(lua_State* L, bool untilnil = false) {
	size_t len;
	const char* str;
	FILE * file = fopen("STACK.txt", "w");
	if (!file)
		return;

	puts("--------------");

	for (int n = 1; n <= lua_gettop(L); n++){

		if (untilnil && lua_isnil(L, n))
			return;

		fprintf(file, "%d: ", n);
		printf("%d: ", n);		

		switch (lua_type(L, n)){
		case LUA_TNIL:
			fprintf(file, "NIL");
			printf("NIL");
			break;
		case LUA_TNUMBER:
			fprintf(file, "NUMBER %f", lua_tonumber(L, n));
			printf("NUMBER %f", lua_tonumber(L, n));
			break;
		case LUA_TBOOLEAN:
			fprintf(file, "BOOLEAN %s", lua_toboolean(L, n) == 0 ? "FALSE" : "TRUE");
			printf("BOOLEAN %s", lua_toboolean(L, n) == 0 ? "FALSE" : "TRUE");
			break;
		case LUA_TSTRING:
			fprintf(file, "STRING %s", lua_tostring(L, n));
			printf("STRING %s", lua_tostring(L, n));
			break;
		case LUA_TTABLE:
			fprintf(file, "TABLE 0x%016llX", (unsigned long long)(uintptr_t)lua_topointer(L, n));
			printf("TABLE 0x%016llX", (unsigned long long)(uintptr_t)lua_topointer(L, n));
			break;
		case LUA_TFUNCTION:
			fprintf(file, "FUNCTION 0x%016llX", (unsigned long long)(uintptr_t)lua_topointer(L, n));
			printf("FUNCTION 0x%016llX", (unsigned long long)(uintptr_t)lua_topointer(L, n));
			break;
		case LUA_TUSERDATA:

			str = luaL_tolstring(L, n, &len);
			lua_pop(L, 1);
			fprintf(file, "USERDATA 0x%016llX", (unsigned long long)(uintptr_t)str);
			printf("USERDATA 0x%016llX", (unsigned long long)(uintptr_t)str);
			break;
		case LUA_TTHREAD:
			fprintf(file, "THREAD 0x%016llX", (unsigned long long)(uintptr_t)lua_topointer(L, n));
			printf("THREAD 0x%016llX", (unsigned long long)(uintptr_t)lua_topointer(L, n));
			break;
		case LUA_TLIGHTUSERDATA:
			fprintf(file, "LIGHTUSERDATA 0x%016llX", (unsigned long long)(uintptr_t)lua_topointer(L, n));
			printf("LIGHTUSERDATA 0x%016llX", (unsigned long long)(uintptr_t)lua_topointer(L, n));
			break;
		}

		fprintf(file, "\n");
		printf("\n");
	}

	fflush(file);
	fclose(file);
}