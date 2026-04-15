#include "luajson.h"
#include "stream.h"
#include "luawchar.h"
#include "luaidentifier.h"

// Unique address used as the JSON null sentinel.
// Both encoder and decoder reference this directly — no registry lookup needed.
static char g_json_null;
// Unique address used as the Lua registry key for the shared bridge LuaJson instance.
static char g_bridge_json_key;
void* lua_json_bridge_registry_key(void) {
	return &g_bridge_json_key;
}

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
		kitsune_free(j->out);
		j->out = NULL;
	}
	if (j->rec) {
		kitsune_free(j->rec);
		j->rec = NULL;
	}
	if (j->chunkBuf) {
		kitsune_free(j->chunkBuf);
		j->chunkBuf = NULL;
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
	// Streaming mode: flush what we have to the stream before growing so
	// the output buffer never exceeds more than one buffer-worth of memory.
	if (j->encStream && j->outLen > 0) {
		lua_stream_write_bytes(L, j->encStream, j->out, j->outLen);
		lua_pop(L, 1);  // discard the boolean result
		j->outLen = 0;
		if (need <= j->outCap)
			return;
	}
	size_t cap = j->outCap ? j->outCap * 2 : 512;
	while (cap < j->outLen + need)
		cap *= 2;
	char* p = (char*)kitsune_realloc(j->out, cap);
	if (!p) {
		luaL_error(L, "Json: out of memory");
		return;
	}
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
	for (size_t i = 0; i < j->recLen; i++) {
		if (j->rec[i] == addr)
			luaL_error(L, "Json: recursion detected");
	}

	if (j->recLen == j->recCap) {
		size_t     cap = j->recCap ? j->recCap * 2 : 8;
		uintptr_t* p   = (uintptr_t*)kitsune_realloc(j->rec, cap * sizeof(uintptr_t));

		if (!p) {
			luaL_error(L, "Json: out of memory");
			return;
		}

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

// Encodes the table at the top of the stack.
static void enc_table(LuaJson* j, lua_State* L, int depth) {
	int tbl = lua_gettop(L);
	rec_push(j, L, (uintptr_t)lua_topointer(L, tbl));

	// -- Classify: single scan for sequence vs object --------------------------
	// Walk all key-value pairs once with lua_next.  Track whether every key is
	// an integer in [1..n]; exit immediately on the first non-sequence key so
	// pure-object tables pay O(1) instead of O(n) for the classification step.
	lua_Integer n     = (lua_Integer)lua_rawlen(L, tbl);
	lua_Integer count = 0;
	bool        seq   = true;

	lua_pushnil(L);
	while (lua_next(L, tbl) != 0) {
		lua_pop(L, 1);   // discard value; keep key for type check
		count++;
		if (seq) {
			if (!lua_isinteger(L, -1)) {
				seq = false;
			} else {
				lua_Integer k = lua_tointeger(L, -1);
				if (k < 1 || k > n)
					seq = false;
			}
			if (!seq) {
				lua_pop(L, 1);   // pop the key that failed
				break;           // remaining pairs are irrelevant
			}
		}
	}
	seq = seq && (count == n);

	// -- Encode ----------------------------------------------------------------
	if (seq) {
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
			first = false;
			enc_indent(j, L, depth + 1);
			luaL_tolstring(L, -2, NULL);   // push string form of key
			enc_string(j, L);
			lua_pop(L, 1);                 // pop key string
			jbuf_emitlit(j, L, ":");
			if (j->pretty)
				jbuf_emitc(j, L, ' ');
			enc_value(j, L, depth + 1);   // encode value at -1
			lua_pop(L, 1);                 // pop value
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
	case LUA_TUSERDATA:
		if (lua_iswchar(L, -1)) {
			// Convert to UTF-8 via the existing Wchar helper, then encode as a JSON string.
			ToUtf8(L);            // pushes a UTF-8 Lua string
			enc_string(j, L);
			lua_pop(L, 1);
			break;
		}
		if (lua_isidentifier(L, -1)) {
			lua_identifier_push_string(L, -1);
			enc_string(j, L);
			lua_pop(L, 1);
			break;
		}
		if (lua_isstream(L, -1)) {
			LuaStream* s = lua_toluastream(L, -1);
			if (s && (s->Caps & STREAM_CAP_READ) && (s->Caps & STREAM_CAP_SEEK)) {
				lua_Integer saved = lua_stream_curpos(L, s);
				lua_stream_setpos(L, s, 0);
				lua_stream_read_chunk(L, s, 0);  // pushes Lua string or nil on empty/error
				if (lua_type(L, -1) == LUA_TSTRING)
					enc_string(j, L);
				else
					jbuf_emitlit(j, L, "null");
				lua_pop(L, 1);
				lua_stream_setpos(L, s, saved);
				break;
			}
		}
		// Non-stream userdata falls through to null.
		jbuf_emitlit(j, L, "null");
		break;
	default:
		// Functions, threads, and non-null userdata are not representable in
		// JSON — emit null so the output stays valid and callers can detect the
		// gap from the missing value rather than from a garbage pointer string.
		jbuf_emitlit(j, L, "null");
		break;
	}
}

// =============================================================================
// Decoder — read helpers
// =============================================================================

static char jread_next(LuaJson* j) {
	if (j->ungetLen > 0)
		return j->unget[--j->ungetLen];
	while (j->srcPos >= j->srcLen) {
		if (!j->chunkFnIdx)
			return '\0';
		lua_State*  L  = j->chunkL;
		lua_pushvalue(L, j->chunkFnIdx);
		lua_call_nohook(L, 0, 1);
		if (lua_type(L, -1) != LUA_TSTRING) {
			lua_pop(L, 1);
			j->chunkFnIdx = 0;
			return '\0';
		}
		size_t      clen;
		const char* cs = lua_tolstring(L, -1, &clen);
		if (clen == 0) {
			lua_pop(L, 1);
			j->chunkFnIdx = 0;
			return '\0';
		}
		if (clen > j->chunkBufCap) {
			char* p = (char*)kitsune_realloc(j->chunkBuf, clen);
			if (!p) {
				lua_pop(L, 1);
				luaL_error(L, "Json: out of memory");
			}
			j->chunkBuf    = p;
			j->chunkBufCap = clen;
		}
		memcpy(j->chunkBuf, cs, clen);
		lua_pop(L, 1);
		j->src    = j->chunkBuf;
		j->srcLen = clen;
		j->srcPos = 0;
	}
	char c = j->src[j->srcPos++];
	if (c == '\n') {
		j->errLine++;
		j->errCol = 0;
	} else {
		j->errCol++;
	}
	return c;
}

static void jread_unget(LuaJson* j, lua_State* L, char c) {
	if (j->ungetLen >= (int)(sizeof j->unget)) {
		luaL_error(L, "Json: unget buffer overflow (internal error)");
		return;
	}
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
					jread_unget(j, L, p2);
					jread_unget(j, L, p1);
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
			if (n >= (int)sizeof(buf) - 1)
				luaL_error(L, "Json: number literal too long at line %zu", j->errLine + 1);
			buf[n++] = c;
		} else {
			jread_unget(j, L, c);
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
	jread_unget(j, L, c);
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
	jread_unget(j, L, c);
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
	j->outLen    = 0;
	j->recLen    = 0;
	j->encStream = NULL;
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
// Instance method entry points (instance required as arg 1):
//   json:Decode(str | fn | stream)  arg 2 = string, chunk-reader fn, or LuaStream
//   json:Encode(value)              arg 2 = Lua value to encode
// =============================================================================

// Forward declaration — defined in the Stream I/O section below.
static int json_stream_chunk_reader(lua_State* L);

// Async stream decode continuation.
// Stack invariant on every entry: L[1]=json, L[2]=source stream, L[3]=in-memory
// accumulator stream, top=latest chunk (string) or nil/false.
// Accumulates chunks via lua_callk (yielding for async streams), then parses
// the complete buffer synchronously once EOF is signalled.
static int JsonDecodeAsyncContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaStream* accum = lua_toluastream(L, 3);

	if (lua_type(L, -1) == LUA_TSTRING) {
		size_t len = 0;
		const char* data = lua_tolstring(L, -1, &len);
		if (len > 0)
			accum->vtbl->write(accum->native, (const BYTE*)data, len);
		lua_pop(L, 1);
		lua_pushvalue(L, 2);
		lua_getfield(L, -1, "Read");
		lua_insert(L, -2);
		lua_callk(L, 1, 1, ctx, JsonDecodeAsyncContinuation);
		return JsonDecodeAsyncContinuation(L, LUA_OK, ctx);
	}

	lua_pop(L, 1);  // pop nil / false — EOF

	lua_Integer total = accum->vtbl->getlen(accum->native);
	if (total <= 0) {
		lua_pushnil(L);
		return 1;
	}
	accum->vtbl->setpos(accum->native, 0);
	accum->vtbl->read(accum->native, L, (size_t)total);
	if (lua_type(L, -1) != LUA_TSTRING) {
		lua_pop(L, 1);
		lua_pushnil(L);
		return 1;
	}

	size_t srcLen = 0;
	const char* src = lua_tolstring(L, -1, &srcLen);
	LuaJson* j = lua_json_check(L, 1);
	dec_reset(j, src, srcLen);
	dec_value(j, L);  // pushes decoded value; accumulated string stays rooted below it
	return 1;
}

int lua_json_decode(lua_State* L) {
	LuaJson*    j = lua_json_check(L, 1);
	size_t      len;
	const char* s;

	if (lua_isstream(L, 2)) {
		LuaStream* st = lua_toluastream(L, 2);
		if (!(st->Caps & STREAM_CAP_READ)) {
			lua_pushnil(L);
			lua_pushstring(L, "stream is not readable");
			return 2;
		}
		if (st->vtbl && st->vtbl->hasdata) {
			// Async stream: accumulate chunks via lua_callk (may yield), then parse.
			lua_pushluastream(L);           // L[3] = accumulator
			lua_pushvalue(L, 2);
			lua_getfield(L, -1, "Read");
			lua_insert(L, -2);
			lua_callk(L, 1, 1, 0, JsonDecodeAsyncContinuation);
			return JsonDecodeAsyncContinuation(L, LUA_OK, 0);
		}
		// Sync stream: pull 4 KiB chunks directly without yielding.
		lua_pushlightuserdata(L, st);
		lua_pushcclosure(L, json_stream_chunk_reader, 1);
		dec_reset(j, NULL, 0);
		j->chunkFnIdx = lua_gettop(L);
		j->chunkL     = L;
		dec_value(j, L);
		j->chunkFnIdx = 0;
		j->chunkL     = NULL;
		return 1;
	}

	if (lua_isfunction(L, 2)) {
		dec_reset(j, NULL, 0);
		j->chunkFnIdx = 2;
		j->chunkL     = L;
	} else {
		s = luaL_checklstring(L, 2, &len);
		dec_reset(j, s, len);
	}

	dec_value(j, L);

	j->chunkFnIdx = 0;
	j->chunkL     = NULL;
	return 1;
}

int lua_json_encode(lua_State* L) {
	LuaJson* j = lua_json_check(L, 1);
	luaL_checkany(L, 2);
	enc_reset(j);
	lua_pushvalue(L, 2);
	enc_value(j, L, 0);
	lua_pop(L, 1);
	lua_pushlstring(L, j->out ? j->out : "", j->outLen);
	return 1;
}

// =============================================================================
// Stream I/O
// =============================================================================

// C closure used by the sync-stream decode path: called repeatedly by the chunked
// decoder.  Upvalue 1: LuaStream* (lightuserdata).
// Returns a non-empty string for each 4 KiB chunk, or nil on EOF.
static int json_stream_chunk_reader(lua_State* L) {
	LuaStream* st = (LuaStream*)lua_touserdata(L, lua_upvalueindex(1));
	return lua_stream_read_chunk(L, st, 4096);
}

// json:EncodeIntoStream(stream, value)
// Encodes a Lua value as JSON and writes the result directly into a stream.
int lua_json_encode_into_stream(lua_State* L) {
	LuaJson*   j  = lua_json_check(L, 1);
	int streamIdx = 2;
	int valueIdx  = 3;

	if (!lua_isstream(L, streamIdx))
		return luaL_argerror(L, streamIdx, "stream expected");

	LuaStream* st = lua_toluastream(L, streamIdx);
	if (!(st->Caps & STREAM_CAP_WRITE)) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "stream is not writable");
		return 2;
	}

	enc_reset(j);        // also clears any stale encStream from a previous error
	j->encStream = st;

	lua_pushvalue(L, valueIdx);
	enc_value(j, L, 0);
	lua_pop(L, 1);

	// Flush bytes still buffered after the last grow boundary.
	if (j->outLen > 0) {
		lua_stream_write_bytes(L, st, j->out, j->outLen);
		j->outLen = 0;
	} else {
		lua_pushboolean(L, true);
	}

	j->encStream = NULL;
	return 1;
}

// json:DecodeIntoStream(stream)
// Decodes one JSON value from a stream.  For seekable sync streams, seeks back
// past any over-read bytes so consecutive calls each get one value.  Async
// streams use the same accumulate-then-parse path as json:Decode(stream).
int lua_json_decode_into_stream(lua_State* L) {
	LuaJson*   j  = lua_json_check(L, 1);
	int streamIdx = 2;

	if (!lua_isstream(L, streamIdx))
		return luaL_argerror(L, streamIdx, "stream expected");

	LuaStream* st = lua_toluastream(L, streamIdx);
	if (!(st->Caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		lua_pushstring(L, "stream is not readable");
		return 2;
	}

	if (st->vtbl && st->vtbl->hasdata) {
		// Async stream: same accumulate-via-lua_callk path as lua_json_decode.
		// Seekback is not applicable — async streams are never seekable.
		lua_pushluastream(L);           // L[3] = accumulator
		lua_pushvalue(L, 2);
		lua_getfield(L, -1, "Read");
		lua_insert(L, -2);
		lua_callk(L, 1, 1, 0, JsonDecodeAsyncContinuation);
		return JsonDecodeAsyncContinuation(L, LUA_OK, 0);
	}

	// Sync stream: pull 4 KiB chunks directly without yielding.
	lua_pushlightuserdata(L, st);
	lua_pushcclosure(L, json_stream_chunk_reader, 1);
	int fnIdx = lua_gettop(L);

	dec_reset(j, NULL, 0);
	j->chunkFnIdx = fnIdx;
	j->chunkL     = L;

	dec_value(j, L);

	j->chunkFnIdx = 0;
	j->chunkL     = NULL;

	// The chunk reader fetches 4 KiB at a time, so it may have read bytes that
	// belong to the NEXT value in the stream.  Seek back past the unconsumed bytes
	// so consecutive DecodeIntoStream calls each get one value.
	if (st->Caps & STREAM_CAP_SEEK) {
		size_t unconsumed = (j->srcLen - j->srcPos) + (size_t)j->ungetLen;
		if (unconsumed > 0) {
			lua_Integer curPos = lua_stream_curpos(L, st);
			lua_stream_setpos(L, st, curPos - (lua_Integer)unconsumed);
		}
	}

	return 1;
}