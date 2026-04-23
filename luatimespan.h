#pragma once
#include "lua_main_incl.h"
#include "platform.h"
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
void lua_timespan_push_string(lua_State* L, int index);

// Constructors
int timespan_zero(lua_State* L);            // Zero()
int timespan_fromticks(lua_State* L);       // FromTicks(n)
int timespan_fromseconds(lua_State* L);     // FromSeconds(n)
int timespan_frommilliseconds(lua_State* L);// FromMilliseconds(n)
int timespan_fromminutes(lua_State* L);     // FromMinutes(n)
int timespan_fromhours(lua_State* L);       // FromHours(n)
int timespan_fromdays(lua_State* L);        // FromDays(n)

// Inspection
int timespan_tostring(lua_State* L);       // __tostring / ToString
int timespan_ticks(lua_State* L);          // Ticks()
int timespan_totalseconds(lua_State* L);   // TotalSeconds()
int timespan_totalmilliseconds(lua_State* L); // TotalMilliseconds()
int timespan_totalminutes(lua_State* L);   // TotalMinutes()
int timespan_totalhours(lua_State* L);     // TotalHours()
int timespan_totaldays(lua_State* L);      // TotalDays()
int timespan_days(lua_State* L);           // Days()
int timespan_hours(lua_State* L);          // Hours()
int timespan_minutes(lua_State* L);        // Minutes()
int timespan_seconds(lua_State* L);        // Seconds()
int timespan_milliseconds(lua_State* L);   // Milliseconds()
int timespan_iszero(lua_State* L);         // IsZero()
int timespan_isnegative(lua_State* L);     // IsNegative()
int timespan_abs(lua_State* L);            // Abs()

// Arithmetic metamethods
int timespan_add(lua_State* L);   // __add  span+span
int timespan_sub(lua_State* L);   // __sub  span-span
int timespan_mul(lua_State* L);   // __mul  span*number or number*span
int timespan_div(lua_State* L);   // __div  span/number
int timespan_unm(lua_State* L);   // __unm  -span

// Comparison metamethods
int timespan_eq(lua_State* L);    // __eq
int timespan_lt(lua_State* L);    // __lt
int timespan_le(lua_State* L);    // __le
