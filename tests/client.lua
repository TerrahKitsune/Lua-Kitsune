local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local clientConfig = {
    enabled = false,
    host = "127.0.0.1",
    port = 5050
}

run("Client table exists", function()
    assert_table(Client, "Client")
end)

if not clientConfig.enabled then
    skip("Client suite", "set clientConfig.enabled = true to run client tests")
    return
end

run("Client.Connect returns instance", function()
    local client = Client.Connect(clientConfig.host, clientConfig.port)
    assert(client, "Client.Connect failed")
    client:Disconnect()
end)
