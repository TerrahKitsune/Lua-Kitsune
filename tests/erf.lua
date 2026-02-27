local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run

run("ERF table exists", function()
    assert_table(ERF, "ERF")
end)
