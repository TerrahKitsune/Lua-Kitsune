local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run

run("Archive table exists", function()
    assert_table(Archive, "Archive")
end)
