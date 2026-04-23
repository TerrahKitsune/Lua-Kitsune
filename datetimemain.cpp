#include "luadatetime.h"
#include "datetimemain.h"

static const struct luaL_Reg datetimefunctions[] = {
	{ "Now",                  datetime_now                 },
	{ "UtcNow",               datetime_utcnow              },
	{ "New",                  datetime_new                 },
	{ "FromUnixSeconds",      datetime_fromunixseconds     },
	{ "FromUnixMilliseconds", datetime_fromunixmilliseconds},
	{ "Parse",                datetime_parse               },
	{ "Year",                 datetime_year                },
	{ "Month",                datetime_month               },
	{ "Day",                  datetime_day                 },
	{ "Hour",                 datetime_hour                },
	{ "Minute",               datetime_minute              },
	{ "Second",               datetime_second              },
	{ "Millisecond",          datetime_millisecond         },
	{ "DayOfWeek",            datetime_dayofweek           },
	{ "OffsetMinutes",        datetime_offsetminutes       },
	{ "UnixSeconds",          datetime_unixseconds         },
	{ "UnixMilliseconds",     datetime_unixmilliseconds    },
	{ "ToUtc",                datetime_toutc               },
	{ "ToLocal",              datetime_tolocal             },
	{ "ToOffset",             datetime_tooffset            },
	{ "Format",               datetime_format              },
	{ "AsString",             datetime_format              },
	{ "IsEmpty",              datetime_isempty             },
	{ "AddDays",              datetime_adddays             },
	{ "AddHours",             datetime_addhours            },
	{ "AddMinutes",           datetime_addminutes          },
	{ "AddSeconds",           datetime_addseconds          },
	{ "AddMilliseconds",      datetime_addmilliseconds     },
	{ "AddTimeSpan",          datetime_addtimespan         },
	{ NULL, NULL }
};

static const struct luaL_Reg datetimemeta[] = {
	{ "__tostring", datetime_tostring   },
	{ "__eq",       datetime_eq         },
	{ "__lt",       datetime_lt         },
	{ "__le",       datetime_le         },
	{ "__add",      datetime_addtimespan},
	{ "__sub",      datetime_sub        },
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
