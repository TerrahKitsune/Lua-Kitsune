local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local enableMacroTests = false

run("Macro table exists", function()
    assert_table(Macro, "Macro")
end)

if not enableMacroTests then
    skip("Macro suite", "set enableMacroTests = true to run input automation tests")
    return
end

run("Macro.Create returns object", function()
    local macro = Macro.Create({})
    assert(macro, "Macro.Create failed")
end)
