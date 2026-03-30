local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local consoleConfig = require("tests.config").console

if not consoleConfig.enabled then
    skip("Console suite", "set config.console.enabled = true to run console tests")
    return
end

run("Session.Console table exists", function()
    assert_table(Session, "Session")
    assert_table(Session.Console, "Session.Console")
end)

run("Session.Console functions exposed", function()
    assert(type(Session.Console.Create) == "function", "Session.Console.Create missing")
    assert(type(Session.Console.Destroy) == "function", "Session.Console.Destroy missing")
    assert(type(Session.Console.Attach) == "function", "Session.Console.Attach missing")
end)
