local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local archiveConfig = require("tests.config").archive

if not archiveConfig.enabled then
    skip("Archive suite", "set config.archive.enabled = true to run archive tests")
    return
end

run("Archive table exists", function()
    assert_table(Archive, "Archive")
end)
