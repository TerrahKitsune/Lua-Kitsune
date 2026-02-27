local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run

run("FileSystem table exists", function()
    assert_table(FileSystem, "FileSystem")
end)
