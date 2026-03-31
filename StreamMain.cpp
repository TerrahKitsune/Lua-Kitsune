#include "stream.h"
#include "StreamMain.h"

static const struct luaL_Reg streamfunctions[] = {
	{ "WriteUtf8",  WriteUtf8 },
	{ "Close",  luastream_gc },
	{ "Create",  NewStream },
	{ "FromString", NewStreamFromString },
	{ "Open",  OpenFileToStream },
	{ "WriteToFile",  WriteToFile },
	{ "ReadFromFile",  ReadFromFile },
	{ "Save",  DumpToFile },
	{ "len",  StreamLen },
	{ "pos",  StreamPos },
	{ "WriteByte",  WriteStreamByte },
	{ "ReadByte",  ReadStreamByte },
	{ "SetByte",  SetStreamByte },
	{ "PeekByte",  PeekStreamByte },
	{ "GetInfo",  GetStreamInfo },
	{ "Shrink",  StreamShrink },
	{ "Seek",  StreamSetPos },
	{ "Buffer",  StreamBuffer },
	{ "Write",  WriteLuaValue },
	{ "Read",  ReadLuaStream },
	{ "WriteFloat",  WriteFloat },
	{ "ReadFloat",  ReadFloat },
	{ "WriteDouble",  WriteDouble },
	{ "ReadDouble",  ReadDouble },
	{ "WriteShort",  WriteShort },
	{ "ReadShort",  ReadShort },
	{ "WriteUnsignedShort",  WriteUShort },
	{ "ReadUnsignedShort",  ReadUShort },
	{ "WriteInt",  WriteInt },
	{ "ReadInt",  ReadInt },
	{ "WriteUnsignedInt",  WriteUInt },
	{ "ReadUnsignedInt",  ReadUInt },
	{ "WriteLong",  WriteLong },
	{ "ReadLong",  ReadLong },
	{ "WriteUnsignedLong",  WriteUnsignedLong },
	{ "ReadUnsignedLong", ReadUnsignedLong },
	{ "ReadUtf8",  ReadUtf8 },
	{ "SetLength", SetLength },
	{ "ReadUntil", ReadUntilLuaStream },
	{ "IndexOf", StreamIndexOf },
	{ "Compress", Compress },
	{ "Decompress", Decompress },
	{ "GetSharedMemoryStreamInfo", GetSharedMemoryStreamInfo },
	{ NULL, NULL }
}; 

static const luaL_Reg streammeta[] = {
	{ "__gc",  luastream_gc },
	{ "__tostring",  luastream_tostring },
	{ NULL, NULL }
};

int luaopen_stream(lua_State* L) {

	// Register the internal heap backend in the Lua registry so constructors
	// can assign it as backendRef for every heap (addressable) stream.
	lua_pushcfunction(L, heap_backend);
	lua_setfield(L, LUA_REGISTRYINDEX, HEAP_BACKEND_KEY);

	luaL_newlibtable(L, streamfunctions);
	luaL_setfuncs(L, streamfunctions, 0);

	// Operation codes for custom backends
	lua_pushinteger(L, STREAM_OP_OPEN);  lua_setfield(L, -2, "OP_OPEN");
	lua_pushinteger(L, STREAM_OP_CLOSE); lua_setfield(L, -2, "OP_CLOSE");
	lua_pushinteger(L, STREAM_OP_READ);  lua_setfield(L, -2, "OP_READ");
	lua_pushinteger(L, STREAM_OP_WRITE); lua_setfield(L, -2, "OP_WRITE");
	lua_pushinteger(L, STREAM_OP_SEEK);  lua_setfield(L, -2, "OP_SEEK");

	// Capability flags returned by OP_OPEN
	lua_pushinteger(L, STREAM_CAP_READ);  lua_setfield(L, -2, "CAP_READ");
	lua_pushinteger(L, STREAM_CAP_WRITE); lua_setfield(L, -2, "CAP_WRITE");
	lua_pushinteger(L, STREAM_CAP_SEEK);  lua_setfield(L, -2, "CAP_SEEK");
	lua_pushinteger(L, STREAM_CAP_PEEK);  lua_setfield(L, -2, "CAP_PEEK");

	luaL_newmetatable(L, STREAM);
	luaL_setfuncs(L, streammeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}