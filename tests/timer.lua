local helpers = require("tests.helpers")
local run = helpers.run

run("Timer basic usage", function()
    local timer = Timer.New()
    Timer.Start(timer)
    Timer.Stop(timer)
    local elapsed = Timer.Elapsed(timer)
    assert(type(elapsed) == "number", "Timer.Elapsed invalid")
end)
