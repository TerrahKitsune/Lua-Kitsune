#include "mcp/LuaMCP.h"
#include "mcp/LuaMCPMain.h"

// =============================================================================
// Module registration
// =============================================================================

static const luaL_Reg mcp_functions[] = {
	{ "Create", lua_mcp_create },
	{ NULL, NULL }
};

static const luaL_Reg mcp_meta[] = {
	{ "__gc",       lua_mcp_gc       },
	{ "__tostring", lua_mcp_tostring },
	{ NULL, NULL }
};

static const luaL_Reg mcp_methods[] = {
	{ "AddTool",   lua_mcp_addtool   },
	{ "Start",     lua_mcp_start     },
	{ "Stop",      lua_mcp_stop      },
	{ "IsRunning", lua_mcp_isrunning },
	{ "CreateElicitation", lua_mcp_createelicitation },
	{ NULL, NULL }
};

static const luaL_Reg mcp_elicitation_meta[] = {
	{ "__gc",       lua_mcp_elicitation_gc       },
	{ "__tostring", lua_mcp_elicitation_tostring },
	{ NULL, NULL }
};

static const luaL_Reg mcp_elicitation_methods[] = {
	{ "AddQuestion", lua_mcp_elicitation_addquestion },
	{ "Elicit",      lua_mcp_elicitation_elicit      },
	{ NULL, NULL }
};

static const luaL_Reg mcp_question_meta[] = {
	{ "__gc",       lua_mcp_question_gc       },
	{ "__tostring", lua_mcp_question_tostring },
	{ NULL, NULL }
};

static const luaL_Reg mcp_question_methods[] = {
	{ "AddAnswer",      lua_mcp_question_addanswer      },
	{ "SetDescription", lua_mcp_question_setdescription },
	{ "SetDefault",     lua_mcp_question_setdefault     },
	{ "SetRange",       lua_mcp_question_setrange       },
	{ "SetLength",      lua_mcp_question_setlength      },
	{ "SetFormat",      lua_mcp_question_setformat      },
	{ "SetMultiple",    lua_mcp_question_setmultiple    },
	{ "SetCallback",    lua_mcp_question_setcallback    },
	{ NULL, NULL }
};

static void mcp_register_type(lua_State* L, const char* name, const luaL_Reg* meta, const luaL_Reg* methods) {
	luaL_newmetatable(L, name);
	luaL_setfuncs(L, meta, 0);
	lua_pushliteral(L, "__index");
	lua_newtable(L);
	luaL_setfuncs(L, methods, 0);
	lua_rawset(L, -3);
	lua_pop(L, 1);
}

int luaopen_mcp(lua_State* L) {
	luaL_newlibtable(L, mcp_functions);
	luaL_setfuncs(L, mcp_functions, 0);

	mcp_register_type(L, LUAMCP,            mcp_meta,             mcp_methods);
	mcp_register_type(L, LUAMCPELICITATION, mcp_elicitation_meta, mcp_elicitation_methods);
	mcp_register_type(L, LUAMCPQUESTION,    mcp_question_meta,    mcp_question_methods);

	return 1;
}
