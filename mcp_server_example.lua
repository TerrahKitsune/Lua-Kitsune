-- Minimal MCP (Model Context Protocol) server example.
--
-- Run with:  kitsune mcp_server_example.lua
-- Register with an MCP client (e.g. Claude Code) by pointing it at this
-- script and the built Kitsune.exe, communicating over stdin/stdout.

local mcp = MCP.Create(
    { Name = "kitsune-lua", Version = VERSION or "1.0.0" }, -- settings
    { logPath = "mcp_server_example.log" }                  -- context: shared state, passed to every tool call
)

mcp:AddTool(
    "log",
    "Appends a line to the server's log file. Usage: log(text)",
    { { name = "text", type = "string", description = "Text to log", required = true } },
    function(context, request)
        local f = io.open(context.logPath, "a")
        f:write(tostring(request.Parameters[1]) .. "\n")
        f:close()
        return "OK"
    end
)

mcp:AddTool(
    "run_lua",
    "Executes a snippet of Lua code against this engine and returns its result. " ..
    "The full Kitsune API (Redis, HttpClient, SQLite, Stream, Json, ...) is available.",
    { { name = "code", type = "string", description = "Lua source; the value returned becomes the tool result", required = true } },
    function(context, request)
        local fn, err = load(request.Arguments.code, "=run_lua", "t", _G)
        if not fn then
            error("compile error: " .. tostring(err))
        end
        local result = fn()
        return tostring(result)
    end
)

local ok, err = mcp:Start()
assert(ok, err)

while mcp:IsRunning() do
    Sleep(20)
end
