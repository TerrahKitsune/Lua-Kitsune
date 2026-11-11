#pragma once
#include "mcp/LuaMCP.h"

// True when `address` ("host:port", optionally prefixed with http://) names a
// loopback host the HTTP transport may bind to.
bool McpHttpIsLoopbackAddress(const char* address);

// mcp:Start() for an HTTP instance (the server userdata is at index 1) -> ok [, errmsg]
int McpHttpStart(lua_State* L, LuaMcpServer* server);
