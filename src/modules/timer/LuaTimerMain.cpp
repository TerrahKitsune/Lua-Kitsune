#include "timer/LuaTimerMain.h"
#include "timer/LuaTimer.h"

static const struct luaL_Reg timerfunctions[] = {
	{ "New", Timer_New },
	{ "IsRunning", Timer_IsRunning },
	{ "Reset", Timer_Reset },
	{ "Start", Timer_Start },
	{ "Stop", Timer_Stop },
	{ "Elapsed", Timer_Elapsed },
	{ "ElapsedTimeSpan", Timer_ElapsedTimeSpan },
	{ NULL, NULL }
};

static const luaL_Reg timermeta[] = {
	{ "__gc", Timer_GC },
	{ "__tostring", Timer_ToString },
	{ NULL, NULL }
};

int luaopen_timer(lua_State *L) {

	luaL_newlibtable(L, timerfunctions);
	luaL_setfuncs(L, timerfunctions, 0);

	luaL_newmetatable(L, TIMER);
	luaL_setfuncs(L, timermeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}