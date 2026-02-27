local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local enableImguiTests = false

run("Imgui table exists", function()
    assert_table(Imgui, "Imgui")
end)

if not enableImguiTests then
    skip("Imgui suite", "set enableImguiTests = true to run Imgui rendering tests")
    return
end

run("Imgui.GetFontSize returns number", function()
    local size = Imgui.GetFontSize()
    assert(type(size) == "number", "Imgui.GetFontSize invalid")
end)
