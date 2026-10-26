#include "timer/LuaTimer.h"
#include "timespan/LuaTimeSpan.h"
#include <string.h>
#include <chrono>

// Single cross-platform time source: steady_clock nanoseconds.
// On Windows this is backed by QueryPerformanceCounter; on Linux by CLOCK_MONOTONIC.
static inline int64_t TimerNowNs() {
	return (int64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()
	).count();
}

static const double TIMER_NS_PER_MS = 1000000.0;

Timer * lua_totimer(lua_State *L, int index) {
	Timer * timer = (Timer*)lua_touserdata(L, index);
	if (timer == NULL)
		luaL_error(L, "paramter is not a %s", TIMER);
	return timer;
}

Timer * luaL_checktimer(lua_State *L, int index) {
	Timer * timer = (Timer*)luaL_checkudata(L, index, TIMER);
	if (timer == NULL)
		luaL_error(L, "paramter is not a %s", TIMER);
	return timer;
}

Timer * lua_pushtimer(lua_State *L) {
	Timer * timer = (Timer*)lua_newuserdata(L, sizeof(Timer));
	if (timer == NULL)
		luaL_error(L, "Unable to create timer");
	luaL_getmetatable(L, TIMER);
	lua_setmetatable(L, -2);
	memset(timer, 0, sizeof(Timer));
	return timer;
}

int Timer_New(lua_State *L) {
	lua_pushtimer(L);
	return 1;
}

int Timer_IsRunning(lua_State *L) {
	Timer * timer = luaL_checktimer(L, 1);
	int started = timer->counterStart > 0 && timer->counterStop <= 0;
	lua_pop(L, 1);
	lua_pushboolean(L, started);
	return 1;
}

int Timer_Reset(lua_State *L) {
	Timer * timer = luaL_checktimer(L, 1);
	timer->counterStart = 0;
	timer->counterStop  = 0;
	timer->storedTime   = 0;
	timer->pcFreq       = 0;
	lua_pop(L, 1);
	return 0;
}

int Timer_Start(lua_State *L) {
	Timer * timer = luaL_checktimer(L, 1);

	if (timer->counterStart > 0) {
		int64_t end = (timer->counterStop > 0) ? timer->counterStop : TimerNowNs();
		timer->storedTime += (double)(end - timer->counterStart) / TIMER_NS_PER_MS;
	}

	timer->pcFreq       = TIMER_NS_PER_MS;
	timer->counterStart = TimerNowNs();
	timer->counterStop  = 0;

	lua_pop(L, 1);
	return 0;
}

int Timer_Stop(lua_State *L) {
	Timer * timer = luaL_checktimer(L, 1);

	if (timer->counterStop <= 0)
		timer->counterStop = TimerNowNs();

	lua_pop(L, 1);
	lua_pushnumber(L, (double)(timer->counterStop - timer->counterStart) / TIMER_NS_PER_MS);
	return 1;
}

int Timer_Elapsed(lua_State *L) {
	Timer * timer = luaL_checktimer(L, 1);

	if (timer->counterStart <= 0) {
		lua_pop(L, 1);
		lua_pushnumber(L, 0);
	}
	else if (timer->counterStop <= 0) {
		lua_pop(L, 1);
		lua_pushnumber(L, timer->storedTime + (double)(TimerNowNs() - timer->counterStart) / TIMER_NS_PER_MS);
	}
	else {
		lua_pop(L, 1);
		lua_pushnumber(L, timer->storedTime + (double)(timer->counterStop - timer->counterStart) / TIMER_NS_PER_MS);
	}
	return 1;
}

int Timer_ElapsedTimeSpan(lua_State *L) {
	Timer * timer = luaL_checktimer(L, 1);
	double elapsedMs;
	if (timer->counterStart <= 0) {
		elapsedMs = 0.0;
	}
	else if (timer->counterStop <= 0) {
		elapsedMs = timer->storedTime + (double)(TimerNowNs() - timer->counterStart) / TIMER_NS_PER_MS;
	}
	else {
		elapsedMs = timer->storedTime + (double)(timer->counterStop - timer->counterStart) / TIMER_NS_PER_MS;
	}
	// 1 ms = 10,000 ticks (100-nanosecond units, same as .NET TimeSpan)
	lua_pop(L, 1);
	lua_pushtimespan(L)->ticks = (int64_t)(elapsedMs * 10000.0);
	return 1;
}

int Timer_GC(lua_State *L) {
	return 0;
}

int Timer_ToString(lua_State *L) {
	char tim[100];
	sprintf(tim, "Timer: 0x%016llX", (unsigned long long)(uintptr_t)lua_totimer(L, 1));
	lua_pushfstring(L, tim);
	return 1;
}