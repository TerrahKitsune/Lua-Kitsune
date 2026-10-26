#include "timespan/LuaTimeSpan.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "Mem.h"

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

static int TimeSpanFormat(int64_t ticks, char* buf, size_t bufsz) {
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

void lua_pushtimespanstring(lua_State* L, int index) {
    LuaTimeSpan* ts = (LuaTimeSpan*)lua_touserdata(L, index);
    char buf[48];
    TimeSpanFormat(ts->ticks, buf, sizeof(buf));
    lua_pushstring(L, buf);
}

// ── Constructors ──────────────────────────────────────────────────────────────

int TimeSpan_Zero(lua_State* L) {
    lua_pushtimespan(L); // already zeroed
    return 1;
}

int TimeSpan_FromTicks(lua_State* L) {
    lua_pushtimespan(L)->ticks = (int64_t)luaL_checkinteger(L, 1);
    return 1;
}

int TimeSpan_FromSeconds(lua_State* L) {
    double v = luaL_checknumber(L, 1);
    lua_pushtimespan(L)->ticks = (int64_t)(v * (double)TS_TICKS_PER_SECOND);
    return 1;
}

int TimeSpan_FromMilliseconds(lua_State* L) {
    double v = luaL_checknumber(L, 1);
    lua_pushtimespan(L)->ticks = (int64_t)(v * (double)TS_TICKS_PER_MILLISECOND);
    return 1;
}

int TimeSpan_FromMinutes(lua_State* L) {
    double v = luaL_checknumber(L, 1);
    lua_pushtimespan(L)->ticks = (int64_t)(v * (double)TS_TICKS_PER_MINUTE);
    return 1;
}

int TimeSpan_FromHours(lua_State* L) {
    double v = luaL_checknumber(L, 1);
    lua_pushtimespan(L)->ticks = (int64_t)(v * (double)TS_TICKS_PER_HOUR);
    return 1;
}

int TimeSpan_FromDays(lua_State* L) {
    double v = luaL_checknumber(L, 1);
    lua_pushtimespan(L)->ticks = (int64_t)(v * (double)TS_TICKS_PER_DAY);
    return 1;
}

// ── Inspection ────────────────────────────────────────────────────────────────

int TimeSpan_ToString(lua_State* L) {
    LuaTimeSpan* ts = lua_totimespan(L, 1);
    char buf[48];
    TimeSpanFormat(ts->ticks, buf, sizeof(buf));
    lua_pushstring(L, buf);
    return 1;
}

int TimeSpan_Ticks(lua_State* L) {
    lua_pushinteger(L, (lua_Integer)lua_totimespan(L, 1)->ticks);
    return 1;
}

int TimeSpan_TotalSeconds(lua_State* L) {
    lua_pushnumber(L, (double)lua_totimespan(L, 1)->ticks / (double)TS_TICKS_PER_SECOND);
    return 1;
}

int TimeSpan_TotalMilliseconds(lua_State* L) {
    lua_pushnumber(L, (double)lua_totimespan(L, 1)->ticks / (double)TS_TICKS_PER_MILLISECOND);
    return 1;
}

int TimeSpan_TotalMinutes(lua_State* L) {
    lua_pushnumber(L, (double)lua_totimespan(L, 1)->ticks / (double)TS_TICKS_PER_MINUTE);
    return 1;
}

int TimeSpan_TotalHours(lua_State* L) {
    lua_pushnumber(L, (double)lua_totimespan(L, 1)->ticks / (double)TS_TICKS_PER_HOUR);
    return 1;
}

int TimeSpan_TotalDays(lua_State* L) {
    lua_pushnumber(L, (double)lua_totimespan(L, 1)->ticks / (double)TS_TICKS_PER_DAY);
    return 1;
}

// Component getters — signed, matching .NET TimeSpan component properties.
int TimeSpan_Days(lua_State* L) {
    int64_t t = lua_totimespan(L, 1)->ticks;
    lua_pushinteger(L, (lua_Integer)(t / TS_TICKS_PER_DAY));
    return 1;
}

int TimeSpan_Hours(lua_State* L) {
    int64_t t = lua_totimespan(L, 1)->ticks;
    lua_pushinteger(L, (lua_Integer)((t / TS_TICKS_PER_HOUR) % 24));
    return 1;
}

int TimeSpan_Minutes(lua_State* L) {
    int64_t t = lua_totimespan(L, 1)->ticks;
    lua_pushinteger(L, (lua_Integer)((t / TS_TICKS_PER_MINUTE) % 60));
    return 1;
}

int TimeSpan_Seconds(lua_State* L) {
    int64_t t = lua_totimespan(L, 1)->ticks;
    lua_pushinteger(L, (lua_Integer)((t / TS_TICKS_PER_SECOND) % 60));
    return 1;
}

int TimeSpan_Milliseconds(lua_State* L) {
    int64_t t = lua_totimespan(L, 1)->ticks;
    lua_pushinteger(L, (lua_Integer)((t / TS_TICKS_PER_MILLISECOND) % 1000));
    return 1;
}

int TimeSpan_IsZero(lua_State* L) {
    lua_pushboolean(L, lua_totimespan(L, 1)->ticks == 0 ? 1 : 0);
    return 1;
}

int TimeSpan_IsNegative(lua_State* L) {
    lua_pushboolean(L, lua_totimespan(L, 1)->ticks < 0 ? 1 : 0);
    return 1;
}

int TimeSpan_Abs(lua_State* L) {
    int64_t t = lua_totimespan(L, 1)->ticks;
    int64_t abs_t = t < 0 ? (t == INT64_MIN ? INT64_MAX : -t) : t;
    lua_pushtimespan(L)->ticks = abs_t;
    return 1;
}

// ── Arithmetic ────────────────────────────────────────────────────────────────

int TimeSpan_Add(lua_State* L) {
    if (!lua_istimespan(L, 1) || !lua_istimespan(L, 2))
        return luaL_error(L, "TimeSpan: __add requires two TimeSpan values");
    lua_pushtimespan(L)->ticks = lua_totimespan(L, 1)->ticks + lua_totimespan(L, 2)->ticks;
    return 1;
}

int TimeSpan_Sub(lua_State* L) {
    if (!lua_istimespan(L, 1) || !lua_istimespan(L, 2))
        return luaL_error(L, "TimeSpan: __sub requires two TimeSpan values");
    lua_pushtimespan(L)->ticks = lua_totimespan(L, 1)->ticks - lua_totimespan(L, 2)->ticks;
    return 1;
}

int TimeSpan_Mul(lua_State* L) {
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

int TimeSpan_Div(lua_State* L) {
    if (!lua_istimespan(L, 1) || lua_type(L, 2) != LUA_TNUMBER)
        return luaL_error(L, "TimeSpan: __div requires TimeSpan / number");
    double divisor = lua_tonumber(L, 2);
    if (divisor == 0.0)
        return luaL_error(L, "TimeSpan: division by zero");
    lua_pushtimespan(L)->ticks = (int64_t)((double)lua_totimespan(L, 1)->ticks / divisor);
    return 1;
}

int TimeSpan_Unm(lua_State* L) {
    int64_t t = lua_totimespan(L, 1)->ticks;
    lua_pushtimespan(L)->ticks = (t == INT64_MIN) ? INT64_MAX : -t;
    return 1;
}

// ── Comparison ────────────────────────────────────────────────────────────────

int TimeSpan_Eq(lua_State* L) {
    if (!lua_istimespan(L, 1) || !lua_istimespan(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, lua_totimespan(L, 1)->ticks == lua_totimespan(L, 2)->ticks ? 1 : 0);
    return 1;
}

int TimeSpan_Lt(lua_State* L) {
    if (!lua_istimespan(L, 1) || !lua_istimespan(L, 2))
        return luaL_error(L, "TimeSpan: cannot compare incompatible types");
    lua_pushboolean(L, lua_totimespan(L, 1)->ticks < lua_totimespan(L, 2)->ticks ? 1 : 0);
    return 1;
}

int TimeSpan_Le(lua_State* L) {
    if (!lua_istimespan(L, 1) || !lua_istimespan(L, 2))
        return luaL_error(L, "TimeSpan: cannot compare incompatible types");
    lua_pushboolean(L, lua_totimespan(L, 1)->ticks <= lua_totimespan(L, 2)->ticks ? 1 : 0);
    return 1;
}
