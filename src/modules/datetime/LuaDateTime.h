#pragma once
#include "LuaMainInclude.h"
#include "Platform.h"
#include <stdint.h>

static const char* LUADATETIME = "DATETIME";

// Ticks: 100-nanosecond intervals since 0001-01-01 00:00:00 UTC
// (matches .NET DateTime/DateTimeOffset epoch).
// offsetMinutes: UTC offset in minutes [-840, +840]; 0 = UTC.
typedef struct LuaDateTime {
	int64_t ticks;
	int16_t offsetMinutes;
	uint8_t pad[6];  // pad to 16 bytes; prevents ABI surprises across TUs
} LuaDateTime;

#define DT_TICKS_PER_MILLISECOND  INT64_C(10000)
#define DT_TICKS_PER_SECOND       INT64_C(10000000)
#define DT_TICKS_PER_MINUTE       INT64_C(600000000)
#define DT_TICKS_PER_HOUR         INT64_C(36000000000)
#define DT_TICKS_PER_DAY          INT64_C(864000000000)

// .NET epoch offset: ticks from 0001-01-01 to Unix epoch (1970-01-01)
// = 621355968000000000 ticks
#define DT_UNIX_EPOCH_TICKS       INT64_C(621355968000000000)

LuaDateTime* lua_pushdatetime(lua_State* L);
LuaDateTime* lua_todatetime(lua_State* L, int index);

// Returns non-zero if the value at the given stack index is a LuaDateTime.
int lua_isdatetime(lua_State* L, int index);

// Pushes the ISO 8601 string representation of the LuaDateTime at the given index.
// Caller must ensure the value at index is a LuaDateTime.
void lua_pushdatetimestring(lua_State* L, int index);

// Parses an ISO 8601 / SQL date-time string into *out (UTC ticks + offset).
// Returns 1 on success, 0 on failure. Does not interact with the Lua stack.
int DateTimeParseC(const char* s, LuaDateTime* out);

// Constructors
int DateTime_Now(lua_State* L);
int DateTime_UtcNow(lua_State* L);
int DateTime_New(lua_State* L);
int DateTime_FromUnixSeconds(lua_State* L);
int DateTime_FromUnixMilliseconds(lua_State* L);
int DateTime_Parse(lua_State* L);

// Component getters
int DateTime_Year(lua_State* L);
int DateTime_Month(lua_State* L);
int DateTime_Day(lua_State* L);
int DateTime_Hour(lua_State* L);
int DateTime_Minute(lua_State* L);
int DateTime_Second(lua_State* L);
int DateTime_Millisecond(lua_State* L);
int DateTime_DayOfWeek(lua_State* L);
int DateTime_OffsetMinutes(lua_State* L);

// Conversion
int DateTime_UnixSeconds(lua_State* L);
int DateTime_UnixMilliseconds(lua_State* L);
int DateTime_ToUtc(lua_State* L);
int DateTime_ToLocal(lua_State* L);
int DateTime_ToOffset(lua_State* L);
int DateTime_Format(lua_State* L);
int DateTime_IsEmpty(lua_State* L);

// Arithmetic
int DateTime_AddDays(lua_State* L);
int DateTime_AddHours(lua_State* L);
int DateTime_AddMinutes(lua_State* L);
int DateTime_AddSeconds(lua_State* L);
int DateTime_AddMilliseconds(lua_State* L);
int DateTime_AddTimeSpan(lua_State* L); // __add(DateTime, TimeSpan) -> DateTime

// Metamethods
int DateTime_ToString(lua_State* L);
int DateTime_Eq(lua_State* L);
int DateTime_Lt(lua_State* L);
int DateTime_Le(lua_State* L);
int DateTime_Sub(lua_State* L);
