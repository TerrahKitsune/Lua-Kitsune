#include "luamcp.h"
#include "luajson.h"
#include "KitsuneEngine.h"
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

// =============================================================================
// Singleton tracking -- a plain POD registry-ref int. Deliberately NOT a
// pointer/object with a constructor or destructor: any global with a
// std::vector/std::string member (or one that owns one) would have its
// destructor run at DLL-unload/static-teardown time, governed by the loader
// rather than by KitsuneCleanup()/EndMemoryManager() -- a real hazard in this
// codebase (mem.cpp routes all C++ allocation, STL included, through
// kitsune_malloc/kitsune_free; EndMemoryManager's USEMEMORYMANAGER path can
// tear the backing allocator down before such a global would be destructed).
// LuaMcpServer itself only ever exists via placement-new inside a Lua
// userdata (see lua_mcp_push), so its lifetime is tied to Lua GC / __gc.
// =============================================================================

static int g_mcpInstanceRef = LUA_NOREF;

// =============================================================================
// Forward declarations (mutually-referential dispatch/poll functions)
// =============================================================================

static int  mcp_poll_step(lua_State* L, LuaMcpServer* server);
static int  mcp_poll_continuation(lua_State* L, int status, lua_KContext ctx);
static int  mcp_poll_entrypoint(lua_State* L);
static int  mcp_dispatch_line(lua_State* L, LuaMcpServer* server, const char* data, size_t len);
static int  mcp_dispatch_tools_call(lua_State* L, LuaMcpServer* server, int params_idx, int id_idx);
static int  mcp_tool_finish(lua_State* L, LuaMcpServer* server, int pcall_status);
static int  mcp_tool_continuation(lua_State* L, int status, lua_KContext ctx);
static void mcp_handle_initialize(lua_State* L, LuaMcpServer* server, int params_idx, int id_idx);

// =============================================================================
// Platform: stdio availability + non-blocking poll.
// Direct adaptation of LuaProcess.cpp's ReadFromPipe/read_nonblocking, pointed
// at our own inherited stdin/stdout instead of a child process's pipe.
// Returns from mcp_poll_stdin: >0 = bytes read, 0 = nothing available right
// now, <0 = EOF (stdin closed -- the normal MCP client-disconnect path).
// =============================================================================

#ifdef _WIN32

static bool mcp_stdio_available() {
	HANDLE hin  = GetStdHandle(STD_INPUT_HANDLE);
	HANDLE hout = GetStdHandle(STD_OUTPUT_HANDLE);
	return hin != NULL && hin != INVALID_HANDLE_VALUE &&
	       hout != NULL && hout != INVALID_HANDLE_VALUE;
}

static int mcp_poll_stdin(char* buf, size_t bufsize) {
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

static bool mcp_stdio_available() {
	return fcntl(0, F_GETFD) != -1 && fcntl(1, F_GETFD) != -1;
}

static int mcp_poll_stdin(char* buf, size_t bufsize) {
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

// =============================================================================
// Userdata lifecycle
// =============================================================================

LuaMcpServer* lua_mcp_push(lua_State* L) {
	LuaMcpServer* s = (LuaMcpServer*)lua_newuserdata(L, sizeof(LuaMcpServer));
	new (s) LuaMcpServer();
	luaL_setmetatable(L, LUAMCP);
	return s;
}

LuaMcpServer* lua_mcp_check(lua_State* L, int idx) {
	return (LuaMcpServer*)luaL_checkudata(L, idx, LUAMCP);
}

bool lua_mcp_is(lua_State* L, int idx) {
	if (!lua_isuserdata(L, idx))
		return false;
	return luaL_testudata(L, idx, LUAMCP) != NULL;
}

int lua_mcp_gc(lua_State* L) {
	LuaMcpServer* server = lua_mcp_check(L, 1);

	server->stopRequested = true;

	for (McpTool& t : server->tools) {
		if (t.fn_ref != LUA_NOREF)
			luaL_unref(L, LUA_REGISTRYINDEX, t.fn_ref);
	}
	if (server->context_ref != LUA_NOREF)
		luaL_unref(L, LUA_REGISTRYINDEX, server->context_ref);

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

int lua_mcp_tostring(lua_State* L) {
	LuaMcpServer* server = lua_mcp_check(L, 1);
	lua_pushfstring(L, "MCP(%d tools)", (int)server->tools.size());
	return 1;
}

// =============================================================================
// JSON bridge helpers -- everything goes through the shared LuaJson bridge
// instance (lua_json_bridge_registry_key()), exactly as luatoolsuite.cpp
// already does for decoding tool arguments. No hand-rolled JSON anywhere.
// =============================================================================

// Decodes `data`/`len` (a JSON-RPC line) into a Lua value, pushing the result.
// Returns true on success (value on top of stack); false on failure (nothing pushed).
static bool mcp_json_decode(lua_State* L, const char* data, size_t len) {
	lua_pushcfunction(L, lua_json_decode);
	lua_rawgetp(L, LUA_REGISTRYINDEX, lua_json_bridge_registry_key());
	lua_pushlstring(L, data, len);
	if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
		lua_pop(L, 1);
		return false;
	}
	return true;
}

// Encodes the Lua value at stack index `value_idx` to a JSON string, pushing
// the result string. Returns true on success (string on top of stack).
static bool mcp_json_encode(lua_State* L, int value_idx) {
	value_idx = lua_absindex(L, value_idx);
	lua_pushcfunction(L, lua_json_encode);
	lua_rawgetp(L, LUA_REGISTRYINDEX, lua_json_bridge_registry_key());
	lua_pushvalue(L, value_idx);
	if (lua_pcall(L, 2, 1, 0) != LUA_OK) {
		lua_pop(L, 1);
		return false;
	}
	return true;
}

static void mcp_write_line(const char* data, size_t len) {
	fwrite(data, 1, len, stdout);
	fputc('\n', stdout);
	fflush(stdout);
}

// Writes {jsonrpc="2.0", id=<value at id_idx>, result=<value at result_idx>}.
static void mcp_send_result(lua_State* L, int id_idx, int result_idx) {
	id_idx     = lua_absindex(L, id_idx);
	result_idx = lua_absindex(L, result_idx);

	lua_newtable(L);
	lua_pushliteral(L, "2.0");
	lua_setfield(L, -2, "jsonrpc");
	lua_pushvalue(L, id_idx);
	lua_setfield(L, -2, "id");
	lua_pushvalue(L, result_idx);
	lua_setfield(L, -2, "result");

	if (mcp_json_encode(L, -1)) {
		size_t len;
		const char* s = lua_tolstring(L, -1, &len);
		if (s) mcp_write_line(s, len);
		lua_pop(L, 1); // encoded string
	}
	lua_pop(L, 1); // envelope table
}

static void mcp_send_error(lua_State* L, int id_idx, int code, const char* message) {
	id_idx = lua_absindex(L, id_idx);

	lua_newtable(L); // envelope
	lua_pushliteral(L, "2.0");
	lua_setfield(L, -2, "jsonrpc");
	lua_pushvalue(L, id_idx);
	lua_setfield(L, -2, "id");

	lua_newtable(L); // error object
	lua_pushinteger(L, code);
	lua_setfield(L, -2, "code");
	lua_pushstring(L, message);
	lua_setfield(L, -2, "message");
	lua_setfield(L, -2, "error");

	if (mcp_json_encode(L, -1)) {
		size_t len;
		const char* s = lua_tolstring(L, -1, &len);
		if (s) mcp_write_line(s, len);
		lua_pop(L, 1);
	}
	lua_pop(L, 1);
}

// =============================================================================
// Result builders
// =============================================================================

static void mcp_push_empty_object(lua_State* L) {
	// Guarantees `{}` on encode regardless of the bridge's emptyObjectAsSentinel
	// setting (default off, which would otherwise encode an empty Lua table as `[]`).
	lua_pushlightuserdata(L, lua_json_empty_object());
}

static void mcp_push_initialize_result(lua_State* L, LuaMcpServer* server) {
	lua_newtable(L); // result

	lua_pushliteral(L, "2024-11-05");
	lua_setfield(L, -2, "protocolVersion");

	lua_newtable(L); // capabilities
	mcp_push_empty_object(L);
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

static void mcp_push_tool_input_schema(lua_State* L, const McpTool& tool) {
	lua_newtable(L); // schema
	lua_pushliteral(L, "object");
	lua_setfield(L, -2, "type");

	if (tool.params.empty()) {
		mcp_push_empty_object(L);
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

static void mcp_push_tool_descriptor(lua_State* L, const McpTool& tool) {
	lua_newtable(L);
	lua_pushlstring(L, tool.name.c_str(), tool.name.size());
	lua_setfield(L, -2, "name");
	lua_pushlstring(L, tool.description.c_str(), tool.description.size());
	lua_setfield(L, -2, "description");
	mcp_push_tool_input_schema(L, tool);
	lua_setfield(L, -2, "inputSchema");
}

static void mcp_push_tools_list_result(lua_State* L, LuaMcpServer* server) {
	lua_newtable(L); // result
	lua_newtable(L); // tools array
	for (size_t i = 0; i < server->tools.size(); i++) {
		mcp_push_tool_descriptor(L, server->tools[i]);
		lua_rawseti(L, -2, (int)(i + 1));
	}
	lua_setfield(L, -2, "tools");
}

// =============================================================================
// Dispatch: initialize
// =============================================================================

static void mcp_handle_initialize(lua_State* L, LuaMcpServer* server, int params_idx, int id_idx) {
	if (lua_istable(L, params_idx)) {
		lua_getfield(L, params_idx, "clientInfo");
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

	mcp_push_initialize_result(L, server);
	mcp_send_result(L, id_idx, -1);
	lua_pop(L, 1); // result table
}

// =============================================================================
// Dispatch: tools/call -- yield-safe, mirrors luatoolsuite.cpp's
// toolsuite_dispatch_tool/toolsuite_continuation shape, simplified since MCP
// dispatches exactly one call per request (no batch/next-index bookkeeping
// needed). `server` itself (already Lua-GC-owned) is passed as the lua_KContext,
// so no extra heap allocation is needed for the yield to survive on.
//
// Stack discipline: by the time lua_pcallk is invoked, the stack is exactly
// [..., msg, id, params, method, fn, context, request]. After the call
// resolves (synchronously or via the continuation), Lua guarantees the stack
// looks like [..., msg, id, params, method, result_or_error] -- so
// mcp_tool_finish can derive every needed index purely from lua_gettop(L).
// =============================================================================

static int mcp_tool_finish(lua_State* L, LuaMcpServer* server, int pcall_status) {
	int top      = lua_gettop(L);
	int id_idx   = top - 3;
	int msg_idx  = top - 4;

	bool ok = (pcall_status == LUA_OK || pcall_status == LUA_YIELD);
	const char* raw = lua_tostring(L, top);
	std::string text = ok ? (raw ? raw : "") : (std::string("error: ") + (raw ? raw : "unknown error"));

	lua_newtable(L); // envelope
	lua_newtable(L); // content array
	int content_idx = lua_gettop(L);
	int content_n    = 0;

	if (!server->capturedOutput.empty()) {
		lua_newtable(L);
		lua_pushliteral(L, "text");
		lua_setfield(L, -2, "type");
		lua_pushlstring(L, server->capturedOutput.c_str(), server->capturedOutput.size());
		lua_setfield(L, -2, "text");
		lua_rawseti(L, content_idx, ++content_n);
	}

	lua_newtable(L);
	lua_pushliteral(L, "text");
	lua_setfield(L, -2, "type");
	lua_pushlstring(L, text.c_str(), text.size());
	lua_setfield(L, -2, "text");
	lua_rawseti(L, content_idx, ++content_n);

	lua_setfield(L, -2, "content");
	if (!ok) {
		lua_pushboolean(L, 1);
		lua_setfield(L, -2, "isError");
	}

	mcp_send_result(L, id_idx, -1);
	lua_pop(L, 1); // envelope

	lua_settop(L, msg_idx - 1); // drop msg/id/params/method/result entirely
	return mcp_poll_step(L, server);
}

static int mcp_tool_continuation(lua_State* L, int status, lua_KContext ctx) {
	return mcp_tool_finish(L, (LuaMcpServer*)ctx, status);
}

static int mcp_dispatch_tools_call(lua_State* L, LuaMcpServer* server, int params_idx, int id_idx) {
	int msg_idx = id_idx - 1;

	lua_getfield(L, params_idx, "name");
	const char* tool_name = lua_tostring(L, -1);

	const McpTool* tool = NULL;
	if (tool_name) {
		for (const McpTool& t : server->tools) {
			if (t.name == tool_name) { tool = &t; break; }
		}
	}
	lua_pop(L, 1); // name

	if (!tool) {
		mcp_send_error(L, id_idx, -32602, tool_name ? "Unknown tool" : "Missing tool name");
		lua_settop(L, msg_idx - 1);
		return mcp_poll_step(L, server);
	}

	lua_getfield(L, params_idx, "arguments"); // table or nil
	int  args_idx = lua_gettop(L);
	bool has_args = lua_istable(L, args_idx);

	server->capturedOutput.clear();

	lua_rawgeti(L, LUA_REGISTRYINDEX, tool->fn_ref);        // fn
	lua_rawgeti(L, LUA_REGISTRYINDEX, server->context_ref); // context

	lua_newtable(L); // request
	int req_idx = lua_gettop(L);

	if (has_args) lua_pushvalue(L, args_idx);
	else          mcp_push_empty_object(L);
	lua_setfield(L, req_idx, "Arguments");

	lua_newtable(L); // Parameters (positional, declared order)
	for (size_t i = 0; i < tool->params.size(); i++) {
		if (has_args) lua_getfield(L, args_idx, tool->params[i].name.c_str());
		else          lua_pushnil(L);
		lua_rawseti(L, -2, (int)(i + 1));
	}
	lua_setfield(L, req_idx, "Parameters");

	lua_pushlstring(L, tool->name.c_str(), tool->name.size());
	lua_setfield(L, req_idx, "Name");

	lua_pushvalue(L, id_idx);
	lua_setfield(L, req_idx, "RequestId");

	lua_newtable(L); // Client
	lua_pushlstring(L, server->clientName.c_str(), server->clientName.size());
	lua_setfield(L, -2, "Name");
	lua_pushlstring(L, server->clientVersion.c_str(), server->clientVersion.size());
	lua_setfield(L, -2, "Version");
	lua_setfield(L, req_idx, "Client");

	lua_remove(L, args_idx); // fn/context/request shift down by one to become contiguous

	int rc = lua_pcallk(L, 2, 1, 0, (lua_KContext)server, mcp_tool_continuation);
	return mcp_tool_finish(L, server, rc);
}

// =============================================================================
// Dispatch: one decoded JSON-RPC line
// =============================================================================

static int mcp_dispatch_line(lua_State* L, LuaMcpServer* server, const char* data, size_t len) {
	if (!mcp_json_decode(L, data, len)) {
		lua_pushnil(L);
		mcp_send_error(L, -1, -32700, "Parse error");
		lua_pop(L, 1);
		return mcp_poll_step(L, server);
	}

	int msg_idx = lua_gettop(L);
	if (!lua_istable(L, msg_idx)) {
		lua_settop(L, msg_idx - 1);
		return mcp_poll_step(L, server);
	}

	lua_getfield(L, msg_idx, "id");
	int id_idx = lua_gettop(L);
	bool has_id = !lua_isnil(L, id_idx);

	lua_getfield(L, msg_idx, "params");
	int params_idx = lua_gettop(L);

	lua_getfield(L, msg_idx, "method");
	const char* method = lua_tostring(L, -1);

	if (!method) {
		if (has_id) mcp_send_error(L, id_idx, -32600, "Invalid Request");
	} else if (strcmp(method, "initialize") == 0) {
		mcp_handle_initialize(L, server, params_idx, id_idx);
	} else if (strcmp(method, "notifications/initialized") == 0) {
		// no response
	} else if (strcmp(method, "ping") == 0) {
		mcp_push_empty_object(L);
		mcp_send_result(L, id_idx, -1);
		lua_pop(L, 1);
	} else if (strcmp(method, "tools/list") == 0) {
		mcp_push_tools_list_result(L, server);
		mcp_send_result(L, id_idx, -1);
		lua_pop(L, 1);
	} else if (strcmp(method, "tools/call") == 0) {
		return mcp_dispatch_tools_call(L, server, params_idx, id_idx); // may yield; tail return
	} else {
		if (has_id) mcp_send_error(L, id_idx, -32601, "Method not found");
	}

	lua_settop(L, msg_idx - 1); // drop method/params/id/msg
	return mcp_poll_step(L, server);
}

// =============================================================================
// Poll loop -- modeled on HttpCurl.cpp's client_call/client_call_continuation:
// a native call that transparently yields when invoked directly from the
// top-level script, with no Tasks.New/coroutine.resume boilerplate needed.
// Runs as its own independently-scheduled coroutine (see lua_mcp_start),
// so it cooperates with the scheduler on its own, fire-and-forget.
// =============================================================================

static void mcp_linebuf_append(LuaMcpServer* server, const char* data, size_t len) {
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

static int mcp_poll_step(lua_State* L, LuaMcpServer* server) {
	if (server->stopRequested)
		return 0;

	void* nl = server->linebuf ? memchr(server->linebuf, '\n', server->linebufLen) : NULL;
	if (nl) {
		size_t line_len = (size_t)((char*)nl - server->linebuf);
		// Copy the line out and shrink linebuf BEFORE dispatching -- dispatch may
		// yield for an arbitrarily long time (a tool callback calling Sleep()/etc).
		std::string line(server->linebuf, line_len);
		size_t consumed = line_len + 1; // include the '\n'
		memmove(server->linebuf, server->linebuf + consumed, server->linebufLen - consumed);
		server->linebufLen -= consumed;
		return mcp_dispatch_line(L, server, line.data(), line.size());
	}

	char buf[4096];
	int  n = mcp_poll_stdin(buf, sizeof(buf));
	if (n < 0)
		return 0; // EOF -- client disconnected; return for real, ending this coroutine
	if (n > 0) {
		mcp_linebuf_append(server, buf, (size_t)n);
		return mcp_poll_step(L, server); // more may already be buffered; check again immediately
	}

	return lua_yieldk(L, 0, (lua_KContext)server, mcp_poll_continuation);
}

static int mcp_poll_continuation(lua_State* L, int status, lua_KContext ctx) {
	(void)status;
	return mcp_poll_step(L, (LuaMcpServer*)ctx);
}

static int mcp_poll_entrypoint(lua_State* L) {
	LuaMcpServer* server = (LuaMcpServer*)lua_touserdata(L, lua_upvalueindex(1));
	return mcp_poll_step(L, server);
}

// =============================================================================
// print()/io.write() overrides -- installed globally the moment Start()
// succeeds. stdout is reserved for JSON-RPC responses; nothing in this
// process may write to it directly once the server is running, regardless
// of which coroutine tries. Output is captured into server->capturedOutput
// rather than silently discarded, so a tool callback's debug output can be
// merged into that call's response -- see mcp_dispatch_tools_call (clears
// the buffer before dispatch) and mcp_tool_finish (merges it after).
// =============================================================================

static int mcp_print_override(lua_State* L) {
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

static int mcp_iowrite_override(lua_State* L) {
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

static void mcp_install_output_redirect(lua_State* L, LuaMcpServer* server) {
	lua_pushlightuserdata(L, server);
	lua_pushcclosure(L, mcp_print_override, 1);
	lua_setglobal(L, "print");

	lua_getglobal(L, "io");
	lua_pushlightuserdata(L, server);
	lua_pushcclosure(L, mcp_iowrite_override, 1);
	lua_setfield(L, -2, "write");
	lua_pop(L, 1); // io table
}

// =============================================================================
// Lua-facing API
// =============================================================================

int lua_mcp_addtool(lua_State* L) {
	LuaMcpServer* server = lua_mcp_check(L, 1);
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
	tool.fn_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	server->tools.push_back(std::move(tool));

	lua_pushboolean(L, 1);
	return 1;
}

int lua_mcp_start(lua_State* L) {
	LuaMcpServer* server = lua_mcp_check(L, 1);

	if (server->started) {
		lua_pushboolean(L, 1);
		return 1;
	}

	if (!mcp_stdio_available()) {
		lua_pushboolean(L, 0);
		lua_pushliteral(L, "stdin/stdout not available");
		return 2;
	}

	mcp_install_output_redirect(L, server);

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
	lua_pushcclosure(L, mcp_poll_entrypoint, 1);

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

int lua_mcp_stop(lua_State* L) {
	LuaMcpServer* server = lua_mcp_check(L, 1);
	server->stopRequested = true;
	return 0;
}

int lua_mcp_isrunning(lua_State* L) {
	LuaMcpServer* server = lua_mcp_check(L, 1);
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

int lua_mcp_create(lua_State* L) {
	if (g_mcpInstanceRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, g_mcpInstanceRef);
		if (lua_mcp_is(L, -1))
			return 1;
		// Stale ref -- shouldn't normally happen since __gc clears it, but
		// recover cleanly rather than trust a dangling registry slot.
		lua_pop(L, 1);
		luaL_unref(L, LUA_REGISTRYINDEX, g_mcpInstanceRef);
		g_mcpInstanceRef = LUA_NOREF;
	}

	LuaMcpServer* server = lua_mcp_push(L);

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
	server->context_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_pushvalue(L, -1); // duplicate the userdata for the singleton tracking slot
	g_mcpInstanceRef = luaL_ref(L, LUA_REGISTRYINDEX);

	return 1; // userdata remains on top of stack
}

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
	{ NULL, NULL }
};

int luaopen_mcp(lua_State* L) {
	luaL_newlibtable(L, mcp_functions);
	luaL_setfuncs(L, mcp_functions, 0);

	luaL_newmetatable(L, LUAMCP);
	luaL_setfuncs(L, mcp_meta, 0);
	lua_pushliteral(L, "__index");
	lua_newtable(L);
	luaL_setfuncs(L, mcp_methods, 0);
	lua_rawset(L, -3);
	lua_pop(L, 1);

	return 1;
}
