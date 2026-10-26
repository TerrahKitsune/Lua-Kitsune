#pragma once
#include "LuaMainInclude.h"
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
int Timer_New(lua_State *L);
int Timer_IsRunning(lua_State *L);
int Timer_Reset(lua_State *L);
int Timer_Start(lua_State *L);
int Timer_Stop(lua_State *L);
int Timer_Elapsed(lua_State *L);
int Timer_ElapsedTimeSpan(lua_State *L);
int Timer_GC(lua_State *L);
int Timer_ToString(lua_State *L);
