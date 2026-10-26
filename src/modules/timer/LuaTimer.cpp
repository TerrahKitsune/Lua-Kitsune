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
	int started = timer->CounterStart > 0 && timer->CounterStop <= 0;
	lua_pop(L, 1);
	lua_pushboolean(L, started);
	return 1;
}

int Timer_Reset(lua_State *L) {
	Timer * timer = luaL_checktimer(L, 1);
	timer->CounterStart = 0;
	timer->CounterStop  = 0;
	timer->StoredTime   = 0;
	timer->PCFreq       = 0;
	lua_pop(L, 1);
	return 0;
}

int Timer_Start(lua_State *L) {
	Timer * timer = luaL_checktimer(L, 1);

	if (timer->CounterStart > 0) {
		int64_t end = (timer->CounterStop > 0) ? timer->CounterStop : TimerNowNs();
		timer->StoredTime += (double)(end - timer->CounterStart) / TIMER_NS_PER_MS;
	}

	timer->PCFreq       = TIMER_NS_PER_MS;
	timer->CounterStart = TimerNowNs();
	timer->CounterStop  = 0;

	lua_pop(L, 1);
	return 0;
}

int Timer_Stop(lua_State *L) {
	Timer * timer = luaL_checktimer(L, 1);

	if (timer->CounterStop <= 0)
		timer->CounterStop = TimerNowNs();

	lua_pop(L, 1);
	lua_pushnumber(L, (double)(timer->CounterStop - timer->CounterStart) / TIMER_NS_PER_MS);
	return 1;
}

int Timer_Elapsed(lua_State *L) {
	Timer * timer = luaL_checktimer(L, 1);

	if (timer->CounterStart <= 0) {
		lua_pop(L, 1);
		lua_pushnumber(L, 0);
	}
	else if (timer->CounterStop <= 0) {
		lua_pop(L, 1);
		lua_pushnumber(L, timer->StoredTime + (double)(TimerNowNs() - timer->CounterStart) / TIMER_NS_PER_MS);
	}
	else {
		lua_pop(L, 1);
		lua_pushnumber(L, timer->StoredTime + (double)(timer->CounterStop - timer->CounterStart) / TIMER_NS_PER_MS);
	}
	return 1;
}

int Timer_ElapsedTimeSpan(lua_State *L) {
	Timer * timer = luaL_checktimer(L, 1);
	double elapsed_ms;
	if (timer->CounterStart <= 0) {
		elapsed_ms = 0.0;
	}
	else if (timer->CounterStop <= 0) {
		elapsed_ms = timer->StoredTime + (double)(TimerNowNs() - timer->CounterStart) / TIMER_NS_PER_MS;
	}
	else {
		elapsed_ms = timer->StoredTime + (double)(timer->CounterStop - timer->CounterStart) / TIMER_NS_PER_MS;
	}
	// 1 ms = 10,000 ticks (100-nanosecond units, same as .NET TimeSpan)
	lua_pop(L, 1);
	lua_pushtimespan(L)->ticks = (int64_t)(elapsed_ms * 10000.0);
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