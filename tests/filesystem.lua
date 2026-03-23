local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local filesystemConfig = require("tests.config").filesystem

if not filesystemConfig.enabled then
    skip("FileSystem suite", "set config.filesystem.enabled = true to run filesystem tests")
    return
end

run("FileSystem table exists", function()
    assert_table(FileSystem, "FileSystem")
end)
