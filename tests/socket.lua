local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local socketConfig = {
    enabled = false,
    host = "127.0.0.1",
    port = 5051
}

run("Socket table exists", function()
    assert_table(Socket, "Socket")
end)

if not socketConfig.enabled then
    skip("Socket suite", "set socketConfig.enabled = true to run socket tests")
    return
end

run("Socket.Connect returns instance", function()
    local socket = Socket.Connect(socketConfig.host, socketConfig.port)
    assert(socket, "Socket.Connect failed")
    socket:Close()
end)
