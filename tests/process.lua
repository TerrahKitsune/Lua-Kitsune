local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local enableProcessTests = false

run("Process table exists", function()
    assert_table(Process, "Process")
end)

if not enableProcessTests then
    skip("Process suite", "set enableProcessTests = true to run process tests")
    return
end

run("Process.All returns table", function()
    local processes = Process.All()
    assert_table(processes, "Process.All")
end)
