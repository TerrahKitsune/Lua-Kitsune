#include "luaidentifier.h"
#include "identifiermain.h"

static const struct luaL_Reg identifierfunctions[] = {
    { "NewUUID",    identifier_newuuid    },
    { "NewOID",     identifier_newoid     },
    { "FromString", identifier_fromstring },
    { "FromBytes",  identifier_frombytes  },
    { "GetType",    identifier_gettype    },
    { "AsBytes",    identifier_asbytes    },
    { "AsString",   identifier_asstring   },
    { "IsEmpty",    identifier_isempty    },
    { NULL, NULL }
};

static const luaL_Reg identifiermeta[] = {
    { "__eq",       identifier_eq         },
    { "__tostring", identifier_tostring   },
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
