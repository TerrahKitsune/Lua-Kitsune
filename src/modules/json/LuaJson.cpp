#include "json/LuaJson.h"
#include "stream/LuaStream.h"
#include "Utf8Bom.h"
#include "text/LuaText.h"
#include "identifier/LuaIdentifier.h"
#include "datetime/LuaDateTime.h"
#include "decimal/LuaDecimal.h"
#include "uint/LuaUInt.h"
#include "timespan/LuaTimeSpan.h"

// Unique address used as the JSON null sentinel.
// Both encoder and decoder reference this directly — no registry lookup needed.
static char g_jsonNull;
// Unique address used as the JSON empty-object sentinel.
// When emptyObjectAsSentinel is enabled, empty Lua tables encode as {} and
// this sentinel decodes back to the lightuserdata so round-trips are lossless.
static char g_jsonEmptyObject;
// Unique address used as the Lua registry key for the shared bridge LuaJson instance.
static char g_bridgeJsonKey;
void* LuaJsonBridgeRegistryKey(void) {
	return &g_bridgeJsonKey;
}

void* LuaJsonNull(void) {
	return &g_jsonNull;
}

void* LuaJsonEmptyObject(void) {
	return &g_jsonEmptyObject;
}

// =============================================================================
// Instance management
// =============================================================================

LuaJson* lua_pushjson(lua_State* L) {
	LuaJson* j = (LuaJson*)lua_newuserdata(L, sizeof(LuaJson));
	memset(j, 0, sizeof(LuaJson));
	luaL_setmetatable(L, LUAJSON);
	return j;
}

LuaJson* luaL_checkjson(lua_State* L, int idx) {
	return (LuaJson*)luaL_checkudata(L, idx, LUAJSON);
}

int Json_Dispose(lua_State* L) {
	LuaJson* j = luaL_checkjson(L, 1);
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

int Json_ToString(lua_State* L) {
	lua_pushfstring(L, "Json: %p", luaL_checkjson(L, 1));
	return 1;
}

int Json_New(lua_State* L) {
	int      pretty = lua_isboolean(L, 1) ? lua_toboolean(L, 1) : 0;
	LuaJson* j      = lua_pushjson(L);
	j->pretty       = pretty;
	return 1;
}

int Json_SetDecodeNull(lua_State* L) {
	LuaJson* j        = luaL_checkjson(L, 1);
	j->nullAsSentinel = lua_toboolean(L, 2);
	lua_pushvalue(L, 1);
	return 1;
}

int Json_SetEncodeEmptyObject(lua_State* L) {
	LuaJson* j                = luaL_checkjson(L, 1);
	j->emptyObjectAsSentinel  = lua_toboolean(L, 2);
	lua_pushvalue(L, 1);
	return 1;
}

// =============================================================================
// Encoder — buffer helpers
// =============================================================================

static void JbufGrow(LuaJson* j, lua_State* L, size_t need) {
	if (j->outLen + need <= j->outCap)
		return;
	// Streaming mode: flush what we have to the stream before growing so
	// the output buffer never exceeds more than one buffer-worth of memory.
	if (j->encStream && j->outLen > 0) {
		LuaStreamWriteBytes(L, j->encStream, j->out, j->outLen);
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

static void JbufEmit(LuaJson* j, lua_State* L, const char* data, size_t len) {
	JbufGrow(j, L, len);
	memcpy(j->out + j->outLen, data, len);
	j->outLen += len;
}

static void JbufEmitc(LuaJson* j, lua_State* L, char c) {
	JbufGrow(j, L, 1);
	j->out[j->outLen++] = c;
}

// For string literals only — sizeof gives the compile-time length.
#define JBUF_EMIT_LIT(j, L, s) JbufEmit(j, L, "" s, sizeof(s) - 1)

// =============================================================================
// Encoder — anti-recursion
// =============================================================================


static void RecPush(LuaJson* j, lua_State* L, uintptr_t addr) {
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

static void RecPop(LuaJson* j) {
	if (j->recLen > 0)
		j->recLen--;
}

// =============================================================================
// Encoder — value functions
// =============================================================================

static void EncValue(LuaJson* j, lua_State* L, int depth);

static void EncIndent(LuaJson* j, lua_State* L, int depth) {
	if (!j->pretty)
		return;
	JbufEmitc(j, L, '\n');
	for (int i = 0; i < depth; i++)
		JbufEmit(j, L, "  ", 2);
}

// Encodes the Lua string at the top of the stack as a JSON string.
static void EncString(LuaJson* j, lua_State* L) {
	size_t      len;
	const char* s = lua_tolstring(L, -1, &len);
	JbufEmitc(j, L, '"');
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)s[i];
		switch (c) {
		case '"':  JBUF_EMIT_LIT(j, L, "\\\""); break;
		case '\\': JBUF_EMIT_LIT(j, L, "\\\\"); break;
		case '\n': JBUF_EMIT_LIT(j, L, "\\n");  break;
		case '\r': JBUF_EMIT_LIT(j, L, "\\r");  break;
		case '\t': JBUF_EMIT_LIT(j, L, "\\t");  break;
		case '\b': JBUF_EMIT_LIT(j, L, "\\b");  break;
		case '\f': JBUF_EMIT_LIT(j, L, "\\f");  break;
		default:
			if (c < 0x20) {
				char esc[7];
				snprintf(esc, sizeof(esc), "\\u%04x", c);
				JbufEmit(j, L, esc, 6);
			} else if (c < 0x80) {
				JbufEmitc(j, L, (char)c);
			} else {
				// Valid UTF-8 passes through unchanged; malformed bytes become U+FFFD so the
				// output is always valid JSON (binary data belongs in Base64 or a Stream).
				size_t start = i;
				uint32_t cp = kitsune_utf8_next(s, len, &i);
				if (cp == 0xFFFD)
					JbufEmit(j, L, "\xEF\xBF\xBD", 3);
				else
					JbufEmit(j, L, s + start, i - start);
				i--;  // the loop increments i
			}
		}
	}
	JbufEmitc(j, L, '"');
}

// Encodes the Lua number at the top of the stack as a JSON number.
static void EncNumber(LuaJson* j, lua_State* L) {
	if (lua_isinteger(L, -1)) {
		char buf[32];
		int  n = snprintf(buf, sizeof(buf), "%lld", (long long)lua_tointeger(L, -1));
		JbufEmit(j, L, buf, (size_t)n);
		return;
	}
	double d = lua_tonumber(L, -1);
	if (isnan(d)) {
		JBUF_EMIT_LIT(j, L, "null");
		return;
	}
	if (isinf(d)) {
		if (d > 0)
			JBUF_EMIT_LIT(j, L, "1e+9999");
		else
			JBUF_EMIT_LIT(j, L, "-1e+9999");
		return;
	}
	char buf[64];
	int  n = snprintf(buf, sizeof(buf), "%.16g", d);
	JbufEmit(j, L, buf, (size_t)n);
}

// Encodes the table at the top of the stack.
static void EncTable(LuaJson* j, lua_State* L, int depth) {
	int tbl = lua_gettop(L);
	RecPush(j, L, (uintptr_t)lua_topointer(L, tbl));

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

	// When emptyObjectAsSentinel is enabled, an empty table encodes as {} rather
	// than [] so it is distinguishable from an empty array on round-trip.
	if (j->emptyObjectAsSentinel && count == 0 && n == 0) {
		RecPop(j);
		JBUF_EMIT_LIT(j, L, "{}");
		return;
	}

	// -- Encode ----------------------------------------------------------------
	if (seq) {
		JbufEmitc(j, L, '[');
		for (lua_Integer i = 1; i <= n; i++) {
			if (i > 1)
				JbufEmitc(j, L, ',');
			EncIndent(j, L, depth + 1);
			lua_rawgeti(L, tbl, i);
			EncValue(j, L, depth + 1);
			lua_pop(L, 1);
		}
		if (n > 0)
			EncIndent(j, L, depth);
		JbufEmitc(j, L, ']');
	} else {
		JbufEmitc(j, L, '{');
		bool first = true;
		lua_pushnil(L);
		while (lua_next(L, tbl) != 0) {
			if (!first)
				JbufEmitc(j, L, ',');
			first = false;
			EncIndent(j, L, depth + 1);
			luaL_tolstring(L, -2, NULL);   // push string form of key
			EncString(j, L);
			lua_pop(L, 1);                 // pop key string
			JBUF_EMIT_LIT(j, L, ":");
			if (j->pretty)
				JbufEmitc(j, L, ' ');
			EncValue(j, L, depth + 1);   // encode value at -1
			lua_pop(L, 1);                 // pop value
		}
		if (!first)
			EncIndent(j, L, depth);
		JbufEmitc(j, L, '}');
	}

	RecPop(j);
}

// Encodes the value at the top of the stack.
static void EncValue(LuaJson* j, lua_State* L, int depth) {
	// Check null sentinel before anything else
	if (lua_islightuserdata(L, -1) && lua_topointer(L, -1) == LuaJsonNull()) {
		JBUF_EMIT_LIT(j, L, "null");
		return;
	}
	// Check empty-object sentinel
	if (lua_islightuserdata(L, -1) && lua_topointer(L, -1) == LuaJsonEmptyObject()) {
		JBUF_EMIT_LIT(j, L, "{}");
		return;
	}
	switch (lua_type(L, -1)) {
	case LUA_TNIL:
		JBUF_EMIT_LIT(j, L, "null");
		break;
	case LUA_TBOOLEAN:
		if (lua_toboolean(L, -1))
			JBUF_EMIT_LIT(j, L, "true");
		else
			JBUF_EMIT_LIT(j, L, "false");
		break;
	case LUA_TNUMBER:
		EncNumber(j, L);
		break;
	case LUA_TSTRING:
		EncString(j, L);
		break;
	case LUA_TTABLE:
		EncTable(j, L, depth);
		break;
	case LUA_TUSERDATA:
		if (lua_isidentifier(L, -1)) {
			lua_pushidentifierstring(L, -1);
			EncString(j, L);
			lua_pop(L, 1);
			break;
		}
		if (lua_isdatetime(L, -1)) {
			lua_pushdatetimestring(L, -1);
			EncString(j, L);
			lua_pop(L, 1);
			break;
		}
		if (lua_isdecimal(L, -1)) {
			lua_pushdecimalstring(L, -1);
			// Decimals encode as JSON numbers (no quotes) to preserve numeric semantics.
			size_t dlen;
			const char* ds = lua_tolstring(L, -1, &dlen);
			if (ds) JbufEmit(j, L, ds, dlen);
			lua_pop(L, 1);
			break;
		}
		if (lua_isuint(L, -1)) {
			char buf[21];
			int  n = snprintf(buf, sizeof(buf), "%llu", (unsigned long long)lua_touint(L, -1)->value);
			JbufEmit(j, L, buf, (size_t)n);
			break;
		}
		if (lua_istimespan(L, -1)) {
			// Emit as a quoted ISO 8601-style duration string.
			lua_pushtimespanstring(L, -1);
			EncString(j, L);
			lua_pop(L, 1);
			break;
		}
		if (lua_isstream(L, -1)) {
			LuaStream* s = lua_tostream(L, -1);
			if (s && (s->caps & STREAM_CAP_READ) && (s->caps & STREAM_CAP_SEEK)) {
				lua_Integer saved = LuaStreamCurPos(L, s);
				LuaStreamSetPos(L, s, 0);
				LuaStreamReadChunk(L, s, 0);  // pushes Lua string or nil on empty/error
				if (lua_type(L, -1) == LUA_TSTRING)
					EncString(j, L);
				else
					JBUF_EMIT_LIT(j, L, "null");
				lua_pop(L, 1);
				LuaStreamSetPos(L, s, saved);
				break;
			}
		}
		// Non-stream userdata falls through to null.
		JBUF_EMIT_LIT(j, L, "null");
		break;
	default:
		// Functions, threads, and non-null userdata are not representable in
		// JSON — emit null so the output stays valid and callers can detect the
		// gap from the missing value rather than from a garbage pointer string.
		JBUF_EMIT_LIT(j, L, "null");
		break;
	}
}

// =============================================================================
// Decoder — read helpers
// =============================================================================

static char JreadNext(LuaJson* j) {
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

static void JreadUnget(LuaJson* j, lua_State* L, char c) {
	if (j->ungetLen >= (int)(sizeof j->unget)) {
		luaL_error(L, "Json: unget buffer overflow (internal error)");
		return;
	}
	j->unget[j->ungetLen++] = c;
}

// Consumes a UTF-8 BOM at the start of chunked input (sync streams and supplier functions;
// string input uses SkipUtf8Bom). Bytes that turn out not to be a BOM are pushed back.
static void DecSkipBom(LuaJson* j, lua_State* L) {
	char c0 = JreadNext(j);
	if (c0 != '\xEF') {
		if (c0)
			JreadUnget(j, L, c0);
		return;
	}
	char c1 = JreadNext(j);
	if (c1 != '\xBB') {
		if (c1)
			JreadUnget(j, L, c1);
		JreadUnget(j, L, c0);
		return;
	}
	char c2 = JreadNext(j);
	if (c2 != '\xBF') {
		if (c2)
			JreadUnget(j, L, c2);
		JreadUnget(j, L, c1);
		JreadUnget(j, L, c0);
	}
}

static char JreadSkip(LuaJson* j) {
	char c;
	while ((c = JreadNext(j)) != '\0')
		if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
			return c;
	return '\0';
}

// =============================================================================
// Decoder — value functions
// =============================================================================

static void DecValue(LuaJson* j, lua_State* L);

// Decodes a JSON string; the opening '"' has already been consumed.
// Pushes the resulting Lua string onto the stack.
static void DecString(LuaJson* j, lua_State* L) {
	luaL_Buffer b;
	luaL_buffinit(L, &b);
	for (;;) {
		char c = JreadNext(j);
		if (c == '\0')
			luaL_error(L, "Json: unterminated string at line %zu", j->errLine + 1);
		if (c == '"')
			break;
		if (c != '\\') {
			luaL_addchar(&b, c);
			continue;
		}
		char esc = JreadNext(j);
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
				hex[k] = JreadNext(j);
				if (!hex[k])
					luaL_error(L, "Json: unexpected end in \\u escape");
			}
			// Unpaired surrogates cannot be represented in UTF-8 and become U+FFFD.
			unsigned int cp = (unsigned int)strtoul(hex, NULL, 16);
			char utf8[4];
			while (cp >= 0xD800 && cp <= 0xDBFF) {
				char p1 = JreadNext(j);
				char p2 = JreadNext(j);
				if (p1 != '\\' || p2 != 'u') {
					JreadUnget(j, L, p2);
					JreadUnget(j, L, p1);
					cp = 0xFFFD;
					break;
				}
				char hex2[5] = { 0 };
				for (int k = 0; k < 4; k++) {
					hex2[k] = JreadNext(j);
					if (!hex2[k])
						luaL_error(L, "Json: unexpected end in \\u escape");
				}
				unsigned int low = (unsigned int)strtoul(hex2, NULL, 16);
				if (low >= 0xDC00 && low <= 0xDFFF) {
					cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
					break;
				}
				// The high surrogate is unpaired; the second escape is its own character
				// (and, if it is another high surrogate, may pair with the escape after it).
				luaL_addlstring(&b, utf8, kitsune_utf8_encode(utf8, 0xFFFD));
				cp = low;
			}
			if (cp >= 0xDC00 && cp <= 0xDFFF) {
				cp = 0xFFFD;
			}
			luaL_addlstring(&b, utf8, kitsune_utf8_encode(utf8, cp));
			break;
		}
		default:
			luaL_error(L, "Json: unknown escape '\\%c' at line %zu", esc, j->errLine + 1);
		}
	}
	luaL_pushresult(&b);
}

// Decodes a JSON number; `first` is the already-consumed first character.
static void DecNumber(LuaJson* j, lua_State* L, char first) {
	char buf[64];
	int  n        = 0;
	bool isFloat = false;
	buf[n++]      = first;
	for (;;) {
		char c = JreadNext(j);
		if (c == '.' || c == 'e' || c == 'E')
			isFloat = true;
		if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
			c == '+' || c == '-') {
			if (n >= (int)sizeof(buf) - 1)
				luaL_error(L, "Json: number literal too long at line %zu", j->errLine + 1);
			buf[n++] = c;
		} else {
			JreadUnget(j, L, c);
			break;
		}
	}
	buf[n] = '\0';
	if (!isFloat) {
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
static void DecObject(LuaJson* j, lua_State* L) {
	lua_newtable(L);
	char c = JreadSkip(j);
	if (c == '}') {
		// When the empty-object sentinel is enabled, replace the empty table with
		// the sentinel lightuserdata so round-trips through Encode/Decode are lossless.
		if (j->emptyObjectAsSentinel) {
			lua_pop(L, 1);
			lua_pushlightuserdata(L, LuaJsonEmptyObject());
		}
		return;
	}
	JreadUnget(j, L, c);
	for (;;) {
		c = JreadSkip(j);
		if (c != '"')
			luaL_error(L, "Json: expected string key at line %zu", j->errLine + 1);
		DecString(j, L);          // push key
		c = JreadSkip(j);
		if (c != ':')
			luaL_error(L, "Json: expected ':' at line %zu", j->errLine + 1);
		DecValue(j, L);           // push value
		lua_rawset(L, -3);         // table[key] = value
		c = JreadSkip(j);
		if (c == '}')
			break;
		if (c != ',')
			luaL_error(L, "Json: expected ',' or '}' at line %zu", j->errLine + 1);
	}
}

// Decodes a JSON array; the opening '[' has already been consumed.
static void DecArray(LuaJson* j, lua_State* L) {
	lua_newtable(L);
	char c = JreadSkip(j);
	if (c == ']')
		return;
	JreadUnget(j, L, c);
	int idx = 0;
	for (;;) {
		DecValue(j, L);
		lua_rawseti(L, -2, ++idx);
		c = JreadSkip(j);
		if (c == ']')
			break;
		if (c != ',')
			luaL_error(L, "Json: expected ',' or ']' at line %zu", j->errLine + 1);
	}
}

// Dispatches to the appropriate decoder based on the first non-whitespace char.
// Pushes exactly one Lua value onto the stack.
static void DecValue(LuaJson* j, lua_State* L) {
	char c = JreadSkip(j);
	switch (c) {
	case '{': DecObject(j, L); break;
	case '[': DecArray(j, L);  break;
	case '"': DecString(j, L); break;
	case 't': {
		char r = JreadNext(j), u = JreadNext(j), e = JreadNext(j);
		if (r != 'r' || u != 'u' || e != 'e')
			luaL_error(L, "Json: invalid token at line %zu", j->errLine + 1);
		lua_pushboolean(L, 1);
		break;
	}
	case 'f': {
		char a = JreadNext(j), l = JreadNext(j),
		     s = JreadNext(j), e = JreadNext(j);
		if (a != 'a' || l != 'l' || s != 's' || e != 'e')
			luaL_error(L, "Json: invalid token at line %zu", j->errLine + 1);
		lua_pushboolean(L, 0);
		break;
	}
	case 'n': {
		char u = JreadNext(j), l1 = JreadNext(j), l2 = JreadNext(j);
		if (u != 'u' || l1 != 'l' || l2 != 'l')
			luaL_error(L, "Json: invalid token at line %zu", j->errLine + 1);
		if (j->nullAsSentinel)
			lua_pushlightuserdata(L, LuaJsonNull());
		else
			lua_pushnil(L);
		break;
	}
	case '\0':
		luaL_error(L, "Json: unexpected end of input");
		break;
	default:
		if (c == '-' || (c >= '0' && c <= '9')) {
			DecNumber(j, L, c);
			break;
		}
		luaL_error(L, "Json: unexpected character '%c' at line %zu col %zu",
		           c, j->errLine + 1, j->errCol);
	}
}

// =============================================================================
// Reset helpers
// =============================================================================

static void EncReset(LuaJson* j) {
	j->outLen    = 0;
	j->recLen    = 0;
	j->encStream = NULL;
}

static void DecReset(LuaJson* j, const char* src, size_t len) {
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
static int JsonStreamChunkReader(lua_State* L);

// Async stream decode continuation.
// Stack invariant on every entry: L[1]=json, L[2]=source stream, L[3]=in-memory
// accumulator stream, top=latest chunk (string) or nil/false.
// Accumulates chunks via lua_callk (yielding for async streams), then parses
// the complete buffer synchronously once EOF is signalled.
static int JsonDecodeAsyncContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaStream* accum = lua_tostream(L, 3);

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
	SkipUtf8Bom(&src, &srcLen);
	LuaJson* j = luaL_checkjson(L, 1);
	DecReset(j, src, srcLen);
	DecValue(j, L);  // pushes decoded value; accumulated string stays rooted below it
	return 1;
}

int Json_Decode(lua_State* L) {
	LuaJson*    j = luaL_checkjson(L, 1);
	size_t      len;
	const char* s;

	if (lua_isstream(L, 2)) {
		LuaStream* st = lua_tostream(L, 2);
		if (!(st->caps & STREAM_CAP_READ)) {
			lua_pushnil(L);
			lua_pushstring(L, "stream is not readable");
			return 2;
		}
		if (st->vtbl && st->vtbl->hasdata) {
			// Async stream: accumulate chunks via lua_callk (may yield), then parse.
			lua_pushstream(L);           // L[3] = accumulator
			lua_pushvalue(L, 2);
			lua_getfield(L, -1, "Read");
			lua_insert(L, -2);
			lua_callk(L, 1, 1, 0, JsonDecodeAsyncContinuation);
			return JsonDecodeAsyncContinuation(L, LUA_OK, 0);
		}
		// Sync stream: pull 4 KiB chunks directly without yielding.
		lua_pushlightuserdata(L, st);
		lua_pushcclosure(L, JsonStreamChunkReader, 1);
		DecReset(j, NULL, 0);
		j->chunkFnIdx = lua_gettop(L);
		j->chunkL     = L;
		DecSkipBom(j, L);
		DecValue(j, L);
		j->chunkFnIdx = 0;
		j->chunkL     = NULL;
		return 1;
	}

	if (lua_isfunction(L, 2)) {
		DecReset(j, NULL, 0);
		j->chunkFnIdx = 2;
		j->chunkL     = L;
		DecSkipBom(j, L);
	} else {
		s = luaL_checklstring(L, 2, &len);
		SkipUtf8Bom(&s, &len);
		DecReset(j, s, len);
	}

	DecValue(j, L);

	j->chunkFnIdx = 0;
	j->chunkL     = NULL;
	return 1;
}

int Json_Encode(lua_State* L) {
	LuaJson* j = luaL_checkjson(L, 1);
	luaL_checkany(L, 2);
	EncReset(j);
	lua_pushvalue(L, 2);
	EncValue(j, L, 0);
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
static int JsonStreamChunkReader(lua_State* L) {
	LuaStream* st = (LuaStream*)lua_touserdata(L, lua_upvalueindex(1));
	return LuaStreamReadChunk(L, st, 4096);
}

// json:EncodeIntoStream(stream, value)
// Encodes a Lua value as JSON and writes the result directly into a stream.
int Json_EncodeIntoStream(lua_State* L) {
	LuaJson*   j  = luaL_checkjson(L, 1);
	int streamIdx = 2;
	int valueIdx  = 3;

	if (!lua_isstream(L, streamIdx))
		return luaL_argerror(L, streamIdx, "stream expected");

	LuaStream* st = lua_tostream(L, streamIdx);
	if (!(st->caps & STREAM_CAP_WRITE)) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "stream is not writable");
		return 2;
	}

	EncReset(j);        // also clears any stale encStream from a previous error
	j->encStream = st;

	lua_pushvalue(L, valueIdx);
	EncValue(j, L, 0);
	lua_pop(L, 1);

	// Flush bytes still buffered after the last grow boundary.
	if (j->outLen > 0) {
		LuaStreamWriteBytes(L, st, j->out, j->outLen);
		j->outLen = 0;
	} else {
		lua_pushboolean(L, true);
	}

	j->encStream = NULL;
	return 1;
}

// json:DecodeFromStream(stream)
// Decodes one JSON value from a stream.  For seekable sync streams, seeks back
// past any over-read bytes so consecutive calls each get one value.  Async
// streams use the same accumulate-then-parse path as json:Decode(stream).
int Json_DecodeFromStream(lua_State* L) {
	LuaJson*   j  = luaL_checkjson(L, 1);
	int streamIdx = 2;

	if (!lua_isstream(L, streamIdx))
		return luaL_argerror(L, streamIdx, "stream expected");

	LuaStream* st = lua_tostream(L, streamIdx);
	if (!(st->caps & STREAM_CAP_READ)) {
		lua_pushnil(L);
		lua_pushstring(L, "stream is not readable");
		return 2;
	}

	if (st->vtbl && st->vtbl->hasdata) {
		// Async stream: same accumulate-via-lua_callk path as Json_Decode.
		// Seekback is not applicable — async streams are never seekable.
		lua_pushstream(L);           // L[3] = accumulator
		lua_pushvalue(L, 2);
		lua_getfield(L, -1, "Read");
		lua_insert(L, -2);
		lua_callk(L, 1, 1, 0, JsonDecodeAsyncContinuation);
		return JsonDecodeAsyncContinuation(L, LUA_OK, 0);
	}

	// Sync stream: pull 4 KiB chunks directly without yielding.
	lua_pushlightuserdata(L, st);
	lua_pushcclosure(L, JsonStreamChunkReader, 1);
	int fnIdx = lua_gettop(L);

	DecReset(j, NULL, 0);
	j->chunkFnIdx = fnIdx;
	j->chunkL     = L;

	DecSkipBom(j, L);
	DecValue(j, L);

	j->chunkFnIdx = 0;
	j->chunkL     = NULL;

	// The chunk reader fetches 4 KiB at a time, so it may have read bytes that
	// belong to the NEXT value in the stream.  Seek back past the unconsumed bytes
	// so consecutive DecodeFromStream calls each get one value.
	if (st->caps & STREAM_CAP_SEEK) {
		size_t unconsumed = (j->srcLen - j->srcPos) + (size_t)j->ungetLen;
		if (unconsumed > 0) {
			lua_Integer curPos = LuaStreamCurPos(L, st);
			LuaStreamSetPos(L, st, curPos - (lua_Integer)unconsumed);
		}
	}

	return 1;
}