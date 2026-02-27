local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run

run("GFF table exists", function()
    assert_table(GFF, "GFF")
end)
