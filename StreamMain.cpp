#include "stream.h"
#include "StreamMain.h"

static const struct luaL_Reg streamfunctions[] = {
	{ "WriteUtf8",  WriteUtf8 },
	{ "Close",  luastream_gc },
	{ "Create",  NewStream },
	{ "Open",  OpenFile },
	{ "OpenSharedMemory",  OpenSharedMemory },
	{ "ToSharedMemory",  ToSharedMemory },
	{ "len",  StreamLen },
	{ "pos",  StreamPos },
	{ "WriteByte",  WriteStreamByte },
	{ "ReadByte",  ReadStreamByte },
	{ "SetByte",  SetStreamByte },
	{ "PeekByte",  PeekStreamByte },
	{ "GetInfo",  GetStreamInfo },
	{ "Seek",  StreamSetPos },
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
	{ "ReadWchar",  ReadWchar },
	{ "ReadUtf8",  ReadUtf8 },
	{ "Compress",  CompressStream },
	{ "Decompress",  DecompressStream },
	{ "HasData",  HasDataLuaStream },
	{ "Id",  StreamId },
	{ NULL, NULL }
}; 

static const luaL_Reg streammeta[] = {
	{ "__gc",  luastream_gc },
	{ "__tostring",  luastream_tostring },
	{ NULL, NULL }
};

uint64_t lua_stream_getid(const LuaStream* s) {
	if (s->vtbl && s->vtbl->getid)
		return s->vtbl->getid(s->native);
	if (s->native)
		return (uint64_t)s->native;
	return (uint64_t)s;
}

int StreamId(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!s)
		return luaL_error(L, "Stream.Id: not a stream");
	lua_pushinteger(L, (lua_Integer)lua_stream_getid(s));
	return 1;
}

int luaopen_stream(lua_State* L) {
	luaL_newlibtable(L, streamfunctions);
	luaL_setfuncs(L, streamfunctions, 0);

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