#include "process/LuaProcessMain.h"
#include "process/LuaProcess.h"

static const struct luaL_Reg processfunctions[] = {
	{ "All", Process_All },
#ifdef _WIN32
	{ "Affinity", Process_Affinity },
	{ "Priority", Process_Priority },
	{ "Threads", Process_Threads },
	{ "GetCPU", Process_GetCPU },
#endif
	{ "Open", Process_Open },
	{ "Start", Process_Start },
	{ "Stop", Process_Stop },
	{ "GetID", Process_GetID },
	{ "GetName", Process_GetName },
	{ "GetExitCode", Process_GetExitCode },
	{ "GetRAM", Process_GetRAM },
	{ "ReadFromPipe", Process_ReadFromPipe },
	{ "WriteToPipe", Process_WriteToPipe },
	{ "ReadErrorFromPipe", Process_ReadErrorFromPipe },
	{ NULL, NULL }
};

static const luaL_Reg processmeta[] = {
	{ "__gc", Process_GC },
	{ "__tostring", Process_ToString },
	{ NULL, NULL }
};

int luaopen_process(lua_State *L) {

	luaL_newlibtable(L, processfunctions);
	luaL_setfuncs(L, processfunctions, 0);

	luaL_newmetatable(L, LUAPROCESS);
	luaL_setfuncs(L, processmeta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 1);
	return 1;
}