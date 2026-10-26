#include "mcp/LuaMCP.h"
#include "mcp/LuaMCPMain.h"

// =============================================================================
// Module registration
// =============================================================================

static const luaL_Reg mcp_functions[] = {
	{ "Create", MCP_Create },
	{ NULL, NULL }
};

static const luaL_Reg mcp_meta[] = {
	{ "__gc",       McpServer_GC       },
	{ "__tostring", McpServer_ToString },
	{ NULL, NULL }
};

static const luaL_Reg mcp_methods[] = {
	{ "AddTool",   McpServer_AddTool   },
	{ "Start",     McpServer_Start     },
	{ "Stop",      McpServer_Stop      },
	{ "IsRunning", McpServer_IsRunning },
	{ "CreateElicitation", McpServer_CreateElicitation },
	{ NULL, NULL }
};

static const luaL_Reg mcp_elicitation_meta[] = {
	{ "__gc",       McpElicitation_GC       },
	{ "__tostring", McpElicitation_ToString },
	{ NULL, NULL }
};

static const luaL_Reg mcp_elicitation_methods[] = {
	{ "AddQuestion", McpElicitation_AddQuestion },
	{ "Elicit",      McpElicitation_Elicit      },
	{ NULL, NULL }
};

static const luaL_Reg mcp_question_meta[] = {
	{ "__gc",       McpQuestion_GC       },
	{ "__tostring", McpQuestion_ToString },
	{ NULL, NULL }
};

static const luaL_Reg mcp_question_methods[] = {
	{ "AddAnswer",      McpQuestion_AddAnswer      },
	{ "SetDescription", McpQuestion_SetDescription },
	{ "SetDefault",     McpQuestion_SetDefault     },
	{ "SetRange",       McpQuestion_SetRange       },
	{ "SetLength",      McpQuestion_SetLength      },
	{ "SetFormat",      McpQuestion_SetFormat      },
	{ "SetMultiple",    McpQuestion_SetMultiple    },
	{ "SetCallback",    McpQuestion_SetCallback    },
	{ NULL, NULL }
};

static void McpRegisterType(lua_State* L, const char* name, const luaL_Reg* meta, const luaL_Reg* methods) {
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

	McpRegisterType(L, LUAMCP,            mcp_meta,             mcp_methods);
	McpRegisterType(L, LUAMCPELICITATION, mcp_elicitation_meta, mcp_elicitation_methods);
	McpRegisterType(L, LUAMCPQUESTION,    mcp_question_meta,    mcp_question_methods);

	return 1;
}
