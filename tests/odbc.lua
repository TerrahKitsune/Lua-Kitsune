local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local odbcConfig = require("tests.config").odbc

run("ODBC table exists", function()
    assert_table(ODBC, "ODBC")
end)

if not odbcConfig.enabled then
    skip("ODBC suite", "set config.odbc.enabled = true to run ODBC tests")
    return
end

run("ODBC.DriverConnect connects", function()
    local db = ODBC.DriverConnect(odbcConfig.connectionString)
    assert(db, "ODBC.DriverConnect failed")
end)
