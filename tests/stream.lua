local helpers = require("tests.helpers")
local run = helpers.run
local skip = helpers.skip

local streamConfig = require("tests.config").stream

if not streamConfig.enabled then
    skip("Stream suite", "set config.stream.enabled = true to run stream tests")
    return
end

local testDataDir = "tests/TestData"

run("Stream writes and reads data", function()
    local stream = Stream.Create()
    stream:Write("stream-data")

    local filePath = testDataDir .. "/stream.bin"
    stream:Save(filePath)

    local opened = Stream.Open(filePath)
    local value = opened:Read()
    assert(value == "stream-data", "Stream read mismatch")
end)
