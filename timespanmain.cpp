#include "luatimespan.h"
#include "timespanmain.h"

static const struct luaL_Reg timespanfunctions[] = {
    { "Zero",            timespan_zero            },
    { "FromTicks",       timespan_fromticks       },
    { "FromSeconds",     timespan_fromseconds     },
    { "FromMilliseconds",timespan_frommilliseconds},
    { "FromMinutes",     timespan_fromminutes     },
    { "FromHours",       timespan_fromhours       },
    { "FromDays",        timespan_fromdays        },
    { "ToString",        timespan_tostring        },
    { "AsString",        timespan_tostring        },
    { "Ticks",           timespan_ticks           },
    { "TotalSeconds",    timespan_totalseconds    },
    { "TotalMilliseconds",timespan_totalmilliseconds},
    { "TotalMinutes",    timespan_totalminutes    },
    { "TotalHours",      timespan_totalhours      },
    { "TotalDays",       timespan_totaldays       },
    { "Days",            timespan_days            },
    { "Hours",           timespan_hours           },
    { "Minutes",         timespan_minutes         },
    { "Seconds",         timespan_seconds         },
    { "Milliseconds",    timespan_milliseconds    },
    { "IsZero",          timespan_iszero          },
    { "IsNegative",      timespan_isnegative      },
    { "Abs",             timespan_abs             },
    { "Add",             timespan_add             },
    { "Sub",             timespan_sub             },
    { NULL, NULL }
};

static const struct luaL_Reg timespanmeta[] = {
    { "__tostring", timespan_tostring },
    { "__eq",       timespan_eq       },
    { "__lt",       timespan_lt       },
    { "__le",       timespan_le       },
    { "__add",      timespan_add      },
    { "__sub",      timespan_sub      },
    { "__mul",      timespan_mul      },
    { "__div",      timespan_div      },
    { "__unm",      timespan_unm      },
    { NULL, NULL }
};

int luaopen_timespan(lua_State* L) {
    luaL_newlibtable(L, timespanfunctions);
    luaL_setfuncs(L, timespanfunctions, 0);

    luaL_newmetatable(L, LUATIMESPAN);
    luaL_setfuncs(L, timespanmeta, 0);
    lua_pushliteral(L, "__index");
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);
    lua_pushliteral(L, "__metatable");
    lua_pushvalue(L, -3);
    lua_rawset(L, -3);

    lua_pop(L, 1);
    return 1;
}
