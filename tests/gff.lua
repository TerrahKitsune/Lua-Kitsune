local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local gffConfig = require("tests.config").gff

if not gffConfig.enabled then
    skip("GFF suite", "set config.gff.enabled = true to run GFF tests")
    return
end

run("GFF table exists", function()
    assert_table(GFF, "GFF")
end)
