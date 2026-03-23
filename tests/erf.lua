local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local erfConfig = require("tests.config").erf

if not erfConfig.enabled then
    skip("ERF suite", "set config.erf.enabled = true to run ERF tests")
    return
end

run("ERF table exists", function()
    assert_table(ERF, "ERF")
end)
