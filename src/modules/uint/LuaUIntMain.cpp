#include "uint/LuaUInt.h"
#include "uint/LuaUIntMain.h"

static const struct luaL_Reg uintfunctions[] = {
	{ "FromString",   UInt_FromString   },
	{ "FromNumber",   UInt_FromNumber   },
	{ "FromUnsigned", UInt_FromUnsigned },
	{ "Zero",         UInt_Zero         },
	{ "ToString",     UInt_ToString     },
	{ "AsString",     UInt_ToString     },
	{ "ToNumber",     UInt_ToNumber     },
	{ "ToInteger",    UInt_ToInteger    },
	{ "ToUnsigned",   UInt_ToUnsigned   },
	{ "IsZero",       UInt_IsZero       },
	{ "Add",          UInt_Add          },
	{ "Sub",          UInt_Sub          },
	{ "Mul",          UInt_Mul          },
	{ "Div",          UInt_Div          },
	{ NULL, NULL }
};

static const struct luaL_Reg uintmeta[] = {
	{ "__tostring", UInt_ToString },
	{ "__eq",       UInt_Eq       },
	{ "__lt",       UInt_Lt       },
	{ "__le",       UInt_Le       },
	{ "__add",      UInt_Add      },
	{ "__sub",      UInt_Sub      },
	{ "__mul",      UInt_Mul      },
	{ "__div",      UInt_Div      },
	{ "__mod",      UInt_Mod      },
	{ "__band",     UInt_BAnd     },
	{ "__bor",      UInt_BOr      },
	{ "__bxor",     UInt_BXor     },
	{ "__bnot",     UInt_BNot     },
	{ "__shl",      UInt_Shl      },
	{ "__shr",      UInt_Shr      },
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
