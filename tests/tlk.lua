local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local tlkConfig = require("tests.config").tlk

if not tlkConfig.enabled then
    skip("TLK suite", "set config.tlk.enabled = true to run TLK tests")
    return
end

run("TLK table exists", function()
    assert_table(TLK, "TLK")
end)
