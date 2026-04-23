#include "luatimespan.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "mem.h"

// ── Userdata helpers ──────────────────────────────────────────────────────────

LuaTimeSpan* lua_pushtimespan(lua_State* L) {
    LuaTimeSpan* ts = (LuaTimeSpan*)lua_newuserdata(L, sizeof(LuaTimeSpan));
    memset(ts, 0, sizeof(LuaTimeSpan));
    luaL_setmetatable(L, LUATIMESPAN);
    return ts;
}

LuaTimeSpan* lua_totimespan(lua_State* L, int index) {
    return (LuaTimeSpan*)luaL_checkudata(L, index, LUATIMESPAN);
}

int lua_istimespan(lua_State* L, int index) {
    if (lua_type(L, index) != LUA_TUSERDATA) return 0;
    if (!lua_getmetatable(L, index)) return 0;
    luaL_getmetatable(L, LUATIMESPAN);
    int result = lua_rawequal(L, -1, -2);
    lua_pop(L, 2);
    return result;
}

// ── Formatting ────────────────────────────────────────────────────────────────
// Produces [-]D.HH:MM:SS.mmm matching .NET TimeSpan.ToString("c") canonical form.

static int timespan_format(int64_t ticks, char* buf, size_t bufsz) {
    int neg = ticks < 0;
    // Use unsigned arithmetic to avoid INT64_MIN UB.
    uint64_t abs_ticks = neg ? (ticks == INT64_MIN ? (uint64_t)INT64_MAX + 1ULL : (uint64_t)(-ticks)) : (uint64_t)ticks;

    uint64_t ms      = abs_ticks / (uint64_t)TS_TICKS_PER_MILLISECOND;
    uint64_t sec_tot = ms / 1000;
    uint64_t ms_part = ms % 1000;
    uint64_t min_tot = sec_tot / 60;
    uint64_t sec     = sec_tot % 60;
    uint64_t hr_tot  = min_tot / 60;
    uint64_t min     = min_tot % 60;
    uint64_t days    = hr_tot / 24;
    uint64_t hr      = hr_tot % 24;

    int n;
    if (days > 0)
        n = snprintf(buf, bufsz, "%s%llu.%02llu:%02llu:%02llu.%03llu",
            neg ? "-" : "",
            (unsigned long long)days,
            (unsigned long long)hr,
            (unsigned long long)min,
            (unsigned long long)sec,
            (unsigned long long)ms_part);
    else
        n = snprintf(buf, bufsz, "%s%02llu:%02llu:%02llu.%03llu",
            neg ? "-" : "",
            (unsigned long long)hr,
            (unsigned long long)min,
            (unsigned long long)sec,
            (unsigned long long)ms_part);
    return n;
}

void lua_timespan_push_string(lua_State* L, int index) {
    LuaTimeSpan* ts = (LuaTimeSpan*)lua_touserdata(L, index);
    char buf[48];
    timespan_format(ts->ticks, buf, sizeof(buf));
    lua_pushstring(L, buf);
}

// ── Constructors ──────────────────────────────────────────────────────────────

int timespan_zero(lua_State* L) {
    lua_pushtimespan(L); // already zeroed
    return 1;
}

int timespan_fromticks(lua_State* L) {
    lua_pushtimespan(L)->ticks = (int64_t)luaL_checkinteger(L, 1);
    return 1;
}

int timespan_fromseconds(lua_State* L) {
    double v = luaL_checknumber(L, 1);
    lua_pushtimespan(L)->ticks = (int64_t)(v * (double)TS_TICKS_PER_SECOND);
    return 1;
}

int timespan_frommilliseconds(lua_State* L) {
    double v = luaL_checknumber(L, 1);
    lua_pushtimespan(L)->ticks = (int64_t)(v * (double)TS_TICKS_PER_MILLISECOND);
    return 1;
}

int timespan_fromminutes(lua_State* L) {
    double v = luaL_checknumber(L, 1);
    lua_pushtimespan(L)->ticks = (int64_t)(v * (double)TS_TICKS_PER_MINUTE);
    return 1;
}

int timespan_fromhours(lua_State* L) {
    double v = luaL_checknumber(L, 1);
    lua_pushtimespan(L)->ticks = (int64_t)(v * (double)TS_TICKS_PER_HOUR);
    return 1;
}

int timespan_fromdays(lua_State* L) {
    double v = luaL_checknumber(L, 1);
    lua_pushtimespan(L)->ticks = (int64_t)(v * (double)TS_TICKS_PER_DAY);
    return 1;
}

// ── Inspection ────────────────────────────────────────────────────────────────

int timespan_tostring(lua_State* L) {
    LuaTimeSpan* ts = lua_totimespan(L, 1);
    char buf[48];
    timespan_format(ts->ticks, buf, sizeof(buf));
    lua_pushstring(L, buf);
    return 1;
}

int timespan_ticks(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)lua_totimespan(L, 1)->ticks);
    return 1;
}

int timespan_totalseconds(lua_State* L) {
    lua_pushnumber(L, (double)lua_totimespan(L, 1)->ticks / (double)TS_TICKS_PER_SECOND);
    return 1;
}

int timespan_totalmilliseconds(lua_State* L) {
    lua_pushnumber(L, (double)lua_totimespan(L, 1)->ticks / (double)TS_TICKS_PER_MILLISECOND);
    return 1;
}

int timespan_totalminutes(lua_State* L) {
    lua_pushnumber(L, (double)lua_totimespan(L, 1)->ticks / (double)TS_TICKS_PER_MINUTE);
    return 1;
}

int timespan_totalhours(lua_State* L) {
    lua_pushnumber(L, (double)lua_totimespan(L, 1)->ticks / (double)TS_TICKS_PER_HOUR);
    return 1;
}

int timespan_totaldays(lua_State* L) {
    lua_pushnumber(L, (double)lua_totimespan(L, 1)->ticks / (double)TS_TICKS_PER_DAY);
    return 1;
}

// Component getters — signed, matching .NET TimeSpan component properties.
int timespan_days(lua_State* L) {
    int64_t t = lua_totimespan(L, 1)->ticks;
    lua_pushinteger(L, (lua_Integer)(t / TS_TICKS_PER_DAY));
    return 1;
}

int timespan_hours(lua_State* L) {
    int64_t t = lua_totimespan(L, 1)->ticks;
    lua_pushinteger(L, (lua_Integer)((t / TS_TICKS_PER_HOUR) % 24));
    return 1;
}

int timespan_minutes(lua_State* L) {
    int64_t t = lua_totimespan(L, 1)->ticks;
    lua_pushinteger(L, (lua_Integer)((t / TS_TICKS_PER_MINUTE) % 60));
    return 1;
}

int timespan_seconds(lua_State* L) {
    int64_t t = lua_totimespan(L, 1)->ticks;
    lua_pushinteger(L, (lua_Integer)((t / TS_TICKS_PER_SECOND) % 60));
    return 1;
}

int timespan_milliseconds(lua_State* L) {
    int64_t t = lua_totimespan(L, 1)->ticks;
    lua_pushinteger(L, (lua_Integer)((t / TS_TICKS_PER_MILLISECOND) % 1000));
    return 1;
}

int timespan_iszero(lua_State* L) {
    lua_pushboolean(L, lua_totimespan(L, 1)->ticks == 0 ? 1 : 0);
    return 1;
}

int timespan_isnegative(lua_State* L) {
    lua_pushboolean(L, lua_totimespan(L, 1)->ticks < 0 ? 1 : 0);
    return 1;
}

int timespan_abs(lua_State* L) {
    int64_t t = lua_totimespan(L, 1)->ticks;
    int64_t abs_t = t < 0 ? (t == INT64_MIN ? INT64_MAX : -t) : t;
    lua_pushtimespan(L)->ticks = abs_t;
    return 1;
}

// ── Arithmetic ────────────────────────────────────────────────────────────────

int timespan_add(lua_State* L) {
    if (!lua_istimespan(L, 1) || !lua_istimespan(L, 2))
        return luaL_error(L, "TimeSpan: __add requires two TimeSpan values");
    lua_pushtimespan(L)->ticks = lua_totimespan(L, 1)->ticks + lua_totimespan(L, 2)->ticks;
    return 1;
}

int timespan_sub(lua_State* L) {
    if (!lua_istimespan(L, 1) || !lua_istimespan(L, 2))
        return luaL_error(L, "TimeSpan: __sub requires two TimeSpan values");
    lua_pushtimespan(L)->ticks = lua_totimespan(L, 1)->ticks - lua_totimespan(L, 2)->ticks;
    return 1;
}

int timespan_mul(lua_State* L) {
    // Supports span * number and number * span.
    if (lua_istimespan(L, 1) && lua_type(L, 2) == LUA_TNUMBER) {
        double factor = lua_tonumber(L, 2);
        lua_pushtimespan(L)->ticks = (int64_t)((double)lua_totimespan(L, 1)->ticks * factor);
        return 1;
    }
    if (lua_type(L, 1) == LUA_TNUMBER && lua_istimespan(L, 2)) {
        double factor = lua_tonumber(L, 1);
        lua_pushtimespan(L)->ticks = (int64_t)(factor * (double)lua_totimespan(L, 2)->ticks);
        return 1;
    }
    return luaL_error(L, "TimeSpan: __mul requires TimeSpan and a number");
}

int timespan_div(lua_State* L) {
    if (!lua_istimespan(L, 1) || lua_type(L, 2) != LUA_TNUMBER)
        return luaL_error(L, "TimeSpan: __div requires TimeSpan / number");
    double divisor = lua_tonumber(L, 2);
    if (divisor == 0.0)
        return luaL_error(L, "TimeSpan: division by zero");
    lua_pushtimespan(L)->ticks = (int64_t)((double)lua_totimespan(L, 1)->ticks / divisor);
    return 1;
}

int timespan_unm(lua_State* L) {
    int64_t t = lua_totimespan(L, 1)->ticks;
    lua_pushtimespan(L)->ticks = (t == INT64_MIN) ? INT64_MAX : -t;
    return 1;
}

// ── Comparison ────────────────────────────────────────────────────────────────

int timespan_eq(lua_State* L) {
    if (!lua_istimespan(L, 1) || !lua_istimespan(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, lua_totimespan(L, 1)->ticks == lua_totimespan(L, 2)->ticks ? 1 : 0);
    return 1;
}

int timespan_lt(lua_State* L) {
    if (!lua_istimespan(L, 1) || !lua_istimespan(L, 2))
        return luaL_error(L, "TimeSpan: cannot compare incompatible types");
    lua_pushboolean(L, lua_totimespan(L, 1)->ticks < lua_totimespan(L, 2)->ticks ? 1 : 0);
    return 1;
}

int timespan_le(lua_State* L) {
    if (!lua_istimespan(L, 1) || !lua_istimespan(L, 2))
        return luaL_error(L, "TimeSpan: cannot compare incompatible types");
    lua_pushboolean(L, lua_totimespan(L, 1)->ticks <= lua_totimespan(L, 2)->ticks ? 1 : 0);
    return 1;
}
