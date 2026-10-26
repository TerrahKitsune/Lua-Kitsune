#include "stream/LuaStream.h"
#include "stream/LuaStreamMain.h"

static const struct luaL_Reg streamfunctions[] = {
	{ "WriteUtf8",  Stream_WriteUtf8 },
	{ "Close",  Stream_Close },
	{ "New",     Stream_New },
	{ "Open",  Stream_Open },
	{ "len",  Stream_len },
	{ "pos",  Stream_pos },
	{ "WriteByte",  Stream_WriteByte },
	{ "ReadByte",  Stream_ReadByte },
	{ "SetByte",  Stream_SetByte },
	{ "PeekByte",  Stream_PeekByte },
	{ "GetInfo",  Stream_GetInfo },
	{ "Seek",  Stream_Seek },
	{ "Write",  Stream_Write },
	{ "Read",  Stream_Read },
	{ "WriteFloat",  Stream_WriteFloat },
	{ "ReadFloat",  Stream_ReadFloat },
	{ "WriteDouble",  Stream_WriteDouble },
	{ "ReadDouble",  Stream_ReadDouble },
	{ "WriteShort",  Stream_WriteShort },
	{ "ReadShort",  Stream_ReadShort },
	{ "WriteUnsignedShort",  Stream_WriteUnsignedShort },
	{ "ReadUnsignedShort",  Stream_ReadUnsignedShort },
	{ "WriteInt",  Stream_WriteInt },
	{ "ReadInt",  Stream_ReadInt },
	{ "WriteUnsignedInt",  Stream_WriteUnsignedInt },
	{ "ReadUnsignedInt",  Stream_ReadUnsignedInt },
	{ "WriteLong",  Stream_WriteLong },
	{ "ReadLong",  Stream_ReadLong },
	{ "WriteUnsignedLong",  Stream_WriteUnsignedLong },
	{ "ReadUnsignedLong", Stream_ReadUnsignedLong },
	{ "ReadUInt64",       Stream_ReadUInt64       },
	{ "ReadDecimal",      Stream_ReadDecimal      },
	{ "ReadIdentifier",   Stream_ReadIdentifier   },
	{ "ReadDateTime",     Stream_ReadDateTime     },
	{ "ReadTimeSpan",     Stream_ReadTimeSpan     },
	{ "ReadUtf16",  Stream_ReadUtf16 },
	{ "WriteUtf16",  Stream_WriteUtf16 },
	{ "ReadUtf8",  Stream_ReadUtf8 },
	{ "Compress",  Stream_Compress },
	{ "Decompress",  Stream_Decompress },
	{ "HasData",  Stream_HasData },
	{ "Id",  Stream_Id },
	{ NULL, NULL }
}; 

static const luaL_Reg streammeta[] = {
	{ "__gc",  Stream_Close },
	{ "__tostring",  Stream_ToString },
	{ NULL, NULL }
};

uint64_t LuaStreamGetId(const LuaStream* s) {
	if (s->vtbl && s->vtbl->getid)
		return s->vtbl->getid(s->native);
	if (s->native)
		return (uint64_t)s->native;
	return (uint64_t)s;
}

int Stream_Id(lua_State* L) {
	LuaStream* s = lua_tostream(L, 1);
	if (!s)
		return luaL_error(L, "Stream.Id: not a stream");
	lua_pushinteger(L, (lua_Integer)LuaStreamGetId(s));
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