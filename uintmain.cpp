#include "luauint.h"
#include "uintmain.h"

static const struct luaL_Reg uintfunctions[] = {
    { "FromString",   uint_fromstring   },
    { "FromNumber",   uint_fromnumber   },
    { "FromUnsigned", uint_fromunsigned },
    { "Zero",         uint_zero         },
    { "ToString",     uint_tostring     },
    { "AsString",     uint_tostring     },
    { "ToNumber",     uint_tonumber     },
    { "ToInteger",    uint_tointeger    },
    { "ToUnsigned",   uint_tounsigned   },
    { "IsZero",       uint_iszero       },
    { "Add",          uint_add          },
    { "Sub",          uint_sub          },
    { "Mul",          uint_mul          },
    { "Div",          uint_div          },
    { NULL, NULL }
};

static const struct luaL_Reg uintmeta[] = {
    { "__tostring", uint_tostring },
    { "__eq",       uint_eq       },
    { "__lt",       uint_lt       },
    { "__le",       uint_le       },
    { "__add",      uint_add      },
    { "__sub",      uint_sub      },
    { "__mul",      uint_mul      },
    { "__div",      uint_div      },
    { "__mod",      uint_mod      },
    { "__band",     uint_band     },
    { "__bor",      uint_bor      },
    { "__bxor",     uint_bxor     },
    { "__bnot",     uint_bnot     },
    { "__shl",      uint_shl      },
    { "__shr",      uint_shr      },
    { NULL, NULL }
};

int luaopen_uint(lua_State* L) {
    luaL_newlibtable(L, uintfunctions);
    luaL_setfuncs(L, uintfunctions, 0);

    luaL_newmetatable(L, LUAUINT);
    luaL_setfuncs(L, uintmeta, 0);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);

    lua_pop(L, 1);
    return 1;
}
