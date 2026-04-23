#include "luamsgpack.h"
#include "luamsgpackmain.h"

static const struct luaL_Reg msgpack_functions[] = {
    { "New",              lua_msgpack_new               },  // MsgPack.New()
    { "Decode",           lua_msgpack_decode            },  // msgpack:Decode(str | stream)
    { "Encode",           lua_msgpack_encode            },  // msgpack:Encode(value)
    { "EncodeIntoStream", lua_msgpack_encode_into_stream },  // msgpack:EncodeIntoStream(stream, value)
    { "DecodeFromStream", lua_msgpack_decode_from_stream },  // msgpack:DecodeFromStream(stream)
    { "Dispose",          lua_msgpack_gc                },  // msgpack:Dispose()
    { NULL, NULL }
};

static const struct luaL_Reg msgpack_meta[] = {
    { "__gc",       lua_msgpack_gc       },
    { "__tostring", lua_msgpack_tostring },
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
