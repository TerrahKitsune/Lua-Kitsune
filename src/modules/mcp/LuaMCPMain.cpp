#include "mcp/LuaMCP.h"
#include "mcp/LuaMCPMain.h"

// =============================================================================
// Module registration
// =============================================================================

static const luaL_Reg mcpFunctions[] = {
	{ "Create", MCP_Create },
	{ NULL, NULL }
};

static const luaL_Reg mcpMeta[] = {
	{ "__gc",       McpServer_GC       },
	{ "__tostring", McpServer_ToString },
	{ NULL, NULL }
};

static const luaL_Reg mcpMethods[] = {
	{ "AddTool",   McpServer_AddTool   },
	{ "Start",     McpServer_Start     },
	{ "Stop",      McpServer_Stop      },
	{ "IsRunning", McpServer_IsRunning },
	{ "CreateElicitation", McpServer_CreateElicitation },
	{ NULL, NULL }
};

static const luaL_Reg mcpElicitationMeta[] = {
	{ "__gc",       McpElicitation_GC       },
	{ "__tostring", McpElicitation_ToString },
	{ NULL, NULL }
};

static const luaL_Reg mcpElicitationMethods[] = {
	{ "AddQuestion", McpElicitation_AddQuestion },
	{ "Elicit",      McpElicitation_Elicit      },
	{ NULL, NULL }
};

static const luaL_Reg mcpQuestionMeta[] = {
	{ "__gc",       McpQuestion_GC       },
	{ "__tostring", McpQuestion_ToString },
	{ NULL, NULL }
};

static const luaL_Reg mcpQuestionMethods[] = {
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
	luaL_newlibtable(L, mcpFunctions);
	luaL_setfuncs(L, mcpFunctions, 0);

	McpRegisterType(L, LUAMCP,            mcpMeta,             mcpMethods);
	McpRegisterType(L, LUAMCPELICITATION, mcpElicitationMeta, mcpElicitationMethods);
	McpRegisterType(L, LUAMCPQUESTION,    mcpQuestionMeta,    mcpQuestionMethods);

	return 1;
}
