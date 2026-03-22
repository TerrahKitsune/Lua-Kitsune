local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local ttsConfig = require("tests.config").tts

run("TTS table exists", function()
    assert_table(TTS, "TTS")
end)

if not ttsConfig.enabled then
    skip("TTS suite", "set config.tts.enabled = true to run speech tests")
    return
end

run("TTS.Create returns instance", function()
    local tts = TTS.Create()
    assert(tts, "TTS.Create failed")
    tts:Dispose()
end)
