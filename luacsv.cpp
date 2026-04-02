#include "luacsv.h"
#include <string.h>
#include "luawchar.h"
#include "stream.h"

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

// Fetches one chunk from the supplier and APPENDS it to the existing streamBuf
// content.  Unlike RefillStreamBuffer, does not reset streamPos or streamLen.
// Used by BufferUntilNewline to accumulate multiple chunks for delimiter sniffing.
static void AppendStreamBuffer(LuaCsv* csv) {
	lua_State* L = csv->streamL;

	lua_rawgeti(L, LUA_REGISTRYINDEX, csv->streamFuncRef);
	lua_call_nohook(L, 0, 1);

	if (lua_iswchar(L, -1)) {
		luaL_tolstring(L, -1, NULL);
		lua_remove(L, -2);
	}

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

	int wlen = MultiByteToWideChar(CP_UTF8, 0, s, (int)slen, NULL, 0);
	if (wlen <= 0) {
		lua_pop(L, 1);
		csv->streamDone = true;
		return;
	}

	size_t totalLen = csv->streamLen + (size_t)wlen;
	if (totalLen > csv->streamAlloc) {
		wchar_t* nb = (wchar_t*)gff_realloc(csv->streamBuf, (totalLen + 1) * sizeof(wchar_t));
		if (!nb) {
			lua_pop(L, 1);
			luaL_error(L, "Out of memory");
		}
		csv->streamBuf   = nb;
		csv->streamAlloc = totalLen;
	}

	MultiByteToWideChar(CP_UTF8, 0, s, (int)slen, csv->streamBuf + csv->streamLen, wlen);
	csv->streamLen       = totalLen;
	csv->streamBuf[totalLen] = L'\0';

	lua_pop(L, 1);
}

// Ensures streamBuf contains at least one complete line before returning.
// Calls RefillStreamBuffer to get the first chunk, then keeps appending chunks
// via AppendStreamBuffer until a newline or CR is seen or the stream is done.
// This guarantees SniffDelimiter sees representative data instead of a
// possibly-incomplete first chunk.
static void BufferUntilNewline(LuaCsv* csv) {
	if (csv->streamPos >= (int)csv->streamLen && !csv->streamDone)
		RefillStreamBuffer(csv);

	while (!csv->streamDone) {
		bool hasNewline = false;
		for (size_t i = 0; i < csv->streamLen && !hasNewline; i++) {
			if (csv->streamBuf[i] == L'\n' || csv->streamBuf[i] == L'\r')
				hasNewline = true;
		}
		if (hasNewline)
			break;
		AppendStreamBuffer(csv);
	}
}

// Pure state query: true only when all input has been consumed.
// Does NOT refill the stream buffer.  Call sites that need an up-to-date answer
// between row boundaries must call EnsureStreamRefilled first.
static bool IsAtEnd(LuaCsv* csv) {
	if (csv->data)
		return csv->pos >= (int)csv->data->len;
	if (csv->streamFuncRef != LUA_NOREF)
		return csv->streamPos >= (int)csv->streamLen && csv->streamDone;
	return true;
}

// Fetches the next chunk from the supplier if the current chunk is exhausted
// and the stream has not yet been marked done.  No-op in string mode and when
// data is still available in the current buffer.
static void EnsureStreamRefilled(LuaCsv* csv) {
	if (csv->streamFuncRef != LUA_NOREF &&
		csv->streamPos >= (int)csv->streamLen &&
		!csv->streamDone)
		RefillStreamBuffer(csv);
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
	const wchar_t* buf = csv->buffer ? csv->buffer : L"";
	size_t         len = csv->len;

	// Fast path: if every wide char is within the ASCII range push a plain Lua
	// string instead of constructing a LuaWChar userdata.  This avoids the heap
	// allocation and GC pressure for the common case of numeric columns, dates,
	// and short English text — the overwhelming majority of real CSV cells.
	bool ascii = true;
	for (size_t i = 0; i < len && ascii; i++) {
		if ((unsigned int)buf[i] > 127u)
			ascii = false;
	}

	if (ascii) {
		luaL_Buffer b;
		luaL_buffinit(L, &b);
		for (size_t i = 0; i < len; i++)
			luaL_addchar(&b, (char)buf[i]);
		luaL_pushresult(&b);
	} else {
		lua_pushwchar(L, buf, len);
	}

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
	lua_createtable(L, 0, 0);

	EnsureStreamRefilled(csv);
	while (!IsAtEnd(csv)) {
		int  subnth = 0;
		lua_createtable(L, 0, 0);
		bool result = true;
		while (result) {
			result = ReadCsvFieldIntoBuffer(csv, L);
			PushAndClearBuffer(csv, L);
			lua_rawseti(L, -2, ++subnth);
		}
		lua_rawseti(L, -2, ++nth);
		EnsureStreamRefilled(csv);
	}
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
	EnsureStreamRefilled(csv);
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
// Returns `noArgDefault` when the caller passes nothing or nil.
//   noArgDefault == L'\0'  →  auto-detect mode (used by CSV.New)
//   noArgDefault == L','   →  explicit comma (used by free-function calls)
// Explicit values:
//   "auto" or boolean true  →  L'\0' (auto-detect)
//   boolean false           →  L','
//   single-char string      →  that character
//   integer                 →  Unicode codepoint
//
// Note: DecodeCsvWith saves and restores csv->delimiter on each call so that
// auto-detect re-sniffs every time.  Caching the sniffed result in the struct
// would save one SniffDelimiter pass per call but would break reuse of a single
// instance across inputs with different delimiters (tested by
// CSV_AutoDetect_ReSniffsDelimiterOnEachDecode).
static wchar_t ParseDelimiter(lua_State* L, int idx, wchar_t noArgDefault) {
	if (lua_gettop(L) < idx || lua_isnil(L, idx))
		return noArgDefault;
	if (lua_isboolean(L, idx))
		return lua_toboolean(L, idx) ? L'\0' : L',';
	if (lua_type(L, idx) == LUA_TSTRING) {
		size_t dlen;
		const char* ds = lua_tolstring(L, idx, &dlen);
		if (dlen == 4 && memcmp(ds, "auto", 4) == 0) return L'\0';
		if (dlen > 0) return (wchar_t)(unsigned char)ds[0];
	} else if (lua_isinteger(L, idx)) {
		return (wchar_t)lua_tointeger(L, idx);
	}
	return noArgDefault;
}

// Internal decode: str_or_wchar at L[1], csv->delimiter pre-set.
// Resets transient parse fields; preserves csv->buffer allocation for reuse.
// Saves and restores csv->delimiter so auto-detect re-fires on every call.
//
// Memory note: the entire input is converted to a wchar_t buffer before
// parsing begins — a UTF-8 string of N bytes requires approximately 2×N bytes
// of additional heap for the wide-char representation.  For multi-megabyte
// files prefer DecodeFromFunction (or csv:DecodeFromFunction) with a stream or
// chunked supplier so peak memory stays bounded to the chunk size.
static int DecodeCsvWith(lua_State* L, LuaCsv* csv) {
	csv->pos  = 0;
	csv->last = L'\0';
	csv->len  = 0;
	if (csv->buffer) csv->buffer[0] = L'\0';

	wchar_t savedDelim = csv->delimiter;

	if (lua_iswchar(L, 1)) {
		lua_pushvalue(L, 1);
	} else {
		lua_pushvalue(L, 1);
		FromUtf8(L);
		lua_remove(L, -2);
	}
	csv->data = lua_towchar(L, -1);
	csv->pos  = 0;

	if (csv->delimiter == L'\0')
		csv->delimiter = (csv->data && csv->data->len > 0)
			? SniffDelimiter(csv->data->str, csv->data->len)
			: L',';

	Decode(csv, L);

	csv->data      = NULL;
	csv->delimiter = savedDelim;
	lua_remove(L, -2);
	return 1;
}

// CSV.Decode entry point: parses delimiter from the stack, runs DecodeCsvWith on
// a temporary local state, then frees the transient buffer.
int LuaDecodeCsv(lua_State* L) {
	LuaCsv csv = {};
	csv.delimiter = ParseDelimiter(L, 2, L',');
	int r = DecodeCsvWith(L, &csv);
	FreeBuffer(&csv);
	return r;
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

static int EncodeCsvWithDelimiter(lua_State* L, char delimiter) {
	luaL_checktype(L, 1, LUA_TTABLE);
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

			lua_rawgeti(L, -1, c);

			size_t fieldLen;
			const char* field = luaL_tolstring(L, -1, &fieldLen);

			if (FieldNeedsQuoting(field, fieldLen, delimiter)) {
				luaL_addchar(&b, '"');
				for (size_t i = 0; i < fieldLen; i++) {
					if (field[i] == '"')
						luaL_addchar(&b, '"');
					luaL_addchar(&b, field[i]);
				}
				luaL_addchar(&b, '"');
			} else {
				luaL_addlstring(&b, field, fieldLen);
			}

			lua_pop(L, 2);
		}
		lua_pop(L, 1);
	}

	luaL_pushresult(&b);
	return 1;
}

int LuaEncodeCsv(lua_State* L) {
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
	return EncodeCsvWithDelimiter(L, delimiter);
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
	csv->streamL = L;

	if (csv->delimiter == L'\0') {
		BufferUntilNewline(csv);
		csv->delimiter = (csv->streamLen > 0)
			? SniffDelimiter(csv->streamBuf, csv->streamLen)
			: L',';
	}

	if (!DecodeOneRow(csv, L))
		return 0;
	return 1;
}

// C closure used when a LuaStream is passed to DecodeFromFunction.
// Upvalue 1 is the stream userdata itself (not a raw pointer) so the closure
// holds a GC-visible reference to the stream for the iterator's entire lifetime.
// Reads 4 KiB per call; returns nil on EOF.
static int csv_stream_chunk_reader(lua_State* L) {
	LuaStream* st = (LuaStream*)lua_touserdata(L, lua_upvalueindex(1));
	return lua_stream_read_chunk(L, st, 4096);
}

// If L[1] is a readable LuaStream, replaces it with a chunk-reader closure.
static void WrapStreamIfNeeded(lua_State* L) {
	if (!lua_isstream(L, 1))
		return;
	LuaStream* st = lua_toluastream(L, 1);
	if (!(st->Caps & STREAM_CAP_READ))
		luaL_argerror(L, 1, "stream is not readable");
	lua_pushvalue(L, 1);
	lua_pushcclosure(L, csv_stream_chunk_reader, 1);
	lua_insert(L, 1);
	lua_remove(L, 2);
}

// Creates the iterator LuaCsv userdata and returns the iterator closure.
// fn/stream must be at L[1]; delim is the already-resolved delimiter.
static int CreateCsvIterator(lua_State* L, wchar_t delim) {
	LuaCsv* csv = (LuaCsv*)lua_newuserdata(L, sizeof(LuaCsv));
	memset(csv, 0, sizeof(LuaCsv));
	csv->delimiter     = delim;
	csv->streamFuncRef = LUA_NOREF;

	if (luaL_newmetatable(L, "LuaCsvStream")) {
		lua_pushcfunction(L, LuaCsvStreamStateGc);
		lua_setfield(L, -2, "__gc");
	}
	lua_setmetatable(L, -2);

	lua_pushvalue(L, 1);
	csv->streamFuncRef = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_pushcclosure(L, CsvStreamIterator, 1);
	return 1;
}

int LuaDecodeFromFunction(lua_State* L) {
	WrapStreamIfNeeded(L);
	luaL_checktype(L, 1, LUA_TFUNCTION);
	return CreateCsvIterator(L, ParseDelimiter(L, 2, L','));
}

// ── CSV instance API ──────────────────────────────────────────────────────────

int lua_csv_gc(lua_State* L) {
	LuaCsv* csv = (LuaCsv*)luaL_checkudata(L, 1, LUACSV);
	FreeBuffer(csv);
	if (csv->streamBuf) {
		gff_free(csv->streamBuf);
		csv->streamBuf = NULL;
	}
	return 0;
}

int lua_csv_tostring(lua_State* L) {
	LuaCsv* csv = (LuaCsv*)luaL_checkudata(L, 1, LUACSV);
	if (csv->delimiter == L'\0')
		lua_pushstring(L, "CSV(auto)");
	else {
		char buf[32];
		snprintf(buf, sizeof(buf), "CSV('%c')", (char)csv->delimiter);
		lua_pushstring(L, buf);
	}
	return 1;
}

// CSV.New([delim]) / CSV.Create([delim])
// When called as csv:New([delim]), the instance at arg 1 is ignored.
int lua_csv_new(lua_State* L) {
	int delimIdx = luaL_testudata(L, 1, LUACSV) ? 2 : 1;
	LuaCsv* csv = (LuaCsv*)lua_newuserdata(L, sizeof(LuaCsv));
	memset(csv, 0, sizeof(LuaCsv));
	csv->delimiter     = ParseDelimiter(L, delimIdx, L'\0');
	csv->streamFuncRef = LUA_NOREF;
	luaL_getmetatable(L, LUACSV);
	lua_setmetatable(L, -2);
	return 1;
}

// csv:Decode(str_or_wchar)
int lua_csv_decode(lua_State* L) {
	LuaCsv* csv = (LuaCsv*)luaL_checkudata(L, 1, LUACSV);
	lua_remove(L, 1);       // str → arg 1
	return DecodeCsvWith(L, csv);
}

// csv:Encode(rows)
int lua_csv_encode(lua_State* L) {
	LuaCsv* csv = (LuaCsv*)luaL_checkudata(L, 1, LUACSV);
	char delimiter = (csv->delimiter == L'\0') ? ',' : (char)csv->delimiter;
	lua_remove(L, 1);       // rows → arg 1
	return EncodeCsvWithDelimiter(L, delimiter);
}

// csv:DecodeFromFunction(fn_or_stream)
int lua_csv_decode_from_function(lua_State* L) {
	LuaCsv* csv = (LuaCsv*)luaL_checkudata(L, 1, LUACSV);
	wchar_t delim = csv->delimiter;
	lua_remove(L, 1);       // fn/stream → arg 1
	WrapStreamIfNeeded(L);
	luaL_checktype(L, 1, LUA_TFUNCTION);
	return CreateCsvIterator(L, delim);
}

