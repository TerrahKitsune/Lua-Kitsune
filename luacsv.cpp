#include "luacsv.h"
#include <string.h>
#include "luawchar.h"
#include "stream.h"
#ifndef _WIN32
#include <iconv.h>
// Converts UTF-8 src into *dstBuf (grown via gff_realloc if needed).
// Returns the number of wchar_t code units written.
static size_t csv_utf8_to_wchar(const char* src, size_t srcLen,
	wchar_t** dstBuf, size_t* dstCap) {
	size_t need = srcLen; // conservative upper bound
	if (need > *dstCap) {
		wchar_t* nb = (wchar_t*)gff_realloc(*dstBuf, (need + 1) * sizeof(wchar_t));
		if (!nb)
			return 0;
		*dstBuf = nb;
		*dstCap = need;
	}
	iconv_t cd = iconv_open("WCHAR_T", "UTF-8");
	if (cd == (iconv_t)-1)
		return 0;
	char* in = (char*)src;
	size_t inLeft = srcLen;
	char* out = (char*)*dstBuf;
	size_t outLeft = need * sizeof(wchar_t);
	iconv(cd, &in, &inLeft, &out, &outLeft);
	iconv_close(cd);
	size_t written = (need * sizeof(wchar_t) - outLeft) / sizeof(wchar_t);
	(*dstBuf)[written] = L'\0';
	return written;
}
#endif

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
#ifdef _WIN32
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
#else
	size_t wlen = csv_utf8_to_wchar(s, slen, &csv->streamBuf, &csv->streamAlloc);
	if (wlen == 0 && slen > 0) {
		lua_pop(L, 1);
		csv->streamDone = true;
		return;
	}
	csv->streamLen = wlen;
#endif

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

#ifdef _WIN32
	int wlenNew = MultiByteToWideChar(CP_UTF8, 0, s, (int)slen, NULL, 0);
	if (wlenNew <= 0) {
		lua_pop(L, 1);
		csv->streamDone = true;
		return;
	}

	size_t totalLen = csv->streamLen + (size_t)wlenNew;
	if (totalLen > csv->streamAlloc) {
		wchar_t* nb = (wchar_t*)gff_realloc(csv->streamBuf, (totalLen + 1) * sizeof(wchar_t));
		if (!nb) {
			lua_pop(L, 1);
			luaL_error(L, "Out of memory");
		}
		csv->streamBuf   = nb;
		csv->streamAlloc = totalLen;
	}

	MultiByteToWideChar(CP_UTF8, 0, s, (int)slen, csv->streamBuf + csv->streamLen, wlenNew);
	csv->streamLen       = totalLen;
	csv->streamBuf[totalLen] = L'\0';
#else
	{
		size_t prevLen = csv->streamLen;
		// Grow the buffer to hold prevLen + slen wchars (conservative upper bound: UTF-8
		// bytes >= UTF-32 code units, so slen is a safe upper bound for the new chunk).
		size_t needed = prevLen + slen;
		if (needed > csv->streamAlloc) {
			wchar_t* nb = (wchar_t*)gff_realloc(csv->streamBuf, (needed + 1) * sizeof(wchar_t));
			if (!nb) { lua_pop(L, 1); luaL_error(L, "Out of memory"); }
			csv->streamBuf   = nb;
			csv->streamAlloc = needed;
		}
		// Convert directly into the append position so the existing [0..prevLen) data
		// is never touched.  The earlier csv_utf8_to_wchar pattern was wrong: it wrote
		// from offset 0, silently corrupting the already-buffered rows.
		size_t wlenNew = 0;
		iconv_t cd = iconv_open("WCHAR_T", "UTF-8");
		if (cd != (iconv_t)-1) {
			char* in = (char*)s;
			size_t inLeft = slen;
			char* out = (char*)(csv->streamBuf + prevLen);
			size_t outLeft = (csv->streamAlloc - prevLen) * sizeof(wchar_t);
			iconv(cd, &in, &inLeft, &out, &outLeft);
			iconv_close(cd);
			wlenNew = ((csv->streamAlloc - prevLen) * sizeof(wchar_t) - outLeft) / sizeof(wchar_t);
		}
		csv->streamLen = prevLen + wlenNew;
		csv->streamBuf[csv->streamLen] = L'\0';
	}
#endif

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
	if (csv->streamFuncRef != LUA_NOREF || csv->streamRef != LUA_NOREF)
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
	} else if (csv->streamFuncRef != LUA_NOREF || csv->streamRef != LUA_NOREF) {
		// Fn-backend or stream-object: read from streamBuf.
		// Fn-backend refills here when the buffer runs out.
		// Stream-object: the iterator pre-fills before calling DecodeOneRow; no refill here.
		if (csv->streamFuncRef != LUA_NOREF &&
			csv->streamPos >= (int)csv->streamLen && !csv->streamDone)
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
	size_t newAlloc = csv->alloc == 0 ? 256 : csv->alloc * 2;
	void* temp = gff_realloc(csv->buffer, (newAlloc + 1) * sizeof(wchar_t));
	if (!temp)
		return false;
	csv->buffer = (wchar_t*)temp;
	csv->alloc  = newAlloc;
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
	if (nLines < maxLines) {
		// Flush the last (possibly unterminated) line — but only when it has at
		// least one candidate delimiter.  An empty or delimiter-free partial line
		// (e.g. "al" after "name;age;city\n") would add a zero-count row that
		// makes the consistency check fail for every valid candidate, causing
		// auto-detect to fall back to comma on chunked / streaming input.
		bool partialHasContent = false;
		for (int c = 0; c < nCand; c++) {
			if (counts[c][nLines] > 0) {
				partialHasContent = true;
				break;
			}
		}
		if (partialHasContent)
			nLines++;
	}
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
	if (csv->streamRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, csv->streamRef);
		csv->streamRef = LUA_NOREF;
	}
	FreeBuffer(csv);
	if (csv->streamBuf) {
		gff_free(csv->streamBuf);
		csv->streamBuf = NULL;
	}
	return 0;
}

// Appends a UTF-8 string (s, slen) to csv->streamBuf, compacting any
// already-consumed data to the front first.  Grows the buffer as needed.
static bool CsvAppendChunkToStreamBuf(LuaCsv* csv, const char* s, size_t slen) {
	// Compact: move remaining unparsed data to front of streamBuf.
	size_t remaining = (csv->streamPos < (int)csv->streamLen)
		? (csv->streamLen - (size_t)csv->streamPos) : 0;
	if (remaining > 0 && csv->streamPos > 0)
		memmove(csv->streamBuf, csv->streamBuf + csv->streamPos, remaining * sizeof(wchar_t));
	csv->streamLen = remaining;
	csv->streamPos = 0;
	// Convert and append the new UTF-8 chunk.
#ifdef _WIN32
	int wlen = MultiByteToWideChar(CP_UTF8, 0, s, (int)slen, NULL, 0);
	if (wlen <= 0)
		return true;  // skip empty/invalid
	if ((size_t)(wlen) + csv->streamLen > csv->streamAlloc) {
		size_t need = csv->streamLen + (size_t)wlen;
		wchar_t* nb = (wchar_t*)gff_realloc(csv->streamBuf, (need + 1) * sizeof(wchar_t));
		if (!nb)
			return false;
		csv->streamBuf   = nb;
		csv->streamAlloc = need;
	}
	MultiByteToWideChar(CP_UTF8, 0, s, (int)slen, csv->streamBuf + csv->streamLen, wlen);
	csv->streamLen += (size_t)wlen;
	csv->streamBuf[csv->streamLen] = L'\0';
#else
	{
		// Use iconv directly at the append offset so the compacted [0..remaining)
		// data is never touched.  The old csv_utf8_to_wchar path wrote from
		// position 0, silently overwriting the remainder and then writing the
		// null terminator past the end of the allocation — causing heap corruption
		// on Linux ("free(): invalid pointer") when multiple chunks were appended.
		size_t prevLen = csv->streamLen;                // = remaining after compaction
		size_t needed  = prevLen + slen;                // safe upper bound in wchar_t
		if (needed > csv->streamAlloc) {
			wchar_t* nb = (wchar_t*)gff_realloc(csv->streamBuf, (needed + 1) * sizeof(wchar_t));
			if (!nb)
				return false;
			csv->streamBuf   = nb;
			csv->streamAlloc = needed;
		}
		size_t wlenNew = 0;
		iconv_t cd = iconv_open("WCHAR_T", "UTF-8");
		if (cd != (iconv_t)-1) {
			char*  in      = (char*)s;
			size_t inLeft  = slen;
			char*  out     = (char*)(csv->streamBuf + prevLen);
			size_t outLeft = (csv->streamAlloc - prevLen) * sizeof(wchar_t);
			iconv(cd, &in, &inLeft, &out, &outLeft);
			iconv_close(cd);
			wlenNew = ((csv->streamAlloc - prevLen) * sizeof(wchar_t) - outLeft) / sizeof(wchar_t);
		}
		csv->streamLen = prevLen + wlenNew;
		csv->streamBuf[csv->streamLen] = L'\0';
	}
#endif
	return true;
}

static int CsvStreamContinuation(lua_State* L, int status, lua_KContext ctx);

// Iterator closure: upvalue 1 is the LuaCsv userdata.
static int CsvStreamIterator(lua_State* L) {
	LuaCsv* csv = (LuaCsv*)lua_touserdata(L, lua_upvalueindex(1));
	csv->streamL = L;

	if (csv->streamRef != LUA_NOREF) {
		// Stream path — works identically for sync and async streams.
		// Keep fetching chunks until the buffer holds at least one complete row
		// boundary (newline or CR) or the stream is done.  The same accumulation
		// also ensures SniffDelimiter sees a full line when in auto-detect mode.
		bool hasRow = csv->streamDone;
		for (size_t i = (size_t)csv->streamPos; i < csv->streamLen && !hasRow; i++) {
			if (csv->streamBuf[i] == L'\n' || csv->streamBuf[i] == L'\r')
				hasRow = true;
		}
		if (!hasRow) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, csv->streamRef);
			lua_getfield(L, -1, "Read");
			lua_insert(L, -2);
			lua_callk(L, 1, 1, 0, CsvStreamContinuation);
			return CsvStreamContinuation(L, LUA_OK, 0);
		}
		if (csv->delimiter == L'\0')
			csv->delimiter = csv->streamLen > (size_t)csv->streamPos
				? SniffDelimiter(csv->streamBuf + csv->streamPos,
								  csv->streamLen - (size_t)csv->streamPos)
				: L',';
	} else if (csv->streamFuncRef != LUA_NOREF && csv->delimiter == L'\0') {
		// Fn-backend: accumulate at least one complete line for delimiter sniffing.
		BufferUntilNewline(csv);
		csv->delimiter = csv->streamLen > 0
			? SniffDelimiter(csv->streamBuf, csv->streamLen)
			: L',';
	}

	if (!DecodeOneRow(csv, L))
		return 0;
	return 1;
}

// Continuation for the stream path — same upvalues as CsvStreamIterator.
// Called when stream:Read() yields (async) or used as the direct fallthrough
// when Read() returns synchronously without yielding.
// Per Lua 5.4: "the continuation function is called with the same thread,
// with the same stack, and with the same upvalues."
static int CsvStreamContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaCsv* csv = (LuaCsv*)lua_touserdata(L, lua_upvalueindex(1));

	if (lua_type(L, -1) == LUA_TSTRING) {
		size_t slen = 0;
		const char* s = lua_tolstring(L, -1, &slen);
		CsvAppendChunkToStreamBuf(csv, s, slen);
		lua_pop(L, 1);
	} else {
		lua_pop(L, 1);
		csv->streamDone = true;
	}

	bool hasRow = csv->streamDone;
	for (size_t i = (size_t)csv->streamPos; i < csv->streamLen && !hasRow; i++) {
		if (csv->streamBuf[i] == L'\n' || csv->streamBuf[i] == L'\r')
			hasRow = true;
	}
	if (!hasRow) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, csv->streamRef);
		lua_getfield(L, -1, "Read");
		lua_insert(L, -2);
		lua_callk(L, 1, 1, 0, CsvStreamContinuation);
		return CsvStreamContinuation(L, LUA_OK, 0);
	}

	if (csv->delimiter == L'\0')
		csv->delimiter = csv->streamLen > (size_t)csv->streamPos
			? SniffDelimiter(csv->streamBuf + csv->streamPos,
							  csv->streamLen - (size_t)csv->streamPos)
			: L',';

	if (!DecodeOneRow(csv, L))
		return 0;
	return 1;
}

// If L[1] is a readable LuaStream, validates it but leaves it as-is.
// All streams (sync and async) use the lua_callk path in CsvStreamIterator;
// no closure wrapping is needed.
static void WrapStreamIfNeeded(lua_State* L) {
	if (!lua_isstream(L, 1))
		return;
	LuaStream* st = lua_toluastream(L, 1);
	if (!(st->Caps & STREAM_CAP_READ))
		luaL_argerror(L, 1, "stream is not readable");
}

// Creates the iterator LuaCsv userdata and returns the iterator closure.
// fn/stream must be at L[1]; delim is the already-resolved delimiter.
static int CreateCsvIterator(lua_State* L, wchar_t delim) {
	LuaCsv* csv = (LuaCsv*)lua_newuserdata(L, sizeof(LuaCsv));
	memset(csv, 0, sizeof(LuaCsv));
	csv->delimiter    = delim;
	csv->streamFuncRef = LUA_NOREF;
	csv->streamRef     = LUA_NOREF;

	if (luaL_newmetatable(L, "LuaCsvStream")) {
		lua_pushcfunction(L, LuaCsvStreamStateGc);
		lua_setfield(L, -2, "__gc");
	}
	lua_setmetatable(L, -2);

	if (lua_isstream(L, 1)) {
		// Store the stream directly; CsvStreamIterator calls Read() via lua_callk.
		// This works for both sync streams (Read returns immediately) and async
		// streams (Read may yield cooperatively).
		lua_pushvalue(L, 1);
		csv->streamRef = luaL_ref(L, LUA_REGISTRYINDEX);
	} else {
		lua_pushvalue(L, 1);
		csv->streamFuncRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	lua_pushcclosure(L, CsvStreamIterator, 1);
	return 1;
}

int LuaDecodeFromFunction(lua_State* L) {
	WrapStreamIfNeeded(L);
	// L[1] is a function after WrapStreamIfNeeded (sync stream wrapped in closure
	// or user-supplied function), OR still a LuaStream for async streams.
	if (!lua_isstream(L, 1))
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
	csv->streamRef     = LUA_NOREF;
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
	if (!lua_isstream(L, 1))
		luaL_checktype(L, 1, LUA_TFUNCTION);
	return CreateCsvIterator(L, delim);
}

