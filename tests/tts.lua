local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local enableTtsTests = false

run("TTS table exists", function()
    assert_table(TTS, "TTS")
end)

if not enableTtsTests then
    skip("TTS suite", "set enableTtsTests = true to run speech tests")
    return
end

run("TTS.Create returns instance", function()
    local tts = TTS.Create()
    assert(tts, "TTS.Create failed")
    tts:Dispose()
end)
