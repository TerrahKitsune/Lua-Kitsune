local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run

run("TLK table exists", function()
    assert_table(TLK, "TLK")
end)
