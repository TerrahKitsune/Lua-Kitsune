local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local csvConfig = require("tests.config").csv

if not csvConfig.enabled then
    skip("CSV suite", "set config.csv.enabled = true to run CSV tests")
    return
end

run("CSV.DecodeString parses rows", function()
    assert_table(CSV, "CSV")
    local csv = CSV.Create()
    local result = csv:DecodeString("Name,Value\nA,1\nB,2\n")
    assert_table(result, "CSV.DecodeString")
    assert_table(result.Rows, "CSV rows")
end)
