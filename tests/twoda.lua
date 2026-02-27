local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run

run("TWODA table exists", function()
    assert_table(TWODA, "TWODA")
end)
