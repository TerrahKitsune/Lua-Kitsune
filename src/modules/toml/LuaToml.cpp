#include "toml/LuaToml.h"
#include "Utf8Bom.h"
#include "identifier/LuaIdentifier.h"
#include "datetime/LuaDateTime.h"
#include "decimal/LuaDecimal.h"
#include "uint/LuaUInt.h"
#include "timespan/LuaTimeSpan.h"
#include <math.h>

// Instance management

LuaToml* lua_pushtoml(lua_State* L) {
	LuaToml* t = (LuaToml*)lua_newuserdata(L, sizeof(LuaToml));
	memset(t, 0, sizeof(LuaToml));
	luaL_setmetatable(L, LUATOML);
	return t;
}

LuaToml* luaL_checktoml(lua_State* L, int idx) {
	return (LuaToml*)luaL_checkudata(L, idx, LUATOML);
}

int Toml_Dispose(lua_State* L) {
	LuaToml* t = luaL_checktoml(L, 1);
	if (t->out) {
		kitsune_free(t->out);
		t->out = NULL;
	}
	if (t->rec) {
		kitsune_free(t->rec);
		t->rec = NULL;
	}
	return 0;
}

int Toml_ToString(lua_State* L) {
	lua_pushfstring(L, "Toml: %p", luaL_checktoml(L, 1));
	return 1;
}

int Toml_New(lua_State* L) {
	int      pretty = lua_isboolean(L, 1) ? lua_toboolean(L, 1) : 0;
	LuaToml* t      = lua_pushtoml(L);
	t->pretty       = pretty;
	return 1;
}

// Anti-recursion

static void RecPush(LuaToml* t, lua_State* L, uintptr_t addr) {
	for (size_t i = 0; i < t->recLen; i++) {
		if (t->rec[i] == addr)
			luaL_error(L, "Toml: recursion detected");
	}
	if (t->recLen == t->recCap) {
		size_t     cap = t->recCap ? t->recCap * 2 : 8;
		uintptr_t* p   = (uintptr_t*)kitsune_realloc(t->rec, cap * sizeof(uintptr_t));
		if (!p)
			luaL_error(L, "Toml: out of memory");
		t->rec    = p;
		t->recCap = cap;
	}
	t->rec[t->recLen++] = addr;
}

static void RecPop(LuaToml* t) {
	if (t->recLen > 0)
		t->recLen--;
}

// Encoder — output buffer helpers

static void TbufGrow(LuaToml* t, lua_State* L, size_t need) {
	if (t->outLen + need <= t->outCap)
		return;
	size_t cap = t->outCap ? t->outCap * 2 : 512;
	while (cap < t->outLen + need)
		cap *= 2;
	char* p = (char*)kitsune_realloc(t->out, cap);
	if (!p)
		luaL_error(L, "Toml: out of memory");
	t->out    = p;
	t->outCap = cap;
}

static void TbufEmit(LuaToml* t, lua_State* L, const char* data, size_t len) {
	TbufGrow(t, L, len);
	memcpy(t->out + t->outLen, data, len);
	t->outLen += len;
}

static void TbufEmitC(LuaToml* t, lua_State* L, char c) {
	TbufGrow(t, L, 1);
	t->out[t->outLen++] = c;
}

#define TBUF_EMIT_LIT(t, L, s) TbufEmit(t, L, "" s, sizeof(s) - 1)

// Encoder — scalar helpers

// Emit a TOML basic string (double-quoted, with escapes).
static void EncString(LuaToml* t, lua_State* L, const char* s, size_t len) {
	TbufEmitC(t, L, '"');
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];
		switch (c) {
		case '"':  TBUF_EMIT_LIT(t, L, "\\\""); break;
		case '\\': TBUF_EMIT_LIT(t, L, "\\\\"); break;
		case '\n': TBUF_EMIT_LIT(t, L, "\\n");  break;
		case '\r': TBUF_EMIT_LIT(t, L, "\\r");  break;
		case '\t': TBUF_EMIT_LIT(t, L, "\\t");  break;
		case '\b': TBUF_EMIT_LIT(t, L, "\\b");  break;
		case '\f': TBUF_EMIT_LIT(t, L, "\\f");  break;
		default:
			if (c < 0x20) {
				char esc[7];
				int  n = snprintf(esc, sizeof(esc), "\\u%04x", c);
				TbufEmit(t, L, esc, (size_t)n);
			} else {
				TbufEmitC(t, L, (char)c);
			}
		}
	}
	TbufEmitC(t, L, '"');
}

// Returns 1 if the string is a valid bare TOML key (A-Z a-z 0-9 - _).
static int IsBareKey(const char* s, size_t len) {
	if (len == 0)
		return 0;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];
		if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
			  (c >= '0' && c <= '9') || c == '-' || c == '_'))
			return 0;
	}
	return 1;
}

// Emit a TOML key — bare if possible, otherwise quoted.
static void EncKey(LuaToml* t, lua_State* L, const char* s, size_t len) {
	if (IsBareKey(s, len))
		TbufEmit(t, L, s, len);
	else
		EncString(t, L, s, len);
}

// Text of the table key copy on top of the stack. TOML keys are strings, so only
// string and number keys are accepted (numbers are converted in place on the copy).
static const char* KeyText(lua_State* L, size_t* len) {
	int kt = lua_type(L, -1);
	if (kt != LUA_TSTRING && kt != LUA_TNUMBER)
		luaL_error(L, "Toml: table keys must be strings or numbers (got %s)", lua_typename(L, kt));
	return lua_tolstring(L, -1, len);
}

static void EncValue(LuaToml* t, lua_State* L, int depth, int isInline);
static void EncTable(LuaToml* t, lua_State* L, int depth, const char* path, size_t pathLen);

// Encoder — classify a Lua table

// Returns 1 if the table on top of the stack is a pure array (1..n integer keys).
static int IsArray(lua_State* L) {
	int     tbl = lua_gettop(L);
	lua_Integer n   = (lua_Integer)lua_rawlen(L, tbl);
	lua_Integer cnt = 0;
	lua_pushnil(L);
	while (lua_next(L, tbl) != 0) {
		lua_pop(L, 1);
		cnt++;
		if (!lua_isinteger(L, -1)) {
			lua_pop(L, 1);
			return 0;
		}
		lua_Integer k = lua_tointeger(L, -1);
		if (k < 1 || k > n) {
			lua_pop(L, 1);
			return 0;
		}
	}
	return cnt == n;
}

// Returns 1 if a table contains any sub-table or array-of-tables children
// (meaning it must be emitted as a TOML section header, not inline).
static int HasTableChildren(lua_State* L) {
	int tbl = lua_gettop(L);
	lua_pushnil(L);
	while (lua_next(L, tbl) != 0) {
		if (lua_type(L, -1) == LUA_TTABLE) {
			lua_pop(L, 2);
			return 1;
		}
		lua_pop(L, 1);
	}
	return 0;
}

// How a table value under a key is written by EncTableBody.
enum {
	TOML_KIND_INLINE        = 0, // key = [...] / key = {} on the current section
	TOML_KIND_SECTION       = 1, // [path] header block
	TOML_KIND_TABLE_ARRAY   = 2  // [[path]] header block per element
};

// Classify the table on top of the stack. Empty tables and arrays that are
// not made up entirely of (non-array or empty) tables are written inline so
// they stay attached to the current section; string-keyed tables become
// sections; arrays of tables become [[path]] blocks.
static int TableKind(lua_State* L) {
	if (!IsArray(L))
		return TOML_KIND_SECTION;
	int         tbl = lua_gettop(L);
	lua_Integer n   = (lua_Integer)lua_rawlen(L, tbl);
	if (n == 0)
		return TOML_KIND_INLINE;
	for (lua_Integer i = 1; i <= n; i++) {
		lua_rawgeti(L, tbl, i);
		int ok = lua_type(L, -1) == LUA_TTABLE &&
				 (!IsArray(L) || lua_rawlen(L, -1) == 0);
		lua_pop(L, 1);
		if (!ok)
			return TOML_KIND_INLINE;
	}
	return TOML_KIND_TABLE_ARRAY;
}

// Encoder — inline value emitter (used for array elements and inline tables)

static void EncInlineArray(LuaToml* t, lua_State* L, int depth);
static void EncInlineTable(LuaToml* t, lua_State* L, int depth);

static void EncValue(LuaToml* t, lua_State* L, int depth, int isInline) {
	switch (lua_type(L, -1)) {
	case LUA_TNIL:
		// TOML has no null — emit empty string as closest approximation
		TBUF_EMIT_LIT(t, L, "\"\"");
		break;
	case LUA_TBOOLEAN:
		if (lua_toboolean(L, -1))
			TBUF_EMIT_LIT(t, L, "true");
		else
			TBUF_EMIT_LIT(t, L, "false");
		break;
	case LUA_TNUMBER:
		if (lua_isinteger(L, -1)) {
			char buf[32];
			int  n = snprintf(buf, sizeof(buf), "%lld", (long long)lua_tointeger(L, -1));
			TbufEmit(t, L, buf, (size_t)n);
		} else {
			double d = lua_tonumber(L, -1);
			if (d != d) {
				TBUF_EMIT_LIT(t, L, "nan");
			} else if (d == HUGE_VAL) {
				TBUF_EMIT_LIT(t, L, "inf");
			} else if (d == -HUGE_VAL) {
				TBUF_EMIT_LIT(t, L, "-inf");
			} else {
				char buf[64];
				int  n = snprintf(buf, sizeof(buf), "%.17g", d);
				// Ensure a decimal point so TOML recognises it as float.
				int hasDot = 0;
				for (int i = 0; i < n; i++) {
					if (buf[i] == '.' || buf[i] == 'e' || buf[i] == 'E' || buf[i] == 'n' || buf[i] == 'i') {
						hasDot = 1;
						break;
					}
				}
				TbufEmit(t, L, buf, (size_t)n);
				if (!hasDot)
					TBUF_EMIT_LIT(t, L, ".0");
			}
		}
		break;
	case LUA_TSTRING: {
		size_t      len;
		const char* s = lua_tolstring(L, -1, &len);
		EncString(t, L, s, len);
		break;
	}
	case LUA_TTABLE:
		if (IsArray(L))
			EncInlineArray(t, L, depth);
		else
			EncInlineTable(t, L, depth);
		break;
	case LUA_TUSERDATA:
		if (lua_isidentifier(L, -1)) {
			lua_pushidentifierstring(L, -1);
			size_t      len;
			const char* s = lua_tolstring(L, -1, &len);
			EncString(t, L, s, len);
			lua_pop(L, 1);
			break;
		}
		if (lua_isdatetime(L, -1)) {
			lua_pushdatetimestring(L, -1);
			size_t      len;
			const char* s = lua_tolstring(L, -1, &len);
			// Emit datetime without quotes — TOML has a native datetime type.
			TbufEmit(t, L, s, len);
			lua_pop(L, 1);
			break;
		}
		if (lua_isdecimal(L, -1)) {
			lua_pushdecimalstring(L, -1);
			size_t      len;
			const char* s = lua_tolstring(L, -1, &len);
			EncString(t, L, s, len);
			lua_pop(L, 1);
			break;
		}
		if (lua_isuint(L, -1)) {
			char buf[21];
			int  n = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)lua_touint(L, -1)->value);
			TbufEmit(t, L, buf, (size_t)n);
			break;
		}
		if (lua_istimespan(L, -1)) {
			lua_pushtimespanstring(L, -1);
			size_t      len;
			const char* s = lua_tolstring(L, -1, &len);
			EncString(t, L, s, len);
			lua_pop(L, 1);
			break;
		}
		TBUF_EMIT_LIT(t, L, "\"\"");
		break;
	default:
		TBUF_EMIT_LIT(t, L, "\"\"");
		break;
	}
}

static void EncInlineArray(LuaToml* t, lua_State* L, int depth) {
	int        tbl = lua_gettop(L);
	lua_Integer n   = (lua_Integer)lua_rawlen(L, tbl);
	RecPush(t, L, (uintptr_t)lua_topointer(L, tbl));
	TbufEmitC(t, L, '[');
	for (lua_Integer i = 1; i <= n; i++) {
		if (i > 1)
			TBUF_EMIT_LIT(t, L, ", ");
		lua_rawgeti(L, tbl, i);
		EncValue(t, L, depth + 1, 1);
		lua_pop(L, 1);
	}
	TbufEmitC(t, L, ']');
	RecPop(t);
}

static void EncInlineTable(LuaToml* t, lua_State* L, int depth) {
	int tbl   = lua_gettop(L);
	int first = 1;
	RecPush(t, L, (uintptr_t)lua_topointer(L, tbl));
	TBUF_EMIT_LIT(t, L, "{");
	lua_pushnil(L);
	while (lua_next(L, tbl) != 0) {
		if (!first)
			TBUF_EMIT_LIT(t, L, ", ");
		first = 0;
		// key
		lua_pushvalue(L, -2);
		size_t      klen;
		const char* k = KeyText(L, &klen);
		EncKey(t, L, k, klen);
		lua_pop(L, 1);
		TBUF_EMIT_LIT(t, L, " = ");
		EncValue(t, L, depth + 1, 1);
		lua_pop(L, 1);
	}
	TBUF_EMIT_LIT(t, L, "}");
	RecPop(t);
}

// Encoder — top-level section emitter

// Emit indent (2 spaces per level) when pretty mode is on.
static void EncIndent(LuaToml* t, lua_State* L, int depth) {
	if (!t->pretty)
		return;
	for (int i = 0; i < depth; i++)
		TBUF_EMIT_LIT(t, L, "  ");
}

// Forward: emit key = value lines for all scalar/array keys in a table,
// then recurse into sub-tables as [section] blocks.
static void EncTableBody(LuaToml* t, lua_State* L, int depth,
	const char* path, size_t pathLen);

static void EncTableBody(LuaToml* t, lua_State* L, int depth,
	const char* path, size_t pathLen) {
	int tbl = lua_gettop(L);
	RecPush(t, L, (uintptr_t)lua_topointer(L, tbl));

	// First pass: emit all key = value pairs that belong to this section:
	// scalars, arrays of scalars (or of arrays / mixed) and empty tables.
	// These must precede any [header] below, or they would land in it.
	lua_pushnil(L);
	while (lua_next(L, tbl) != 0) {
		int vtype = lua_type(L, -1);
		int kind  = vtype == LUA_TTABLE ? TableKind(L) : TOML_KIND_INLINE;
		if (kind != TOML_KIND_INLINE) {
			// Defer sub-tables and arrays of tables to second pass.
			lua_pop(L, 1);
			continue;
		}
		// key
		lua_pushvalue(L, -2);
		size_t      klen;
		const char* k = KeyText(L, &klen);
		EncIndent(t, L, depth);
		EncKey(t, L, k, klen);
		lua_pop(L, 1);
		TBUF_EMIT_LIT(t, L, " = ");
		if (vtype == LUA_TTABLE && lua_rawlen(L, -1) == 0)
			TBUF_EMIT_LIT(t, L, "{}"); // empty table round-trips as a table
		else
			EncValue(t, L, depth, 0);
		TbufEmitC(t, L, '\n');
		lua_pop(L, 1);
	}

	// Second pass: emit sub-tables as [section] or [[array]] headers.
	lua_pushnil(L);
	while (lua_next(L, tbl) != 0) {
		int kind = lua_type(L, -1) == LUA_TTABLE ? TableKind(L) : TOML_KIND_INLINE;
		if (kind == TOML_KIND_INLINE) {
			lua_pop(L, 1);
			continue;
		}

		// Encode this key as one dotted-path segment (bare or quoted) by
		// emitting it at the end of the output buffer and taking it back.
		lua_pushvalue(L, -2);
		size_t      klen;
		const char* k    = KeyText(L, &klen);
		size_t      mark = t->outLen;
		EncKey(t, L, k, klen);
		size_t segLen = t->outLen - mark;
		lua_pop(L, 1);

		// Build the dotted header path: each segment quoted individually. It is kept
		// as a Lua string below the value (key, path, value), so an error raised
		// while encoding the sub-table can't leak it.
		luaL_Buffer pb;
		luaL_buffinit(L, &pb);
		if (pathLen > 0) {
			luaL_addlstring(&pb, path, pathLen);
			luaL_addchar(&pb, '.');
		}
		luaL_addlstring(&pb, t->out + mark, segLen);
		luaL_pushresult(&pb);
		t->outLen = mark;
		lua_insert(L, -2);
		size_t      newPathLen;
		const char* newPath = lua_tolstring(L, -2, &newPathLen);

		if (kind == TOML_KIND_TABLE_ARRAY) {
			// Array of tables: emit [[path]] for each element.
			int        arr  = lua_gettop(L);
			lua_Integer n   = (lua_Integer)lua_rawlen(L, arr);
			for (lua_Integer i = 1; i <= n; i++) {
				lua_rawgeti(L, arr, i);
				if (t->pretty && t->outLen > 0)
					TbufEmitC(t, L, '\n');
				EncIndent(t, L, depth);
				TBUF_EMIT_LIT(t, L, "[[");
				TbufEmit(t, L, newPath, newPathLen);
				TBUF_EMIT_LIT(t, L, "]]\n");
				EncTableBody(t, L, depth + 1, newPath, newPathLen);
				lua_pop(L, 1);
			}
		} else {
			// Plain sub-table: emit [path] header.
			if (t->pretty && t->outLen > 0)
				TbufEmitC(t, L, '\n');
			EncIndent(t, L, depth);
			TBUF_EMIT_LIT(t, L, "[");
			TbufEmit(t, L, newPath, newPathLen);
			TBUF_EMIT_LIT(t, L, "]\n");
			EncTableBody(t, L, depth + 1, newPath, newPathLen);
		}

		lua_pop(L, 2);   // value and header path; the key stays for lua_next
	}

	RecPop(t);
}

// Decoder — tomlc99 DOM -> Lua tables

static void DecTable(lua_State* L, toml_table_t* tab);
static void DecArray(lua_State* L, toml_array_t* arr);

static void DecArray(lua_State* L, toml_array_t* arr) {
	char kind = toml_array_kind(arr);
	int  n    = toml_array_nelem(arr);
	lua_createtable(L, n, 0);

	for (int i = 0; i < n; i++) {
		if (kind == 't') {
			toml_table_t* sub = toml_table_at(arr, i);
			DecTable(L, sub);
		} else if (kind == 'a') {
			toml_array_t* sub = toml_array_at(arr, i);
			DecArray(L, sub);
		} else {
			// 'v' (value) or 'm' (mixed) — try each type in order.
			toml_datum_t d;
			d = toml_string_at(arr, i);
			if (d.ok) {
				lua_pushstring(L, d.u.s);
				free(d.u.s);
				goto pushed;
			}
			d = toml_int_at(arr, i);
			if (d.ok) {
				lua_pushinteger(L, (lua_Integer)d.u.i);
				goto pushed;
			}
			d = toml_double_at(arr, i);
			if (d.ok) {
				lua_pushnumber(L, (lua_Number)d.u.d);
				goto pushed;
			}
			d = toml_bool_at(arr, i);
			if (d.ok) {
				lua_pushboolean(L, d.u.b);
				goto pushed;
			}
			d = toml_timestamp_at(arr, i);
			if (d.ok) {
				// Push timestamp as an ISO 8601 string.
				char buf[64];
				toml_timestamp_t* ts = d.u.ts;
				if (ts->year && ts->hour) {
					snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d%s",
							 *ts->year, *ts->month, *ts->day,
							 *ts->hour, *ts->minute, *ts->second,
							 ts->z ? ts->z : "");
				} else if (ts->year) {
					snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
							 *ts->year, *ts->month, *ts->day);
				} else {
					snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
							 *ts->hour, *ts->minute, *ts->second);
				}
				free(ts);
				lua_pushstring(L, buf);
				goto pushed;
			}
			lua_pushnil(L);
			pushed:;
		}
		lua_rawseti(L, -2, i + 1);
	}
}

static void DecTable(lua_State* L, toml_table_t* tab) {
	lua_newtable(L);
	int idx = 0;
	const char* key;
	while ((key = toml_key_in(tab, idx++)) != NULL) {
		lua_pushstring(L, key);

		// Try sub-table first.
		toml_table_t* sub = toml_table_in(tab, key);
		if (sub) {
			DecTable(L, sub);
			lua_rawset(L, -3);
			continue;
		}

		// Try array.
		toml_array_t* arr = toml_array_in(tab, key);
		if (arr) {
			DecArray(L, arr);
			lua_rawset(L, -3);
			continue;
		}

		// Try scalar values.
		toml_datum_t d;
		d = toml_string_in(tab, key);
		if (d.ok) {
			lua_pushstring(L, d.u.s);
			free(d.u.s);
			lua_rawset(L, -3);
			continue;
		}
		d = toml_int_in(tab, key);
		if (d.ok) {
			lua_pushinteger(L, (lua_Integer)d.u.i);
			lua_rawset(L, -3);
			continue;
		}
		d = toml_double_in(tab, key);
		if (d.ok) {
			lua_pushnumber(L, (lua_Number)d.u.d);
			lua_rawset(L, -3);
			continue;
		}
		d = toml_bool_in(tab, key);
		if (d.ok) {
			lua_pushboolean(L, d.u.b);
			lua_rawset(L, -3);
			continue;
		}
		d = toml_timestamp_in(tab, key);
		if (d.ok) {
			char buf[64];
			toml_timestamp_t* ts = d.u.ts;
			if (ts->year && ts->hour) {
				snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d%s",
						 *ts->year, *ts->month, *ts->day,
						 *ts->hour, *ts->minute, *ts->second,
						 ts->z ? ts->z : "");
			} else if (ts->year) {
				snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
						 *ts->year, *ts->month, *ts->day);
			} else {
				snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
						 *ts->hour, *ts->minute, *ts->second);
			}
			free(ts);
			lua_pushstring(L, buf);
			lua_rawset(L, -3);
			continue;
		}

		// Unknown — push nil value, pop the key.
		lua_pop(L, 1);
	}
}


int Toml_Encode(lua_State* L) {
	LuaToml* t = luaL_checktoml(L, 1);
	luaL_checktype(L, 2, LUA_TTABLE);

	t->outLen = 0;
	t->recLen = 0;

	lua_pushvalue(L, 2);
	EncTableBody(t, L, 0, "", 0);
	lua_pop(L, 1);

	lua_pushlstring(L, t->out ? t->out : "", t->outLen);
	return 1;
}

int Toml_Decode(lua_State* L) {
	luaL_checktoml(L, 1);
	size_t      len;
	const char* src = luaL_checklstring(L, 2, &len);
	SkipUtf8Bom(&src, &len);

	// toml_parse requires a NUL-terminated, mutable string.
	char* buf = (char*)kitsune_malloc(len + 1);
	if (!buf)
		luaL_error(L, "Toml: out of memory");
	memcpy(buf, src, len);
	buf[len] = '\0';

	char errbuf[256];
	toml_table_t* tab = toml_parse(buf, errbuf, sizeof(errbuf));
	kitsune_free(buf);

	if (!tab) {
		lua_pushnil(L);
		lua_pushstring(L, errbuf);
		return 2;
	}

	DecTable(L, tab);
	toml_free(tab);
	return 1;
}
