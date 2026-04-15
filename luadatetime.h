#pragma once
#include "lua_main_incl.h"
#include "platform.h"
#include <stdint.h>

static const char* LUADATETIME = "DATETIME";

// Ticks: 100-nanosecond intervals since 0001-01-01 00:00:00 UTC
// (matches .NET DateTime/DateTimeOffset epoch).
// offset_minutes: UTC offset in minutes [-840, +840]; 0 = UTC.
typedef struct LuaDateTime {
	int64_t ticks;
	int16_t offset_minutes;
} LuaDateTime;

// Ticks per unit constants.
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
void lua_datetime_push_string(lua_State* L, int index);

// Parses an ISO 8601 / SQL date-time string into *out (UTC ticks + offset).
// Returns 1 on success, 0 on failure. Does not interact with the Lua stack.
int datetime_parse_c(const char* s, LuaDateTime* out);

// Constructors
int datetime_now(lua_State* L);
int datetime_utcnow(lua_State* L);
int datetime_new(lua_State* L);
int datetime_fromunixseconds(lua_State* L);
int datetime_fromunixmilliseconds(lua_State* L);
int datetime_parse(lua_State* L);

// Component getters
int datetime_year(lua_State* L);
int datetime_month(lua_State* L);
int datetime_day(lua_State* L);
int datetime_hour(lua_State* L);
int datetime_minute(lua_State* L);
int datetime_second(lua_State* L);
int datetime_millisecond(lua_State* L);
int datetime_dayofweek(lua_State* L);
int datetime_offsetminutes(lua_State* L);

// Conversion
int datetime_unixseconds(lua_State* L);
int datetime_unixmilliseconds(lua_State* L);
int datetime_toutc(lua_State* L);
int datetime_tolocal(lua_State* L);
int datetime_tooffset(lua_State* L);
int datetime_format(lua_State* L);
int datetime_isempty(lua_State* L);

// Arithmetic
int datetime_adddays(lua_State* L);
int datetime_addhours(lua_State* L);
int datetime_addminutes(lua_State* L);
int datetime_addseconds(lua_State* L);
int datetime_addmilliseconds(lua_State* L);

// Metamethods
int datetime_tostring(lua_State* L);
int datetime_eq(lua_State* L);
int datetime_lt(lua_State* L);
int datetime_le(lua_State* L);
int datetime_sub(lua_State* L);
