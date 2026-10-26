#include "datetime/LuaDateTime.h"
#include "datetime/LuaDateTimeMain.h"

static const struct luaL_Reg datetimefunctions[] = {
	{ "Now",                  DateTime_Now                 },
	{ "UtcNow",               DateTime_UtcNow              },
	{ "New",                  DateTime_New                 },
	{ "FromUnixSeconds",      DateTime_FromUnixSeconds     },
	{ "FromUnixMilliseconds", DateTime_FromUnixMilliseconds},
	{ "Parse",                DateTime_Parse               },
	{ "Year",                 DateTime_Year                },
	{ "Month",                DateTime_Month               },
	{ "Day",                  DateTime_Day               },
	{ "Hour",                 DateTime_Hour                },
	{ "Minute",               DateTime_Minute              },
	{ "Second",               DateTime_Second              },
	{ "Millisecond",          DateTime_Millisecond         },
	{ "DayOfWeek",            DateTime_DayOfWeek           },
	{ "OffsetMinutes",        DateTime_OffsetMinutes       },
	{ "UnixSeconds",          DateTime_UnixSeconds         },
	{ "UnixMilliseconds",     DateTime_UnixMilliseconds    },
	{ "ToUtc",                DateTime_ToUtc               },
	{ "ToLocal",              DateTime_ToLocal             },
	{ "ToOffset",             DateTime_ToOffset            },
	{ "Format",               DateTime_Format              },
	{ "AsString",             DateTime_Format              },
	{ "IsEmpty",              DateTime_IsEmpty             },
	{ "AddDays",              DateTime_AddDays             },
	{ "AddHours",             DateTime_AddHours            },
	{ "AddMinutes",           DateTime_AddMinutes          },
	{ "AddSeconds",           DateTime_AddSeconds          },
	{ "AddMilliseconds",      DateTime_AddMilliseconds     },
	{ "AddTimeSpan",          DateTime_AddTimeSpan         },
	{ NULL, NULL }
};

static const struct luaL_Reg datetimemeta[] = {
	{ "__tostring", DateTime_ToString   },
	{ "__eq",       DateTime_Eq         },
	{ "__lt",       DateTime_Lt         },
	{ "__le",       DateTime_Le         },
	{ "__add",      DateTime_AddTimeSpan},
	{ "__sub",      DateTime_Sub        },
	{ NULL, NULL }
};

int luaopen_datetime(lua_State* L) {
	luaL_newlibtable(L, datetimefunctions);
	luaL_setfuncs(L, datetimefunctions, 0);

	luaL_newmetatable(L, LUADATETIME);
	luaL_setfuncs(L, datetimemeta, 0);
	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}
