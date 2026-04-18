#pragma once
#include "lua_main_incl.h"
#include "platform.h"

static const char* STREAM = "STREAM";

// ── Capability flags ──────────────────────────────────────────────────────────
// Stored in LuaStream::Caps.
#define STREAM_CAP_READ   (1 << 0)
#define STREAM_CAP_WRITE  (1 << 1)
#define STREAM_CAP_SEEK   (1 << 2)
// Note: CAP_PEEK (1 << 3) is intentionally absent.  PeekStreamByte is gated on
// CAP_READ | CAP_SEEK — any stream that can seek can be peeked by the generic
// save-pos / read / restore-pos path.  Lua fn backends that still return bit 3
// in their open caps have it silently stored but it is never consulted.

// ── Backend operation codes ───────────────────────────────────────────────────
// Used only by Lua function backends (Stream.Create(fn)).
// Native C backends (memory, file) use the vtable below instead.
#define STREAM_OP_OPEN    0   // ()     -> integer caps
#define STREAM_OP_CLOSE   1   // ()     -> true | false [, msg]
#define STREAM_OP_READ    2   // (len)  -> string | false [, msg]
#define STREAM_OP_WRITE   3   // (data) -> true   | false [, msg]
#define STREAM_OP_CURPOS  4   // ()     -> integer pos
#define STREAM_OP_LEN     5   // ()     -> integer len
#define STREAM_OP_SETPOS  6   // (pos)  -> true   | false [, msg]
#define STREAM_OP_INFO    7   // ()     -> table
#define STREAM_OP_HASDATA 8   // ()     -> integer bytes_ready | false

// ── Native C vtable ───────────────────────────────────────────────────────────
// Implemented by memory and file backends.  A NULL vtbl means Lua fn backend.
// All functions receive the opaque 'native' pointer so the vtable itself is
// stateless and stored as a static const.
//
// read:   reads up to 'len' bytes (0 = all remaining).
//         MUST push exactly one Lua value onto the stack: a string on success,
//         or false on EOF/error.  For async backends this function may call
//         lua_yieldk — sync backends simply return without yielding.
// write:  writes 'len' bytes; returns true on success.
// setpos: moves the cursor; returns true on success.
// curpos: returns current cursor position.
// getlen: returns total data length.
// close:  frees all resources owned by 'native'.
// info:   pushes a backend-specific info table onto the Lua stack; returns 1.
//         May call lua_yieldk for async backends (e.g. HTTP response headers).
typedef struct LuaStreamVtable {
	int         (*read)    (void* native, lua_State* L, size_t len);
	bool        (*write)   (void* native, const BYTE* data, size_t len);
	bool        (*setpos)  (void* native, lua_Integer pos);
	lua_Integer (*curpos)  (void* native);
	lua_Integer (*getlen)  (void* native);
	// L is provided for backends that hold Lua registry refs (e.g. chunked stream).
	// Existing backends that do not need L simply ignore it.
	void        (*close)   (void* native, lua_State* L);
	int         (*info)    (void* native, lua_State* L);
	// hasdata: non-blocking availability check. 1 = data ready; 0 = not yet.
	//   Must never yield or call any Lua API that may yield.
	int         (*hasdata) (void* native);
	// getid: returns a stable identity value for cache-key comparison.
	//   NULL = fall back to (uint64_t)native, or (uint64_t)stream for Lua fn backends.
	uint64_t    (*getid)   (void* native);
} LuaStreamVtable;

typedef struct LuaStream {
	int                      backendRef;  // LUA_NOREF for vtable streams; Lua fn ref otherwise
	BYTE                     Caps;
	const LuaStreamVtable*   vtbl;        // NULL = Lua fn backend; non-NULL = native C backend
	void*                    native;      // InMemoryStream* or InFileStream*; owned by vtbl->close
} LuaStream;

static const size_t MIN_STREAM_SIZE  = 1024;
static const BYTE   HEAP_STREAM_CAPS = STREAM_CAP_READ | STREAM_CAP_WRITE | STREAM_CAP_SEEK;

// ── Constructor helpers ───────────────────────────────────────────────────────
LuaStream* lua_pushluastream(lua_State* L);
LuaStream* lua_pushluastream(lua_State* L, const BYTE* data, size_t len);
// Pushes a native-backed LuaStream (STREAM metatable) with the given vtable,
// native pointer, and capability flags.  Always uses the STREAM metatable so
// luastream_gc fires the vtable close correctly.
LuaStream* lua_pushluastream_native(lua_State* L, const LuaStreamVtable* vtbl,
	void* native, BYTE caps);
LuaStream* lua_toluastream(lua_State* L, int index);
int        lua_isstream(lua_State* L, int index);

// Reads up to 'len' bytes from a stream via vtable or Lua fn backend.
// Pushes a non-empty Lua string on success, or nil on EOF/error.
// Used by JSON and CSV streaming chunk readers.  Returns 1.
int lua_stream_read_chunk(lua_State* L, LuaStream* s, size_t len);

// Writes 'len' bytes to a stream via vtable or Lua fn backend.
// Leaves one boolean result on the Lua stack (true = success).  Returns 1.
int lua_stream_write_bytes(lua_State* L, LuaStream* s, const char* data, size_t len);

// Returns the current cursor position without touching the Lua stack.
lua_Integer lua_stream_curpos(lua_State* L, LuaStream* s);

// Sets the cursor position; returns true on success.  Does not touch the Lua stack.
bool lua_stream_setpos(lua_State* L, LuaStream* s, lua_Integer pos);

// Returns the total data length of a stream without touching the Lua stack.
lua_Integer lua_stream_getlen(lua_State* L, LuaStream* s);

// Returns a stable identity value for the stream suitable for cache-key comparison.
// Calls vtbl->getid if available; falls back to (uint64_t)native, then (uint64_t)stream.
uint64_t lua_stream_getid(const LuaStream* s);

// ── Stream construction
int NewStream(lua_State* L);
int OpenFile(lua_State* L);

// ── Stream info
int StreamPos(lua_State* L);
int StreamLen(lua_State* L);
int StreamSetPos(lua_State* L);
int GetStreamInfo(lua_State* L);
int StreamId(lua_State* L);

// ── Read operations ───────────────────────────────────────────────────────────
int ReadLuaStream(lua_State* L);
int ReadStreamByte(lua_State* L);
int PeekStreamByte(lua_State* L);
// Non-blocking: returns the number of bytes ready (or true/false for backends
// with a dedicated hasdata check).  Returns false when no data is immediately
// available.  Sync streams without vtbl->hasdata return bytes-remaining instead.
int HasDataLuaStream(lua_State* L);
int ReadUtf8(lua_State* L);
int ReadFloat(lua_State* L);
int ReadDouble(lua_State* L);
int ReadShort(lua_State* L);
int ReadUShort(lua_State* L);
int ReadInt(lua_State* L);
int ReadUInt(lua_State* L);
int ReadLong(lua_State* L);
int ReadUnsignedLong(lua_State* L);
int ReadWchar(lua_State* L);

// ── Write operations ──────────────────────────────────────────────────────────
int WriteLuaValue(lua_State* L);
int WriteStreamByte(lua_State* L);
int SetStreamByte(lua_State* L);
int WriteUtf8(lua_State* L);
int WriteFloat(lua_State* L);
int WriteDouble(lua_State* L);
int WriteShort(lua_State* L);
int WriteUShort(lua_State* L);
int WriteInt(lua_State* L);
int WriteUInt(lua_State* L);
int WriteLong(lua_State* L);
int WriteUnsignedLong(lua_State* L);

// ── Compression ───────────────────────────────────────────────────────────────
int CompressStream(lua_State* L);
int DecompressStream(lua_State* L);

// ── Metamethods ───────────────────────────────────────────────────────────────
int luastream_gc(lua_State* L);
int luastream_tostring(lua_State* L);