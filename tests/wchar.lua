local helpers = require("tests.helpers")
local run = helpers.run
local skip = helpers.skip

local wcharConfig = require("tests.config").wchar

if not wcharConfig.enabled then
    skip("Wchar suite", "set config.wchar.enabled = true to run Wchar tests")
    return
end

run("Wchar FromUtf8/ToUtf8", function()
    local wchar = Wchar.FromUtf8("hello")
    local text = Wchar.ToUtf8(wchar)
    assert(text == "hello", "Wchar.ToUtf8 mismatch")
end)
