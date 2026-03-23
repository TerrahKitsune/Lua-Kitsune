local helpers = require("tests.helpers")
local run = helpers.run
local skip = helpers.skip

local sqliteConfig = require("tests.config").sqlite

if not sqliteConfig.enabled then
    skip("SQLite suite", "set config.sqlite.enabled = true to run SQLite tests")
    return
end

run("SQLite query", function()
    local db = SQLite.Open()
    assert(db, "SQLite.Open failed")
    local ok = db:Query("select 1")
    assert(ok, "SQLite query failed")
end)
