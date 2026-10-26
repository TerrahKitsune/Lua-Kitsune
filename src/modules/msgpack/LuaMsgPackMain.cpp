#include "msgpack/LuaMsgPack.h"
#include "msgpack/LuaMsgPackMain.h"

static const struct luaL_Reg msgpack_functions[] = {
    { "New",              MsgPack_New               },  // MsgPack.New()
    { "Decode",           MsgPack_Decode            },  // msgpack:Decode(str | stream)
    { "Encode",           MsgPack_Encode            },  // msgpack:Encode(value)
    { "EncodeIntoStream", MsgPack_EncodeIntoStream },  // msgpack:EncodeIntoStream(stream, value)
    { "DecodeFromStream", MsgPack_DecodeFromStream },  // msgpack:DecodeFromStream(stream)
    { "Dispose",          MsgPack_Dispose                },  // msgpack:Dispose()
    { NULL, NULL }
};

static const struct luaL_Reg msgpack_meta[] = {
    { "__gc",       MsgPack_Dispose       },
    { "__tostring", MsgPack_ToString },
    { NULL, NULL }
};

int luaopen_msgpack(lua_State* L) {
    luaL_newlibtable(L, msgpack_functions);
    luaL_setfuncs(L, msgpack_functions, 0);

    // Instance metatable: __gc, __tostring, and __index = module table so all
    // msgpack_functions are reachable as both MsgPack.Xxx() and msgpack:Xxx().
    luaL_newmetatable(L, LUAMSGPACK);
    luaL_setfuncs(L, msgpack_meta, 0);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -3);   // module table
    lua_rawset(L, -3);
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);   // module table
    lua_rawset(L, -3);

    lua_pop(L, 1);   // pop metatable
    return 1;        // return module table
}
