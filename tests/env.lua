local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local envConfig = require("tests.config").env

if not envConfig.enabled then
    skip("Env suite", "set config.env.enabled = true to run Env tests")
    return
end

run("Env.Create/Get", function()
    assert_table(Env, "Env")
    local env = Env.Create("test")
    assert_table(env, "Env.Create")
    local fetched = Env.Get("test")
    assert_table(fetched, "Env.Get")
end)
