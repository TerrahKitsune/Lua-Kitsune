local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run

run("Env.Create/Get", function()
    assert_table(Env, "Env")
    local env = Env.Create("test")
    assert_table(env, "Env.Create")
    local fetched = Env.Get("test")
    assert_table(fetched, "Env.Get")
end)
