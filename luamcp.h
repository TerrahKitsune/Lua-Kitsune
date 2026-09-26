#pragma once
#include "lua_main_incl.h"
#include <deque>
#include <map>
#include <string>
#include <vector>

#define LUAMCP            "LUAMCP"
#define LUAMCPELICITATION "LUAMCPELICITATION"
#define LUAMCPQUESTION    "LUAMCPQUESTION"

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

// A server-to-client request (e.g. elicitation/create) awaiting its response.
// Keyed by the request id string in LuaMcpServer::pending. Whoever reads the
// matching response line off stdin (the poll loop or any waiting Elicit call)
// decodes it once and parks it here as a registry ref for the waiter to take.
struct McpPendingRequest {
	bool done    = false;
	int  msg_ref = LUA_NOREF; // the decoded response message, once done
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
	std::string protocolVersion;          // negotiated in initialize
	int  clientCapsRef     = LUA_NOREF;   // the client's `capabilities` table from initialize
	bool clientElicitation = false;       // client declared capabilities.elicitation

	std::vector<McpTool> tools;

	// Outstanding server-to-client requests, by id (see McpPendingRequest).
	std::map<std::string, McpPendingRequest> pending;
	long long nextRequestId = 1;

	// Client requests read off stdin while an Elicit call was waiting for its
	// response (e.g. a second tools/call). Held raw and dispatched by the poll
	// loop, in order, before it reads anything new.
	std::deque<std::string> deferred;
	std::string line; // the poll loop's current line (see mcp_poll_step)
	bool eof = false; // stdin reached EOF (client disconnected)
	lua_State* toolThread = nullptr; // coroutine running the current tool callback, null between calls

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

// =============================================================================
// Elicitation -- a reusable, append-only form definition (see Plans/McpElicitation.md).
// Ownership runs child -> parent: an Elicitation holds a registry ref to its MCP
// server, a Question handle holds a registry ref to its Elicitation. Each releases
// its parent ref in __gc, so the structure is torn down in reverse creation order.
// =============================================================================

enum McpQuestionType {
	MCP_Q_STRING,
	MCP_Q_INTEGER,
	MCP_Q_NUMBER,
	MCP_Q_BOOLEAN,
};

struct McpAnswer {
	std::string value;
	std::string label;
	int         fn_ref = LUA_NOREF;
};

// Every optional attribute is write-once: its has* flag is set by the setter,
// and a second call raises.
struct McpQuestion {
	std::string     name;
	std::string     title;
	McpQuestionType type     = MCP_Q_STRING;
	bool            required = false;

	std::vector<McpAnswer> answers;

	bool        hasDescription = false;
	std::string description;

	bool hasDefault  = false;
	int  default_ref = LUA_NOREF;

	bool        hasRange = false;
	bool        hasMin = false, hasMax = false;
	lua_Integer minI = 0, maxI = 0; // MCP_Q_INTEGER
	lua_Number  minN = 0, maxN = 0; // MCP_Q_NUMBER

	bool        hasLength = false;
	bool        hasMinLen = false, hasMaxLen = false;
	lua_Integer minLen = 0, maxLen = 0;

	bool        hasFormat = false;
	std::string format;

	bool        multiple = false;
	bool        hasMinItems = false, hasMaxItems = false;
	lua_Integer minItems = 0, maxItems = 0;

	bool hasCallback = false;
	int  fn_ref      = LUA_NOREF;
};

// Only ever exists via placement-new inside a Lua userdata (lua_mcp_createelicitation).
struct LuaMcpElicitation {
	std::string   message;
	int           default_ref = LUA_NOREF;
	lua_Integer   timeoutMs   = 0;         // 0 = wait as long as the dialog is open
	int           mcp_ref     = LUA_NOREF; // pins the MCP server
	LuaMcpServer* server      = nullptr;   // valid while mcp_ref is held

	std::vector<McpQuestion> questions;
};

// A plain handle: questions are never removed, so the index stays valid.
struct LuaMcpQuestion {
	int                elicitation_ref = LUA_NOREF; // pins the Elicitation
	LuaMcpElicitation* elicitation     = nullptr;   // valid while elicitation_ref is held
	size_t             index           = 0;
};

// mcp:CreateElicitation(message, defaultCallback [, timeout]) -> Elicitation
int lua_mcp_createelicitation(lua_State* L);

int lua_mcp_gc(lua_State* L);
int lua_mcp_tostring(lua_State* L);

int luaopen_mcp(lua_State* L);
