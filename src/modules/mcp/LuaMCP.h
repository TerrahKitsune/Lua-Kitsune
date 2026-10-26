#pragma once
#include "LuaMainInclude.h"
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
// callback.
struct McpTool {
	std::string               name;
	std::string               description;
	std::vector<McpToolParam> params;
	int                        fnRef = LUA_NOREF;
};

// A server-to-client request (e.g. elicitation/create) awaiting its response.
// Keyed by the request id string in LuaMcpServer::pending. Whoever reads the
// matching response line off stdin (the poll loop or any waiting Elicit call)
// decodes it once and parks it here as a registry ref for the waiter to take.
struct McpPendingRequest {
	bool done    = false;
	int  msgRef = LUA_NOREF; // the decoded response message, once done
};

// The MCP server userdata. Only ever exists via placement-new inside a Lua
// userdata block (see lua_pushmcpserver) -- never as a plain C++ global/static,
// since its std::string/std::vector members must be destroyed through the
// normal Lua GC/__gc path (tied to KitsuneInit/KitsuneCleanup), not at
// DLL-unload/static-teardown time (see LuaMCP.cpp for why that matters here).
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
	std::string line; // the poll loop's current line (see McpPollStep)
	bool eof = false; // stdin reached EOF (client disconnected)
	lua_State* toolThread = nullptr; // coroutine running the current tool callback, null between calls

	int  contextRef = LUA_NOREF; // the shared table passed to every tool callback
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

LuaMcpServer* lua_pushmcpserver(lua_State* L);
LuaMcpServer* luaL_checkmcpserver(lua_State* L, int idx);
bool          lua_ismcpserver(lua_State* L, int idx);

// MCP.Create(opt settings, opt context) -> mcp
// Singleton: if a live instance already exists, returns it unchanged and
// ignores the arguments (see the singleton tracking slot in LuaMCP.cpp).
int MCP_Create(lua_State* L);

// mcp:AddTool(name, description, parameters, fn)
int McpServer_AddTool(lua_State* L);

// mcp:Start() -> ok [, errmsg]
int McpServer_Start(lua_State* L);

// mcp:Stop()
int McpServer_Stop(lua_State* L);

// mcp:IsRunning() -> bool
int McpServer_IsRunning(lua_State* L);

// Elicitation -- a reusable, append-only form definition.
// Ownership runs child -> parent: an Elicitation holds a registry ref to its MCP
// server, a Question handle holds a registry ref to its Elicitation. Each releases
// its parent ref in __gc, so the structure is torn down in reverse creation order.

enum McpQuestionType {
	MCP_Q_STRING,
	MCP_Q_INTEGER,
	MCP_Q_NUMBER,
	MCP_Q_BOOLEAN,
};

struct McpAnswer {
	std::string value;
	std::string label;
	int         fnRef = LUA_NOREF;
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
	int  defaultRef = LUA_NOREF;

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
	int  fnRef      = LUA_NOREF;
};

// Only ever exists via placement-new inside a Lua userdata (McpServer_CreateElicitation).
struct LuaMcpElicitation {
	std::string   message;
	int           defaultRef = LUA_NOREF;
	lua_Integer   timeoutMs   = 0;         // 0 = wait as long as the dialog is open
	int           mcpRef     = LUA_NOREF; // pins the MCP server
	LuaMcpServer* server      = nullptr;   // valid while mcpRef is held

	std::vector<McpQuestion> questions;
};

// A plain handle: questions are never removed, so the index stays valid.
struct LuaMcpQuestion {
	int                elicitationRef = LUA_NOREF; // pins the Elicitation
	LuaMcpElicitation* elicitation     = nullptr;   // valid while elicitationRef is held
	size_t             index           = 0;
};

// mcp:CreateElicitation(message, defaultCallback [, timeout]) -> Elicitation
int McpServer_CreateElicitation(lua_State* L);

int McpServer_GC(lua_State* L);
int McpServer_ToString(lua_State* L);

int McpElicitation_AddQuestion(lua_State* L);
int McpElicitation_Elicit(lua_State* L);
int McpElicitation_GC(lua_State* L);
int McpElicitation_ToString(lua_State* L);

int McpQuestion_AddAnswer(lua_State* L);
int McpQuestion_SetDescription(lua_State* L);
int McpQuestion_SetDefault(lua_State* L);
int McpQuestion_SetRange(lua_State* L);
int McpQuestion_SetLength(lua_State* L);
int McpQuestion_SetFormat(lua_State* L);
int McpQuestion_SetMultiple(lua_State* L);
int McpQuestion_SetCallback(lua_State* L);
int McpQuestion_GC(lua_State* L);
int McpQuestion_ToString(lua_State* L);
