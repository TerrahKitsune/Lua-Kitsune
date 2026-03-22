local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local imguiConfig = require("tests.config").imgui

run("Imgui table exists", function()
    assert_table(Imgui, "Imgui")
end)

if not imguiConfig.enabled then
    skip("Imgui suite", "set config.imgui.enabled = true to run Imgui rendering tests")
    return
end

run("Imgui.GetFontSize returns number", function()
    local size = Imgui.GetFontSize()
    assert(type(size) == "number", "Imgui.GetFontSize invalid")
end)
