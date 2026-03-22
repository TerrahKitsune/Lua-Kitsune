local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local soundConfig = require("tests.config").sound

run("Sound table exists", function()
    assert_table(Sound, "Sound")
end)

if not soundConfig.enabled then
    skip("Sound suite", "set config.sound.enabled = true to run sound playback tests")
    return
end

run("Sound.Beep returns boolean", function()
    local ok = Sound.Beep(880, 100)
    assert(type(ok) == "boolean", "Sound.Beep did not return boolean")
end)
