#include "mcp/LuaMCPHttp.h"
#include "httpserver/LuaHttpServer.h"
#include "httpserver/LuaHttpServerRequest.h"
#include "identifier/LuaIdentifier.h"
#include <cstring>

// The JSON-response half of MCP's Streamable HTTP transport: every JSON-RPC
// message is one POST to MCP_HTTP_PATH and gets one HTTP response. There is no
// SSE stream, so the server can never send a request of its own (elicitation).
//
// Everything runs on the poll task, one request at a time. While a tool
// callback runs or yields, the Accept coroutine is not resumed, so later
// requests wait in the socket until that call has been answered.

#define MCP_HTTP_PATH    "/mcp"
#define MCP_HTTP_HANDLED (-1)

// Fixed stack slots of the poll task while it handles one request. A tool
// callback's result lands right above them.
enum {
	HT_REQUEST = 1, // HttpRequest userdata
	HT_RESPONSE,    // its HttpResponse
	HT_HEADERS,     // request headers, lowercase names
	HT_MESSAGE,     // decoded JSON-RPC message, nil until decoded
	HT_ID,          // its id, nil for none
};

static int McpHttpPollStep(lua_State* L, LuaMcpServer* server);

// Loopback checks

// Length of the host that `s` starts with ("host", "host:port", "[v6]:port", "host/path").
static size_t McpHttpHostLength(const char* s) {
	if (*s == '[') {
		const char* end = strchr(s, ']');
		return end ? (size_t)(end - s) + 1 : strlen(s);
	}
	return strcspn(s, ":/");
}

static bool McpHttpHostIs(const char* host, size_t len, const char* expected) {
	return strlen(expected) == len && strncmp(host, expected, len) == 0;
}

bool McpHttpIsLoopbackAddress(const char* address) {
	if (strncmp(address, "http://", 7) == 0)
		address += 7;
	size_t len = McpHttpHostLength(address);
	if (address[len] != ':')
		return false;
	// HttpServer.Listen can't bind a bracketed IPv6 host, so [::1] is not offered.
	return McpHttpHostIs(address, len, "127.0.0.1") || McpHttpHostIs(address, len, "localhost");
}

// A browser page served from anywhere else must not reach a local server (DNS rebinding).
static bool McpHttpOriginIsLoopback(const char* origin) {
	const char* host = strstr(origin, "://");
	if (!host)
		return false;
	host += 3;
	size_t len = McpHttpHostLength(host);
	return McpHttpHostIs(host, len, "127.0.0.1") || McpHttpHostIs(host, len, "localhost") ||
	       McpHttpHostIs(host, len, "[::1]");
}

// Responding

// Calls an HttpResponse method on HT_RESPONSE with the nargs values on top of
// the stack (popped). Errors are dropped: these raise once the client has left.
static void McpHttpResponseCall(lua_State* L, lua_CFunction method, int nargs) {
	lua_pushcfunction(L, method);
	lua_pushvalue(L, HT_RESPONSE);
	lua_rotate(L, -(nargs + 2), 2);
	if (lua_pcall(L, nargs + 1, 0, 0) != LUA_OK)
		lua_pop(L, 1);
}

static void McpHttpSetHeader(lua_State* L, const char* name, const char* value) {
	lua_pushstring(L, name);
	lua_pushstring(L, value);
	McpHttpResponseCall(L, HttpResponse_SetHeader, 2);
}

// Sends the response. With hasBody the JSON text on top of the stack is the body (popped).
static void McpHttpSend(lua_State* L, int status, bool hasBody) {
	lua_pushinteger(L, status);
	McpHttpResponseCall(L, HttpResponse_SetCode, 1);
	if (hasBody)
		McpHttpSetHeader(L, "Content-Type", "application/json");
	McpHttpResponseCall(L, HttpResponse_Send, hasBody ? 1 : 0);
}

static void McpHttpSendResult(lua_State* L, int resultIdx) {
	if (McpPushResultJson(L, HT_ID, resultIdx))
		McpHttpSend(L, 200, true);
	else
		McpHttpSend(L, 500, false);
}

static void McpHttpSendError(lua_State* L, int status, int code, const char* message) {
	if (McpPushErrorJson(L, HT_ID, code, message))
		McpHttpSend(L, status, true);
	else
		McpHttpSend(L, status, false);
}

// The returned string is owned by the headers table and stays valid for the request.
static const char* McpHttpHeader(lua_State* L, const char* name) {
	lua_getfield(L, HT_HEADERS, name);
	const char* value = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
	lua_pop(L, 1);
	return value;
}

// Finds the session named by the MCP-Session-Id header. Sends the error
// response and returns NULL when there is none.
static McpSession* McpHttpRequireSession(lua_State* L, LuaMcpServer* server, const char** sessionId) {
	const char* id = McpHttpHeader(L, "mcp-session-id");
	if (!id) {
		McpHttpSendError(L, 400, -32600, "Missing MCP-Session-Id header");
		return NULL;
	}
	auto it = server->sessions.find(id);
	if (it == server->sessions.end()) {
		McpHttpSendError(L, 404, -32600, "Unknown session");
		return NULL;
	}
	*sessionId = id;
	return &it->second;
}

// CORS for a page on another loopback port (a web UI calling from the browser):
// the browser only hands it responses that name its origin, and only the
// response headers listed here.
static void McpHttpAllowOrigin(lua_State* L, const char* origin) {
	McpHttpSetHeader(L, "Access-Control-Allow-Origin", origin);
	McpHttpSetHeader(L, "Access-Control-Expose-Headers", "MCP-Session-Id");
	McpHttpSetHeader(L, "Vary", "Origin");
}

// The preflight a browser sends before a cross-origin POST or DELETE.
static void McpHttpHandlePreflight(lua_State* L) {
	McpHttpSetHeader(L, "Access-Control-Allow-Methods", "POST, DELETE");
	const char* requested = McpHttpHeader(L, "access-control-request-headers");
	if (requested)
		McpHttpSetHeader(L, "Access-Control-Allow-Headers", requested);
	McpHttpSetHeader(L, "Access-Control-Max-Age", "600");
	McpHttpSend(L, 204, false);
}

// Tool calls run on the poll coroutine through lua_pcallk: when the callback
// finishes, synchronously or through the continuation, the stack is the HT_
// slots followed by the callback's result or error.

static int McpHttpToolFinish(lua_State* L, LuaMcpServer* server, int pcallStatus) {
	server->toolThread = nullptr;
	McpPushToolResult(L, server, pcallStatus, HT_ID + 1);
	McpHttpSendResult(L, -1);
	return McpHttpPollStep(L, server);
}

static int McpHttpToolContinuation(lua_State* L, int status, lua_KContext ctx) {
	return McpHttpToolFinish(L, (LuaMcpServer*)ctx, status);
}

static int McpHttpDispatchToolsCall(lua_State* L, LuaMcpServer* server, const McpSession& session, const char* sessionId) {
	lua_getfield(L, HT_MESSAGE, "params");
	int paramsIdx = lua_gettop(L);

	bool           hasName = false;
	const McpTool* tool    = NULL;
	if (lua_istable(L, paramsIdx)) {
		lua_getfield(L, paramsIdx, "name");
		const char* toolName = lua_tostring(L, -1);
		hasName = toolName != NULL;
		tool    = McpFindTool(server, toolName);
		lua_pop(L, 1);
	}
	if (!tool) {
		McpHttpSendError(L, 200, -32602, hasName ? "Unknown tool" : "Missing tool name");
		return MCP_HTTP_HANDLED;
	}

	lua_getfield(L, paramsIdx, "arguments");
	int argsIdx = lua_gettop(L);

	server->capturedOutput.clear();

	lua_rawgeti(L, LUA_REGISTRYINDEX, tool->fnRef);
	lua_rawgeti(L, LUA_REGISTRYINDEX, server->contextRef);
	McpPushToolRequest(L, tool, session, sessionId, false, HT_ID, argsIdx);

	lua_remove(L, argsIdx);
	lua_remove(L, paramsIdx); // fn/context/request now sit right above the HT_ slots

	server->toolThread = L;
	int rc = lua_pcallk(L, 2, 1, 0, (lua_KContext)server, McpHttpToolContinuation);
	return McpHttpToolFinish(L, server, rc);
}

// Request handling

static void McpHttpHandleInitialize(lua_State* L, LuaMcpServer* server) {
	if (lua_isnil(L, HT_ID)) {
		McpHttpSendError(L, 400, -32600, "Invalid Request");
		return;
	}

	Identifier_NewUUID(L);
	if (!lua_isidentifier(L, -1)) {
		McpHttpSend(L, 500, false);
		return;
	}
	lua_pushidentifierstring(L, -1);
	const char* sessionId = lua_tostring(L, -1); // stays on the stack until the response is sent

	McpSession& session = server->sessions[sessionId];
	lua_getfield(L, HT_MESSAGE, "params");
	McpApplyInitialize(L, session, -1);
	lua_pop(L, 1);

	McpHttpSetHeader(L, "MCP-Session-Id", sessionId);
	McpPushInitializeResult(L, server, session);
	McpHttpSendResult(L, -1);
}

static void McpHttpHandleDelete(lua_State* L, LuaMcpServer* server) {
	const char* sessionId = NULL;
	McpSession* session = McpHttpRequireSession(L, server, &sessionId);
	if (!session)
		return;
	if (session->clientCapsRef > 0)
		luaL_unref(L, LUA_REGISTRYINDEX, session->clientCapsRef);
	server->sessions.erase(sessionId);
	McpHttpSend(L, 200, false);
}

// Handles the POSTed message. Returns MCP_HTTP_HANDLED, or the poll task's
// return value when a tool call took over.
static int McpHttpHandlePost(lua_State* L, LuaMcpServer* server, LuaHttpRequest* request) {
	if (!McpJsonDecode(L, request->body ? request->body : "", request->body_len)) {
		McpHttpSendError(L, 400, -32700, "Parse error");
		return MCP_HTTP_HANDLED;
	}
	lua_replace(L, HT_MESSAGE);
	if (!lua_istable(L, HT_MESSAGE)) {
		McpHttpSendError(L, 400, -32600, "Invalid Request");
		return MCP_HTTP_HANDLED;
	}
	lua_getfield(L, HT_MESSAGE, "id");
	lua_replace(L, HT_ID);

	// Owned by the message table.
	lua_getfield(L, HT_MESSAGE, "method");
	const char* method = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : NULL;
	lua_pop(L, 1);

	if (!method) {
		// A response to a server request. This transport never sends one, so
		// there is nothing to route it to; a single message that is neither is invalid.
		lua_getfield(L, HT_MESSAGE, "result");
		lua_getfield(L, HT_MESSAGE, "error");
		bool isResponse = !lua_isnil(L, -2) || !lua_isnil(L, -1);
		lua_pop(L, 2);
		if (isResponse)
			McpHttpSend(L, 202, false);
		else
			McpHttpSendError(L, 400, -32600, "Invalid Request");
		return MCP_HTTP_HANDLED;
	}

	if (strcmp(method, "initialize") == 0) {
		McpHttpHandleInitialize(L, server);
		return MCP_HTTP_HANDLED;
	}

	const char* sessionId = NULL;
	McpSession* session = McpHttpRequireSession(L, server, &sessionId);
	if (!session)
		return MCP_HTTP_HANDLED;

	const char* version = McpHttpHeader(L, "mcp-protocol-version");
	if (version && !McpIsSupportedVersion(version)) {
		McpHttpSendError(L, 400, -32600, "Unsupported MCP-Protocol-Version");
		return MCP_HTTP_HANDLED;
	}

	if (lua_isnil(L, HT_ID)) {
		McpHttpSend(L, 202, false); // a notification
	} else if (strcmp(method, "ping") == 0) {
		McpPushEmptyObject(L);
		McpHttpSendResult(L, -1);
	} else if (strcmp(method, "tools/list") == 0) {
		McpPushToolsListResult(L, server);
		McpHttpSendResult(L, -1);
	} else if (strcmp(method, "tools/call") == 0) {
		return McpHttpDispatchToolsCall(L, server, *session, sessionId);
	} else {
		McpHttpSendError(L, 200, -32601, "Method not found");
	}
	return MCP_HTTP_HANDLED;
}

// Handles the HttpRequest at index 1. Same return value as McpHttpHandlePost.
static int McpHttpHandleRequest(lua_State* L, LuaMcpServer* server) {
	LuaHttpRequest* request = (LuaHttpRequest*)lua_touserdata(L, HT_REQUEST);
	lua_rawgeti(L, LUA_REGISTRYINDEX, request->responseRef);
	lua_rawgeti(L, LUA_REGISTRYINDEX, request->headersRef);
	lua_settop(L, HT_ID);

	const char* url     = request->url ? request->url : "";
	size_t      pathLen = strcspn(url, "?");
	if (pathLen != strlen(MCP_HTTP_PATH) || strncmp(url, MCP_HTTP_PATH, pathLen) != 0) {
		McpHttpSend(L, 404, false);
		return MCP_HTTP_HANDLED;
	}

	const char* origin = McpHttpHeader(L, "origin");
	if (origin && !McpHttpOriginIsLoopback(origin)) {
		McpHttpSendError(L, 403, -32600, "Origin not allowed");
		return MCP_HTTP_HANDLED;
	}
	if (origin)
		McpHttpAllowOrigin(L, origin);

	const char* method = request->method ? request->method : "";
	if (strcmp(method, "POST") == 0)
		return McpHttpHandlePost(L, server, request);

	if (strcmp(method, "DELETE") == 0) {
		McpHttpHandleDelete(L, server);
	} else if (strcmp(method, "OPTIONS") == 0) {
		McpHttpHandlePreflight(L);
	} else {
		// GET included: this server offers no SSE stream.
		McpHttpSetHeader(L, "Allow", "POST, DELETE, OPTIONS");
		McpHttpSend(L, 405, false);
	}
	return MCP_HTTP_HANDLED;
}

// Poll loop

// Resumes the Accept coroutine once. Returns true with an HttpRequest pushed
// when one is ready. Otherwise returns false; *ended tells an idle server
// from one whose coroutine has finished or failed.
static bool McpHttpPump(lua_State* L, LuaMcpServer* server, bool* ended) {
	*ended = true;
	if (server->acceptRef <= 0)
		return false;

	lua_rawgeti(L, LUA_REGISTRYINDEX, server->acceptRef);
	lua_State* co = lua_tothread(L, -1);
	int nres = 0;
	if (!co || lua_resume(co, L, 0, &nres) != LUA_YIELD) {
		lua_pop(L, 1);
		return false;
	}
	*ended = false;

	if (nres != 1) {
		lua_pop(co, nres);
		lua_pop(L, 1);
		return false;
	}
	lua_xmove(co, L, 1);
	lua_remove(L, -2); // the coroutine
	return true;
}

static void McpHttpClose(lua_State* L, LuaMcpServer* server) {
	if (server->httpServerRef <= 0)
		return;

	// Responses are only written while the server is pumped; give the ones
	// already sent (a tool that called Stop) a chance to leave.
	bool ended;
	if (McpHttpPump(L, server, &ended))
		lua_pop(L, 1);

	lua_pushcfunction(L, HttpServer_Close);
	lua_rawgeti(L, LUA_REGISTRYINDEX, server->httpServerRef);
	if (lua_pcall(L, 1, 0, 0) != LUA_OK)
		lua_pop(L, 1);

	luaL_unref(L, LUA_REGISTRYINDEX, server->httpServerRef);
	server->httpServerRef = LUA_NOREF;
	luaL_unref(L, LUA_REGISTRYINDEX, server->acceptRef);
	server->acceptRef = LUA_NOREF;
}

static int McpHttpPollContinuation(lua_State* L, int status, lua_KContext ctx) {
	(void)status;
	return McpHttpPollStep(L, (LuaMcpServer*)ctx);
}

static int McpHttpPollStep(lua_State* L, LuaMcpServer* server) {
	for (;;) {
		lua_settop(L, 0);
		if (server->stopRequested) {
			McpHttpClose(L, server);
			return 0;
		}

		bool ended;
		if (!McpHttpPump(L, server, &ended)) {
			if (!ended)
				break;
			McpHttpClose(L, server);
			return 0;
		}

		int handled = McpHttpHandleRequest(L, server);
		if (handled != MCP_HTTP_HANDLED)
			return handled;
	}
	return lua_yieldk(L, 0, (lua_KContext)server, McpHttpPollContinuation);
}

static int McpHttpPollEntrypoint(lua_State* L) {
	LuaMcpServer* server = (LuaMcpServer*)lua_touserdata(L, lua_upvalueindex(1));
	return McpHttpPollStep(L, server);
}

// print / io.write. stdout is not reserved on this transport, so only output
// from the coroutine running a tool call is captured into that call's result;
// everything else goes to the function that was in place before Start.
// Upvalues: the server userdata (which keeps it alive), the original function.

static int McpHttpCallOriginal(lua_State* L) {
	int n = lua_gettop(L);
	lua_pushvalue(L, lua_upvalueindex(2));
	lua_insert(L, 1);
	lua_call(L, n, LUA_MULTRET);
	return lua_gettop(L);
}

static int McpHttpPrint(lua_State* L) {
	LuaMcpServer* server = (LuaMcpServer*)lua_touserdata(L, lua_upvalueindex(1));
	if (L == server->toolThread)
		return McpCapturePrint(L, server);
	return McpHttpCallOriginal(L);
}

static int McpHttpIoWrite(lua_State* L) {
	LuaMcpServer* server = (LuaMcpServer*)lua_touserdata(L, lua_upvalueindex(1));
	if (L == server->toolThread)
		return McpCaptureWrite(L, server);
	return McpHttpCallOriginal(L);
}

static void McpHttpInstallOutputCapture(lua_State* L, int serverIdx) {
	lua_pushvalue(L, serverIdx);
	lua_getglobal(L, "print");
	lua_pushcclosure(L, McpHttpPrint, 2);
	lua_setglobal(L, "print");

	lua_getglobal(L, "io");
	lua_pushvalue(L, serverIdx);
	lua_getfield(L, -2, "write");
	lua_pushcclosure(L, McpHttpIoWrite, 2);
	lua_setfield(L, -2, "write");
	lua_pop(L, 1); // io table
}

int McpHttpStart(lua_State* L, LuaMcpServer* server) {
	lua_pushcfunction(L, HttpServer_Listen);
	lua_pushlstring(L, server->address.c_str(), server->address.size());
	if (lua_pcall(L, 1, 2, 0) != LUA_OK || lua_isnil(L, -2)) {
		// the error message is on top either way
		lua_pushboolean(L, 0);
		lua_insert(L, -2);
		return 2;
	}
	lua_pop(L, 1); // the unused errmsg slot

	lua_pushcfunction(L, HttpServer_Accept);
	lua_pushvalue(L, -2);
	lua_call(L, 1, 1);
	server->acceptRef     = luaL_ref(L, LUA_REGISTRYINDEX);
	server->httpServerRef = luaL_ref(L, LUA_REGISTRYINDEX);

	// The task holds the server userdata itself: nothing else keeps an HTTP
	// instance alive once the script drops it, and the listener must outlive that.
	lua_pushvalue(L, 1);
	lua_pushcclosure(L, McpHttpPollEntrypoint, 1);
	if (!McpStartPollTask(L, server)) {
		McpHttpClose(L, server);
		lua_pushboolean(L, 0);
		lua_insert(L, -2);
		return 2;
	}

	McpHttpInstallOutputCapture(L, 1);
	lua_pushboolean(L, 1);
	return 1;
}
