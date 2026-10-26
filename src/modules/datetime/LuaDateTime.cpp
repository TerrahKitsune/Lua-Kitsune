#include "datetime/LuaDateTime.h"
#include "timespan/LuaTimeSpan.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "Mem.h"
#ifdef _WIN32
#include <Windows.h>
#endif

LuaDateTime* lua_pushdatetime(lua_State* L) {
	LuaDateTime* dt = (LuaDateTime*)lua_newuserdata(L, sizeof(LuaDateTime));
	memset(dt, 0, sizeof(LuaDateTime));
	luaL_setmetatable(L, LUADATETIME);
	return dt;
}

LuaDateTime* lua_todatetime(lua_State* L, int index) {
	return (LuaDateTime*)luaL_checkudata(L, index, LUADATETIME);
}

int lua_isdatetime(lua_State* L, int index) {
	if (lua_type(L, index) != LUA_TUSERDATA) return 0;
	if (!lua_getmetatable(L, index)) return 0;
	luaL_getmetatable(L, LUADATETIME);
	int result = lua_rawequal(L, -1, -2);
	lua_pop(L, 2);
	return result;
}

// Days per month (non-leap / leap).
static const int daysInMonth[2][13] = {
	{ 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

// Cumulative days from Jan 1 to start of month (non-leap / leap).
static const int daysBeforeMonth[2][14] = {
	{ 0, 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
	{ 0, 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
};

static int IsLeap(int y) {
	return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

// Days from 0001-01-01 to start of year y (1-based).
static int64_t DaysBeforeYear(int y) {
	int n = y - 1;
	return (int64_t)n * 365 + n / 4 - n / 100 + n / 400;
}

typedef struct {
	int year, month, day, hour, minute, second, millisecond;
} DtFields;

// Decompose ticks (UTC absolute) into calendar fields.
static void TicksToFields(int64_t ticks, DtFields* f) {
	int64_t totalDays = ticks / DT_TICKS_PER_DAY;
	int64_t timeTicks = ticks % DT_TICKS_PER_DAY;
	if (timeTicks < 0) {
		timeTicks += DT_TICKS_PER_DAY;
		totalDays--;
	}

	// 400-year blocks.
	int64_t n400 = totalDays / 146097;
	totalDays -= n400 * 146097;
	int n100 = (int)(totalDays / 36524);
	if (n100 == 4) n100 = 3;
	totalDays -= n100 * 36524;
	int n4 = (int)(totalDays / 1461);
	totalDays -= n4 * 1461;
	int n1 = (int)(totalDays / 365);
	if (n1 == 4) n1 = 3;
	totalDays -= n1 * 365;

	f->year = (int)(n400 * 400 + n100 * 100 + n4 * 4 + n1 + 1);

	int leap = IsLeap(f->year);
	int m = 1;
	while (m <= 12 && daysBeforeMonth[leap][m + 1] <= (int64_t)totalDays)
		m++;
	f->month = m;
	f->day = (int)(totalDays - daysBeforeMonth[leap][m] + 1);

	int64_t t = timeTicks;
	f->millisecond = (int)((t % DT_TICKS_PER_SECOND) / DT_TICKS_PER_MILLISECOND);
	t /= DT_TICKS_PER_SECOND;
	f->second = (int)(t % 60);
	t /= 60;
	f->minute = (int)(t % 60);
	f->hour = (int)(t / 60);
}

// Compose calendar fields into ticks (UTC absolute).
static int64_t FieldsToTicks(int y, int mo, int d, int h, int mi, int s, int ms) {
	int leap = IsLeap(y);
	int64_t days = DaysBeforeYear(y)
		+ daysBeforeMonth[leap][mo]
		+ (d - 1);
	return days * DT_TICKS_PER_DAY
		+ (int64_t)h * DT_TICKS_PER_HOUR
		+ (int64_t)mi * DT_TICKS_PER_MINUTE
		+ (int64_t)s * DT_TICKS_PER_SECOND
		+ (int64_t)ms * DT_TICKS_PER_MILLISECOND;
}

static int64_t GetUtcTicks(void) {
#ifdef _WIN32
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	// FILETIME epoch: 1601-01-01 00:00:00 UTC
	// .NET epoch:     0001-01-01 00:00:00 UTC
	// Difference: 504911232000000000 ticks
	int64_t ft64 = ((int64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
	return ft64 + INT64_C(504911232000000000);
#else
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	int64_t unixTicks = (int64_t)ts.tv_sec * DT_TICKS_PER_SECOND
		+ (int64_t)(ts.tv_nsec / 100);
	return unixTicks + DT_UNIX_EPOCH_TICKS;
#endif
}

static int16_t GetLocalOffsetMinutes(void) {
#ifdef _WIN32
	TIME_ZONE_INFORMATION tz;
	DWORD r = GetTimeZoneInformation(&tz);
	// Bias is the base minutes-west-of-UTC offset.
	// StandardBias and DaylightBias are added on top depending on which period is active.
	int bias = (int)tz.Bias;
	if (r == TIME_ZONE_ID_DAYLIGHT)
		bias += (int)tz.DaylightBias;
	else
		bias += (int)tz.StandardBias;
	return (int16_t)(-bias);
#else
	time_t t = time(NULL);
	struct tm localTm;
	localtime_r(&t, &localTm);
	return (int16_t)(localTm.tm_gmtoff / 60);
#endif
}

// Writes ISO 8601: "YYYY-MM-DDTHH:MM:SS.mmmZ" (UTC) or with "+HH:MM" offset.
// buf must be at least 35 chars.
static int FormatIso8601(const LuaDateTime* dt, char* buf, size_t bufsz) {
	int64_t localTicks = dt->ticks + (int64_t)dt->offsetMinutes * DT_TICKS_PER_MINUTE;
	DtFields f;
	TicksToFields(localTicks, &f);

	if (dt->offsetMinutes == 0) {
		return snprintf(buf, bufsz,
			"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
			f.year, f.month, f.day,
			f.hour, f.minute, f.second, f.millisecond);
	}
	else {
		int off = (int)dt->offsetMinutes;
		char sign = off >= 0 ? '+' : '-';
		if (off < 0) off = -off;
		return snprintf(buf, bufsz,
			"%04d-%02d-%02dT%02d:%02d:%02d.%03d%c%02d:%02d",
			f.year, f.month, f.day,
			f.hour, f.minute, f.second, f.millisecond,
			sign, off / 60, off % 60);
	}
}

void lua_pushdatetimestring(lua_State* L, int index) {
	LuaDateTime* dt = (LuaDateTime*)lua_touserdata(L, index);
	char buf[35];
	FormatIso8601(dt, buf, sizeof(buf));
	lua_pushstring(L, buf);
}

// Parses "YYYY-MM-DD[T HH:MM[:SS[.fff]]][Z|+HH:MM|-HH:MM]".
// Returns 1 on success and fills *out (UTC ticks + offset), 0 on failure.
// *hasOffsetOut is set to 1 if an explicit Z or +/-offset was present.
// endOut (optional) receives the position after the last character consumed.
static int ParseIso8601(const char* s, LuaDateTime* out, int* hasOffsetOut, const char** endOut = NULL) {
	int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0, ms = 0;
	int offH = 0, offM = 0;
	int offSign = 0; // 0 = not set/Z, +1 = east, -1 = west
	int hasExplicitOffset = 0;  // set for both Z and +/-HH:MM
	int n = 0;

	if (sscanf(s, "%d-%d-%d%n", &y, &mo, &d, &n) < 3) return 0;
	if (y < 1 || y > 9999 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
	if (d > daysInMonth[IsLeap(y)][mo]) return 0;

	const char* p = s + n;
	if (*p == 'T' || *p == ' ') {
		p++;
		int nt = 0;
		if (sscanf(p, "%d:%d%n", &h, &mi, &nt) < 2) return 0;
		p += nt;
		if (*p == ':') {
			p++;
			int ns = 0;
			if (sscanf(p, "%d%n", &sec, &ns) < 1) return 0;
			p += ns;
			if (*p == '.') {
				p++;
				int nf = 0;
				if (sscanf(p, "%d%n", &ms, &nf) == 1 && nf > 0) {
					// Normalise to milliseconds (3 digits).
					if (nf < 3) { for (int k = nf; k < 3; k++) ms *= 10; }
					else if (nf > 3) { for (int k = nf; k > 3; k--) ms /= 10; }
					p += nf;
				}
			}
		}
	}

	if (*p == 'Z') {
		offSign = 0;
		hasExplicitOffset = 1;
		p++;
	}
	else if (*p == '+' || *p == '-') {
		// ±HH, ±HHMM or ±HH:MM (Postgres prints whole-hour offsets as "+00", "-05").
		offSign = (*p == '+') ? 1 : -1;
		hasExplicitOffset = 1;
		p++;
		if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return 0;
		offH = (p[0] - '0') * 10 + (p[1] - '0');
		p += 2;
		int colon = (*p == ':');
		if (colon) p++;
		if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
			offM = (p[0] - '0') * 10 + (p[1] - '0');
			p += 2;
		}
		else if (colon) {
			return 0;   // "+05:" without minutes
		}
	}

	if (h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 60) return 0;
	if (ms < 0 || ms > 999) return 0;
	if (offH < 0 || offH > 23 || offM < 0 || offM > 59) return 0;
	int totalOff = offH * 60 + offM;
	if (totalOff > 840) return 0;  // beyond legal UTC offset range

	int16_t offsetMin = (int16_t)(offSign * totalOff);
	int64_t localTicks = FieldsToTicks(y, mo, d, h, mi, sec, ms);
	int64_t utcTicks = localTicks - (int64_t)offsetMin * DT_TICKS_PER_MINUTE;

	out->ticks = utcTicks;
	out->offsetMinutes = offsetMin;
	*hasOffsetOut = hasExplicitOffset;
	if (endOut) *endOut = p;
	return 1;
}

// Constructors

int DateTime_Now(lua_State* L) {
	LuaDateTime* dt = lua_pushdatetime(L);
	dt->ticks = GetUtcTicks();
	dt->offsetMinutes = GetLocalOffsetMinutes();
	return 1;
}

int DateTime_UtcNow(lua_State* L) {
	LuaDateTime* dt = lua_pushdatetime(L);
	dt->ticks = GetUtcTicks();
	dt->offsetMinutes = 0;
	return 1;
}

int DateTime_New(lua_State* L) {
	int y = (int)luaL_checkinteger(L, 1);
	int mo = (int)luaL_checkinteger(L, 2);
	int d = (int)luaL_checkinteger(L, 3);
	int h = (int)luaL_optinteger(L, 4, 0);
	int mi = (int)luaL_optinteger(L, 5, 0);
	int s = (int)luaL_optinteger(L, 6, 0);
	int ms = (int)luaL_optinteger(L, 7, 0);
	lua_Integer rawOff = luaL_optinteger(L, 8, 0);
	if (rawOff < -840 || rawOff > 840)
		return luaL_error(L, "DateTime.New: offset out of range [-840, 840]");
	int16_t offsetMin = (int16_t)rawOff;

	if (y < 1 || y > 9999 || mo < 1 || mo > 12 || d < 1 ||
		h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59 ||
		ms < 0 || ms > 999) {
		luaL_error(L, "DateTime.New: invalid component value");
	}
	if (d > daysInMonth[IsLeap(y)][mo])
		luaL_error(L, "DateTime.New: day out of range for month");

	LuaDateTime* dt = lua_pushdatetime(L);
	int64_t localTicks = FieldsToTicks(y, mo, d, h, mi, s, ms);
	dt->ticks = localTicks - (int64_t)offsetMin * DT_TICKS_PER_MINUTE;
	dt->offsetMinutes = offsetMin;
	return 1;
}

int DateTime_FromUnixSeconds(lua_State* L) {
	lua_Number ts = luaL_checknumber(L, 1);
	lua_Integer rawOff = luaL_optinteger(L, 2, 0);
	if (rawOff < -840 || rawOff > 840)
		return luaL_error(L, "DateTime.FromUnixSeconds: offset out of range [-840, 840]");
	int16_t offsetMin = (int16_t)rawOff;
	LuaDateTime* dt = lua_pushdatetime(L);
	dt->ticks = (int64_t)(ts * DT_TICKS_PER_SECOND) + DT_UNIX_EPOCH_TICKS;
	dt->offsetMinutes = offsetMin;
	return 1;
}

int DateTime_FromUnixMilliseconds(lua_State* L) {
	lua_Integer ts = luaL_checkinteger(L, 1);
	lua_Integer rawOff = luaL_optinteger(L, 2, 0);
	if (rawOff < -840 || rawOff > 840)
		return luaL_error(L, "DateTime.FromUnixMilliseconds: offset out of range [-840, 840]");
	int16_t offsetMin = (int16_t)rawOff;
	LuaDateTime* dt = lua_pushdatetime(L);
	dt->ticks = ts * DT_TICKS_PER_MILLISECOND + DT_UNIX_EPOCH_TICKS;
	dt->offsetMinutes = offsetMin;
	return 1;
}

// ParseIso8601 is exposed as DateTimeParseC for C callers (MySQL/Postgres).
// Unlike DateTime.Parse it must consume the whole value, so server output it
// doesn't fully understand (Postgres "0044-03-15 BC", an LMT offset with
// seconds) falls back to a string instead of becoming a wrong DateTime.
int DateTimeParseC(const char* s, LuaDateTime* out) {
	int has_offset;
	const char* end = NULL;
	return ParseIso8601(s, out, &has_offset, &end) && *end == '\0';
}

int DateTime_Parse(lua_State* L) {
	size_t len;
	const char* s = luaL_checklstring(L, 1, &len);
	lua_Integer rawOff = luaL_optinteger(L, 2, 0);
	if (rawOff < -840 || rawOff > 840)
		return luaL_error(L, "DateTime.Parse: fallback offset out of range [-840, 840]");
	int16_t fallbackOffset = (int16_t)rawOff;

	LuaDateTime tmp;
	memset(&tmp, 0, sizeof(tmp));
	int hasExplicitOffset = 0;
	if (!ParseIso8601(s, &tmp, &hasExplicitOffset)) {
		lua_pushnil(L);
		return 1;
	}
	if (!hasExplicitOffset) {
		tmp.ticks -= (int64_t)fallbackOffset * DT_TICKS_PER_MINUTE;
		tmp.offsetMinutes = fallbackOffset;
	}
	LuaDateTime* dt = lua_pushdatetime(L);
	*dt = tmp;
	return 1;
}

// Component getters

static DtFields GetLocalFields(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	int64_t localTicks = dt->ticks + (int64_t)dt->offsetMinutes * DT_TICKS_PER_MINUTE;
	DtFields f;
	TicksToFields(localTicks, &f);
	return f;
}

int DateTime_Year(lua_State* L) {
	DtFields f = GetLocalFields(L);
	lua_pushinteger(L, f.year);
	return 1;
}

int DateTime_Month(lua_State* L) {
	DtFields f = GetLocalFields(L);
	lua_pushinteger(L, f.month);
	return 1;
}

int DateTime_Day(lua_State* L) {
	DtFields f = GetLocalFields(L);
	lua_pushinteger(L, f.day);
	return 1;
}

int DateTime_Hour(lua_State* L) {
	DtFields f = GetLocalFields(L);
	lua_pushinteger(L, f.hour);
	return 1;
}

int DateTime_Minute(lua_State* L) {
	DtFields f = GetLocalFields(L);
	lua_pushinteger(L, f.minute);
	return 1;
}

int DateTime_Second(lua_State* L) {
	DtFields f = GetLocalFields(L);
	lua_pushinteger(L, f.second);
	return 1;
}

int DateTime_Millisecond(lua_State* L) {
	DtFields f = GetLocalFields(L);
	lua_pushinteger(L, f.millisecond);
	return 1;
}

int DateTime_DayOfWeek(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	// Day 0 in ticks is a Monday (0001-01-01).
	// .NET DayOfWeek: 0=Sunday, 1=Monday, ..., 6=Saturday.
	int64_t localTicks = dt->ticks + (int64_t)dt->offsetMinutes * DT_TICKS_PER_MINUTE;
	int64_t dayNum = localTicks / DT_TICKS_PER_DAY;
	if (localTicks < 0 && localTicks % DT_TICKS_PER_DAY != 0) dayNum--;
	// 0001-01-01 was a Monday (dow 1 in .NET = 1).
	int dow = (int)((dayNum + 1) % 7);
	if (dow < 0) dow += 7;
	lua_pushinteger(L, dow);
	return 1;
}

int DateTime_OffsetMinutes(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	lua_pushinteger(L, dt->offsetMinutes);
	return 1;
}

// Conversion

int DateTime_UnixSeconds(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	lua_Number secs = (lua_Number)(dt->ticks - DT_UNIX_EPOCH_TICKS) / (lua_Number)DT_TICKS_PER_SECOND;
	lua_pushnumber(L, secs);
	return 1;
}

int DateTime_UnixMilliseconds(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	lua_Integer ms = (dt->ticks - DT_UNIX_EPOCH_TICKS) / DT_TICKS_PER_MILLISECOND;
	lua_pushinteger(L, ms);
	return 1;
}

int DateTime_ToUtc(lua_State* L) {
	LuaDateTime* src = lua_todatetime(L, 1);
	LuaDateTime* dst = lua_pushdatetime(L);
	dst->ticks = src->ticks;
	dst->offsetMinutes = 0;
	return 1;
}

int DateTime_ToLocal(lua_State* L) {
	LuaDateTime* src = lua_todatetime(L, 1);
	LuaDateTime* dst = lua_pushdatetime(L);
	dst->ticks = src->ticks;
	dst->offsetMinutes = GetLocalOffsetMinutes();
	return 1;
}

int DateTime_ToOffset(lua_State* L) {
	LuaDateTime* src = lua_todatetime(L, 1);
	lua_Integer raw = luaL_checkinteger(L, 2);
	if (raw < -840 || raw > 840)
		return luaL_error(L, "DateTime.ToOffset: offset out of range [-840, 840]");
	int16_t new_offset = (int16_t)raw;
	LuaDateTime* dst = lua_pushdatetime(L);
	dst->ticks = src->ticks;
	dst->offsetMinutes = new_offset;
	return 1;
}

int DateTime_Format(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	const char* fmt = luaL_optstring(L, 2, NULL);

	int64_t localTicks = dt->ticks + (int64_t)dt->offsetMinutes * DT_TICKS_PER_MINUTE;
	DtFields f;
	TicksToFields(localTicks, &f);

	if (!fmt) {
		char buf[35];
		FormatIso8601(dt, buf, sizeof(buf));
		lua_pushstring(L, buf);
		return 1;
	}

	char buf[256];
	struct tm t;
	memset(&t, 0, sizeof(t));
	t.tm_year = f.year - 1900;
	t.tm_mon = f.month - 1;
	t.tm_mday = f.day;
	t.tm_hour = f.hour;
	t.tm_min = f.minute;
	t.tm_sec = f.second;
	t.tm_yday = daysBeforeMonth[IsLeap(f.year)][f.month] + (f.day - 1);
	t.tm_isdst = -1;  // unknown; let strftime decide
	// Day-of-week: compute from the local (offset-adjusted) date, not raw UTC ticks.
	// DaysBeforeYear(f.year) + daysBeforeMonth[leap][f.month] + (f.day - 1) gives
	// the absolute day number; day 0001-01-01 was a Monday (weekday 1 in .NET / 1 in tm).
	// tm_wday: 0=Sun, 1=Mon, ..., 6=Sat.
	{
		int64_t absDay = DaysBeforeYear(f.year)
			+ daysBeforeMonth[IsLeap(f.year)][f.month]
			+ (f.day - 1);
		t.tm_wday = (int)((absDay + 1) % 7);  // +1: 0001-01-01 was Monday(1), Mon%7=1
	}
	strftime(buf, sizeof(buf), fmt, &t);
	lua_pushstring(L, buf);
	return 1;
}

int DateTime_IsEmpty(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	lua_pushboolean(L, dt->ticks == 0 ? 1 : 0);
	return 1;
}

// Arithmetic

static int AddTicks(lua_State* L, int64_t delta) {
	LuaDateTime* src = lua_todatetime(L, 1);
	LuaDateTime* dst = lua_pushdatetime(L);
	dst->ticks = src->ticks + delta;
	dst->offsetMinutes = src->offsetMinutes;
	return 1;
}

int DateTime_AddDays(lua_State* L) {
	lua_Number n = luaL_checknumber(L, 2);
	return AddTicks(L, (int64_t)(n * (lua_Number)DT_TICKS_PER_DAY));
}

int DateTime_AddHours(lua_State* L) {
	lua_Number n = luaL_checknumber(L, 2);
	return AddTicks(L, (int64_t)(n * (lua_Number)DT_TICKS_PER_HOUR));
}

int DateTime_AddMinutes(lua_State* L) {
	lua_Number n = luaL_checknumber(L, 2);
	return AddTicks(L, (int64_t)(n * (lua_Number)DT_TICKS_PER_MINUTE));
}

int DateTime_AddSeconds(lua_State* L) {
	lua_Number n = luaL_checknumber(L, 2);
	return AddTicks(L, (int64_t)(n * (lua_Number)DT_TICKS_PER_SECOND));
}

int DateTime_AddMilliseconds(lua_State* L) {
	lua_Number n = luaL_checknumber(L, 2);
	return AddTicks(L, (int64_t)(n * (lua_Number)DT_TICKS_PER_MILLISECOND));
}

// Metamethods

int DateTime_ToString(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	char buf[35];
	FormatIso8601(dt, buf, sizeof(buf));
	lua_pushstring(L, buf);
	return 1;
}

int DateTime_Eq(lua_State* L) {
	LuaDateTime* a = (LuaDateTime*)luaL_testudata(L, 1, LUADATETIME);
	LuaDateTime* b = (LuaDateTime*)luaL_testudata(L, 2, LUADATETIME);
	if (!a || !b) { lua_pushboolean(L, 0); return 1; }
	// Compare UTC ticks only (offset is display information).
	lua_pushboolean(L, a->ticks == b->ticks ? 1 : 0);
	return 1;
}

int DateTime_Lt(lua_State* L) {
	LuaDateTime* a = lua_todatetime(L, 1);
	LuaDateTime* b = lua_todatetime(L, 2);
	lua_pushboolean(L, a->ticks < b->ticks ? 1 : 0);
	return 1;
}

int DateTime_Le(lua_State* L) {
	LuaDateTime* a = lua_todatetime(L, 1);
	LuaDateTime* b = lua_todatetime(L, 2);
	lua_pushboolean(L, a->ticks <= b->ticks ? 1 : 0);
	return 1;
}

// DateTime + TimeSpan arithmetic

// __add: DateTime + TimeSpan → DateTime
int DateTime_AddTimeSpan(lua_State* L) {
	LuaDateTime* dt = NULL;
	LuaTimeSpan* ts = NULL;
	// Support both orderings: dt + span and span + dt (commutative for add).
	if (lua_isdatetime(L, 1) && lua_istimespan(L, 2)) {
		dt = lua_todatetime(L, 1);
		ts = lua_totimespan(L, 2);
	}
	else if (lua_istimespan(L, 1) && lua_isdatetime(L, 2)) {
		ts = lua_totimespan(L, 1);
		dt = lua_todatetime(L, 2);
	}
	else {
		return luaL_error(L, "DateTime: __add requires a DateTime and a TimeSpan");
	}
	LuaDateTime* result = lua_pushdatetime(L);
	result->ticks          = dt->ticks + ts->ticks;
	result->offsetMinutes = dt->offsetMinutes;
	return 1;
}

// __sub: DateTime - DateTime → TimeSpan; DateTime - TimeSpan → DateTime
int DateTime_Sub(lua_State* L) {
	if (lua_isdatetime(L, 1) && lua_isdatetime(L, 2)) {
		LuaDateTime* a = lua_todatetime(L, 1);
		LuaDateTime* b = lua_todatetime(L, 2);
		lua_pushtimespan(L)->ticks = a->ticks - b->ticks;
		return 1;
	}
	if (lua_isdatetime(L, 1) && lua_istimespan(L, 2)) {
		LuaDateTime* dt = lua_todatetime(L, 1);
		LuaTimeSpan* ts = lua_totimespan(L, 2);
		LuaDateTime* result = lua_pushdatetime(L);
		result->ticks          = dt->ticks - ts->ticks;
		result->offsetMinutes = dt->offsetMinutes;
		return 1;
	}
	return luaL_error(L, "DateTime: __sub requires DateTime-DateTime or DateTime-TimeSpan");
}
