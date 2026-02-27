local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run

run("KeyBif table exists", function()
    assert_table(KeyBif, "KeyBif")
end)
