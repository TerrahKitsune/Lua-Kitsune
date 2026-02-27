local helpers = require("tests.helpers")
local run = helpers.run

run("Wchar FromUtf8/ToUtf8", function()
    local wchar = Wchar.FromUtf8("hello")
    local text = Wchar.ToUtf8(wchar)
    assert(text == "hello", "Wchar.ToUtf8 mismatch")
end)
