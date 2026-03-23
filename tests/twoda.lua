local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local twodaConfig = require("tests.config").twoda

if not twodaConfig.enabled then
    skip("Twoda suite", "set config.twoda.enabled = true to run 2DA tests")
    return
end

run("TWODA table exists", function()
    assert_table(TWODA, "TWODA")
end)
