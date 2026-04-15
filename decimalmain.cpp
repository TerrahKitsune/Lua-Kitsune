#include "luadecimal.h"
#include "decimalmain.h"

static const struct luaL_Reg decimalfunctions[] = {
	{ "FromString",  decimal_fromstring  },
	{ "FromNumber",  decimal_fromnumber  },
	{ "Zero",        decimal_zero        },
	{ "ToString",    decimal_tostring    },
	{ "AsString",    decimal_tostring    },
	{ "ToNumber",    decimal_tonumber    },
	{ "Scale",       decimal_scale       },
	{ "Precision",   decimal_precision   },
	{ "IsEmpty",     decimal_isempty     },
	{ "IsNegative",  decimal_isnegative  },
	{ "Abs",         decimal_abs         },
	{ "Round",       decimal_round       },
	{ "Truncate",    decimal_truncate    },
	{ "Add",         decimal_add         },
	{ "Sub",         decimal_sub         },
	{ "Mul",         decimal_mul         },
	{ "Div",         decimal_div         },
	{ NULL, NULL }
};

static const struct luaL_Reg decimalmeta[] = {
	{ "__tostring", decimal_tostring },
	{ "__eq",       decimal_eq       },
	{ "__lt",       decimal_lt       },
	{ "__le",       decimal_le       },
	{ "__add",      decimal_add      },
	{ "__sub",      decimal_sub      },
	{ "__mul",      decimal_mul      },
	{ "__div",      decimal_div      },
	{ "__mod",      decimal_mod      },
	{ "__unm",      decimal_unm      },
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
