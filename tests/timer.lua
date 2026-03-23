local helpers = require("tests.helpers")
local run = helpers.run
local skip = helpers.skip

local timerConfig = require("tests.config").timer

if not timerConfig.enabled then
    skip("Timer suite", "set config.timer.enabled = true to run timer tests")
    return
end

run("Timer basic usage", function()
    local timer = Timer.New()
    Timer.Start(timer)
    Timer.Stop(timer)
    local elapsed = Timer.Elapsed(timer)
    assert(type(elapsed) == "number", "Timer.Elapsed invalid")
end)
