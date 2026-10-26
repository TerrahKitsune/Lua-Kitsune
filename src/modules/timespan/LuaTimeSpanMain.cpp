#include "timespan/LuaTimeSpan.h"
#include "timespan/LuaTimeSpanMain.h"

static const struct luaL_Reg timespanfunctions[] = {
    { "Zero",            TimeSpan_Zero            },
    { "FromTicks",       TimeSpan_FromTicks       },
    { "FromSeconds",     TimeSpan_FromSeconds     },
    { "FromMilliseconds",TimeSpan_FromMilliseconds},
    { "FromMinutes",     TimeSpan_FromMinutes     },
    { "FromHours",       TimeSpan_FromHours       },
    { "FromDays",        TimeSpan_FromDays        },
    { "ToString",        TimeSpan_ToString        },
    { "AsString",        TimeSpan_ToString        },
    { "Ticks",           TimeSpan_Ticks           },
    { "TotalSeconds",    TimeSpan_TotalSeconds    },
    { "TotalMilliseconds",TimeSpan_TotalMilliseconds},
    { "TotalMinutes",    TimeSpan_TotalMinutes    },
    { "TotalHours",      TimeSpan_TotalHours      },
    { "TotalDays",       TimeSpan_TotalDays       },
    { "Days",            TimeSpan_Days            },
    { "Hours",           TimeSpan_Hours           },
    { "Minutes",         TimeSpan_Minutes         },
    { "Seconds",         TimeSpan_Seconds         },
    { "Milliseconds",    TimeSpan_Milliseconds    },
    { "IsZero",          TimeSpan_IsZero          },
    { "IsNegative",      TimeSpan_IsNegative      },
    { "Abs",             TimeSpan_Abs             },
    { "Add",             TimeSpan_Add             },
    { "Sub",             TimeSpan_Sub             },
    { NULL, NULL }
};

static const struct luaL_Reg timespanmeta[] = {
    { "__tostring", TimeSpan_ToString },
    { "__eq",       TimeSpan_Eq       },
    { "__lt",       TimeSpan_Lt       },
    { "__le",       TimeSpan_Le       },
    { "__add",      TimeSpan_Add      },
    { "__sub",      TimeSpan_Sub      },
    { "__mul",      TimeSpan_Mul      },
    { "__div",      TimeSpan_Div      },
    { "__unm",      TimeSpan_Unm      },
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
