#include "mcp/LuaMCP.h"
#include "json/LuaJson.h"
#include "KitsuneEngine.h"
#include <chrono>
#include <cstring>
#include <cstdio>
#include <new>

#ifdef _WIN32
#include <Windows.h>
#else
#include <poll.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// Singleton tracking -- a plain POD registry-ref int. Deliberately NOT a
// pointer/object with a constructor or destructor: any global with a
// std::vector/std::string member (or one that owns one) would have its
// destructor run at DLL-unload/static-teardown time, governed by the loader
// rather than by KitsuneCleanup()/EndMemoryManager() -- a real hazard in this
// codebase (Mem.cpp routes all C++ allocation, STL included, through
// kitsune_malloc/kitsune_free; EndMemoryManager's USEMEMORYMANAGER path can
// tear the backing allocator down before such a global would be destructed).
// LuaMcpServer itself only ever exists via placement-new inside a Lua
// userdata (see lua_pushmcpserver), so its lifetime is tied to Lua GC / __gc.
static int g_mcpInstanceRef = LUA_NOREF;

static int  McpPollStep(lua_State* L, LuaMcpServer* server);
static int  McpPollContinuation(lua_State* L, int status, lua_KContext ctx);
static int  McpPollEntrypoint(lua_State* L);
static int  McpDispatchLine(lua_State* L, LuaMcpServer* server, const char* data, size_t len);
static int  McpDispatchToolsCall(lua_State* L, LuaMcpServer* server, int paramsIdx, int idIdx);
static int  McpToolFinish(lua_State* L, LuaMcpServer* server, int pcallStatus);
static int  McpToolContinuation(lua_State* L, int status, lua_KContext ctx);
static void McpHandleInitialize(lua_State* L, LuaMcpServer* server, int paramsIdx, int idIdx);
static int  McpNextLine(LuaMcpServer* server, std::string& out);
static void McpRouteResponse(lua_State* L, LuaMcpServer* server, int msgIdx, int idIdx);

// Platform: stdio availability + non-blocking poll.
// Direct adaptation of LuaProcess.cpp's Process_ReadFromPipe/ReadNonblocking, pointed
// at our own inherited stdin/stdout instead of a child process's pipe.
// Returns from McpPollStdin: >0 = bytes read, 0 = nothing available right
// now, <0 = EOF (stdin closed -- the normal MCP client-disconnect path).

#ifdef _WIN32

static bool McpStdioAvailable() {
	HANDLE hin  = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
	return hin != NULL && hin != INVALID_HANDLE_VALUE &&
	       hout != NULL && hout != INVALID_HANDLE_VALUE;
}

static int McpPollStdin(char* buf, size_t bufsize) {
	HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
	if (hin == NULL || hin == INVALID_HANDLE_VALUE)
		return -1;

	DWORD avail = 0;
	if (!PeekNamedPipe(hin, NULL, 0, NULL, &avail, NULL)) {
		DWORD err = GetLastError();
		if (err == ERROR_BROKEN_PIPE)
			return -1;
		// Not a pipe (e.g. an interactive console) -- there is no reliable
		// non-blocking peek available here. This isn't the expected shape for
		// a real MCP client (which always redirects stdin to a pipe), so we
		// simply report "nothing right now" rather than risk a blocking read.
		return 0;
	}
	if (avail == 0)
		return 0;

	DWORD toRead = (DWORD)(avail < (DWORD)bufsize ? avail : (DWORD)bufsize);
	DWORD read   = 0;
	if (!ReadFile(hin, buf, toRead, &read, NULL)) {
		DWORD err = GetLastError();
		return (err == ERROR_BROKEN_PIPE) ? -1 : 0;
	}
	if (read == 0)
		return -1;
	return (int)read;
}

#else

static bool McpStdioAvailable() {
	return fcntl(0, F_GETFD) != -1 && fcntl(1, F_GETFD) != -1;
}

static int McpPollStdin(char* buf, size_t bufsize) {
	struct pollfd pfd;
	pfd.fd      = 0;
	pfd.events  = POLLIN;
	pfd.revents = 0;
	int pr = poll(&pfd, 1, 0);
	if (pr <= 0)
		return 0;
	if (!(pfd.revents & (POLLIN | POLLHUP)))
		return 0;

	ssize_t n = read(0, buf, bufsize);
	if (n < 0)
		return 0; // transient error -- treat as "nothing yet"
	if (n == 0)
		return -1; // EOF
	return (int)n;
}

#endif

// Userdata lifecycle

LuaMcpServer* lua_pushmcpserver(lua_State* L) {
	LuaMcpServer* s = (LuaMcpServer*)lua_newuserdata(L, sizeof(LuaMcpServer));
	new (s) LuaMcpServer();
	luaL_setmetatable(L, LUAMCP);
	return s;
}

LuaMcpServer* luaL_checkmcpserver(lua_State* L, int idx) {
	return (LuaMcpServer*)luaL_checkudata(L, idx, LUAMCP);
}

bool lua_ismcpserver(lua_State* L, int idx) {
	if (!lua_isuserdata(L, idx))
		return false;
	return luaL_testudata(L, idx, LUAMCP) != NULL;
}

int McpServer_GC(lua_State* L) {
	LuaMcpServer* server = luaL_checkmcpserver(L, 1);

	server->stopRequested = true;

	for (McpTool& t : server->tools) {
		if (t.fnRef != LUA_NOREF)
			luaL_unref(L, LUA_REGISTRYINDEX, t.fnRef);
	}
	if (server->contextRef != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, server->contextRef);
	if (server->clientCapsRef != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, server->clientCapsRef);
	for (auto& kv : server->pending) {
		if (kv.second.msgRef != LUA_NOREF)
			luaL_unref(L, LUA_REGISTRYINDEX, kv.second.msgRef);
	}

	if (server->linebuf) {
		kitsune_free(server->linebuf);
		server->linebuf = NULL;
	}

	if (server->taskRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, server->taskRef);
		server->taskRef = LUA_NOREF;
	}

	if (g_mcpInstanceRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_mcpInstanceRef);
		bool sameInstance = lua_touserdata(L, -1) == (void*)server;
		lua_pop(L, 1);
		if (sameInstance) {
			luaL_unref(L, LUA_REGISTRYINDEX, g_mcpInstanceRef);
			g_mcpInstanceRef = LUA_NOREF;
		}
	}

	server->~LuaMcpServer();
	return 0;
}

int McpServer_ToString(lua_State* L) {
	LuaMcpServer* server = luaL_checkmcpserver(L, 1);
	lua_pushfstring(L, "MCP(%d tools)", (int)server->tools.size());
	return 1;
}

// JSON bridge helpers -- everything goes through the shared LuaJson bridge
// instance (LuaJsonBridgeRegistryKey()). No hand-rolled JSON anywhere.

// Decodes `data`/`len` (a JSON-RPC line) into a Lua value, pushing the result.
// Returns true on success (value on top of stack); false on failure (nothing pushed).
static bool McpJsonDecode(lua_State* L, const char* data, size_t len) {
	lua_pushcfunction(L, Json_Decode);
	lua_rawgetp(L, LUA_REGISTRYINDEX, LuaJsonBridgeRegistryKey());
	lua_pushlstring(L, data, len);
	if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
		lua_pop(L, 1);
		return false;
	}
	return true;
}

// Encodes the Lua value at stack index `valueIdx` to a JSON string, pushing
// the result string. Returns true on success (string on top of stack).
static bool McpJsonEncode(lua_State* L, int valueIdx) {
	valueIdx = lua_absindex(L, valueIdx);
	lua_pushcfunction(L, Json_Encode);
	lua_rawgetp(L, LUA_REGISTRYINDEX, LuaJsonBridgeRegistryKey());
	lua_pushvalue(L, valueIdx);
	if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
		lua_pop(L, 1);
		return false;
	}
	return true;
}

// The bridge decodes JSON null as the Json.Null sentinel, not nil: treat both as absent.
static bool McpIsNull(lua_State* L, int idx) {
	return lua_isnil(L, idx) || lua_touserdata(L, idx) == LuaJsonNull();
}

static void McpWriteLine(const char* data, size_t len) {
	fwrite(data, 1, len, stdout);
	fputc('\n', stdout);
	fflush(stdout);
}

// Writes {jsonrpc="2.0", id=<value at idIdx>, result=<value at result_idx>}.
static void McpSendResult(lua_State* L, int idIdx, int result_idx) {
	idIdx     = lua_absindex(L, idIdx);
	result_idx = lua_absindex(L, result_idx);

	lua_newtable(L);
	lua_pushliteral(L, "2.0");
	lua_setfield(L, -2, "jsonrpc");
	lua_pushvalue(L, idIdx);
	lua_setfield(L, -2, "id");
	lua_pushvalue(L, result_idx);
	lua_setfield(L, -2, "result");

	if (McpJsonEncode(L, -1)) {
		size_t len;
		const char* s = lua_tolstring(L, -1, &len);
		if (s) McpWriteLine(s, len);
		lua_pop(L, 1); // encoded string
	}
	lua_pop(L, 1); // envelope table
}

static void McpSendError(lua_State* L, int idIdx, int code, const char* message) {
	idIdx = lua_absindex(L, idIdx);

	lua_newtable(L); // envelope
	lua_pushliteral(L, "2.0");
	lua_setfield(L, -2, "jsonrpc");
	lua_pushvalue(L, idIdx);
	lua_setfield(L, -2, "id");

	lua_newtable(L); // error object
	lua_pushinteger(L, code);
	lua_setfield(L, -2, "code");
	lua_pushstring(L, message);
	lua_setfield(L, -2, "message");
	lua_setfield(L, -2, "error");

	if (McpJsonEncode(L, -1)) {
		size_t len;
		const char* s = lua_tolstring(L, -1, &len);
		if (s) McpWriteLine(s, len);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}

// Result builders

static void McpPushEmptyObject(lua_State* L) {
	// Guarantees `{}` on encode regardless of the bridge's emptyObjectAsSentinel
	// setting (default off, which would otherwise encode an empty Lua table as `[]`).
	lua_pushlightuserdata(L, LuaJsonEmptyObject());
}

static void McpPushInitializeResult(lua_State* L, LuaMcpServer* server) {
	lua_newtable(L); // result

	lua_pushlstring(L, server->protocolVersion.c_str(), server->protocolVersion.size());
	lua_setfield(L, -2, "protocolVersion");

	lua_newtable(L); // capabilities
	McpPushEmptyObject(L);
	lua_setfield(L, -2, "tools");
	lua_setfield(L, -2, "capabilities");

	lua_newtable(L); // serverInfo
	lua_pushlstring(L, server->name.c_str(), server->name.size());
	lua_setfield(L, -2, "name");
	lua_pushlstring(L, server->version.c_str(), server->version.size());
	lua_setfield(L, -2, "version");
	lua_setfield(L, -2, "serverInfo");

	if (!server->instructions.empty()) {
		lua_pushlstring(L, server->instructions.c_str(), server->instructions.size());
		lua_setfield(L, -2, "instructions");
	}
}

static void McpPushToolInputSchema(lua_State* L, const McpTool& tool) {
	lua_newtable(L); // schema
	lua_pushliteral(L, "object");
	lua_setfield(L, -2, "type");

	if (tool.params.empty()) {
		McpPushEmptyObject(L);
	} else {
		lua_newtable(L); // properties
		for (const McpToolParam& p : tool.params) {
			lua_newtable(L); // one property descriptor
			lua_pushlstring(L, p.type.c_str(), p.type.size());
			lua_setfield(L, -2, "type");
			if (!p.description.empty()) {
				lua_pushlstring(L, p.description.c_str(), p.description.size());
				lua_setfield(L, -2, "description");
			}
			lua_setfield(L, -2, p.name.c_str());
		}
	}
	lua_setfield(L, -2, "properties");

	lua_newtable(L); // required array
	int ri = 1;
	for (const McpToolParam& p : tool.params) {
		if (p.required) {
			lua_pushlstring(L, p.name.c_str(), p.name.size());
			lua_rawseti(L, -2, ri++);
		}
	}
	lua_setfield(L, -2, "required");
}

static void McpPushToolDescriptor(lua_State* L, const McpTool& tool) {
	lua_newtable(L);
	lua_pushlstring(L, tool.name.c_str(), tool.name.size());
	lua_setfield(L, -2, "name");
	lua_pushlstring(L, tool.description.c_str(), tool.description.size());
	lua_setfield(L, -2, "description");
	McpPushToolInputSchema(L, tool);
	lua_setfield(L, -2, "inputSchema");
}

static void McpPushToolsListResult(lua_State* L, LuaMcpServer* server) {
	lua_newtable(L); // result
	lua_newtable(L); // tools array
	for (size_t i = 0; i < server->tools.size(); i++) {
		McpPushToolDescriptor(L, server->tools[i]);
		lua_rawseti(L, -2, (int)(i + 1));
	}
	lua_setfield(L, -2, "tools");
}

// Dispatch: initialize

// Protocol revisions this server can speak, newest first. None of the changes
// between them affect what this server sends for tools; the newer revisions are
// what let clients offer elicitation.
static const char* const mcpSupportedVersions[] = {
	"2025-11-25",
	"2025-06-18",
	"2025-03-26",
	"2024-11-05",
};

static void McpHandleInitialize(lua_State* L, LuaMcpServer* server, int paramsIdx, int idIdx) {
	// Echo the client's requested version when we support it, otherwise offer
	// our latest and let the client decide (per the lifecycle spec).
	server->protocolVersion = mcpSupportedVersions[0];
	server->clientElicitation = false;
	if (server->clientCapsRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, server->clientCapsRef);
		server->clientCapsRef = LUA_NOREF;
	}

	if (lua_istable(L, paramsIdx)) {
		lua_getfield(L, paramsIdx, "protocolVersion");
		const char* requested = lua_isstring(L, -1) ? lua_tostring(L, -1) : NULL;
		if (requested) {
			for (const char* v : mcpSupportedVersions) {
				if (strcmp(v, requested) == 0) { server->protocolVersion = v; break; }
			}
		}
		lua_pop(L, 1);

		lua_getfield(L, paramsIdx, "capabilities");
		if (lua_istable(L, -1)) {
			lua_getfield(L, -1, "elicitation"); // `{}` decodes to an empty table, so test for presence
			server->clientElicitation = !McpIsNull(L, -1);
			lua_pop(L, 1);
			server->clientCapsRef = luaL_ref(L, LUA_REGISTRYINDEX); // pops capabilities
		} else {
			lua_pop(L, 1);
		}

		lua_getfield(L, paramsIdx, "clientInfo");
		if (lua_istable(L, -1)) {
			lua_getfield(L, -1, "name");
			if (lua_isstring(L, -1)) server->clientName = lua_tostring(L, -1);
			lua_pop(L, 1);

			lua_getfield(L, -1, "version");
			if (lua_isstring(L, -1)) server->clientVersion = lua_tostring(L, -1);
			lua_pop(L, 1);
		}
		lua_pop(L, 1); // clientInfo
	}

	McpPushInitializeResult(L, server);
	McpSendResult(L, idIdx, -1);
	lua_pop(L, 1); // result table
}

// Dispatch: tools/call -- yield-safe via a lua_pcallk continuation. MCP
// dispatches exactly one call per request, so no batch bookkeeping is
// needed. `server` itself (already Lua-GC-owned) is passed as the lua_KContext,
// so no extra heap allocation is needed for the yield to survive on.
//
// Stack discipline: by the time lua_pcallk is invoked, the stack is exactly
// [..., msg, id, params, method, fn, context, request]. After the call
// resolves (synchronously or via the continuation), Lua guarantees the stack
// looks like [..., msg, id, params, method, result_or_error] -- so
// McpToolFinish can derive every needed index purely from lua_gettop(L).

static int McpToolFinish(lua_State* L, LuaMcpServer* server, int pcallStatus) {
	server->toolThread = nullptr;
	int top      = lua_gettop(L);
	int idIdx   = top - 3;
	int msgIdx  = top - 4;

	bool ok = (pcallStatus == LUA_OK || pcallStatus == LUA_YIELD);
	const char* raw = lua_tostring(L, top);
	std::string text = ok ? (raw ? raw : "") : (std::string("error: ") + (raw ? raw : "unknown error"));

	lua_newtable(L); // envelope
	lua_newtable(L); // content array
	int contentIdx = lua_gettop(L);
	int contentN    = 0;

	if (!server->capturedOutput.empty()) {
		lua_newtable(L);
		lua_pushliteral(L, "text");
		lua_setfield(L, -2, "type");
		lua_pushlstring(L, server->capturedOutput.c_str(), server->capturedOutput.size());
		lua_setfield(L, -2, "text");
		lua_rawseti(L, contentIdx, ++contentN);
	}

	lua_newtable(L);
	lua_pushliteral(L, "text");
	lua_setfield(L, -2, "type");
	lua_pushlstring(L, text.c_str(), text.size());
	lua_setfield(L, -2, "text");
	lua_rawseti(L, contentIdx, ++contentN);

	lua_setfield(L, -2, "content");
	if (!ok) {
		lua_pushboolean(L, 1);
		lua_setfield(L, -2, "isError");
	}

	McpSendResult(L, idIdx, -1);
	lua_pop(L, 1); // envelope

	lua_settop(L, msgIdx - 1); // drop msg/id/params/method/result entirely
	return McpPollStep(L, server);
}

static int McpToolContinuation(lua_State* L, int status, lua_KContext ctx) {
	return McpToolFinish(L, (LuaMcpServer*)ctx, status);
}

static int McpDispatchToolsCall(lua_State* L, LuaMcpServer* server, int paramsIdx, int idIdx) {
	int msgIdx = idIdx - 1;

	lua_getfield(L, paramsIdx, "name");
	const char* toolName = lua_tostring(L, -1);

	const McpTool* tool = NULL;
	if (toolName) {
		for (const McpTool& t : server->tools) {
			if (t.name == toolName) { tool = &t; break; }
		}
	}
	lua_pop(L, 1); // name

	if (!tool) {
		McpSendError(L, idIdx, -32602, toolName ? "Unknown tool" : "Missing tool name");
		lua_settop(L, msgIdx - 1);
		return McpPollStep(L, server);
	}

	lua_getfield(L, paramsIdx, "arguments"); // table or nil
	int  argsIdx = lua_gettop(L);
	bool hasArgs = lua_istable(L, argsIdx);

	server->capturedOutput.clear();

	lua_rawgeti(L, LUA_REGISTRYINDEX, tool->fnRef);        // fn
	lua_rawgeti(L, LUA_REGISTRYINDEX, server->contextRef); // context

	lua_newtable(L); // request
	int reqIdx = lua_gettop(L);

	if (hasArgs) lua_pushvalue(L, argsIdx);
	else          McpPushEmptyObject(L);
	lua_setfield(L, reqIdx, "Arguments");

	lua_newtable(L); // Parameters (positional, declared order)
	for (size_t i = 0; i < tool->params.size(); i++) {
		if (hasArgs) lua_getfield(L, argsIdx, tool->params[i].name.c_str());
		else          lua_pushnil(L);
		lua_rawseti(L, -2, (int)(i + 1));
	}
	lua_setfield(L, reqIdx, "Parameters");

	lua_pushlstring(L, tool->name.c_str(), tool->name.size());
	lua_setfield(L, reqIdx, "Name");

	lua_pushvalue(L, idIdx);
	lua_setfield(L, reqIdx, "RequestId");

	// Caller-specific data lives on the request, not the server: a future HTTP
	// transport serves several callers from one process. stdio is one connection, "0".
	lua_pushliteral(L, "0");
	lua_setfield(L, reqIdx, "McpSessionId");
	lua_pushboolean(L, server->clientElicitation ? 1 : 0);
	lua_setfield(L, reqIdx, "CanElicit");

	lua_newtable(L); // Client
	lua_pushlstring(L, server->clientName.c_str(), server->clientName.size());
	lua_setfield(L, -2, "Name");
	lua_pushlstring(L, server->clientVersion.c_str(), server->clientVersion.size());
	lua_setfield(L, -2, "Version");
	if (server->clientCapsRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, server->clientCapsRef);
		lua_setfield(L, -2, "Capabilities");
	}
	lua_setfield(L, reqIdx, "Client");

	lua_remove(L, argsIdx); // fn/context/request shift down by one to become contiguous

	server->toolThread = L; // lets Elicit tell it's inside this call
	int rc = lua_pcallk(L, 2, 1, 0, (lua_KContext)server, McpToolContinuation);
	return McpToolFinish(L, server, rc);
}

static int McpDispatchLine(lua_State* L, LuaMcpServer* server, const char* data, size_t len) {
	if (!McpJsonDecode(L, data, len)) {
		lua_pushnil(L);
		McpSendError(L, -1, -32700, "Parse error");
		lua_pop(L, 1);
		return McpPollStep(L, server);
	}

	int msgIdx = lua_gettop(L);
	if (!lua_istable(L, msgIdx)) {
		lua_settop(L, msgIdx - 1);
		return McpPollStep(L, server);
	}

	lua_getfield(L, msgIdx, "id");
	int idIdx = lua_gettop(L);
	bool hasId = !lua_isnil(L, idIdx);

	lua_getfield(L, msgIdx, "params");
	int paramsIdx = lua_gettop(L);

	lua_getfield(L, msgIdx, "method");
	const char* method = lua_tostring(L, -1);

	if (!method) {
		// A response to one of our own requests (elicitation/create) -- never answered.
		lua_getfield(L, msgIdx, "result");
		lua_getfield(L, msgIdx, "error");
		bool isResponse = !lua_isnil(L, -2) || !lua_isnil(L, -1);
		lua_pop(L, 2);
		if (isResponse)     McpRouteResponse(L, server, msgIdx, idIdx);
		else if (hasId)     McpSendError(L, idIdx, -32600, "Invalid Request");
	} else if (strcmp(method, "initialize") == 0) {
		McpHandleInitialize(L, server, paramsIdx, idIdx);
	} else if (strcmp(method, "notifications/initialized") == 0) {
		// no response
	} else if (strcmp(method, "ping") == 0) {
		McpPushEmptyObject(L);
		McpSendResult(L, idIdx, -1);
		lua_pop(L, 1);
	} else if (strcmp(method, "tools/list") == 0) {
		McpPushToolsListResult(L, server);
		McpSendResult(L, idIdx, -1);
		lua_pop(L, 1);
	} else if (strcmp(method, "tools/call") == 0) {
		return McpDispatchToolsCall(L, server, paramsIdx, idIdx); // may yield; tail return
	} else {
		if (hasId) McpSendError(L, idIdx, -32601, "Method not found");
	}

	lua_settop(L, msgIdx - 1); // drop method/params/id/msg
	return McpPollStep(L, server);
}

// Poll loop -- modeled on LuaHttpClient.cpp's HttpClient_Call/ClientCallContinuation:
// a native call that transparently yields when invoked directly from the
// top-level script, with no Tasks.New/coroutine.resume boilerplate needed.
// Runs as its own independently-scheduled coroutine (see McpServer_Start),
// so it cooperates with the scheduler on its own, fire-and-forget.

static void McpLinebufAppend(LuaMcpServer* server, const char* data, size_t len) {
	size_t needed = server->linebufLen + len;
	if (needed > server->linebufCap) {
		size_t newCap = server->linebufCap ? server->linebufCap : 256;
		while (newCap < needed)
			newCap *= 2;
		char* p = (char*)kitsune_realloc(server->linebuf, newCap);
		if (!p)
			return; // OOM: drop the data silently, matching the engine's general OOM handling elsewhere
		server->linebuf    = p;
		server->linebufCap = newCap;
	}
	memcpy(server->linebuf + server->linebufLen, data, len);
	server->linebufLen += len;
}

// Takes the next complete '\n'-terminated line from linebuf, reading more from
// stdin as needed. Shared by the poll loop and waiting Elicit calls, so both
// consume the same stream. Returns 1 = line in `out`, 0 = nothing available
// right now, -1 = EOF (also latched in server->eof).
static int McpNextLine(LuaMcpServer* server, std::string& out) {
	for (;;) {
		void* nl = server->linebuf ? memchr(server->linebuf, '\n', server->linebufLen) : NULL;
		if (nl) {
			size_t lineLen = (size_t)((char*)nl - server->linebuf);
			// Copy the line out and shrink linebuf BEFORE dispatching -- dispatch may
			// yield for an arbitrarily long time (a tool callback calling Sleep()/etc).
			out.assign(server->linebuf, lineLen);
			size_t consumed = lineLen + 1; // include the '\n'
			memmove(server->linebuf, server->linebuf + consumed, server->linebufLen - consumed);
			server->linebufLen -= consumed;
			return 1;
		}
		if (server->eof)
			return -1;

		char buf[4096];
		int  n = McpPollStdin(buf, sizeof(buf));
		if (n < 0) {
			server->eof = true;
			return -1;
		}
		if (n == 0)
			return 0;
		McpLinebufAppend(server, buf, (size_t)n); // more may already be buffered; check again immediately
	}
}

static int McpPollStep(lua_State* L, LuaMcpServer* server) {
	if (server->stopRequested)
		return 0;

	// The line lives in server->line, not a local: a tool callback can yield
	// inside dispatch, and Lua (built as C) longjmps past C++ destructors.
	// dispatch decodes it before anything can yield or read the next line.
	std::string& line = server->line;

	// Requests that arrived while an Elicit call was waiting go first, in order.
	if (!server->deferred.empty()) {
		line = std::move(server->deferred.front());
		server->deferred.pop_front();
		return McpDispatchLine(L, server, line.data(), line.size());
	}

	int r = McpNextLine(server, line);
	if (r > 0)
		return McpDispatchLine(L, server, line.data(), line.size());
	if (r < 0)
		return 0; // EOF -- client disconnected; return for real, ending this coroutine

	return lua_yieldk(L, 0, (lua_KContext)server, McpPollContinuation);
}

static int McpPollContinuation(lua_State* L, int status, lua_KContext ctx) {
	(void)status;
	return McpPollStep(L, (LuaMcpServer*)ctx);
}

static int McpPollEntrypoint(lua_State* L) {
	LuaMcpServer* server = (LuaMcpServer*)lua_touserdata(L, lua_upvalueindex(1));
	return McpPollStep(L, server);
}

// print()/io.write() overrides -- installed globally the moment Start()
// succeeds. stdout is reserved for JSON-RPC responses; nothing in this
// process may write to it directly once the server is running, regardless
// of which coroutine tries. Output is captured into server->capturedOutput
// rather than silently discarded, so a tool callback's debug output can be
// merged into that call's response -- see McpDispatchToolsCall (clears
// the buffer before dispatch) and McpToolFinish (merges it after).

static int McpPrintOverride(lua_State* L) {
	LuaMcpServer* server = (LuaMcpServer*)lua_touserdata(L, lua_upvalueindex(1));
	int n = lua_gettop(L);
	for (int i = 1; i <= n; i++) {
		size_t len;
		const char* s = luaL_tolstring(L, i, &len); // pushes a string; honors __tostring
		if (i > 1) server->capturedOutput += '\t';
		server->capturedOutput.append(s, len);
		lua_pop(L, 1);
	}
	server->capturedOutput += '\n';
	return 0;
}

static int McpIoWriteOverride(lua_State* L) {
	LuaMcpServer* server = (LuaMcpServer*)lua_touserdata(L, lua_upvalueindex(1));
	int n = lua_gettop(L);
	for (int i = 1; i <= n; i++) {
		size_t len;
		const char* s = luaL_tolstring(L, i, &len);
		server->capturedOutput.append(s, len);
		lua_pop(L, 1);
	}
	return 0;
}

// io.write is always captured (above), so redirecting the default output with io.output
// would silently have no effect. It is disabled instead, so the limitation is explicit.
static int McpIoOutputDisabled(lua_State* L) {
	return luaL_error(L, "io.output is not available in MCP mode (stdout carries the JSON-RPC "
		"stream and io.write is captured); write to a file handle from io.open instead");
}

static void McpInstallOutputRedirect(lua_State* L, LuaMcpServer* server) {
	lua_pushlightuserdata(L, server);
	lua_pushcclosure(L, McpPrintOverride, 1);
	lua_setglobal(L, "print");

	lua_getglobal(L, "io");
	lua_pushlightuserdata(L, server);
	lua_pushcclosure(L, McpIoWriteOverride, 1);
	lua_setfield(L, -2, "write");
	lua_pushcfunction(L, McpIoOutputDisabled);
	lua_setfield(L, -2, "output");
	lua_pop(L, 1); // io table
}

// Server-to-client requests: waiting for a response
//
// Elicit writes elicitation/create, then waits for the response by reading stdin
// itself -- it only runs inside a tool callback, where the poll loop is suspended
// and can't read it for us. While waiting it answers `ping`, parks responses in
// `pending` by id, and defers every other message for the poll loop to dispatch later.

static double McpNowMs() {
	using namespace std::chrono;
	return (double)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// Parks a response in `pending` if something is waiting for it. Anything else
// (a late answer to a timed-out request, an id we never sent) is dropped.
static void McpRouteResponse(lua_State* L, LuaMcpServer* server, int msgIdx, int idIdx) {
	msgIdx = lua_absindex(L, msgIdx);
	lua_pushvalue(L, idIdx);
	const char* key = lua_tostring(L, -1); // converts the copy, never the original id
	auto it = key ? server->pending.find(key) : server->pending.end();
	lua_pop(L, 1);
	if (it == server->pending.end() || it->second.done)
		return;
	lua_pushvalue(L, msgIdx);
	it->second.msgRef = luaL_ref(L, LUA_REGISTRYINDEX);
	it->second.done    = true;
}

static void McpHandleLineWhileWaiting(lua_State* L, LuaMcpServer* server, std::string& line) {
	int base = lua_gettop(L);
	if (McpJsonDecode(L, line.data(), line.size()) && lua_istable(L, -1)) {
		int msgIdx = lua_gettop(L);
		lua_getfield(L, msgIdx, "id");
		int idIdx = lua_gettop(L);
		lua_getfield(L, msgIdx, "method");
		const char* method = lua_tostring(L, -1);

		if (!method) {
			lua_getfield(L, msgIdx, "result");
			lua_getfield(L, msgIdx, "error");
			bool isResponse = !lua_isnil(L, -2) || !lua_isnil(L, -1);
			lua_pop(L, 2);
			if (isResponse) {
				McpRouteResponse(L, server, msgIdx, idIdx);
				lua_settop(L, base);
				return;
			}
		} else if (strcmp(method, "ping") == 0 && !lua_isnil(L, idIdx)) {
			McpPushEmptyObject(L);
			McpSendResult(L, idIdx, -1);
			lua_settop(L, base);
			return;
		}
	}
	// Parse errors included: the poll loop reports those itself when it gets there.
	lua_settop(L, base);
	server->deferred.push_back(std::move(line));
}

static void McpSendCancelled(lua_State* L, const char* id, const char* reason) {
	lua_newtable(L);
	lua_pushliteral(L, "2.0");
	lua_setfield(L, -2, "jsonrpc");
	lua_pushliteral(L, "notifications/cancelled");
	lua_setfield(L, -2, "method");
	lua_newtable(L);
	lua_pushstring(L, id);
	lua_setfield(L, -2, "requestId");
	lua_pushstring(L, reason);
	lua_setfield(L, -2, "reason");
	lua_setfield(L, -2, "params");

	if (McpJsonEncode(L, -1)) {
		size_t len;
		const char* s = lua_tolstring(L, -1, &len);
		if (s) McpWriteLine(s, len);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}

static void McpPendingErase(lua_State* L, LuaMcpServer* server, const std::string& key) {
	auto it = server->pending.find(key);
	if (it == server->pending.end())
		return;
	if (it->second.msgRef != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, it->second.msgRef);
	server->pending.erase(it);
}

// Elicitation / Question userdata -- definition side.
// Everything is append-only and write-once:
// duplicate names/values and repeated setters raise, nothing can be removed.

static void McpUnref(lua_State* L, int& ref) {
	if (ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ref);
		ref = LUA_NOREF;
	}
}

static LuaMcpElicitation* luaL_checkmcpelicitation(lua_State* L, int idx) {
	return (LuaMcpElicitation*)luaL_checkudata(L, idx, LUAMCPELICITATION);
}

// The returned reference is only valid until the next AddQuestion on the same elicitation.
static McpQuestion& luaL_checkmcpquestion(lua_State* L, int idx) {
	LuaMcpQuestion* h = (LuaMcpQuestion*)luaL_checkudata(L, idx, LUAMCPQUESTION);
	return h->elicitation->questions[h->index];
}

static const char* McpQuestionTypeName(McpQuestionType t) {
	switch (t) {
	case MCP_Q_INTEGER: return "integer";
	case MCP_Q_NUMBER:  return "number";
	case MCP_Q_BOOLEAN: return "boolean";
	default:            return "string";
	}
}

static const McpAnswer* McpFindAnswer(const McpQuestion& q, const char* value) {
	for (const McpAnswer& a : q.answers) {
		if (a.value == value) return &a;
	}
	return nullptr;
}

static void McpOnce(lua_State* L, bool& flag, const char* setter, const McpQuestion& q) {
	if (flag)
		luaL_error(L, "%s: already set on question '%s'", setter, q.name.c_str());
	flag = true;
}

// Accepts an integer, or a float with an integral value. Pushes nothing.
static bool McpToInteger(lua_State* L, int idx, lua_Integer* out) {
	if (lua_isinteger(L, idx)) {
		*out = lua_tointeger(L, idx);
		return true;
	}
	if (lua_type(L, idx) == LUA_TNUMBER) {
		lua_Number n = lua_tonumber(L, idx);
		// [-2^63, 2^63): the range a lua_Integer can hold exactly
		if (!(n >= -9223372036854775808.0 && n < 9223372036854775808.0) || n != (lua_Number)(lua_Integer)n)
			return false;
		*out = (lua_Integer)n;
		return true;
	}
	return false;
}

static lua_Integer McpUtf8Length(const char* s, size_t len) {
	lua_Integer n = 0;
	for (size_t i = 0; i < len; i++) {
		if (((unsigned char)s[i] & 0xC0) != 0x80) n++;
	}
	return n;
}

// Reads an optional non-negative integer bound at `idx` (nil = no bound).
static bool McpOptCount(lua_State* L, int idx, const char* setter, lua_Integer* out) {
	if (lua_isnoneornil(L, idx))
		return false;
	if (!McpToInteger(L, idx, out) || *out < 0)
		luaL_error(L, "%s: bounds must be non-negative integers or nil", setter);
	return true;
}

int McpServer_CreateElicitation(lua_State* L) {
	LuaMcpServer* server = luaL_checkmcpserver(L, 1);
	size_t mlen;
	const char* message = luaL_checklstring(L, 2, &mlen);
	luaL_checktype(L, 3, LUA_TFUNCTION);
	lua_Integer timeout = 0;
	if (!lua_isnoneornil(L, 4) && (!McpToInteger(L, 4, &timeout) || timeout < 0))
		return luaL_argerror(L, 4, "timeout must be a non-negative integer (milliseconds) or nil");

	LuaMcpElicitation* e = (LuaMcpElicitation*)lua_newuserdata(L, sizeof(LuaMcpElicitation));
	new (e) LuaMcpElicitation();
	luaL_setmetatable(L, LUAMCPELICITATION);

	e->message.assign(message, mlen);
	e->timeoutMs = timeout;
	lua_pushvalue(L, 3);
	e->defaultRef = luaL_ref(L, LUA_REGISTRYINDEX);
	lua_pushvalue(L, 1);
	e->mcpRef = luaL_ref(L, LUA_REGISTRYINDEX);
	e->server  = server;
	return 1;
}

int McpElicitation_AddQuestion(lua_State* L) {
	LuaMcpElicitation* e = luaL_checkmcpelicitation(L, 1);
	const char* name  = luaL_checkstring(L, 2);
	const char* tname = luaL_checkstring(L, 3);
	const char* title = luaL_checkstring(L, 4);
	luaL_checktype(L, 5, LUA_TBOOLEAN);

	McpQuestionType type;
	if      (strcmp(tname, "string") == 0)  type = MCP_Q_STRING;
	else if (strcmp(tname, "integer") == 0) type = MCP_Q_INTEGER;
	else if (strcmp(tname, "number") == 0)  type = MCP_Q_NUMBER;
	else if (strcmp(tname, "boolean") == 0) type = MCP_Q_BOOLEAN;
	else return luaL_error(L, "AddQuestion: unknown type '%s' (expected \"string\", \"integer\", \"number\" or \"boolean\")", tname);

	if (!*name)
		return luaL_error(L, "AddQuestion: name must not be empty");
	for (const McpQuestion& q : e->questions) {
		if (q.name == name)
			return luaL_error(L, "AddQuestion: a question named '%s' already exists", name);
	}

	LuaMcpQuestion* h = (LuaMcpQuestion*)lua_newuserdata(L, sizeof(LuaMcpQuestion));
	h->elicitationRef = LUA_NOREF;
	h->elicitation     = e;
	h->index           = e->questions.size();
	luaL_setmetatable(L, LUAMCPQUESTION);
	lua_pushvalue(L, 1);
	h->elicitationRef = luaL_ref(L, LUA_REGISTRYINDEX);

	McpQuestion q;
	q.name     = name;
	q.title    = title;
	q.type     = type;
	q.required = lua_toboolean(L, 5) != 0;
	e->questions.push_back(std::move(q));
	return 1;
}

int McpQuestion_AddAnswer(lua_State* L) {
	McpQuestion& q = luaL_checkmcpquestion(L, 1);
	const char* value = luaL_checkstring(L, 2);
	const char* label = luaL_checkstring(L, 3);
	if (!lua_isnoneornil(L, 4))
		luaL_checktype(L, 4, LUA_TFUNCTION);

	if (q.type != MCP_Q_STRING)
		return luaL_error(L, "AddAnswer: question '%s' is %s; answers are only allowed on string questions", q.name.c_str(), McpQuestionTypeName(q.type));
	if (q.hasLength || q.hasFormat)
		return luaL_error(L, "AddAnswer: question '%s' is free text (SetLength/SetFormat); it can't also have answers", q.name.c_str());
	if (McpFindAnswer(q, value))
		return luaL_error(L, "AddAnswer: question '%s' already has an answer '%s'", q.name.c_str(), value);

	McpAnswer a;
	a.value = value;
	a.label = label;
	if (!lua_isnoneornil(L, 4)) {
		lua_pushvalue(L, 4);
		a.fnRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	q.answers.push_back(std::move(a));
	return 0;
}

int McpQuestion_SetDescription(lua_State* L) {
	McpQuestion& q = luaL_checkmcpquestion(L, 1);
	const char* text = luaL_checkstring(L, 2);
	McpOnce(L, q.hasDescription, "SetDescription", q);
	q.description = text;
	return 0;
}

int McpQuestion_SetDefault(lua_State* L) {
	McpQuestion& q = luaL_checkmcpquestion(L, 1);
	luaL_checkany(L, 2);
	lua_settop(L, 2);

	switch (q.type) {
	case MCP_Q_BOOLEAN:
		if (!lua_isboolean(L, 2))
			return luaL_error(L, "SetDefault: question '%s' is boolean; got %s", q.name.c_str(), luaL_typename(L, 2));
		break;
	case MCP_Q_INTEGER: {
		lua_Integer i;
		if (!McpToInteger(L, 2, &i))
			return luaL_error(L, "SetDefault: question '%s' is integer; got %s", q.name.c_str(), luaL_typename(L, 2));
		lua_pushinteger(L, i);
		lua_replace(L, 2);
		break;
	}
	case MCP_Q_NUMBER:
		if (lua_type(L, 2) != LUA_TNUMBER)
			return luaL_error(L, "SetDefault: question '%s' is number; got %s", q.name.c_str(), luaL_typename(L, 2));
		break;
	default:
		// Multi-select takes a list of answer values; everything else a string.
		// Membership in the answers is checked when the form is built.
		if (q.multiple ? !lua_istable(L, 2) : lua_type(L, 2) != LUA_TSTRING)
			return luaL_error(L, "SetDefault: question '%s' expects %s; got %s", q.name.c_str(),
				q.multiple ? "a list of answer values" : "a string", luaL_typename(L, 2));
		break;
	}

	McpOnce(L, q.hasDefault, "SetDefault", q);
	lua_pushvalue(L, 2);
	q.defaultRef = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int McpQuestion_SetRange(lua_State* L) {
	McpQuestion& q = luaL_checkmcpquestion(L, 1);
	if (q.type != MCP_Q_INTEGER && q.type != MCP_Q_NUMBER)
		return luaL_error(L, "SetRange: question '%s' is %s; SetRange needs integer or number", q.name.c_str(), McpQuestionTypeName(q.type));
	if (lua_isnoneornil(L, 2) && lua_isnoneornil(L, 3))
		return luaL_error(L, "SetRange: give a minimum, a maximum, or both");

	bool hasMin = !lua_isnoneornil(L, 2), hasMax = !lua_isnoneornil(L, 3);
	lua_Integer minI = 0, maxI = 0;
	lua_Number  minN = 0, maxN = 0;
	if (q.type == MCP_Q_INTEGER) {
		if ((hasMin && !McpToInteger(L, 2, &minI)) || (hasMax && !McpToInteger(L, 3, &maxI)))
			return luaL_error(L, "SetRange: question '%s' is integer; bounds must be integers or nil", q.name.c_str());
		if (hasMin && hasMax && minI > maxI)
			return luaL_error(L, "SetRange: minimum is greater than maximum");
	} else {
		if (hasMin) minN = luaL_checknumber(L, 2);
		if (hasMax) maxN = luaL_checknumber(L, 3);
		if (hasMin && hasMax && minN > maxN)
			return luaL_error(L, "SetRange: minimum is greater than maximum");
	}

	McpOnce(L, q.hasRange, "SetRange", q);
	q.hasMin = hasMin; q.hasMax = hasMax;
	q.minI = minI; q.maxI = maxI;
	q.minN = minN; q.maxN = maxN;
	return 0;
}

int McpQuestion_SetLength(lua_State* L) {
	McpQuestion& q = luaL_checkmcpquestion(L, 1);
	if (q.type != MCP_Q_STRING || !q.answers.empty() || q.multiple)
		return luaL_error(L, "SetLength: question '%s' isn't free text; SetLength needs a string question without answers", q.name.c_str());
	lua_Integer mn = 0, mx = 0;
	bool hasMin = McpOptCount(L, 2, "SetLength", &mn);
	bool hasMax = McpOptCount(L, 3, "SetLength", &mx);
	if (!hasMin && !hasMax)
		return luaL_error(L, "SetLength: give a minimum, a maximum, or both");
	if (hasMin && hasMax && mn > mx)
		return luaL_error(L, "SetLength: minimum is greater than maximum");

	McpOnce(L, q.hasLength, "SetLength", q);
	q.hasMinLen = hasMin; q.hasMaxLen = hasMax;
	q.minLen = mn; q.maxLen = mx;
	return 0;
}

int McpQuestion_SetFormat(lua_State* L) {
	McpQuestion& q = luaL_checkmcpquestion(L, 1);
	const char* fmt = luaL_checkstring(L, 2);
	if (q.type != MCP_Q_STRING || !q.answers.empty() || q.multiple)
		return luaL_error(L, "SetFormat: question '%s' isn't free text; SetFormat needs a string question without answers", q.name.c_str());
	if (strcmp(fmt, "email") != 0 && strcmp(fmt, "uri") != 0 && strcmp(fmt, "date") != 0 && strcmp(fmt, "date-time") != 0)
		return luaL_error(L, "SetFormat: unknown format '%s' (expected \"email\", \"uri\", \"date\" or \"date-time\")", fmt);

	McpOnce(L, q.hasFormat, "SetFormat", q);
	q.format = fmt;
	return 0;
}

int McpQuestion_SetMultiple(lua_State* L) {
	McpQuestion& q = luaL_checkmcpquestion(L, 1);
	if (q.type != MCP_Q_STRING || q.hasLength || q.hasFormat)
		return luaL_error(L, "SetMultiple: question '%s' can't be multi-select; SetMultiple needs a string question with answers", q.name.c_str());
	if (q.hasDefault)
		return luaL_error(L, "SetMultiple: call SetMultiple before SetDefault on question '%s' (the default becomes a list)", q.name.c_str());
	lua_Integer mn = 0, mx = 0;
	bool hasMin = McpOptCount(L, 2, "SetMultiple", &mn);
	bool hasMax = McpOptCount(L, 3, "SetMultiple", &mx);
	if (hasMin && hasMax && mn > mx)
		return luaL_error(L, "SetMultiple: minimum is greater than maximum");

	McpOnce(L, q.multiple, "SetMultiple", q);
	q.hasMinItems = hasMin; q.hasMaxItems = hasMax;
	q.minItems = mn; q.maxItems = mx;
	return 0;
}

int McpQuestion_SetCallback(lua_State* L) {
	McpQuestion& q = luaL_checkmcpquestion(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);
	McpOnce(L, q.hasCallback, "SetCallback", q);
	lua_pushvalue(L, 2);
	q.fnRef = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

int McpElicitation_GC(lua_State* L) {
	LuaMcpElicitation* e = luaL_checkmcpelicitation(L, 1);
	for (McpQuestion& q : e->questions) {
		McpUnref(L, q.fnRef);
		McpUnref(L, q.defaultRef);
		for (McpAnswer& a : q.answers)
			McpUnref(L, a.fnRef);
	}
	McpUnref(L, e->defaultRef);
	McpUnref(L, e->mcpRef); // releases the parent last
	e->server = nullptr;
	e->~LuaMcpElicitation();
	return 0;
}

int McpElicitation_ToString(lua_State* L) {
	LuaMcpElicitation* e = luaL_checkmcpelicitation(L, 1);
	lua_pushfstring(L, "Elicitation(%d questions)", (int)e->questions.size());
	return 1;
}

int McpQuestion_GC(lua_State* L) {
	LuaMcpQuestion* h = (LuaMcpQuestion*)luaL_checkudata(L, 1, LUAMCPQUESTION);
	McpUnref(L, h->elicitationRef);
	h->elicitation = nullptr;
	return 0;
}

int McpQuestion_ToString(lua_State* L) {
	LuaMcpQuestion* h = (LuaMcpQuestion*)luaL_checkudata(L, 1, LUAMCPQUESTION);
	if (!h->elicitation) {
		lua_pushliteral(L, "Question(released)");
		return 1;
	}
	lua_pushfstring(L, "Question(%s)", h->elicitation->questions[h->index].name.c_str());
	return 1;
}

// Elicitation -- building the form

// Definition errors that can only be seen once everything is added (e.g. a
// default that isn't one of the answers). Programming errors: they raise.
static void McpValidateElicitation(lua_State* L, LuaMcpElicitation* e) {
	for (const McpQuestion& q : e->questions) {
		if (q.multiple && q.answers.empty())
			luaL_error(L, "Elicit: question '%s' is multi-select but has no answers", q.name.c_str());
		if (!q.hasDefault || q.type != MCP_Q_STRING || q.answers.empty())
			continue;

		lua_rawgeti(L, LUA_REGISTRYINDEX, q.defaultRef);
		if (q.multiple) {
			int n = (int)lua_rawlen(L, -1);
			for (int i = 1; i <= n; i++) {
				lua_rawgeti(L, -1, i);
				if (lua_type(L, -1) != LUA_TSTRING || !McpFindAnswer(q, lua_tostring(L, -1)))
					luaL_error(L, "Elicit: default of question '%s' contains something that isn't one of its answers", q.name.c_str());
				lua_pop(L, 1);
			}
		} else if (!McpFindAnswer(q, lua_tostring(L, -1))) {
			luaL_error(L, "Elicit: default '%s' of question '%s' isn't one of its answers", lua_tostring(L, -1), q.name.c_str());
		}
		lua_pop(L, 1);
	}
}

// Pushes an array of { const = value, title = label }.
static void McpPushTitledAnswers(lua_State* L, const McpQuestion& q) {
	lua_newtable(L);
	for (size_t i = 0; i < q.answers.size(); i++) {
		lua_newtable(L);
		lua_pushlstring(L, q.answers[i].value.c_str(), q.answers[i].value.size());
		lua_setfield(L, -2, "const");
		lua_pushlstring(L, q.answers[i].label.c_str(), q.answers[i].label.size());
		lua_setfield(L, -2, "title");
		lua_rawseti(L, -2, (int)i + 1);
	}
}

// `titled`: the client speaks 2025-11-25+, where titled answers are oneOf/anyOf
// of {const,title}. Older revisions get enum + enumNames (multi-select never
// reaches here for them -- Elicit treats it as unsupported).
static void McpPushElicitationSchema(lua_State* L, LuaMcpElicitation* e, bool titled) {
	lua_newtable(L);
	int schemaIdx = lua_gettop(L);
	lua_pushliteral(L, "object");
	lua_setfield(L, schemaIdx, "type");

	lua_newtable(L);
	int propsIdx = lua_gettop(L);
	lua_newtable(L);
	int reqIdx = lua_gettop(L);
	int nreq = 0;

	for (const McpQuestion& q : e->questions) {
		lua_newtable(L);
		int p = lua_gettop(L);

		if (q.multiple) {
			lua_pushliteral(L, "array");
			lua_setfield(L, p, "type");
			lua_newtable(L); // items
			McpPushTitledAnswers(L, q);
			lua_setfield(L, -2, "anyOf");
			lua_setfield(L, p, "items");
			if (q.hasMinItems) { lua_pushinteger(L, q.minItems); lua_setfield(L, p, "minItems"); }
			if (q.hasMaxItems) { lua_pushinteger(L, q.maxItems); lua_setfield(L, p, "maxItems"); }
		} else if (!q.answers.empty()) {
			lua_pushliteral(L, "string");
			lua_setfield(L, p, "type");
			if (titled) {
				McpPushTitledAnswers(L, q);
				lua_setfield(L, p, "oneOf");
			} else {
				lua_newtable(L);
				lua_newtable(L);
				for (size_t i = 0; i < q.answers.size(); i++) {
					lua_pushlstring(L, q.answers[i].value.c_str(), q.answers[i].value.size());
					lua_rawseti(L, -3, (int)i + 1);
					lua_pushlstring(L, q.answers[i].label.c_str(), q.answers[i].label.size());
					lua_rawseti(L, -2, (int)i + 1);
				}
				lua_setfield(L, p, "enumNames");
				lua_setfield(L, p, "enum");
			}
		} else {
			lua_pushstring(L, McpQuestionTypeName(q.type));
			lua_setfield(L, p, "type");
			if (q.hasMinLen) { lua_pushinteger(L, q.minLen); lua_setfield(L, p, "minLength"); }
			if (q.hasMaxLen) { lua_pushinteger(L, q.maxLen); lua_setfield(L, p, "maxLength"); }
			if (q.hasFormat) { lua_pushlstring(L, q.format.c_str(), q.format.size()); lua_setfield(L, p, "format"); }
			if (q.type == MCP_Q_INTEGER) {
				if (q.hasMin) { lua_pushinteger(L, q.minI); lua_setfield(L, p, "minimum"); }
				if (q.hasMax) { lua_pushinteger(L, q.maxI); lua_setfield(L, p, "maximum"); }
			} else if (q.type == MCP_Q_NUMBER) {
				if (q.hasMin) { lua_pushnumber(L, q.minN); lua_setfield(L, p, "minimum"); }
				if (q.hasMax) { lua_pushnumber(L, q.maxN); lua_setfield(L, p, "maximum"); }
			}
		}

		lua_pushlstring(L, q.title.c_str(), q.title.size());
		lua_setfield(L, p, "title");
		if (q.hasDescription) {
			lua_pushlstring(L, q.description.c_str(), q.description.size());
			lua_setfield(L, p, "description");
		}
		if (q.hasDefault) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, q.defaultRef);
			lua_setfield(L, p, "default");
		}

		lua_setfield(L, propsIdx, q.name.c_str());
		if (q.required) {
			lua_pushlstring(L, q.name.c_str(), q.name.size());
			lua_rawseti(L, reqIdx, ++nreq);
		}
	}

	lua_setfield(L, schemaIdx, "required");
	if (e->questions.empty()) {
		lua_pop(L, 1);
		McpPushEmptyObject(L); // `{}`, not `[]`
	}
	lua_setfield(L, schemaIdx, "properties");
}

// Elicitation -- asking, and turning the answer into results
//
// Elicit's stack, shared by every stage and continuation:
//   1 self  2 context  3 request  4 request id  5 deadline (or nil)
//   6 answers  7 results  8 planned callbacks  9 next callback index
//   10 current callback entry (only while one is running)
// Callbacks run through lua_pcallk so they may yield (Sleep, database calls, ...).
// No stage holds a C++ object with a destructor across a yield: Lua is built as
// C and longjmps past destructors.

enum {
	EL_SELF = 1, EL_CONTEXT, EL_REQUEST, EL_ID, EL_DEADLINE,
	EL_ANSWERS, EL_RESULTS, EL_CALLS, EL_NEXT, EL_ENTRY
};

static int McpElicitFail(lua_State* L, const char* err) {
	lua_pushboolean(L, 0);
	lua_pushstring(L, err);
	return 2;
}

// Detail string on top -> false, "invalid response from client: <detail>".
static int McpElicitInvalid(lua_State* L) {
	lua_pushfstring(L, "invalid response from client: %s", lua_tostring(L, -1));
	lua_pushboolean(L, 0);
	lua_insert(L, -2);
	return 2;
}

// Error object on top (from a failed callback) -> false, message.
static int McpElicitCallbackError(lua_State* L) {
	luaL_tolstring(L, -1, NULL);
	lua_pushboolean(L, 0);
	lua_insert(L, -2);
	return 2;
}

static int McpElicitDefaultContinuation(lua_State* L, int status, lua_KContext ctx);

static int McpElicitDefaultFinish(lua_State* L, int status) {
	if (status != LUA_OK && status != LUA_YIELD)
		return McpElicitCallbackError(L);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_pushboolean(L, 1); // ran fine, returned nothing
	}
	lua_pushboolean(L, 1);
	lua_insert(L, -2);
	return 2; // true, <default's return>
}

static int McpElicitDefaultContinuation(lua_State* L, int status, lua_KContext ctx) {
	(void)ctx;
	return McpElicitDefaultFinish(L, status);
}

// `reason` must be a string literal: the stack is trimmed before it's pushed.
static int McpElicitRunDefault(lua_State* L, LuaMcpElicitation* e, const char* reason) {
	lua_settop(L, EL_DEADLINE);
	lua_rawgeti(L, LUA_REGISTRYINDEX, e->defaultRef);
	lua_pushvalue(L, EL_CONTEXT);
	lua_pushvalue(L, EL_REQUEST);
	lua_pushstring(L, reason);
	int rc = lua_pcallk(L, 3, 1, 0, (lua_KContext)e, McpElicitDefaultContinuation);
	return McpElicitDefaultFinish(L, rc);
}

static int McpElicitCbStep(lua_State* L, LuaMcpElicitation* e);

static int McpElicitCbFinish(lua_State* L, LuaMcpElicitation* e, int status) {
	if (status != LUA_OK && status != LUA_YIELD)
		return McpElicitCallbackError(L);
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_pushboolean(L, 1); // ran fine, returned nothing
	}
	int ret = lua_gettop(L); // [1..9, entry, ret]

	lua_rawgeti(L, EL_ENTRY, 4); // picked answer, or nil for a question without answers
	if (lua_isnil(L, -1)) {
		lua_pop(L, 1);
		lua_rawgeti(L, EL_ENTRY, 3); // question name
		lua_pushvalue(L, ret);
		lua_rawset(L, EL_RESULTS);   // results[name] = ret
	} else {
		int answer = lua_gettop(L);
		lua_rawgeti(L, EL_ENTRY, 3);
		lua_rawget(L, EL_RESULTS);   // results[name] (a table, created while planning)
		lua_pushvalue(L, answer);
		lua_pushvalue(L, ret);
		lua_rawset(L, -3);           // results[name][answer] = ret
	}

	lua_pushinteger(L, lua_tointeger(L, EL_NEXT) + 1);
	lua_replace(L, EL_NEXT);
	return McpElicitCbStep(L, e);
}

static int McpElicitCbContinuation(lua_State* L, int status, lua_KContext ctx) {
	return McpElicitCbFinish(L, (LuaMcpElicitation*)ctx, status);
}

static int McpElicitCbStep(lua_State* L, LuaMcpElicitation* e) {
	lua_settop(L, EL_NEXT);
	lua_Integer i = lua_tointeger(L, EL_NEXT);
	if (i > (lua_Integer)lua_rawlen(L, EL_CALLS)) {
		lua_pushboolean(L, 1);
		lua_pushvalue(L, EL_RESULTS);
		return 2; // true, results
	}

	lua_rawgeti(L, EL_CALLS, i); // EL_ENTRY = { fn, value, question name, answer-or-nil }
	lua_rawgeti(L, EL_ENTRY, 1);
	lua_pushvalue(L, EL_CONTEXT);
	lua_pushvalue(L, EL_REQUEST);
	lua_rawgeti(L, EL_ENTRY, 2);
	lua_pushvalue(L, EL_ANSWERS);
	int rc = lua_pcallk(L, 4, 1, 0, (lua_KContext)e, McpElicitCbContinuation);
	return McpElicitCbFinish(L, e, rc);
}

// Value on top -> appended to the planned callbacks as { fn, value, qname, answer }.
static void McpPlanCall(lua_State* L, int callsIdx, lua_Integer* n, int fnRef, const std::string& qname, const char* answer) {
	lua_newtable(L);
	lua_insert(L, -2);
	lua_rawseti(L, -2, 2);
	lua_rawgeti(L, LUA_REGISTRYINDEX, fnRef);
	lua_rawseti(L, -2, 1);
	lua_pushlstring(L, qname.c_str(), qname.size());
	lua_rawseti(L, -2, 3);
	if (answer) {
		lua_pushstring(L, answer);
		lua_rawseti(L, -2, 4);
	}
	lua_rawseti(L, callsIdx, ++*n);
}

// A picked answer: plan its callback (or the question's), or record `true`.
static void McpPlanAnswer(lua_State* L, int callsIdx, lua_Integer* n, int pickedIdx,
                            const McpQuestion& q, const McpAnswer& a) {
	int fn = a.fnRef != LUA_NOREF ? a.fnRef : q.fnRef;
	if (fn != LUA_NOREF) {
		lua_pushlstring(L, a.value.c_str(), a.value.size());
		McpPlanCall(L, callsIdx, n, fn, q.name, a.value.c_str());
	} else {
		lua_pushboolean(L, 1);
		lua_setfield(L, pickedIdx, a.value.c_str());
	}
}

// Validates the accepted `content` (at contentIdx) against the definition and
// plans the callbacks. Nothing here yields.
static int McpElicitPlan(lua_State* L, LuaMcpElicitation* e, int contentIdx) {
	lua_newtable(L);
	int answersIdx = lua_gettop(L);
	lua_newtable(L);
	int resultsIdx = lua_gettop(L);
	lua_newtable(L);
	int callsIdx = lua_gettop(L);
	lua_Integer ncalls = 0;

	lua_pushnil(L);
	while (lua_next(L, contentIdx)) {
		lua_pop(L, 1);
		if (lua_type(L, -1) != LUA_TSTRING) {
			lua_pushliteral(L, "answer keys must be strings");
			return McpElicitInvalid(L);
		}
		const char* key = lua_tostring(L, -1);
		bool known = false;
		for (const McpQuestion& q : e->questions) {
			if (q.name == key) { known = true; break; }
		}
		if (!known) {
			lua_pushfstring(L, "unknown question '%s'", key);
			return McpElicitInvalid(L);
		}
	}

	int base = lua_gettop(L);
	for (const McpQuestion& q : e->questions) {
		lua_settop(L, base);
		const char* qn = q.name.c_str();
		lua_getfield(L, contentIdx, qn);
		int v = lua_gettop(L);

		if (McpIsNull(L, v)) {
			if (q.required) {
				lua_pushfstring(L, "missing required answer '%s'", qn);
				return McpElicitInvalid(L);
			}
			continue;
		}

		if (q.multiple) {
			if (!lua_istable(L, v)) {
				lua_pushfstring(L, "'%s' should be a list", qn);
				return McpElicitInvalid(L);
			}
			lua_Integer n = (lua_Integer)lua_rawlen(L, v);
			if ((q.hasMinItems && n < q.minItems) || (q.hasMaxItems && n > q.maxItems)) {
				lua_pushfstring(L, "'%s' has %d picks, outside the allowed range", qn, (int)n);
				return McpElicitInvalid(L);
			}
			lua_newtable(L);
			int set = lua_gettop(L);
			lua_newtable(L);
			int copy = lua_gettop(L);
			for (lua_Integer i = 1; i <= n; i++) {
				lua_rawgeti(L, v, i);
				if (lua_type(L, -1) != LUA_TSTRING || !McpFindAnswer(q, lua_tostring(L, -1))) {
					lua_pushfstring(L, "'%s' contains something that isn't one of its answers", qn);
					return McpElicitInvalid(L);
				}
				lua_pushvalue(L, -1);
				lua_rawget(L, set);
				if (!lua_isnil(L, -1)) {
					lua_pushfstring(L, "'%s' contains the same answer twice", qn);
					return McpElicitInvalid(L);
				}
				lua_pop(L, 1);
				lua_pushvalue(L, -1);
				lua_pushboolean(L, 1);
				lua_rawset(L, set);
				lua_rawseti(L, copy, i);
			}
			lua_pushvalue(L, copy);
			lua_setfield(L, answersIdx, qn);

			lua_newtable(L);
			int picked = lua_gettop(L);
			lua_pushvalue(L, picked);
			lua_setfield(L, resultsIdx, qn);
			for (const McpAnswer& a : q.answers) { // declaration order
				lua_getfield(L, set, a.value.c_str());
				bool isPicked = !lua_isnil(L, -1);
				lua_pop(L, 1);
				if (isPicked)
					McpPlanAnswer(L, callsIdx, &ncalls, picked, q, a);
			}
			continue;
		}

		if (!q.answers.empty()) {
			const McpAnswer* a = lua_type(L, v) == LUA_TSTRING ? McpFindAnswer(q, lua_tostring(L, v)) : nullptr;
			if (!a) {
				lua_pushfstring(L, "'%s' isn't one of the answers of '%s'", luaL_tolstring(L, v, NULL), qn);
				return McpElicitInvalid(L);
			}
			lua_pushvalue(L, v);
			lua_setfield(L, answersIdx, qn);
			lua_newtable(L);
			int picked = lua_gettop(L);
			lua_pushvalue(L, picked);
			lua_setfield(L, resultsIdx, qn);
			McpPlanAnswer(L, callsIdx, &ncalls, picked, q, *a);
			continue;
		}

		switch (q.type) {
		case MCP_Q_STRING: {
			if (lua_type(L, v) != LUA_TSTRING) {
				lua_pushfstring(L, "'%s' should be a string", qn);
				return McpElicitInvalid(L);
			}
			size_t len;
			const char* s = lua_tolstring(L, v, &len);
			lua_Integer chars = McpUtf8Length(s, len);
			if ((q.hasMinLen && chars < q.minLen) || (q.hasMaxLen && chars > q.maxLen)) {
				lua_pushfstring(L, "'%s' has a length outside the allowed range", qn);
				return McpElicitInvalid(L);
			}
			break;
		}
		case MCP_Q_INTEGER: {
			lua_Integer i;
			if (!McpToInteger(L, v, &i)) {
				lua_pushfstring(L, "'%s' should be an integer", qn);
				return McpElicitInvalid(L);
			}
			if ((q.hasMin && i < q.minI) || (q.hasMax && i > q.maxI)) {
				lua_pushfstring(L, "'%s' is outside the allowed range", qn);
				return McpElicitInvalid(L);
			}
			lua_pushinteger(L, i);
			lua_replace(L, v);
			break;
		}
		case MCP_Q_NUMBER: {
			if (lua_type(L, v) != LUA_TNUMBER) {
				lua_pushfstring(L, "'%s' should be a number", qn);
				return McpElicitInvalid(L);
			}
			lua_Number x = lua_tonumber(L, v);
			if ((q.hasMin && x < q.minN) || (q.hasMax && x > q.maxN)) {
				lua_pushfstring(L, "'%s' is outside the allowed range", qn);
				return McpElicitInvalid(L);
			}
			break;
		}
		case MCP_Q_BOOLEAN:
			if (!lua_isboolean(L, v)) {
				lua_pushfstring(L, "'%s' should be a boolean", qn);
				return McpElicitInvalid(L);
			}
			break;
		}

		lua_pushvalue(L, v);
		lua_setfield(L, answersIdx, qn);
		lua_pushvalue(L, v);
		if (q.fnRef != LUA_NOREF) {
			McpPlanCall(L, callsIdx, &ncalls, q.fnRef, q.name, nullptr);
		} else {
			lua_setfield(L, resultsIdx, qn);
		}
	}

	lua_copy(L, answersIdx, EL_ANSWERS);
	lua_copy(L, resultsIdx, EL_RESULTS);
	lua_copy(L, callsIdx, EL_CALLS);
	lua_pushinteger(L, 1);
	lua_copy(L, -1, EL_NEXT);
	lua_settop(L, EL_NEXT);
	return McpElicitCbStep(L, e);
}

// The client's response message is on top (at EL_ANSWERS, the first free slot).
static int McpElicitProcess(lua_State* L, LuaMcpElicitation* e) {
	lua_settop(L, EL_ANSWERS);
	int msg = EL_ANSWERS;

	lua_getfield(L, msg, "error");
	if (lua_istable(L, -1)) {
		lua_getfield(L, -1, "message");
		const char* m = lua_tostring(L, -1);
		lua_pushfstring(L, "elicitation failed: %s", m ? m : "unknown error");
		lua_pushboolean(L, 0);
		lua_insert(L, -2);
		return 2;
	}
	lua_settop(L, msg);

	lua_getfield(L, msg, "result");
	int result = lua_gettop(L);
	if (!lua_istable(L, result)) {
		lua_pushliteral(L, "missing result");
		return McpElicitInvalid(L);
	}
	lua_getfield(L, result, "action");
	const char* action = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
	if (!action) {
		lua_pushliteral(L, "missing action");
		return McpElicitInvalid(L);
	}
	if (strcmp(action, "decline") == 0)
		return McpElicitRunDefault(L, e, "decline");
	if (strcmp(action, "cancel") == 0)
		return McpElicitRunDefault(L, e, "cancel");
	if (strcmp(action, "accept") != 0) {
		lua_pushfstring(L, "unknown action '%s'", action);
		return McpElicitInvalid(L);
	}

	lua_getfield(L, result, "content");
	if (McpIsNull(L, -1) || lua_touserdata(L, -1) == LuaJsonEmptyObject()) {
		lua_pop(L, 1);
		lua_newtable(L);
	} else if (!lua_istable(L, -1)) {
		lua_pushliteral(L, "content is not an object");
		return McpElicitInvalid(L);
	}
	return McpElicitPlan(L, e, lua_gettop(L));
}

// One wait attempt. Returns: >= 0 results pushed (an error), -1 = nothing yet
// (yield), -2 = response message pushed, -3 = timed out.
// Kept free of any C++ object that would be live across the caller's yield.
static int McpElicitPoll(lua_State* L, LuaMcpServer* server, const std::string& key) {
	for (;;) {
		auto it = server->pending.find(key);
		if (it == server->pending.end())
			return McpElicitFail(L, "elicitation request was abandoned");
		if (it->second.done) {
			int ref = it->second.msgRef;
			server->pending.erase(it);
			lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
			luaL_unref(L, LUA_REGISTRYINDEX, ref);
			return -2;
		}
		if (server->stopRequested) {
			McpPendingErase(L, server, key);
			return McpElicitFail(L, "MCP server stopped");
		}
		if (server->eof) {
			McpPendingErase(L, server, key);
			return McpElicitFail(L, "client disconnected");
		}
		if (!lua_isnil(L, EL_DEADLINE) && McpNowMs() >= lua_tonumber(L, EL_DEADLINE)) {
			McpSendCancelled(L, key.c_str(), "timeout");
			McpPendingErase(L, server, key);
			return -3;
		}

		std::string line;
		int r = McpNextLine(server, line);
		if (r > 0)
			McpHandleLineWhileWaiting(L, server, line);
		else if (r == 0)
			return -1;
		// r < 0: eof is latched; the next iteration reports it
	}
}

static int McpElicitWaitContinuation(lua_State* L, int status, lua_KContext ctx);

static int McpElicitWait(lua_State* L, LuaMcpElicitation* e) {
	lua_settop(L, EL_DEADLINE); // drop anything the scheduler passed on resume
	int code;
	{
		std::string key(lua_tostring(L, EL_ID));
		code = McpElicitPoll(L, e->server, key);
	}
	if (code >= 0)
		return code;
	if (code == -2)
		return McpElicitProcess(L, e);
	if (code == -3)
		return McpElicitRunDefault(L, e, "timeout");
	return lua_yieldk(L, 0, (lua_KContext)e, McpElicitWaitContinuation);
}

static int McpElicitWaitContinuation(lua_State* L, int status, lua_KContext ctx) {
	(void)status;
	return McpElicitWait(L, (LuaMcpElicitation*)ctx);
}

int McpElicitation_Elicit(lua_State* L) {
	LuaMcpElicitation* e = luaL_checkmcpelicitation(L, 1);
	luaL_checkany(L, 2);
	luaL_checktype(L, 3, LUA_TTABLE);
	lua_settop(L, EL_REQUEST);

	McpValidateElicitation(L, e);

	// Waiting means yielding the tool's coroutine, so only a running tool call
	// can ask. Checked before anything is sent: a failed yield after sending
	// would leave the client showing a form no one waits for.
	LuaMcpServer* server = e->server;
	if (!server || L != server->toolThread)
		return luaL_error(L, "Elicit: must be called from inside a tool call");
	if (!lua_isyieldable(L))
		return luaL_error(L, "Elicit: can't wait here (called through a C function that doesn't allow yielding)");
	if (!server || !server->started || server->stopRequested || server->eof)
		return McpElicitFail(L, "MCP server is not running");

	lua_pushnil(L); // EL_ID
	lua_pushnil(L); // EL_DEADLINE

	// Titled answers and multi-select need 2025-11-25; ISO dates compare as strings.
	bool titled = strcmp(server->protocolVersion.c_str(), "2025-11-25") >= 0;
	bool needsMultiple = false;
	for (const McpQuestion& q : e->questions)
		needsMultiple = needsMultiple || q.multiple;
	if (!server->clientElicitation || (needsMultiple && !titled))
		return McpElicitRunDefault(L, e, "unsupported");

	char id[32];
	snprintf(id, sizeof(id), "kitsune-%lld", server->nextRequestId++);

	lua_newtable(L); // envelope
	lua_pushliteral(L, "2.0");
	lua_setfield(L, -2, "jsonrpc");
	lua_pushstring(L, id);
	lua_setfield(L, -2, "id");
	lua_pushliteral(L, "elicitation/create");
	lua_setfield(L, -2, "method");
	lua_newtable(L); // params
	lua_pushlstring(L, e->message.c_str(), e->message.size());
	lua_setfield(L, -2, "message");
	McpPushElicitationSchema(L, e, titled);
	lua_setfield(L, -2, "requestedSchema");
	lua_setfield(L, -2, "params");

	bool sent = false;
	if (McpJsonEncode(L, -1)) {
		size_t len;
		const char* s = lua_tolstring(L, -1, &len);
		if (s) {
			McpWriteLine(s, len);
			sent = true;
		}
	}
	lua_settop(L, EL_DEADLINE);
	if (!sent)
		return McpElicitFail(L, "could not encode the elicitation request");

	server->pending[id] = McpPendingRequest();

	lua_pushstring(L, id);
	lua_replace(L, EL_ID);
	if (e->timeoutMs > 0) {
		lua_pushnumber(L, McpNowMs() + (double)e->timeoutMs);
		lua_replace(L, EL_DEADLINE);
	}
	return McpElicitWait(L, e);
}

// Lua-facing API

int McpServer_AddTool(lua_State* L) {
	LuaMcpServer* server = luaL_checkmcpserver(L, 1);
	const char* name        = luaL_checkstring(L, 2);
	const char* description = luaL_checkstring(L, 3);
	luaL_checktype(L, 4, LUA_TTABLE);
	luaL_checktype(L, 5, LUA_TFUNCTION);

	McpTool tool;
	tool.name        = name;
	tool.description = description;

	int n = (int)lua_rawlen(L, 4);
	for (int i = 1; i <= n; i++) {
		lua_rawgeti(L, 4, i);
		if (!lua_istable(L, -1)) {
			lua_pop(L, 1);
			continue;
		}

		McpToolParam p;
		p.required = false;
		p.type     = "string";

		lua_getfield(L, -1, "name");
		if (lua_isstring(L, -1)) p.name = lua_tostring(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, -1, "type");
		if (lua_isstring(L, -1)) p.type = lua_tostring(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, -1, "description");
		if (lua_isstring(L, -1)) p.description = lua_tostring(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, -1, "required");
		if (!lua_isnil(L, -1)) p.required = lua_toboolean(L, -1) != 0;
		lua_pop(L, 1);

		if (!p.name.empty())
			tool.params.push_back(std::move(p));
		lua_pop(L, 1); // the param descriptor table
	}

	lua_pushvalue(L, 5);
	tool.fnRef = luaL_ref(L, LUA_REGISTRYINDEX);
	server->tools.push_back(std::move(tool));

	lua_pushboolean(L, 1);
	return 1;
}

int McpServer_Start(lua_State* L) {
	LuaMcpServer* server = luaL_checkmcpserver(L, 1);

	if (server->started) {
		lua_pushboolean(L, 1);
		return 1;
	}

	if (!McpStdioAvailable()) {
		lua_pushboolean(L, 0);
		lua_pushliteral(L, "stdin/stdout not available");
		return 2;
	}

	McpInstallOutputRedirect(L, server);

	// Tasks.New(fn) -- the engine's own Lua-facing primitive for spinning up an
	// independently-scheduled coroutine. Called here as a plain Lua function
	// call (not through the public KitsuneEngine.h host API), because it's
	// designed to be invoked from *inside* already-running script code -- unlike
	// KitsuneExecuteVariableAsync et al., which explicitly refuse calls made
	// from the scheduler thread (which is exactly where this Lua call executes).
	lua_getglobal(L, "Tasks");
	lua_getfield(L, -1, "New");
	lua_remove(L, -2); // drop the Tasks table, keep New on top

	lua_pushlightuserdata(L, server);
	lua_pushcclosure(L, McpPollEntrypoint, 1);

	if (lua_pcall(L, 1, 1, 0) != LUA_OK) {
		const char* err = lua_tostring(L, -1);
		lua_pushboolean(L, 0);
		lua_pushstring(L, err ? err : "Tasks.New failed");
		return 2;
	}

	// Result is a LuaTask userdata; pin it so IsRunning()/__gc can query/release it later.
	server->taskRef = luaL_ref(L, LUA_REGISTRYINDEX);
	server->started = true;
	lua_pushboolean(L, 1);
	return 1;
}

int McpServer_Stop(lua_State* L) {
	LuaMcpServer* server = luaL_checkmcpserver(L, 1);
	server->stopRequested = true;
	return 0;
}

int McpServer_IsRunning(lua_State* L) {
	LuaMcpServer* server = luaL_checkmcpserver(L, 1);
	if (!server->started || server->taskRef == LUA_NOREF) {
		lua_pushboolean(L, 0);
		return 1;
	}

	// task:GetStatus() -- again a plain Lua call on the LuaTask handle, not the
	// KitsuneGetStatus host API (untested from this context and unnecessary:
	// the task handle already exposes its own status query safely).
	lua_rawgeti(L, LUA_REGISTRYINDEX, server->taskRef); // task
	lua_getfield(L, -1, "GetStatus");                   // task, GetStatus
	lua_pushvalue(L, -2);                                // task, GetStatus, task

	bool running = false;
	if (lua_pcall(L, 1, 1, 0) == LUA_OK) {
		int status = (int)lua_tointeger(L, -1);
		running = status != KITSUNE_STATUS_NONE && status != KITSUNE_STATUS_DONE &&
		          status != KITSUNE_STATUS_FAULTED && status != KITSUNE_STATUS_CANCELLED;
	}
	lua_pop(L, 2); // status-or-error, task

	lua_pushboolean(L, running ? 1 : 0);
	return 1;
}

int MCP_Create(lua_State* L) {
	if (g_mcpInstanceRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_mcpInstanceRef);
		if (lua_ismcpserver(L, -1))
			return 1;
		// Stale ref -- shouldn't normally happen since __gc clears it, but
		// recover cleanly rather than trust a dangling registry slot.
		lua_pop(L, 1);
		luaL_unref(L, LUA_REGISTRYINDEX, g_mcpInstanceRef);
		g_mcpInstanceRef = LUA_NOREF;
	}

	LuaMcpServer* server = lua_pushmcpserver(L);

	if (lua_istable(L, 1)) {
		lua_getfield(L, 1, "Name");
		if (lua_isstring(L, -1)) server->name = lua_tostring(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 1, "Version");
		if (lua_isstring(L, -1)) server->version = lua_tostring(L, -1);
		lua_pop(L, 1);

		lua_getfield(L, 1, "Instructions");
		if (lua_isstring(L, -1)) server->instructions = lua_tostring(L, -1);
		lua_pop(L, 1);
	}
	if (server->name.empty())    server->name    = "kitsune-lua";
	if (server->version.empty()) server->version = "1.0.0";

	if (lua_istable(L, 2)) lua_pushvalue(L, 2);
	else                   lua_newtable(L);
	server->contextRef = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_pushvalue(L, -1); // duplicate the userdata for the singleton tracking slot
	g_mcpInstanceRef = luaL_ref(L, LUA_REGISTRYINDEX);

	return 1; // userdata remains on top of stack
}
