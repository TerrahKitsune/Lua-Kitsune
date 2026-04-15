#include "luadatetime.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "mem.h"
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
	return luaL_testudata(L, index, LUADATETIME) != NULL;
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

static int is_leap(int y) {
	return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
}

// Days from 0001-01-01 to start of year y (1-based).
static int64_t days_before_year(int y) {
	int n = y - 1;
	return (int64_t)n * 365 + n / 4 - n / 100 + n / 400;
}

typedef struct {
	int year, month, day, hour, minute, second, millisecond;
} DtFields;

// Decompose ticks (UTC absolute) into calendar fields.
static void ticks_to_fields(int64_t ticks, DtFields* f) {
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

	int leap = is_leap(f->year);
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
static int64_t fields_to_ticks(int y, int mo, int d, int h, int mi, int s, int ms) {
	int leap = is_leap(y);
	int64_t days = days_before_year(y)
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
static int64_t get_utc_ticks(void) {
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

static int16_t get_local_offset_minutes(void) {
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
static int format_iso8601(const LuaDateTime* dt, char* buf, size_t bufsz) {
	// Apply offset to get local ticks for display.
	int64_t local_ticks = dt->ticks + (int64_t)dt->offset_minutes * DT_TICKS_PER_MINUTE;
	DtFields f;
	ticks_to_fields(local_ticks, &f);

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

void lua_datetime_push_string(lua_State* L, int index) {
	LuaDateTime* dt = (LuaDateTime*)lua_touserdata(L, index);
	char buf[35];
	format_iso8601(dt, buf, sizeof(buf));
	lua_pushstring(L, buf);
}

// ── ISO 8601 parsing ──────────────────────────────────────────────────────────

// Parses "YYYY-MM-DD[T HH:MM[:SS[.fff]]][Z|+HH:MM|-HH:MM]".
// Returns 1 on success and fills *out (UTC ticks + offset), 0 on failure.
// *has_offset_out is set to 1 if an explicit Z or +/-offset was present.
static int parse_iso8601(const char* s, LuaDateTime* out, int* has_offset_out) {
	int y = 0, mo = 0, d = 0, h = 0, mi = 0, sec = 0, ms = 0;
	int off_h = 0, off_m = 0;
	int off_sign = 0; // 0 = not set/Z, +1 = east, -1 = west
	int has_explicit_offset = 0;  // set for both Z and +/-HH:MM
	int n = 0;

	if (sscanf(s, "%d-%d-%d%n", &y, &mo, &d, &n) < 3) return 0;
	if (y < 1 || y > 9999 || mo < 1 || mo > 12 || d < 1 || d > 31) return 0;
	if (d > s_days_in_month[is_leap(y)][mo]) return 0;

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
		off_sign = (*p == '+') ? 1 : -1;
		has_explicit_offset = 1;
		p++;
		int nz = 0;
		if (sscanf(p, "%d:%d%n", &off_h, &off_m, &nz) < 2) {
			// Try without colon: +HHMM
			if (sscanf(p, "%2d%2d%n", &off_h, &off_m, &nz) < 2) return 0;
		}
	}

	if (h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 60) return 0;
	if (ms < 0 || ms > 999) return 0;
	if (off_h < 0 || off_h > 23 || off_m < 0 || off_m > 59) return 0;
	int total_off = off_h * 60 + off_m;
	if (total_off > 840) return 0;  // beyond legal UTC offset range

	int16_t offset_min = (int16_t)(off_sign * total_off);
	int64_t local_ticks = fields_to_ticks(y, mo, d, h, mi, sec, ms);
	// Convert to UTC by subtracting the offset.
	int64_t utc_ticks = local_ticks - (int64_t)offset_min * DT_TICKS_PER_MINUTE;

	out->ticks = utc_ticks;
	out->offset_minutes = offset_min;
	*has_offset_out = has_explicit_offset;
	return 1;
}

// ── Constructors ──────────────────────────────────────────────────────────────

int datetime_now(lua_State* L) {
	LuaDateTime* dt = lua_pushdatetime(L);
	dt->ticks = get_utc_ticks();
	dt->offset_minutes = get_local_offset_minutes();
	return 1;
}

int datetime_utcnow(lua_State* L) {
	LuaDateTime* dt = lua_pushdatetime(L);
	dt->ticks = get_utc_ticks();
	dt->offset_minutes = 0;
	return 1;
}

int datetime_new(lua_State* L) {
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
	if (d > s_days_in_month[is_leap(y)][mo])
		luaL_error(L, "DateTime.New: day out of range for month");

	LuaDateTime* dt = lua_pushdatetime(L);
	int64_t local_ticks = fields_to_ticks(y, mo, d, h, mi, s, ms);
	dt->ticks = local_ticks - (int64_t)offset_min * DT_TICKS_PER_MINUTE;
	dt->offset_minutes = offset_min;
	return 1;
}

int datetime_fromunixseconds(lua_State* L) {
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

int datetime_fromunixmilliseconds(lua_State* L) {
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

// parse_iso8601 is exposed as datetime_parse_c for C callers (MySQL/Postgres).
int datetime_parse_c(const char* s, LuaDateTime* out) {
	int has_offset;
	return parse_iso8601(s, out, &has_offset);
}

int datetime_parse(lua_State* L) {
	size_t len;
	const char* s = luaL_checklstring(L, 1, &len);
	lua_Integer raw_off = luaL_optinteger(L, 2, 0);
	if (raw_off < -840 || raw_off > 840)
		return luaL_error(L, "DateTime.Parse: fallback offset out of range [-840, 840]");
	int16_t fallback_offset = (int16_t)raw_off;

	LuaDateTime tmp;
	memset(&tmp, 0, sizeof(tmp));
	int has_explicit_offset = 0;
	if (!parse_iso8601(s, &tmp, &has_explicit_offset)) {
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

static DtFields get_local_fields(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	int64_t local_ticks = dt->ticks + (int64_t)dt->offset_minutes * DT_TICKS_PER_MINUTE;
	DtFields f;
	ticks_to_fields(local_ticks, &f);
	return f;
}

int datetime_year(lua_State* L) {
	DtFields f = get_local_fields(L);
	lua_pushinteger(L, f.year);
	return 1;
}

int datetime_month(lua_State* L) {
	DtFields f = get_local_fields(L);
	lua_pushinteger(L, f.month);
	return 1;
}

int datetime_day(lua_State* L) {
	DtFields f = get_local_fields(L);
	lua_pushinteger(L, f.day);
	return 1;
}

int datetime_hour(lua_State* L) {
	DtFields f = get_local_fields(L);
	lua_pushinteger(L, f.hour);
	return 1;
}

int datetime_minute(lua_State* L) {
	DtFields f = get_local_fields(L);
	lua_pushinteger(L, f.minute);
	return 1;
}

int datetime_second(lua_State* L) {
	DtFields f = get_local_fields(L);
	lua_pushinteger(L, f.second);
	return 1;
}

int datetime_millisecond(lua_State* L) {
	DtFields f = get_local_fields(L);
	lua_pushinteger(L, f.millisecond);
	return 1;
}

int datetime_dayofweek(lua_State* L) {
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

int datetime_offsetminutes(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	lua_pushinteger(L, dt->offset_minutes);
	return 1;
}

// ── Conversion ───────────────────────────────────────────────────────────────

int datetime_unixseconds(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	lua_Number secs = (lua_Number)(dt->ticks - DT_UNIX_EPOCH_TICKS) / (lua_Number)DT_TICKS_PER_SECOND;
	lua_pushnumber(L, secs);
	return 1;
}

int datetime_unixmilliseconds(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	lua_Integer ms = (dt->ticks - DT_UNIX_EPOCH_TICKS) / DT_TICKS_PER_MILLISECOND;
	lua_pushinteger(L, ms);
	return 1;
}

int datetime_toutc(lua_State* L) {
	LuaDateTime* src = lua_todatetime(L, 1);
	LuaDateTime* dst = lua_pushdatetime(L);
	dst->ticks = src->ticks;
	dst->offset_minutes = 0;
	return 1;
}

int datetime_tolocal(lua_State* L) {
	LuaDateTime* src = lua_todatetime(L, 1);
	LuaDateTime* dst = lua_pushdatetime(L);
	dst->ticks = src->ticks;
	dst->offset_minutes = get_local_offset_minutes();
	return 1;
}

int datetime_tooffset(lua_State* L) {
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

int datetime_format(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	const char* fmt = luaL_optstring(L, 2, NULL);

	int64_t local_ticks = dt->ticks + (int64_t)dt->offset_minutes * DT_TICKS_PER_MINUTE;
	DtFields f;
	ticks_to_fields(local_ticks, &f);

	if (!fmt) {
		// Default: ISO 8601
		char buf[35];
		format_iso8601(dt, buf, sizeof(buf));
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
	t.tm_yday = s_days_before_month[is_leap(f.year)][f.month] + (f.day - 1);
	t.tm_isdst = -1;  // unknown; let strftime decide
	// Day-of-week: compute from the local (offset-adjusted) date, not raw UTC ticks.
	// days_before_year(f.year) + s_days_before_month[leap][f.month] + (f.day - 1) gives
	// the absolute day number; day 0001-01-01 was a Monday (weekday 1 in .NET / 1 in tm).
	// tm_wday: 0=Sun, 1=Mon, ..., 6=Sat.
	{
		int64_t abs_day = days_before_year(f.year)
			+ s_days_before_month[is_leap(f.year)][f.month]
			+ (f.day - 1);
		t.tm_wday = (int)((abs_day + 1) % 7);  // +1: 0001-01-01 was Monday(1), Mon%7=1
	}
	strftime(buf, sizeof(buf), fmt, &t);
	lua_pushstring(L, buf);
	return 1;
}

int datetime_isempty(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	lua_pushboolean(L, dt->ticks == 0 ? 1 : 0);
	return 1;
}

// ── Arithmetic ───────────────────────────────────────────────────────────────

static int add_ticks(lua_State* L, int64_t delta) {
	LuaDateTime* src = lua_todatetime(L, 1);
	LuaDateTime* dst = lua_pushdatetime(L);
	dst->ticks = src->ticks + delta;
	dst->offset_minutes = src->offset_minutes;
	return 1;
}

int datetime_adddays(lua_State* L) {
	lua_Number n = luaL_checknumber(L, 2);
	return add_ticks(L, (int64_t)(n * (lua_Number)DT_TICKS_PER_DAY));
}

int datetime_addhours(lua_State* L) {
	lua_Number n = luaL_checknumber(L, 2);
	return add_ticks(L, (int64_t)(n * (lua_Number)DT_TICKS_PER_HOUR));
}

int datetime_addminutes(lua_State* L) {
	lua_Number n = luaL_checknumber(L, 2);
	return add_ticks(L, (int64_t)(n * (lua_Number)DT_TICKS_PER_MINUTE));
}

int datetime_addseconds(lua_State* L) {
	lua_Number n = luaL_checknumber(L, 2);
	return add_ticks(L, (int64_t)(n * (lua_Number)DT_TICKS_PER_SECOND));
}

int datetime_addmilliseconds(lua_State* L) {
	lua_Number n = luaL_checknumber(L, 2);
	return add_ticks(L, (int64_t)(n * (lua_Number)DT_TICKS_PER_MILLISECOND));
}

// ── Metamethods ───────────────────────────────────────────────────────────────

int datetime_tostring(lua_State* L) {
	LuaDateTime* dt = lua_todatetime(L, 1);
	char buf[35];
	format_iso8601(dt, buf, sizeof(buf));
	lua_pushstring(L, buf);
	return 1;
}

int datetime_eq(lua_State* L) {
	LuaDateTime* a = (LuaDateTime*)luaL_testudata(L, 1, LUADATETIME);
	LuaDateTime* b = (LuaDateTime*)luaL_testudata(L, 2, LUADATETIME);
	if (!a || !b) { lua_pushboolean(L, 0); return 1; }
	// Compare UTC ticks only (offset is display information).
	lua_pushboolean(L, a->ticks == b->ticks ? 1 : 0);
	return 1;
}

int datetime_lt(lua_State* L) {
	LuaDateTime* a = lua_todatetime(L, 1);
	LuaDateTime* b = lua_todatetime(L, 2);
	lua_pushboolean(L, a->ticks < b->ticks ? 1 : 0);
	return 1;
}

int datetime_le(lua_State* L) {
	LuaDateTime* a = lua_todatetime(L, 1);
	LuaDateTime* b = lua_todatetime(L, 2);
	lua_pushboolean(L, a->ticks <= b->ticks ? 1 : 0);
	return 1;
}

// Returns difference in seconds as a number.
int datetime_sub(lua_State* L) {
	LuaDateTime* a = lua_todatetime(L, 1);
	LuaDateTime* b = lua_todatetime(L, 2);
	lua_Number diff = (lua_Number)(a->ticks - b->ticks) / (lua_Number)DT_TICKS_PER_SECOND;
	lua_pushnumber(L, diff);
	return 1;
}
