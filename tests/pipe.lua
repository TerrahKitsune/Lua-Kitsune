local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local pipeConfig = require("tests.config").pipe

run("Pipe table exists", function()
    assert_table(Pipe, "Pipe")
end)

if not pipeConfig.enabled then
    skip("Pipe suite", "set config.pipe.enabled = true to run pipe tests")
    return
end

run("Pipe.Create returns instance", function()
    local pipe = Pipe.Create(pipeConfig.name)
    assert(pipe, "Pipe.Create failed")
    pipe:Close()
end)
