#pragma once
#include "lua_main_incl.h"
#include <string>
#include <vector>

#define LUAMCP "LUAMCP"

// A single declared parameter for a tool's input schema.
// `type` defaults to "string" (set at AddTool time, never left empty) so schema
// building never needs to special-case a missing type.
struct McpToolParam {
	std::string name;
	std::string type;
	std::string description;
	bool        required = false;
};

// One registered tool: name/description/schema plus a registry ref to the Lua
// callback. Mirrors ToolSuiteTool (luatoolsuite.h) in shape only -- this module
// does not depend on KITSUNE_LLAMA / luatoolsuite.cpp in any way.
struct McpTool {
	std::string               name;
	std::string               description;
	std::vector<McpToolParam> params;
	int                        fn_ref = LUA_NOREF;
};

// The MCP server userdata. Only ever exists via placement-new inside a Lua
// userdata block (see lua_mcp_push) -- never as a plain C++ global/static,
// since its std::string/std::vector members must be destroyed through the
// normal Lua GC/__gc path (tied to KitsuneInit/KitsuneCleanup), not at
// DLL-unload/static-teardown time (see luamcp.cpp for why that matters here).
struct LuaMcpServer {
	std::string name;
	std::string version;
	std::string instructions;
	std::string clientName;
	std::string clientVersion;

	std::vector<McpTool> tools;

	int  context_ref = LUA_NOREF; // the shared table passed to every tool callback
	int  taskRef      = LUA_NOREF; // registry ref to the LuaTask (Tasks.New) running the poll loop
	bool stopRequested = false;
	bool started       = false;

	// Accumulates print()/io.write() output while a tool callback is running
	// (both are globally overridden the moment Start() succeeds -- stdout is
	// reserved for JSON-RPC responses, so nothing may write to it directly).
	// Cleared at the start of each tool dispatch, merged into that call's
	// response at the end. Output produced outside of a dispatch (nothing
	// currently listening) is discarded the next time a dispatch clears it.
	std::string capturedOutput;

	// Raw accumulation buffer for stdin bytes, grown via kitsune_realloc (same
	// idiom as LuaJson's out/outLen/outCap) until a full '\n'-terminated line
	// is available. Freed in __gc; never touched once the coroutine has ended.
	char*  linebuf    = nullptr;
	size_t linebufLen = 0;
	size_t linebufCap = 0;
};

LuaMcpServer* lua_mcp_push(lua_State* L);
LuaMcpServer* lua_mcp_check(lua_State* L, int idx);
bool          lua_mcp_is(lua_State* L, int idx);

// MCP.Create(opt settings, opt context) -> mcp
// Singleton: if a live instance already exists, returns it unchanged and
// ignores the arguments (see the singleton tracking slot in luamcp.cpp).
int lua_mcp_create(lua_State* L);

// mcp:AddTool(name, description, parameters, fn)
int lua_mcp_addtool(lua_State* L);

// mcp:Start() -> ok [, errmsg]
int lua_mcp_start(lua_State* L);

// mcp:Stop()
int lua_mcp_stop(lua_State* L);

// mcp:IsRunning() -> bool
int lua_mcp_isrunning(lua_State* L);

int lua_mcp_gc(lua_State* L);
int lua_mcp_tostring(lua_State* L);

int luaopen_mcp(lua_State* L);
