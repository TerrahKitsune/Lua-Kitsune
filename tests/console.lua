local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local consoleConfig = require("tests.config").console

if not consoleConfig.enabled then
    skip("Console suite", "set config.console.enabled = true to run console tests")
    return
end

run("Console table exists", function()
    assert_table(Console, "Console")
end)

run("Console functions exposed", function()
    assert(type(Console.Create) == "function", "Console.Create missing")
    assert(type(Console.Destroy) == "function", "Console.Destroy missing")
    assert(type(Console.Attach) == "function", "Console.Attach missing")
end)
