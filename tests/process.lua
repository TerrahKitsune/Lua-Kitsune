local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local processConfig = require("tests.config").process

run("Process table exists", function()
    assert_table(Process, "Process")
end)

if not processConfig.enabled then
    skip("Process suite", "set config.process.enabled = true to run process tests")
    return
end

run("Process.All returns table", function()
    local processes = Process.All()
    assert_table(processes, "Process.All")
end)
