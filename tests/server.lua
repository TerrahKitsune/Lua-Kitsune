local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local serverConfig = require("tests.config").server

run("Server table exists", function()
    assert_table(Server, "Server")
end)

if not serverConfig.enabled then
    skip("Server suite", "set config.server.enabled = true to run server tests")
    return
end

run("Server.Start returns instance", function()
    local server = Server.Start(serverConfig.port)
    assert(server, "Server.Start failed")
    server:Stop()
end)
