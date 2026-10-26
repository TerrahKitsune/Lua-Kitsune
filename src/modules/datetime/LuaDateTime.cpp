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

// ── Userdata helpers ──────────────────────────────────────────────────────────

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

// ── Internal calendar helpers ─────────────────────────────────────────────────

// Days per month (non-leap / leap).
static const int s_days_in_month[2][13] = {
	{ 0, 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 },
	{ 0, 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }
};

// Cumulative days from Jan 1 to start of month (non-leap / leap).
static const int s_days_before_month[2][14] = {
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
	int64_t total_days = ticks / DT_TICKS_PER_DAY;
	int64_t time_ticks = ticks % DT_TICKS_PER_DAY;
	if (time_ticks < 0) {
		time_ticks += DT_TICKS_PER_DAY;
		total_days--;
	}

	// 400-year blocks.
	int64_t n400 = total_days / 146097;
	total_days -= n400 * 146097;
	int n100 = (int)(total_days / 36524);
	if (n100 == 4) n100 = 3;
	total_days -= n100 * 36524;
	int n4 = (int)(total_days / 1461);
	total_days -= n4 * 1461;
	int n1 = (int)(total_days / 365);
	if (n1 == 4) n1 = 3;
	total_days -= n1 * 365;

	f->year = (int)(n400 * 400 + n100 * 100 + n4 * 4 + n1 + 1);

	int leap = IsLeap(f->year);
	int m = 1;
	while (m <= 12 && s_days_before_month[leap][m + 1] <= (int64_t)total_days)
		m++;
	f->month = m;
	f->day = (int)(total_days - s_days_before_month[leap][m] + 1);

	int64_t t = time_ticks;
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
		+ s_days_before_month[leap][mo]
		+ (d - 1);
	return days * DT_TICKS_PER_DAY
		+ (int64_t)h * DT_TICKS_PER_HOUR
		+ (int64_t)mi * DT_TICKS_PER_MINUTE
		+ (int64_t)s * DT_TICKS_PER_SECOND
		+ (int64_t)ms * DT_TICKS_PER_MILLISECOND;
}

// ── Platform clock ────────────────────────────────────────────────────────────

// Returns current UTC ticks and the local UTC offset in minutes.
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
	int64_t unix_ticks = (int64_t)ts.tv_sec * DT_TICKS_PER_SECOND
		+ (int64_t)(ts.tv_nsec / 100);
	return unix_ticks + DT_UNIX_EPOCH_TICKS;
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
		bias += (int)tz.StandardBias;  // applies in standard time (and unknown)
	return (int16_t)(-bias);
#else
	time_t t = time(NULL);
	struct tm local_tm;
	localtime_r(&t, &local_tm);
	return (int16_t)(local_tm.tm_gmtoff / 60);
#endif
}

// ── ISO 8601 formatting ───────────────────────────────────────────────────────

// Writes ISO 8601: "YYYY-MM-DDTHH:MM:SS.mmmZ" (UTC) or with "+HH:MM" offset.
// buf must be at least 35 chars.
static int FormatIso8601(const LuaDateTime* dt, char* buf, size_t bufsz) {
	// Apply offset to get local ticks for display.
	int64_t local_ticks = dt->ticks + (int64_t)dt->offset_minutes * DT_TICKS_PER_MINUTE;
	DtFields f;
	TicksToFields(local_ticks, &f);

	if (dt->offset_minutes == 0) {
		return snprintf(buf, bufsz,
			"%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
			f.year, f.month, f.day,
			f.hour, f.minute, f.second, f.millisecond);
	}
	else {
		int off = (int)dt->offset_minutes;
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

// ── ISO 8601 parsing ──────────────────────────────────────────────────────────

// Parses "YYYY-MM-DD[T HH:MM[:SS[.fff]]][Z|+HH:MM|-HH:MM]".
// Returns 1 on success and fills *out (UTC ticks + offset), 0 on failure.
// *has_offset_out is set to 1 if an explicit Z or +/-offset was present.
// end_out (optional) receives the position after the last character consumed.
static int ParseIso8601(const char* s, LuaDateTime* out, int* has_offset_out, const char** end_out = NULL) {
	int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0, ms = 0;
	int off_h = 0, off_m = 0;
	int off_sign = 0; // 0 = not set/Z, +1 = east, -1 = west
	int has_explicit_offset = 0;  // set for both Z and +/-HH:MM
	int n = 0;

	if (sscanf(s, "%d-%d-%d%n", &y, &mo, &d, &n) < 3) return 0;
	if (y < 1 || y > 9999 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
	if (d > s_days_in_month[IsLeap(y)][mo]) return 0;

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
		off_sign = 0;
		has_explicit_offset = 1;
		p++;
	}
	else if (*p == '+' || *p == '-') {
		// ±HH, ±HHMM or ±HH:MM (Postgres prints whole-hour offsets as "+00", "-05").
		off_sign = (*p == '+') ? 1 : -1;
		has_explicit_offset = 1;
		p++;
		if (p[0] < '0' || p[0] > '9' || p[1] < '0' || p[1] > '9') return 0;
		off_h = (p[0] - '0') * 10 + (p[1] - '0');
		p += 2;
		int colon = (*p == ':');
		if (colon) p++;
		if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9') {
			off_m = (p[0] - '0') * 10 + (p[1] - '0');
			p += 2;
		}
		else if (colon) {
			return 0;   // "+05:" without minutes
		}
	}

	if (h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 60) return 0;
	if (ms < 0 || ms > 999) return 0;
	if (off_h < 0 || off_h > 23 || off_m < 0 || off_m > 59) return 0;
	int total_off = off_h * 60 + off_m;
	if (total_off > 840) return 0;  // beyond legal UTC offset range

	int16_t offset_min = (int16_t)(off_sign * total_off);
	int64_t local_ticks = FieldsToTicks(y, mo, d, h, mi, sec, ms);
	// Convert to UTC by subtracting the offset.
	int64_t utc_ticks = local_ticks - (int64_t)offset_min * DT_TICKS_PER_MINUTE;

	out->ticks = utc_ticks;
	out->offset_minutes = offset_min;
	*has_offset_out = has_explicit_offset;
	if (end_out) *end_out = p;
	return 1;
}

// ── Constructors ──────────────────────────────────────────────────────────────

int DateTime_Now(lua_State* L) {
	LuaDateTime* dt = lua_pushdatetime(L);
	dt->ticks = GetUtcTicks();
	dt->offset_minutes = GetLocalOffsetMinutes();
	return 1;
}

int DateTime_UtcNow(lua_State* L) {
	LuaDateTime* dt = lua_pushdatetime(L);
	dt->ticks = GetUtcTicks();
	dt->offset_minutes = 0;
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
	lua_Integer raw_off = luaL_optinteger(L, 8, 0);
	if (raw_off < -840 || raw_off > 840)
		return luaL_error(L, "DateTime.New: offset out of range [-840, 840]");
	int16_t offset_min = (int16_t)raw_off;

	if (y < 1 || y > 9999 || mo < 1 || mo > 12 || d < 1 ||
		h < 0 || h > 23 || mi < 0 || mi > 59 || s < 0 || s > 59 ||
		ms < 0 || ms > 999) {
		luaL_error(L, "DateTime.New: invalid component value");
	}
	if (d > s_days_in_month[IsLeap(y)][mo])
		luaL_error(L, "DateTime.New: day out of range for month");

	LuaDateTime* dt = lua_pushdatetime(L);
	int64_t local_ticks = FieldsToTicks(y, mo, d, h, mi, s, ms);
	dt->ticks = local_ticks - (int64_t)offset_min * DT_TICKS_PER_MINUTE;
	dt->offset_minutes = offset_min;
	return 1;
}

int DateTime_FromUnixSeconds(lua_State* L) {
	lua_Number ts = luaL_checknumber(L, 1);
	lua_Integer raw_off = luaL_optinteger(L, 2, 0);
	if (raw_off < -840 || raw_off > 840)
		return luaL_error(L, "DateTime.FromUnixSeconds: offset out of range [-840, 840]");
	int16_t offset_min = (int16_t)raw_off;
	LuaDateTime* dt = lua_pushdatetime(L);
	dt->ticks = (int64_t)(ts * DT_TICKS_PER_SECOND) + DT_UNIX_EPOCH_TICKS;
	dt->offset_minutes = offset_min;
	return 1;
}

int DateTime_FromUnixMilliseconds(lua_State* L) {
	lua_Integer ts = luaL_checkinteger(L, 1);
	lua_Integer raw_off = luaL_optinteger(L, 2, 0);
	if (raw_off < -840 || raw_off > 840)
		return luaL_error(L, "DateTime.FromUnixMilliseconds: offset out of range [-840, 840]");
	int16_t offset_min = (int16_t)raw_off;
	LuaDateTime* dt = lua_pushdatetime(L);
	dt->ticks = ts * DT_TICKS_PER_MILLISECOND + DT_UNIX_EPOCH_TICKS;
	dt->offset_minutes = offset_min;
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
	lua_Integer raw_off = luaL_optinteger(L, 2, 0);
	if (raw_off < -840 || raw_off > 840)
		return luaL_error(L, "DateTime.Parse: fallback offset out of range [-840, 840]");
	int16_t fallback_offset = (int16_t)raw_off;

	LuaDateTime tmp;
	memset(&tmp, 0, sizeof(tmp));
	int has_explicit_offset = 0;
	if (!ParseIso8601(s, &tmp, &has_explicit_offset)) {
		lua_pushnil(L);
		return 1;
	}
	// If no offset was embedded in the string, apply the fallback.
	if (!has_explicit_offset) {
		tmp.ticks -= (int64_t)fallback_offset * DT_TICKS_PER_MINUTE;
		tmp.offset_minutes = fallback_offset;
	}
	LuaDateTime* dt = lua_pushdatetime(L);
	*dt = tmp;
	return 1;
}

// ── Component getters ─────────────────────────────────────────────────────────

static DtFields GetLocalFields(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	int64_t local_ticks = dt->ticks + (int64_t)dt->offset_minutes * DT_TICKS_PER_MINUTE;
	DtFields f;
	TicksToFields(local_ticks, &f);
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
	int64_t local_ticks = dt->ticks + (int64_t)dt->offset_minutes * DT_TICKS_PER_MINUTE;
	int64_t day_num = local_ticks / DT_TICKS_PER_DAY;
	if (local_ticks < 0 && local_ticks % DT_TICKS_PER_DAY != 0) day_num--;
	// 0001-01-01 was a Monday (dow 1 in .NET = 1).
	int dow = (int)((day_num + 1) % 7);
	if (dow < 0) dow += 7;
	lua_pushinteger(L, dow);
	return 1;
}

int DateTime_OffsetMinutes(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	lua_pushinteger(L, dt->offset_minutes);
	return 1;
}

// ── Conversion ───────────────────────────────────────────────────────────────

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
	dst->offset_minutes = 0;
	return 1;
}

int DateTime_ToLocal(lua_State* L) {
	LuaDateTime* src = lua_todatetime(L, 1);
	LuaDateTime* dst = lua_pushdatetime(L);
	dst->ticks = src->ticks;
	dst->offset_minutes = GetLocalOffsetMinutes();
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
	dst->offset_minutes = new_offset;
	return 1;
}

int DateTime_Format(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	const char* fmt = luaL_optstring(L, 2, NULL);

	int64_t local_ticks = dt->ticks + (int64_t)dt->offset_minutes * DT_TICKS_PER_MINUTE;
	DtFields f;
	TicksToFields(local_ticks, &f);

	if (!fmt) {
		// Default: ISO 8601
		char buf[35];
		FormatIso8601(dt, buf, sizeof(buf));
		lua_pushstring(L, buf);
		return 1;
	}

	// Simple strftime-style substitution.
	char buf[256];
	struct tm t;
	memset(&t, 0, sizeof(t));
	t.tm_year = f.year - 1900;
	t.tm_mon = f.month - 1;
	t.tm_mday = f.day;
	t.tm_hour = f.hour;
	t.tm_min = f.minute;
	t.tm_sec = f.second;
	t.tm_yday = s_days_before_month[IsLeap(f.year)][f.month] + (f.day - 1);
	t.tm_isdst = -1;  // unknown; let strftime decide
	// Day-of-week: compute from the local (offset-adjusted) date, not raw UTC ticks.
	// DaysBeforeYear(f.year) + s_days_before_month[leap][f.month] + (f.day - 1) gives
	// the absolute day number; day 0001-01-01 was a Monday (weekday 1 in .NET / 1 in tm).
	// tm_wday: 0=Sun, 1=Mon, ..., 6=Sat.
	{
		int64_t abs_day = DaysBeforeYear(f.year)
			+ s_days_before_month[IsLeap(f.year)][f.month]
			+ (f.day - 1);
		t.tm_wday = (int)((abs_day + 1) % 7);  // +1: 0001-01-01 was Monday(1), Mon%7=1
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

// ── Arithmetic ───────────────────────────────────────────────────────────────

static int AddTicks(lua_State* L, int64_t delta) {
	LuaDateTime* src = lua_todatetime(L, 1);
	LuaDateTime* dst = lua_pushdatetime(L);
	dst->ticks = src->ticks + delta;
	dst->offset_minutes = src->offset_minutes;
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

// ── Metamethods ───────────────────────────────────────────────────────────────

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

// ── DateTime + TimeSpan arithmetic ───────────────────────────────────────────

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
	result->offset_minutes = dt->offset_minutes;
	return 1;
}

// __sub: DateTime - DateTime → TimeSpan; DateTime - TimeSpan → DateTime
int DateTime_Sub(lua_State* L) {
	if (lua_isdatetime(L, 1) && lua_isdatetime(L, 2)) {
		// DateTime - DateTime → LuaTimeSpan
		LuaDateTime* a = lua_todatetime(L, 1);
		LuaDateTime* b = lua_todatetime(L, 2);
		lua_pushtimespan(L)->ticks = a->ticks - b->ticks;
		return 1;
	}
	if (lua_isdatetime(L, 1) && lua_istimespan(L, 2)) {
		// DateTime - TimeSpan → DateTime
		LuaDateTime* dt = lua_todatetime(L, 1);
		LuaTimeSpan* ts = lua_totimespan(L, 2);
		LuaDateTime* result = lua_pushdatetime(L);
		result->ticks          = dt->ticks - ts->ticks;
		result->offset_minutes = dt->offset_minutes;
		return 1;
	}
	return luaL_error(L, "DateTime: __sub requires DateTime-DateTime or DateTime-TimeSpan");
}
