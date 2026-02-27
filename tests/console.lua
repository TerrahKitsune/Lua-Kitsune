local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run

run("Console table exists", function()
    assert_table(Console, "Console")
end)

run("Console functions exposed", function()
    assert(type(Console.Create) == "function", "Console.Create missing")
    assert(type(Console.Destroy) == "function", "Console.Destroy missing")
    assert(type(Console.Attach) == "function", "Console.Attach missing")
end)
