#pragma once
#include "LuaMainInclude.h"

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

int Mutex_Open(lua_State* L);
int Mutex_Lock(lua_State* L);
int Mutex_Unlock(lua_State* L);
int Mutex_Info(lua_State* L);

int Mutex_GC(lua_State* L);
int Mutex_ToString(lua_State* L);
