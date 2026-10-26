#pragma once
#include "LuaMainInclude.h"
#include "Platform.h"
#include <stdint.h>

static const char* LUATIMESPAN = "TIMESPAN";

// Signed duration stored as a count of 100-nanosecond ticks.
// Matches .NET System.TimeSpan's internal representation exactly.
// Negative values represent durations directed backwards in time.
typedef struct LuaTimeSpan {
	int64_t ticks;
} LuaTimeSpan;

#define TS_TICKS_PER_MILLISECOND  INT64_C(10000)
#define TS_TICKS_PER_SECOND       INT64_C(10000000)
#define TS_TICKS_PER_MINUTE       INT64_C(600000000)
#define TS_TICKS_PER_HOUR         INT64_C(36000000000)
#define TS_TICKS_PER_DAY          INT64_C(864000000000)

LuaTimeSpan* lua_pushtimespan(lua_State* L);
LuaTimeSpan* lua_totimespan(lua_State* L, int index);
int          lua_istimespan(lua_State* L, int index);

// Pushes the canonical [-]D.HH:MM:SS.mmm string representation.
void lua_pushtimespanstring(lua_State* L, int index);

// Constructors
int TimeSpan_Zero(lua_State* L);            // Zero()
int TimeSpan_FromTicks(lua_State* L);       // FromTicks(n)
int TimeSpan_FromSeconds(lua_State* L);     // FromSeconds(n)
int TimeSpan_FromMilliseconds(lua_State* L);// FromMilliseconds(n)
int TimeSpan_FromMinutes(lua_State* L);     // FromMinutes(n)
int TimeSpan_FromHours(lua_State* L);       // FromHours(n)
int TimeSpan_FromDays(lua_State* L);        // FromDays(n)

// Inspection
int TimeSpan_ToString(lua_State* L);       // __tostring / ToString
int TimeSpan_Ticks(lua_State* L);          // Ticks()
int TimeSpan_TotalSeconds(lua_State* L);   // TotalSeconds()
int TimeSpan_TotalMilliseconds(lua_State* L); // TotalMilliseconds()
int TimeSpan_TotalMinutes(lua_State* L);   // TotalMinutes()
int TimeSpan_TotalHours(lua_State* L);     // TotalHours()
int TimeSpan_TotalDays(lua_State* L);      // TotalDays()
int TimeSpan_Days(lua_State* L);           // Days()
int TimeSpan_Hours(lua_State* L);          // Hours()
int TimeSpan_Minutes(lua_State* L);        // Minutes()
int TimeSpan_Seconds(lua_State* L);        // Seconds()
int TimeSpan_Milliseconds(lua_State* L);   // Milliseconds()
int TimeSpan_IsZero(lua_State* L);         // IsZero()
int TimeSpan_IsNegative(lua_State* L);     // IsNegative()
int TimeSpan_Abs(lua_State* L);            // Abs()

// Arithmetic metamethods
int TimeSpan_Add(lua_State* L);   // __add  span+span
int TimeSpan_Sub(lua_State* L);   // __sub  span-span
int TimeSpan_Mul(lua_State* L);   // __mul  span*number or number*span
int TimeSpan_Div(lua_State* L);   // __div  span/number
int TimeSpan_Unm(lua_State* L);   // __unm  -span

// Comparison metamethods
int TimeSpan_Eq(lua_State* L);    // __eq
int TimeSpan_Lt(lua_State* L);    // __lt
int TimeSpan_Le(lua_State* L);    // __le
