#include "decimal/LuaDecimal.h"
#include "decimal/LuaDecimalMain.h"

static const struct luaL_Reg decimalfunctions[] = {
	{ "FromString",  Decimal_FromString  },
	{ "FromNumber",  Decimal_FromNumber  },
	{ "Zero",        Decimal_Zero        },
	{ "ToString",    Decimal_ToString    },
	{ "AsString",    Decimal_ToString    },
	{ "ToNumber",    Decimal_ToNumber    },
	{ "Scale",       Decimal_Scale       },
	{ "Precision",   Decimal_Precision   },
	{ "IsEmpty",     Decimal_IsEmpty     },
	{ "IsNegative",  Decimal_IsNegative  },
	{ "Abs",         Decimal_Abs         },
	{ "Round",       Decimal_Round       },
	{ "Truncate",    Decimal_Truncate    },
	{ "Add",         Decimal_Add         },
	{ "Sub",         Decimal_Sub         },
	{ "Mul",         Decimal_Mul         },
	{ "Div",         Decimal_Div         },
	{ NULL, NULL }
};

static const struct luaL_Reg decimalmeta[] = {
	{ "__tostring", Decimal_ToString },
	{ "__eq",       Decimal_Eq       },
	{ "__lt",       Decimal_Lt       },
	{ "__le",       Decimal_Le       },
	{ "__add",      Decimal_Add      },
	{ "__sub",      Decimal_Sub      },
	{ "__mul",      Decimal_Mul      },
	{ "__div",      Decimal_Div      },
	{ "__mod",      Decimal_Mod      },
	{ "__unm",      Decimal_Unm      },
	{ NULL, NULL }
};

int luaopen_decimal(lua_State* L) {
	luaL_newlibtable(L, decimalfunctions);
	luaL_setfuncs(L, decimalfunctions, 0);

	luaL_newmetatable(L, LUADECIMAL);
	luaL_setfuncs(L, decimalmeta, 0);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}
