#pragma once
#include "lua_main_incl.h"
#include <stdint.h>
static const char * TIMER = "Timer";

typedef struct Timer {
	double  PCFreq;        // ns per ms = 1,000,000.0
	int64_t CounterStart;  // steady_clock ns at Start(), 0 when not started
	int64_t CounterStop;   // steady_clock ns at Stop(),  0 when still running
	double  StoredTime;    // accumulated ms from previous Start/Stop cycles
} Timer;

Timer * lua_totimer(lua_State *L, int index);
Timer * luaL_checktimer(lua_State *L, int index);
Timer * lua_pushtimer(lua_State *L);
int TimerNew(lua_State *L);
int TimerIsRunning(lua_State *L);
int TimerReset(lua_State *L);
int TimerStart(lua_State *L);
int TimerStop(lua_State *L);
int TimerGetElapsed(lua_State *L);
int TimerGetElapsedTimeSpan(lua_State *L);
int Timer_gc(lua_State *L);
int Timer_tostring(lua_State *L);
