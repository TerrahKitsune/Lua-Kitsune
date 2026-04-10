#pragma once
#include "lua_main_incl.h"

static const char* LUAMUTEX = "LUAMUTEX";

// ── Windows ───────────────────────────────────────────────────────────────────
#ifdef _WIN32
#include <Windows.h>

typedef struct LuaMutex {
	HANDLE mutex;
	char   mutexname[MAX_PATH];
	bool   istaken;
} LuaMutex;

// ── Linux ─────────────────────────────────────────────────────────────────────
#else
#include <semaphore.h>

#define MUTEX_NAME_MAX 256

typedef struct LuaMutex {
	sem_t* sem;
	char   semname[MUTEX_NAME_MAX];    // POSIX name with leading '/' (for sem_open)
	char   mutexname[MUTEX_NAME_MAX];  // original Lua name (returned by Info)
	bool   istaken;
} LuaMutex;

#endif

LuaMutex* lua_pushmutex(lua_State* L);
LuaMutex* lua_tomutex(lua_State* L, int index);

int LuaCreateMutex(lua_State* L);
int LuaLockMutex(lua_State* L);
int LuaUnlockMutex(lua_State* L);
int LuaGetMutexInfo(lua_State* L);

int mutex_gc(lua_State* L);
int mutex_tostring(lua_State* L);
