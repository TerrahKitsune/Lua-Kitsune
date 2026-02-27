local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run

run("CSV.DecodeString parses rows", function()
    assert_table(CSV, "CSV")
    local csv = CSV.Create()
    local result = csv:DecodeString("Name,Value\nA,1\nB,2\n")
    assert_table(result, "CSV.DecodeString")
    assert_table(result.Rows, "CSV rows")
end)
