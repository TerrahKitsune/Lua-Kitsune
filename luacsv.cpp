#include "luacsv.h"
#include <string.h>
#include "luawchar.h"

// Calls the chunk-supplier function stored in csv->streamFuncRef.
// If the supplier returns a LuaWChar userdata it is converted to a UTF-8 string first,
// then the string is decoded to wchar_t and stored in streamBuf.
// Sets streamDone=true on nil / false / non-string / empty result.
static void RefillStreamBuffer(LuaCsv* csv) {
	csv->streamPos = 0;
	csv->streamLen = 0;
	lua_State* L = csv->streamL;

	lua_rawgeti(L, LUA_REGISTRYINDEX, csv->streamFuncRef);
	lua_call_nohook(L, 0, 1);  // errors propagate to the Lua caller naturally

	// LuaWChar userdata → convert to UTF-8 string, then fall through to the string path
	if (lua_iswchar(L, -1)) {
		luaL_tolstring(L, -1, NULL);  // pushes UTF-8 string on top
		lua_remove(L, -2);            // remove the Wchar; string is now at -1
	}

	// Anything other than a non-empty string signals end-of-stream
	if (lua_type(L, -1) != LUA_TSTRING) {
		lua_pop(L, 1);
		csv->streamDone = true;
		return;
	}

	size_t slen;
	const char* s = lua_tolstring(L, -1, &slen);
	if (!s || slen == 0) {
		lua_pop(L, 1);
		csv->streamDone = true;
		return;
	}

	// Convert UTF-8 → wchar_t
	int wlen = MultiByteToWideChar(CP_UTF8, 0, s, (int)slen, NULL, 0);
	if (wlen <= 0) {
		lua_pop(L, 1);
		csv->streamDone = true;
		return;
	}

	if ((size_t)wlen > csv->streamAlloc) {
		wchar_t* nb = (wchar_t*)gff_realloc(csv->streamBuf, ((size_t)wlen + 1) * sizeof(wchar_t));
		if (!nb) {
			lua_pop(L, 1);
			csv->streamDone = true;
			luaL_error(L, "Out of memory");
		}
		csv->streamBuf   = nb;
		csv->streamAlloc = (size_t)wlen;
	}

	MultiByteToWideChar(CP_UTF8, 0, s, (int)slen, csv->streamBuf, wlen);
	csv->streamBuf[wlen] = L'\0';
	csv->streamLen       = (size_t)wlen;

	lua_pop(L, 1);
}

static bool IsAtEnd(LuaCsv* csv) {
	if (csv->data)
		return csv->pos >= (int)csv->data->len;
	if (csv->streamFuncRef != LUA_NOREF) {
		if (csv->streamPos < (int)csv->streamLen) return false;
		if (csv->streamDone) return true;
		RefillStreamBuffer(csv);  // peek ahead to confirm exhaustion
		return csv->streamLen == 0;
	}
	return true;
}

static wchar_t GetNext(LuaCsv* csv, bool peek = false) {
	wchar_t last;
	if (csv->data) {
		// String mode: exhausted data returns L'\0', never falls into streaming branch.
		if (csv->pos < (int)csv->data->len) {
			last = csv->data->str[csv->pos];
			if (!peek)
				csv->pos++;
		} else {
			last = L'\0';
		}
	} else if (csv->streamFuncRef != LUA_NOREF) {
		if (csv->streamPos >= (int)csv->streamLen && !csv->streamDone)
			RefillStreamBuffer(csv);
		if (csv->streamPos < (int)csv->streamLen) {
			last = csv->streamBuf[csv->streamPos];
			if (!peek)
				csv->streamPos++;
		} else {
			last = L'\0';  // stream exhausted — signals IsEndline to finish the last row
		}
	} else {
		last = L'\0';
	}
	csv->last = last;
	return last;
}

static wchar_t SkipForwards(LuaCsv* csv) {
	wchar_t next = GetNext(csv, true);
	while (next == L' ' || next == L'\t') {
		GetNext(csv);
		next = GetNext(csv, true);
	}
	return next;
}

static bool ResizeBuffer(LuaCsv* csv) {
	void* temp = gff_realloc(csv->buffer, (csv->alloc + 1024 + 1) * sizeof(wchar_t));
	if (!temp)
		return false;
	csv->buffer = (wchar_t*)temp;
	csv->alloc += 1024;
	return true;
}

static bool WriteToBuffer(LuaCsv* csv, wchar_t wc) {
	if (!csv->buffer || csv->len >= csv->alloc) {
		if (!ResizeBuffer(csv))
			return false;
	}
	csv->buffer[csv->len++] = wc;
	csv->buffer[csv->len]   = L'\0';
	return true;
}

static void ClearBuffer(LuaCsv* csv) {
	csv->len = 0;
	if (csv->buffer)
		csv->buffer[0] = L'\0';
}

static void FreeBuffer(LuaCsv* csv) {
	if (csv->buffer) {
		gff_free(csv->buffer);
		csv->buffer = NULL;
	}
	csv->len   = 0;
	csv->alloc = 0;
}

static bool IsEndline(LuaCsv* csv) {
	if (csv->last == L'\n' || csv->last == L'\0') return true;
	if (csv->last == L'\r') {
		if (GetNext(csv, true) == L'\n')
			GetNext(csv);  // consume \n in \r\n
		return true;
	}
	return false;
}

static void PushAndClearBuffer(LuaCsv* csv, lua_State* L) {
	lua_pushwchar(L, csv->buffer ? csv->buffer : L"", csv->len);
	ClearBuffer(csv);
}

static void DecodeComments(LuaCsv* csv, lua_State* L) {
	wchar_t next = SkipForwards(csv);
	lua_createtable(L, 0, 0);
	if (next != L'*')
		return;

	int nth = 0;
	GetNext(csv);         // consume the '*' marker (SkipForwards only peeked it)
	next = GetNext(csv);  // read first char of comment text

	while (true) {
		while (!IsEndline(csv)) {
			if (!WriteToBuffer(csv, next)) {
				FreeBuffer(csv);
				luaL_error(L, "Out of memory");
			}
			next = GetNext(csv);
		}
		PushAndClearBuffer(csv, L);
		lua_rawseti(L, -2, ++nth);

		next = SkipForwards(csv);
		if (next != L'*') break;

		GetNext(csv);         // consume the '*' of the next comment line
		next = GetNext(csv);  // read first char of that line's text
	}
}

static bool ReadCsvFieldIntoBuffer(LuaCsv* csv, lua_State* L) {
	SkipForwards(csv);
	bool isSkipping = false;
	wchar_t wc;

	while (true) {
		wc = GetNext(csv, false);

		if (wc == L'"') {
			if (isSkipping && GetNext(csv, true) == L'"') {
				wchar_t escaped = GetNext(csv);
				if (!WriteToBuffer(csv, escaped)) {
					FreeBuffer(csv);
					luaL_error(L, "Out of memory");
				}
			} else if (isSkipping) {
				while (true) {
					if (IsEndline(csv) || IsAtEnd(csv)) return false;
					if (wc == csv->delimiter)           return true;
					wc = GetNext(csv);
				}
			} else {
				isSkipping = true;
			}
		} else if (isSkipping) {
			if (IsAtEnd(csv)) break;
			if (!WriteToBuffer(csv, wc)) {
				FreeBuffer(csv);
				luaL_error(L, "Out of memory");
			}
		} else if (wc == csv->delimiter) {
			return true;
		} else if (IsEndline(csv)) {
			break;
		} else {
			if (!WriteToBuffer(csv, wc)) {
				FreeBuffer(csv);
				luaL_error(L, "Out of memory");
			}
			if (IsAtEnd(csv)) break;
		}
	}
	return false;
}

static void DecodeRows(LuaCsv* csv, lua_State* L) {
	int nth = 0;
	int subnth;
	lua_createtable(L, 0, 0);

	do {
		subnth = 0;
		lua_createtable(L, 0, 0);
		bool result = true;
		while (result) {
			result = ReadCsvFieldIntoBuffer(csv, L);
			PushAndClearBuffer(csv, L);
			lua_rawseti(L, -2, ++subnth);
		}
		lua_rawseti(L, -2, ++nth);
	} while (!IsAtEnd(csv));
}

static void Decode(LuaCsv* csv, lua_State* L) {
	lua_createtable(L, 0, 2);
	lua_pushstring(L, "Comments");
	DecodeComments(csv, L);
	lua_settable(L, -3);
	lua_pushstring(L, "Rows");
	DecodeRows(csv, L);
	lua_settable(L, -3);
}

// Parses exactly one CSV row into a Lua table pushed on top of the stack.
// Returns true if a row was successfully parsed, false if the source is exhausted.
static bool DecodeOneRow(LuaCsv* csv, lua_State* L) {
	if (IsAtEnd(csv)) return false;
	lua_createtable(L, 0, 0);
	int col = 0;
	bool more = true;
	while (more) {
		more = ReadCsvFieldIntoBuffer(csv, L);
		PushAndClearBuffer(csv, L);
		lua_rawseti(L, -2, ++col);
	}
	return true;
}

// Scans up to 5 lines of wchar data and returns the most consistently-occurring
// delimiter among the candidates {',', '\t', ';', '|'}.  Quoted fields are skipped
// so a delimiter inside quotes cannot skew the count.  Falls back to L',' when
// no candidate appears consistently (e.g. single-column data).
static wchar_t SniffDelimiter(const wchar_t* data, size_t len) {
	static const wchar_t candidates[] = { L',', L'\t', L';', L'|' };
	const int nCand     = 4;
	const int maxLines  = 5;

	int counts[4][5] = {};
	int nLines = 0;
	bool inQuote = false;

	for (size_t i = 0; i < len && nLines < maxLines; i++) {
		wchar_t ch = data[i];
		if (ch == L'"') {
			inQuote = !inQuote;
			continue;
		}
		if (inQuote) continue;
		if (ch == L'\r') {
			if (i + 1 < len && data[i + 1] == L'\n') i++;
			nLines++;
			continue;
		}
		if (ch == L'\n') { nLines++; continue; }
		for (int c = 0; c < nCand; c++) {
			if (ch == candidates[c]) counts[c][nLines]++;
		}
	}
	if (nLines < maxLines) nLines++;  // flush last (possibly unterminated) line
	if (nLines == 0) return L',';

	int bestCand  = 0;  // default to comma
	int bestCount = 0;
	for (int c = 0; c < nCand; c++) {
		int first = counts[c][0];
		if (first == 0) continue;
		bool consistent = true;
		for (int l = 1; l < nLines; l++) {
			if (counts[c][l] != first) { consistent = false; break; }
		}
		if (consistent && first > bestCount) {
			bestCount = first;
			bestCand  = c;
		}
	}
	return candidates[bestCand];
}

// ── Parse delimiter from arg at stack index idx ──────────────────────────────
// Returns L'\0' as the "auto-detect" sentinel when the caller passes "auto" or
// boolean true.  The sentinel is resolved to a real delimiter by SniffDelimiter
// in LuaDecodeCsv / CsvStreamIterator; LuaEncodeCsv maps it to L','.
static wchar_t ParseDelimiter(lua_State* L, int idx) {
	if (lua_gettop(L) >= idx && !lua_isnil(L, idx)) {
		if (lua_isboolean(L, idx)) {
			return lua_toboolean(L, idx) ? L'\0' : L',';
		}
		if (lua_type(L, idx) == LUA_TSTRING) {
			size_t dlen;
			const char* ds = lua_tolstring(L, idx, &dlen);
			if (dlen == 4 && memcmp(ds, "auto", 4) == 0) return L'\0';
			if (dlen > 0) return (wchar_t)(unsigned char)ds[0];
		} else if (lua_isinteger(L, idx)) {
			return (wchar_t)lua_tointeger(L, idx);
		}
	}
	return L',';
}

// CSV.Decode(str_or_wchar [, delimiter])
int LuaDecodeCsv(lua_State* L) {
	LuaCsv csv = {};
	csv.delimiter = ParseDelimiter(L, 2);

	// Anchor the source Wchar on the stack so GC cannot collect it during Decode.
	if (lua_iswchar(L, 1)) {
		lua_pushvalue(L, 1);
	} else {
		lua_pushvalue(L, 1);
		FromUtf8(L);       // convert plain string to Wchar; copy of arg1 is one level below
		lua_remove(L, -2); // remove copy; Wchar is now at top, rooted
	}
	csv.data = lua_towchar(L, -1);
	csv.pos  = 0;

	// Auto-detect: sniff delimiter from the data before parsing.
	if (csv.delimiter == L'\0')
		csv.delimiter = (csv.data && csv.data->len > 0)
			? SniffDelimiter(csv.data->str, csv.data->len)
			: L',';

	Decode(&csv, L);  // result table at top; anchored Wchar one below

	csv.data = NULL;
	lua_remove(L, -2);  // remove anchored Wchar; result table stays on top

	FreeBuffer(&csv);
	return 1;
}

// CSV.Encode(rows [, delimiter])
static bool FieldNeedsQuoting(const char* s, size_t len, char delimiter) {
	// Leading whitespace (space or tab) would be stripped by SkipForwards on decode,
	// so quote the field to preserve it inside the quotes.
	if (len > 0 && (s[0] == ' ' || s[0] == '\t'))
		return true;
	for (size_t i = 0; i < len; i++) {
		if (s[i] == delimiter || s[i] == '"' || s[i] == '\n' || s[i] == '\r')
			return true;
	}
	return false;
}

int LuaEncodeCsv(lua_State* L) {
	luaL_checktype(L, 1, LUA_TTABLE);

	// Parse delimiter as a single-byte char for the output encoding.
	// "auto" / L'\0' sentinel has no meaning for output; map to comma.
	char delimiter = ',';
	if (lua_gettop(L) >= 2 && !lua_isnil(L, 2)) {
		if (lua_type(L, 2) == LUA_TSTRING) {
			size_t dlen;
			const char* ds = lua_tolstring(L, 2, &dlen);
			if (dlen == 4 && memcmp(ds, "auto", 4) == 0) { /* keep comma */ }
			else if (dlen > 0) delimiter = ds[0];
		} else if (lua_isinteger(L, 2)) {
			delimiter = (char)lua_tointeger(L, 2);
		}
	}

	luaL_Buffer b;
	luaL_buffinit(L, &b);

	lua_Integer rowCount = luaL_len(L, 1);
	for (lua_Integer r = 1; r <= rowCount; r++) {
		if (r > 1)
			luaL_addchar(&b, '\n');

		lua_rawgeti(L, 1, r);
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			continue;
		}

		lua_Integer colCount = luaL_len(L, -1);
		for (lua_Integer c = 1; c <= colCount; c++) {
			if (c > 1)
				luaL_addchar(&b, delimiter);

			lua_rawgeti(L, -1, c);  // push field value

			// luaL_tolstring invokes __tostring so Wchar fields are converted to UTF-8.
			size_t fieldLen;
			const char* field = luaL_tolstring(L, -1, &fieldLen);

			if (FieldNeedsQuoting(field, fieldLen, delimiter)) {
				luaL_addchar(&b, '"');
				for (size_t i = 0; i < fieldLen; i++) {
					if (field[i] == '"')
						luaL_addchar(&b, '"');  // RFC 4180 escape
					luaL_addchar(&b, field[i]);
				}
				luaL_addchar(&b, '"');
			} else {
				luaL_addlstring(&b, field, fieldLen);
			}

			lua_pop(L, 2);  // pop string result and original field value
		}
		lua_pop(L, 1);  // pop row
	}

	luaL_pushresult(&b);
	return 1;
}

// ── Streaming iterator ────────────────────────────────────────────────────────

// __gc for the LuaCsv full-userdata that backs the iterator closure.
static int LuaCsvStreamStateGc(lua_State* L) {
	LuaCsv* csv = (LuaCsv*)lua_touserdata(L, 1);
	if (csv->streamFuncRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, csv->streamFuncRef);
		csv->streamFuncRef = LUA_NOREF;
	}
	FreeBuffer(csv);
	if (csv->streamBuf) {
		gff_free(csv->streamBuf);
		csv->streamBuf = NULL;
	}
	return 0;
}

// Iterator closure: upvalue 1 is the LuaCsv userdata.
static int CsvStreamIterator(lua_State* L) {
	LuaCsv* csv = (LuaCsv*)lua_touserdata(L, lua_upvalueindex(1));
	csv->streamL = L;  // refresh to current thread on every call

	// Auto-detect: on the very first call the delimiter sentinel is still L'\0'.
	// Buffer the first chunk and sniff it; subsequent calls skip this block because
	// csv->delimiter will have been set to a real character.
	if (csv->delimiter == L'\0') {
		if (csv->streamPos >= (int)csv->streamLen && !csv->streamDone)
			RefillStreamBuffer(csv);
		csv->delimiter = (csv->streamLen > 0)
			? SniffDelimiter(csv->streamBuf, csv->streamLen)
			: L',';
	}

	if (!DecodeOneRow(csv, L))
		return 0;  // returns nil → generic for-loop ends
	return 1;      // returns the row table
}

// CSV.DecodeFromFunction(fn [, delimiter]) → iterator
// fn is called with no arguments; it should return a UTF-8 string, a Wchar object,
// or nil/false/"" to signal end-of-stream.  Each iteration yields one row table.
int LuaDecodeFromFunction(lua_State* L) {
	luaL_checktype(L, 1, LUA_TFUNCTION);
	wchar_t delim = ParseDelimiter(L, 2);

	// Allocate the stream state as a Lua full userdata so it is collected
	// automatically (via __gc) when the iterator closure is garbage-collected.
	LuaCsv* csv = (LuaCsv*)lua_newuserdata(L, sizeof(LuaCsv));
	memset(csv, 0, sizeof(LuaCsv));
	csv->delimiter     = delim;
	csv->streamFuncRef = LUA_NOREF;

	if (luaL_newmetatable(L, "LuaCsvStream")) {
		lua_pushcfunction(L, LuaCsvStreamStateGc);
		lua_setfield(L, -2, "__gc");
	}
	lua_setmetatable(L, -2);

	// Anchor the supplier function in the registry so the GC cannot collect it
	// while the iterator closure is still alive.
	lua_pushvalue(L, 1);
	csv->streamFuncRef = luaL_ref(L, LUA_REGISTRYINDEX);

	// The userdata is on top; make it the sole upvalue of the iterator closure.
	lua_pushcclosure(L, CsvStreamIterator, 1);
	return 1;
}

// ── CSV.New factory ───────────────────────────────────────────────────────────
// Each method is a C closure whose sole upvalue is the delimiter value (a Lua
// string, integer, or the string "auto").  The methods delegate directly to the
// existing LuaDecodeCsv / LuaEncodeCsv / LuaDecodeFromFunction functions.

// csv:Decode(str)  →  CSV.Decode(str, delimiter)
static int LuaCsvObjectDecode(lua_State* L) {
	lua_pushcfunction(L, LuaDecodeCsv);
	lua_pushvalue(L, 2);                    // str (arg 2; arg 1 is self)
	lua_pushvalue(L, lua_upvalueindex(1));  // bound delimiter
	lua_call_nohook(L, 2, 1);
	return 1;
}

// csv:Encode(rows)  →  CSV.Encode(rows, delimiter)
static int LuaCsvObjectEncode(lua_State* L) {
	lua_pushcfunction(L, LuaEncodeCsv);
	lua_pushvalue(L, 2);                    // rows
	lua_pushvalue(L, lua_upvalueindex(1));  // bound delimiter ("auto" → comma)
	lua_call_nohook(L, 2, 1);
	return 1;
}

// csv:DecodeFromFunction(fn)  →  CSV.DecodeFromFunction(fn, delimiter)
static int LuaCsvObjectDecodeFromFunction(lua_State* L) {
	lua_pushcfunction(L, LuaDecodeFromFunction);
	lua_pushvalue(L, 2);                    // fn
	lua_pushvalue(L, lua_upvalueindex(1));  // bound delimiter
	lua_call_nohook(L, 2, 1);
	return 1;
}

// CSV.New([delimiter]) → {Decode, Encode, DecodeFromFunction}
// Omitting delimiter (or passing nil) binds "auto" so every Decode call sniffs.
int LuaCsvNew(lua_State* L) {
	bool hasDelim = lua_gettop(L) >= 1 && !lua_isnil(L, 1);

	lua_newtable(L);  // result table

	// Decode
	if (hasDelim) lua_pushvalue(L, 1); else lua_pushstring(L, "auto");
	lua_pushcclosure(L, LuaCsvObjectDecode, 1);
	lua_setfield(L, -2, "Decode");

	// Encode
	if (hasDelim) lua_pushvalue(L, 1); else lua_pushstring(L, "auto");
	lua_pushcclosure(L, LuaCsvObjectEncode, 1);
	lua_setfield(L, -2, "Encode");

	// DecodeFromFunction
	if (hasDelim) lua_pushvalue(L, 1); else lua_pushstring(L, "auto");
	lua_pushcclosure(L, LuaCsvObjectDecodeFromFunction, 1);
	lua_setfield(L, -2, "DecodeFromFunction");

	return 1;
}
