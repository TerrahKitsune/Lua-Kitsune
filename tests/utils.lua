local helpers = require("tests.helpers")
local assert_type = helpers.assert_type
local assert_boolean = helpers.assert_boolean
local assert_table = helpers.assert_table
local assert_string_or_wchar = helpers.assert_string_or_wchar
local run = helpers.run
local skip = helpers.skip

local utilsConfig = require("tests.config").utils

if not utilsConfig.enabled then
    skip("Utils suite", "set config.utils.enabled = true to run utils tests")
    return
end

run("GetIsAdmin returns boolean", function()
    assert_boolean(GetIsAdmin(), "GetIsAdmin")
end)

run("c table includes LF", function()
    assert_table(c, "c")
    assert(c.LF == "\n", "c.LF expected newline")
end)

run("ResList table exists", function()
    assert_table(ResList, "ResList")
end)

run("Clipboard functions return expected types", function()
    assert_boolean(SetClipboard("kitsune-test"), "SetClipboard")
    local value = GetClipboard()
    assert_string_or_wchar(value, "GetClipboard")
end)

run("BencodeDecode works for a simple value", function()
    local result = BencodeDecode("d3:cow3:moo4:spam4:eggse")
    assert_table(result, "BencodeDecode")
    local dictionary = result[1]
    assert_table(dictionary, "BencodeDecode[1]")
    assert(dictionary.cow == "moo", "BencodeDecode cow value mismatch")
    assert(dictionary.spam == "eggs", "BencodeDecode spam value mismatch")
end)

run("ARGS is an array", function()
    assert_table(ARGS, "ARGS")
    for i = 1, #ARGS do
        assert(ARGS[i] ~= nil, "ARGS contains nil at index " .. i)
    end
end)
