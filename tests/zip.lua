local helpers = require("tests.helpers")
local run = helpers.run
local skip = helpers.skip

local zipConfig = require("tests.config").zip

if not zipConfig.enabled then
    skip("Zip suite", "set config.zip.enabled = true to run Zip tests")
    return
end

local testDataDir = "tests/TestData"

if type(Zip) ~= "table" then
    skip("Zip suite", "Zip module not available")
    return
end

run("Zip add and extract", function()
    local zipPath = testDataDir .. "/test.zip"
    local zip = Zip.Open(zipPath)
    assert(zip, "Zip.Open failed")
    local index = zip:AddData("hello.txt", "world")
    assert(index, "Zip.AddData failed")
    local data = zip:Extract("hello.txt")
    assert(data == "world", "Zip.Extract mismatch")
    zip:Close()
end)
