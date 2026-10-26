#include "identifier/LuaIdentifier.h"
#include "identifier/LuaIdentifierMain.h"

static const struct luaL_Reg identifierfunctions[] = {
    { "NewUUID",    Identifier_NewUUID    },
    { "NewOID",     Identifier_NewOID     },
    { "FromString", Identifier_FromString },
    { "FromBytes",  Identifier_FromBytes  },
    { "GetType",    Identifier_GetType    },
    { "AsBytes",    Identifier_AsBytes    },
    { "AsString",   Identifier_AsString   },
    { "IsEmpty",    Identifier_IsEmpty    },
    { NULL, NULL }
};

static const luaL_Reg identifiermeta[] = {
    { "__eq",       Identifier_Eq         },
    { "__tostring", Identifier_ToString   },
    { NULL, NULL }
};

int luaopen_identifier(lua_State* L) {

    luaL_newlibtable(L, identifierfunctions);
    luaL_setfuncs(L, identifierfunctions, 0);

    luaL_newmetatable(L, LUAIDENTIFIER);
    luaL_setfuncs(L, identifiermeta, 0);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);

    lua_pop(L, 1);
    return 1;
}
