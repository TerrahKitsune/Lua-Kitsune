#include "luajson.h"

// Unique address used as the JSON null sentinel.
// Both encoder and decoder reference this directly — no registry lookup needed.
static char g_json_null;
void* lua_json_null(void) {
	return &g_json_null;
}

// =============================================================================
// Instance management
// =============================================================================

LuaJson* lua_json_push(lua_State* L) {
	LuaJson* j = (LuaJson*)lua_newuserdata(L, sizeof(LuaJson));
	memset(j, 0, sizeof(LuaJson));
	luaL_setmetatable(L, LUAJSON);
	return j;
}

LuaJson* lua_json_check(lua_State* L, int idx) {
	return (LuaJson*)luaL_checkudata(L, idx, LUAJSON);
}

int lua_json_gc(lua_State* L) {
	LuaJson* j = lua_json_check(L, 1);
	if (j->out) {
		gff_free(j->out);
		j->out = NULL;
	}
	if (j->rec) {
		gff_free(j->rec);
		j->rec = NULL;
	}
	return 0;
}

int lua_json_tostring(lua_State* L) {
	lua_pushfstring(L, "Json: %p", lua_json_check(L, 1));
	return 1;
}

int lua_json_new(lua_State* L) {
	int      pretty = lua_isboolean(L, 1) ? lua_toboolean(L, 1) : 0;
	LuaJson* j      = lua_json_push(L);
	j->pretty       = pretty;
	return 1;
}

// =============================================================================
// Encoder — buffer helpers
// =============================================================================

static void jbuf_grow(LuaJson* j, lua_State* L, size_t need) {
	if (j->outLen + need <= j->outCap)
		return;
	size_t cap = j->outCap ? j->outCap * 2 : 512;
	while (cap < j->outLen + need)
		cap *= 2;
	char* p = (char*)gff_realloc(j->out, cap);
	if (!p)
		luaL_error(L, "Json: out of memory");
	j->out    = p;
	j->outCap = cap;
}

static void jbuf_emit(LuaJson* j, lua_State* L, const char* data, size_t len) {
	jbuf_grow(j, L, len);
	memcpy(j->out + j->outLen, data, len);
	j->outLen += len;
}

static void jbuf_emitc(LuaJson* j, lua_State* L, char c) {
	jbuf_grow(j, L, 1);
	j->out[j->outLen++] = c;
}

// For string literals only — sizeof gives the compile-time length.
#define jbuf_emitlit(j, L, s) jbuf_emit(j, L, "" s, sizeof(s) - 1)

// =============================================================================
// Encoder — anti-recursion
// =============================================================================

static void rec_push(LuaJson* j, lua_State* L, uintptr_t addr) {
	for (size_t i = 0; i < j->recLen; i++)
		if (j->rec[i] == addr)
			luaL_error(L, "Json: recursion detected");
	if (j->recLen == j->recCap) {
		size_t     cap = j->recCap ? j->recCap * 2 : 8;
		uintptr_t* p   = (uintptr_t*)gff_realloc(j->rec, cap * sizeof(uintptr_t));
		if (!p)
			luaL_error(L, "Json: out of memory");
		j->rec    = p;
		j->recCap = cap;
	}
	j->rec[j->recLen++] = addr;
}

static void rec_pop(LuaJson* j) {
	if (j->recLen > 0)
		j->recLen--;
}

// =============================================================================
// Encoder — value functions
// =============================================================================

static void enc_value(LuaJson* j, lua_State* L, int depth);

static void enc_indent(LuaJson* j, lua_State* L, int depth) {
	if (!j->pretty)
		return;
	jbuf_emitc(j, L, '\n');
	for (int i = 0; i < depth; i++)
		jbuf_emit(j, L, "  ", 2);
}

// Encodes the Lua string at the top of the stack as a JSON string.
static void enc_string(LuaJson* j, lua_State* L) {
	size_t      len;
	const char* s = lua_tolstring(L, -1, &len);
	jbuf_emitc(j, L, '"');
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];
		switch (c) {
		case '"':  jbuf_emitlit(j, L, "\\\""); break;
		case '\\': jbuf_emitlit(j, L, "\\\\"); break;
		case '\n': jbuf_emitlit(j, L, "\\n");  break;
		case '\r': jbuf_emitlit(j, L, "\\r");  break;
		case '\t': jbuf_emitlit(j, L, "\\t");  break;
		case '\b': jbuf_emitlit(j, L, "\\b");  break;
		case '\f': jbuf_emitlit(j, L, "\\f");  break;
		default:
			if (c < 0x20) {
				char esc[7];
				snprintf(esc, sizeof(esc), "\\u%04x", c);
				jbuf_emit(j, L, esc, 6);
			} else {
				jbuf_emitc(j, L, (char)c);
			}
		}
	}
	jbuf_emitc(j, L, '"');
}

// Encodes the Lua number at the top of the stack as a JSON number.
static void enc_number(LuaJson* j, lua_State* L) {
	if (lua_isinteger(L, -1)) {
		char buf[32];
		int  n = snprintf(buf, sizeof(buf), "%lld", (long long)lua_tointeger(L, -1));
		jbuf_emit(j, L, buf, (size_t)n);
		return;
	}
	double d = lua_tonumber(L, -1);
	if (isnan(d)) {
		jbuf_emitlit(j, L, "null");
		return;
	}
	if (isinf(d)) {
		if (d > 0)
			jbuf_emitlit(j, L, "1e+9999");
		else
			jbuf_emitlit(j, L, "-1e+9999");
		return;
	}
	char buf[64];
	int  n = snprintf(buf, sizeof(buf), "%.16g", d);
	jbuf_emit(j, L, buf, (size_t)n);
}

// Returns true if the table at absolute index `tbl` is a sequence (keys 1..n).
// A truly empty table encodes as [] (empty array).
static bool is_sequence(lua_State* L, int tbl) {
	lua_Integer n = (lua_Integer)lua_rawlen(L, tbl);
	if (n == 0) {
		// Truly empty table → [] (empty array).
		// Table with only string/mixed keys → {} (object).
		lua_pushnil(L);
		if (lua_next(L, tbl) == 0)
			return true;  // no keys at all
		lua_pop(L, 2);  // pop key + value
		return false;
	}
	lua_Integer count = 0;
	lua_pushnil(L);
	while (lua_next(L, tbl) != 0) {
		lua_pop(L, 1);  // pop value, keep key
		if (!lua_isinteger(L, -1)) {
			lua_pop(L, 1);
			return false;
		}
		lua_Integer k = lua_tointeger(L, -1);
		if (k < 1 || k > n) {
			lua_pop(L, 1);
			return false;
		}
		count++;
	}
	return count == n;
}

// Encodes the table at the top of the stack.
static void enc_table(LuaJson* j, lua_State* L, int depth) {
	int tbl = lua_gettop(L);
	rec_push(j, L, (uintptr_t)lua_topointer(L, tbl));

	if (is_sequence(L, tbl)) {
		lua_Integer n = (lua_Integer)lua_rawlen(L, tbl);
		jbuf_emitc(j, L, '[');
		for (lua_Integer i = 1; i <= n; i++) {
			if (i > 1)
				jbuf_emitc(j, L, ',');
			enc_indent(j, L, depth + 1);
			lua_rawgeti(L, tbl, i);
			enc_value(j, L, depth + 1);
			lua_pop(L, 1);
		}
		if (n > 0)
			enc_indent(j, L, depth);
		jbuf_emitc(j, L, ']');
	} else {
		jbuf_emitc(j, L, '{');
		bool first = true;
		lua_pushnil(L);
		while (lua_next(L, tbl) != 0) {
			if (!first)
				jbuf_emitc(j, L, ',');
			enc_indent(j, L, depth + 1);
			first = false;
			luaL_tolstring(L, -2, NULL);  // push string form of key
			enc_string(j, L);
			lua_pop(L, 1);                // pop key string
			jbuf_emitlit(j, L, ":");
			if (j->pretty)
				jbuf_emitc(j, L, ' ');
			enc_value(j, L, depth + 1);  // encode value at -1
			lua_pop(L, 1);               // pop value
		}
		if (!first)
			enc_indent(j, L, depth);
		jbuf_emitc(j, L, '}');
	}

	rec_pop(j);
}

// Encodes the value at the top of the stack.
static void enc_value(LuaJson* j, lua_State* L, int depth) {
	// Check null sentinel before anything else
	if (lua_islightuserdata(L, -1) && lua_topointer(L, -1) == lua_json_null()) {
		jbuf_emitlit(j, L, "null");
		return;
	}
	switch (lua_type(L, -1)) {
	case LUA_TNIL:
		jbuf_emitlit(j, L, "null");
		break;
	case LUA_TBOOLEAN:
		if (lua_toboolean(L, -1))
			jbuf_emitlit(j, L, "true");
		else
			jbuf_emitlit(j, L, "false");
		break;
	case LUA_TNUMBER:
		enc_number(j, L);
		break;
	case LUA_TSTRING:
		enc_string(j, L);
		break;
	case LUA_TTABLE:
		enc_table(j, L, depth);
		break;
	default:
		// Fallback: use __tostring (handles Wchar, Stream, etc.)
		luaL_tolstring(L, -1, NULL);  // push string representation
		enc_string(j, L);
		lua_pop(L, 1);                // pop the string
		break;
	}
}

// =============================================================================
// Decoder — read helpers
// =============================================================================

static char jread_next(LuaJson* j) {
	if (j->ungetLen > 0)
		return j->unget[--j->ungetLen];
	if (j->srcPos >= j->srcLen)
		return '\0';
	char c = j->src[j->srcPos++];
	if (c == '\n') {
		j->errLine++;
		j->errCol = 0;
	} else {
		j->errCol++;
	}
	return c;
}

static void jread_unget(LuaJson* j, char c) {
	if (j->ungetLen < 8)
		j->unget[j->ungetLen++] = c;
}

static char jread_skip(LuaJson* j) {
	char c;
	while ((c = jread_next(j)) != '\0')
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
			return c;
	return '\0';
}

// =============================================================================
// Decoder — UTF-8 helper
// =============================================================================

static int utf8_encode(unsigned int cp, char* out) {
	if (cp <= 0x7F) {
		out[0] = (char)cp;
		return 1;
	}
	if (cp <= 0x7FF) {
		out[0] = (char)(0xC0 | (cp >> 6));
		out[1] = (char)(0x80 | (cp & 0x3F));
		return 2;
	}
	if (cp <= 0xFFFF) {
		out[0] = (char)(0xE0 | (cp >> 12));
		out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char)(0x80 | (cp & 0x3F));
		return 3;
	}
	out[0] = (char)(0xF0 | (cp >> 18));
	out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char)(0x80 | ((cp >> 6)  & 0x3F));
	out[3] = (char)(0x80 | (cp & 0x3F));
	return 4;
}

// =============================================================================
// Decoder — value functions
// =============================================================================

static void dec_value(LuaJson* j, lua_State* L);

// Decodes a JSON string; the opening '"' has already been consumed.
// Pushes the resulting Lua string onto the stack.
static void dec_string(LuaJson* j, lua_State* L) {
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	for (;;) {
		char c = jread_next(j);
		if (c == '\0')
			luaL_error(L, "Json: unterminated string at line %zu", j->errLine + 1);
		if (c == '"')
			break;
		if (c != '\\') {
			luaL_addchar(&b, c);
			continue;
		}
		char esc = jread_next(j);
		switch (esc) {
		case '"':  luaL_addchar(&b, '"');  break;
		case '\\': luaL_addchar(&b, '\\'); break;
		case '/':  luaL_addchar(&b, '/');  break;
		case 'n':  luaL_addchar(&b, '\n'); break;
		case 'r':  luaL_addchar(&b, '\r'); break;
		case 't':  luaL_addchar(&b, '\t'); break;
		case 'b':  luaL_addchar(&b, '\b'); break;
		case 'f':  luaL_addchar(&b, '\f'); break;
		case 'u': {
			char hex[5] = { 0 };
			for (int k = 0; k < 4; k++) {
				hex[k] = jread_next(j);
				if (!hex[k])
					luaL_error(L, "Json: unexpected end in \\u escape");
			}
			unsigned int cp = (unsigned int)strtoul(hex, NULL, 16);
			if (cp >= 0xD800 && cp <= 0xDBFF) {
				char p1 = jread_next(j);
				char p2 = jread_next(j);
				if (p1 == '\\' && p2 == 'u') {
					char hex2[5] = { 0 };
					for (int k = 0; k < 4; k++)
						hex2[k] = jread_next(j);
					unsigned int low = (unsigned int)strtoul(hex2, NULL, 16);
					if (low >= 0xDC00 && low <= 0xDFFF)
						cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
					// Invalid low surrogate: encode high alone, discard low hex
				} else {
					jread_unget(j, p2);
					jread_unget(j, p1);
				}
			}
			char utf8[5] = { 0 };
			int  n       = utf8_encode(cp, utf8);
			luaL_addlstring(&b, utf8, (size_t)n);
			break;
		}
		default:
			luaL_error(L, "Json: unknown escape '\\%c' at line %zu", esc, j->errLine + 1);
		}
	}
	luaL_pushresult(&b);
}

// Decodes a JSON number; `first` is the already-consumed first character.
static void dec_number(LuaJson* j, lua_State* L, char first) {
	char buf[64];
	int  n        = 0;
	bool is_float = false;
	buf[n++]      = first;
	for (;;) {
		char c = jread_next(j);
		if (c == '.' || c == 'e' || c == 'E')
			is_float = true;
		if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
		    c == '+' || c == '-') {
			if (n < (int)sizeof(buf) - 1)
				buf[n++] = c;
		} else {
			jread_unget(j, c);
			break;
		}
	}
	buf[n] = '\0';
	if (!is_float) {
		char*     end;
		long long v = strtoll(buf, &end, 10);
		if (end != buf && *end == '\0') {
			lua_pushinteger(L, (lua_Integer)v);
			return;
		}
	}
	lua_pushnumber(L, strtod(buf, NULL));
}

// Decodes a JSON object; the opening '{' has already been consumed.
static void dec_object(LuaJson* j, lua_State* L) {
	lua_newtable(L);
	char c = jread_skip(j);
	if (c == '}')
		return;
	jread_unget(j, c);
	for (;;) {
		c = jread_skip(j);
		if (c != '"')
			luaL_error(L, "Json: expected string key at line %zu", j->errLine + 1);
		dec_string(j, L);          // push key
		c = jread_skip(j);
		if (c != ':')
			luaL_error(L, "Json: expected ':' at line %zu", j->errLine + 1);
		dec_value(j, L);           // push value
		lua_rawset(L, -3);         // table[key] = value
		c = jread_skip(j);
		if (c == '}')
			break;
		if (c != ',')
			luaL_error(L, "Json: expected ',' or '}' at line %zu", j->errLine + 1);
	}
}

// Decodes a JSON array; the opening '[' has already been consumed.
static void dec_array(LuaJson* j, lua_State* L) {
	lua_newtable(L);
	char c = jread_skip(j);
	if (c == ']')
		return;
	jread_unget(j, c);
	int idx = 0;
	for (;;) {
		dec_value(j, L);
		lua_rawseti(L, -2, ++idx);
		c = jread_skip(j);
		if (c == ']')
			break;
		if (c != ',')
			luaL_error(L, "Json: expected ',' or ']' at line %zu", j->errLine + 1);
	}
}

// Dispatches to the appropriate decoder based on the first non-whitespace char.
// Pushes exactly one Lua value onto the stack.
static void dec_value(LuaJson* j, lua_State* L) {
	char c = jread_skip(j);
	switch (c) {
	case '{': dec_object(j, L); break;
	case '[': dec_array(j, L);  break;
	case '"': dec_string(j, L); break;
	case 't': {
		char r = jread_next(j), u = jread_next(j), e = jread_next(j);
		if (r != 'r' || u != 'u' || e != 'e')
			luaL_error(L, "Json: invalid token at line %zu", j->errLine + 1);
		lua_pushboolean(L, 1);
		break;
	}
	case 'f': {
		char a = jread_next(j), l = jread_next(j),
		     s = jread_next(j), e = jread_next(j);
		if (a != 'a' || l != 'l' || s != 's' || e != 'e')
			luaL_error(L, "Json: invalid token at line %zu", j->errLine + 1);
		lua_pushboolean(L, 0);
		break;
	}
	case 'n': {
		char u = jread_next(j), l1 = jread_next(j), l2 = jread_next(j);
		if (u != 'u' || l1 != 'l' || l2 != 'l')
			luaL_error(L, "Json: invalid token at line %zu", j->errLine + 1);
		lua_pushlightuserdata(L, lua_json_null());
		break;
	}
	case '\0':
		luaL_error(L, "Json: unexpected end of input");
		break;
	default:
		if (c == '-' || (c >= '0' && c <= '9')) {
			dec_number(j, L, c);
			break;
		}
		luaL_error(L, "Json: unexpected character '%c' at line %zu col %zu",
		           c, j->errLine + 1, j->errCol);
	}
}

// =============================================================================
// Reset helpers
// =============================================================================

static void enc_reset(LuaJson* j) {
	j->outLen = 0;
	j->recLen = 0;
}

static void dec_reset(LuaJson* j, const char* src, size_t len) {
	j->src      = src;
	j->srcLen   = len;
	j->srcPos   = 0;
	j->ungetLen = 0;
	j->errLine  = 0;
	j->errCol   = 0;
}

// =============================================================================
// Instance method entry points — also handle the static calling convention:
//   json:Decode(str)               arg 1 = LuaJson instance, arg 2 = str
//   Json.Decode(str)               arg 1 = str  (stack-local state, no heap needed)
//   json:Encode(value)             arg 1 = LuaJson instance, arg 2 = value
//   Json.Encode(value [, pretty])  arg 1 = value  (GC-managed temp instance)
// =============================================================================

int lua_json_decode(lua_State* L) {
	LuaJson*    j = (LuaJson*)luaL_testudata(L, 1, LUAJSON);
	size_t      len;
	const char* s;
	LuaJson     tmp;
	if (j) {
		s = luaL_checklstring(L, 2, &len);
	} else {
		memset(&tmp, 0, sizeof(tmp));
		j = &tmp;
		s = luaL_checklstring(L, 1, &len);
	}
	dec_reset(j, s, len);
	dec_value(j, L);
	return 1;
}

int lua_json_encode(lua_State* L) {
	LuaJson* j = (LuaJson*)luaL_testudata(L, 1, LUAJSON);
	int value_idx;
	if (j) {
		value_idx = 2;
	} else {
		int pretty  = lua_toboolean(L, 2);
		lua_settop(L, 1);                    // [value(1)]
		j           = lua_json_push(L);      // [value(1), j(2)]
		j->pretty   = pretty;
		lua_insert(L, 1);                    // [j(1), value(2)]
		value_idx   = 2;
	}
	luaL_checkany(L, value_idx);
	enc_reset(j);
	lua_pushvalue(L, value_idx);
	enc_value(j, L, 0);
	lua_pop(L, 1);
	lua_pushlstring(L, j->out ? j->out : "", j->outLen);
	return 1;
}