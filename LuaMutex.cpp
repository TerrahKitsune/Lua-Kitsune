#define _WIN32_WINNT 0x0500
#include "LuaMutex.h"
#include <string.h>
#include <stdlib.h>

// Windows implementation
#ifdef _WIN32

#include <windows.h>
#include <sddl.h>

int LuaCreateMutex(lua_State* L) {

	const char* name = luaL_checkstring(L, 1);
	HANDLE mutex = CreateMutex(NULL, false, name);
	lua_pop(L, lua_gettop(L));

	if (mutex == NULL) {
		lua_pushnil(L);
		lua_pushinteger(L, GetLastError());
		return 2;
	}

	LuaMutex* luamutex = lua_pushmutex(L);
	memcpy(luamutex->mutexname, name, MAX_PATH);
	luamutex->mutex = mutex;
	return 1;
}
int LuaLockMutex(lua_State* L) {

	LuaMutex* mutex = lua_tomutex(L, 1);
	lua_Integer timeout = luaL_optinteger(L, 2, -1);

	if (mutex->istaken) {
		lua_pop(L, lua_gettop(L));
		lua_pushboolean(L, mutex->istaken);
		return 1;
	}

	DWORD result = WaitForSingleObject(mutex->mutex, (DWORD)timeout);

	if (result == WAIT_ABANDONED) {
		mutex->istaken = false;
		result = ReleaseMutex(mutex->mutex);
		if (!result) {
			lua_pop(L, lua_gettop(L));
			lua_pushboolean(L, mutex->istaken);
			return 1;
		}
		result = WaitForSingleObject(mutex->mutex, (DWORD)timeout);
	}

	mutex->istaken = result == WAIT_OBJECT_0;
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, mutex->istaken);
	return 1;
}

int LuaUnlockMutex(lua_State* L) {

	LuaMutex* mutex = lua_tomutex(L, 1);

	if (!mutex->istaken) {
		lua_pop(L, lua_gettop(L));
		return 0;
	}

	ReleaseMutex(mutex->mutex);
	mutex->istaken = false;
	lua_pop(L, lua_gettop(L));
	return 0;
}

int LuaGetMutexInfo(lua_State* L) {

	LuaMutex* mutex = lua_tomutex(L, 1);
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, mutex->istaken);
	lua_pushstring(L, mutex->mutexname);
	lua_pushinteger(L, (lua_Integer)(uintptr_t)mutex->mutex);
	return 3;
}

int mutex_gc(lua_State* L) {

	LuaMutex* mutex = lua_tomutex(L, 1);

	if (mutex->mutex) {
		if (mutex->istaken)
			ReleaseMutex(mutex->mutex);
		CloseHandle(mutex->mutex);
	}

	memset(mutex, 0, sizeof(LuaMutex));
	return 0;
}

int mutex_tostring(lua_State* L) {

	LuaMutex* mutex = lua_tomutex(L, 1);
	lua_pushfstring(L, "Mutex: %p %s", (void*)mutex, mutex->mutexname);
	return 1;
}

// Linux implementation
#else

#include <fcntl.h>
#include <errno.h>
#include <time.h>

static void sem_normalize(const char* src, char* semname, char* mutexname, size_t n) {

	strncpy(mutexname, src, n - 1);
	mutexname[n - 1] = 0;

	if (src[0] == '/') {
		strncpy(semname, src, n - 1);
		semname[n - 1] = 0;
	}
	else {
		semname[0] = '/';
		strncpy(semname + 1, src, n - 2);
		semname[n - 1] = 0;
	}
}

int LuaCreateMutex(lua_State* L) {

	const char* name = luaL_checkstring(L, 1);
	lua_pop(L, lua_gettop(L));

	LuaMutex* m = lua_pushmutex(L);
	sem_normalize(name, m->semname, m->mutexname, MUTEX_NAME_MAX);

	m->sem = sem_open(m->semname, O_CREAT, 0666, 1);
	if (m->sem == SEM_FAILED) {
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushinteger(L, errno);
		return 2;
	}

	m->istaken = false;
	return 1;
}

int LuaLockMutex(lua_State* L) {

	LuaMutex* m = lua_tomutex(L, 1);
	lua_Integer timeout_ms = luaL_optinteger(L, 2, -1);
	lua_pop(L, lua_gettop(L));

	if (m->istaken) {
		lua_pushboolean(L, true);
		return 1;
	}

	int result;
	if (timeout_ms < 0) {
		result = sem_wait(m->sem);
	}
	else if (timeout_ms == 0) {
		result = sem_trywait(m->sem);
	}
	else {
		struct timespec ts;
		clock_gettime(CLOCK_REALTIME, &ts);
		ts.tv_sec  += timeout_ms / 1000;
		ts.tv_nsec += (timeout_ms % 1000) * 1000000LL;
		if (ts.tv_nsec >= 1000000000LL) {
			ts.tv_sec++;
			ts.tv_nsec -= 1000000000LL;
		}
		result = sem_timedwait(m->sem, &ts);
	}

	m->istaken = (result == 0);
	lua_pushboolean(L, m->istaken);
	return 1;
}

int LuaUnlockMutex(lua_State* L) {

	LuaMutex* m = lua_tomutex(L, 1);
	lua_pop(L, lua_gettop(L));

	if (m->istaken) {
		sem_post(m->sem);
		m->istaken = false;
	}

	return 0;
}

int LuaGetMutexInfo(lua_State* L) {

	LuaMutex* m = lua_tomutex(L, 1);
	lua_pop(L, lua_gettop(L));
	lua_pushboolean(L, m->istaken);
	lua_pushstring(L, m->mutexname);
	lua_pushinteger(L, 0);
	return 3;
}

int mutex_gc(lua_State* L) {

	LuaMutex* m = lua_tomutex(L, 1);

	if (m->sem && m->sem != SEM_FAILED) {
		if (m->istaken)
			sem_post(m->sem);
		sem_close(m->sem);
	}

	memset(m, 0, sizeof(LuaMutex));
	return 0;
}

int mutex_tostring(lua_State* L) {

	LuaMutex* m = lua_tomutex(L, 1);
	lua_pushfstring(L, "Mutex: %p %s", (void*)m, m->mutexname);
	return 1;
}

#endif

// Shared helpers

LuaMutex* lua_pushmutex(lua_State* L) {

	LuaMutex* mutex = (LuaMutex*)lua_newuserdata(L, sizeof(LuaMutex));
	if (mutex == NULL) {
		luaL_error(L, "Unable to push mutex");
		return NULL;
	}
	luaL_getmetatable(L, LUAMUTEX);
	lua_setmetatable(L, -2);
	memset(mutex, 0, sizeof(LuaMutex));
	return mutex;
}

LuaMutex* lua_tomutex(lua_State* L, int index) {

	LuaMutex* mutex = (LuaMutex*)luaL_checkudata(L, index, LUAMUTEX);
	if (mutex == NULL) {
		luaL_error(L, "parameter is not a %s", LUAMUTEX);
		return NULL;
	}
	return mutex;
}

