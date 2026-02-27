local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local mysqlConfig = {
    enabled = false,
    host = "127.0.0.1",
    port = 3306,
    user = "root",
    password = "",
    database = ""
}

run("MySQL table exists", function()
    assert_table(MySQL, "MySQL")
end)

if not mysqlConfig.enabled then
    skip("MySQL suite", "set mysqlConfig.enabled = true to run MySQL tests")
    return
end

run("MySQL.Connect returns instance", function()
    local mysql = MySQL.Connect(mysqlConfig.host, mysqlConfig.user, mysqlConfig.password, mysqlConfig.database, mysqlConfig.port)
    assert(mysql, "MySQL.Connect failed")
end)
