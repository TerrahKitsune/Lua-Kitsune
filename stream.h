#pragma once
#include "lua_main_incl.h"
#include <Windows.h>

static const char* STREAM = "STREAM";

// ── Capability flags ──────────────────────────────────────────────────────────
// Stored in LuaStream::caps and returned as a bitmask by OP_OPEN.
#define STREAM_CAP_READ   (1 << 0)
#define STREAM_CAP_WRITE  (1 << 1)
#define STREAM_CAP_SEEK   (1 << 2)
#define STREAM_CAP_PEEK   (1 << 3)

// ── Backend operation codes ───────────────────────────────────────────────────
// First argument passed to the backend function.
// Internal heap streams use data != NULL and bypass READ/WRITE/SEEK entirely.
#define STREAM_OP_OPEN    0   // ()     -> integer caps         (called once at construction)
#define STREAM_OP_CLOSE   1   // ()     -> true | false [, msg] (called from __gc)
#define STREAM_OP_READ    2   // (len)  -> string | false [, msg]
#define STREAM_OP_WRITE   3   // (data) -> true   | false [, msg]
#define STREAM_OP_CURPOS  5   // ()     -> integer pos current position
#define STREAM_OP_LEN     6   // ()     -> integer len total length
#define STREAM_OP_SETPOS  7   // (pos)  -> true   | false [, msg]
#define STREAM_OP_INFO    8   // ()     -> any    backend-defined info table (called from GetInfo)

typedef struct LuaStream {
	int     backendRef;  // LUA_NOREF = heap stream; else registry ref to the backend function
	BYTE	Caps;        // Capability flags (STREAM_CAP_*)
} LuaStream;

static const size_t MIN_STREAM_SIZE  = 1024;
static const BYTE   HEAP_STREAM_CAPS = STREAM_CAP_READ | STREAM_CAP_WRITE | STREAM_CAP_SEEK | STREAM_CAP_PEEK;

// ── Constructor helpers ───────────────────────────────────────────────────────
LuaStream* lua_pushluastream(lua_State* L);
LuaStream* lua_pushluastream(lua_State* L, const BYTE* data, size_t len);
LuaStream* lua_toluastream(lua_State* L, int index);
int        lua_isstream(lua_State* L, int index);

// ── Stream construction ───────────────────────────────────────────────────────
int NewStream(lua_State* L);

// ── Stream info ───────────────────────────────────────────────────────────────
int StreamPos(lua_State* L);
int StreamLen(lua_State* L);
int StreamSetPos(lua_State* L);
int GetStreamInfo(lua_State* L);

// ── Read operations ───────────────────────────────────────────────────────────
int ReadLuaStream(lua_State* L);
int ReadStreamByte(lua_State* L);
int PeekStreamByte(lua_State* L);
int ReadUtf8(lua_State* L);
int ReadFloat(lua_State* L);
int ReadDouble(lua_State* L);
int ReadShort(lua_State* L);
int ReadUShort(lua_State* L);
int ReadInt(lua_State* L);
int ReadUInt(lua_State* L);
int ReadLong(lua_State* L);
int ReadUnsignedLong(lua_State* L);

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