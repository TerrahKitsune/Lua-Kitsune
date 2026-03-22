local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local httpConfig = require("tests.config").http

run("Http table exists", function()
    assert_table(Http, "Http")
end)

if not httpConfig.enabled then
    skip("HTTP suite", "set config.http.enabled = true to run HTTP tests")
    return
end
