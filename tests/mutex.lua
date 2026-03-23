local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local mutexConfig = require("tests.config").mutex

if not mutexConfig.enabled then
    skip("Mutex suite", "set config.mutex.enabled = true to run mutex tests")
    return
end

run("Mutex open/lock/unlock", function()
    assert_table(Mutex, "Mutex")
    local mutex = Mutex.Open("KitsuneTestMutex")
    assert(mutex, "Mutex.Open failed")
    assert(mutex:Lock(10), "Mutex:Lock failed")
    mutex:Unlock()
end)
