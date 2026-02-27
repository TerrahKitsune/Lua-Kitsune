local helpers = require("tests.helpers")
local run = helpers.run

run("SQLite query", function()
    local db = SQLite.Open()
    assert(db, "SQLite.Open failed")
    local ok = db:Query("select 1")
    assert(ok, "SQLite query failed")
end)
