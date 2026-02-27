local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run

run("Mutex open/lock/unlock", function()
    assert_table(Mutex, "Mutex")
    local mutex = Mutex.Open("KitsuneTestMutex")
    assert(mutex, "Mutex.Open failed")
    assert(mutex:Lock(10), "Mutex:Lock failed")
    mutex:Unlock()
end)
