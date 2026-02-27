local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local enableServiceTests = false

run("Services table exists", function()
    assert_table(Services, "Services")
end)

if not enableServiceTests then
    skip("Services suite", "set enableServiceTests = true to enumerate services")
    return
end

run("Services.All returns list", function()
    local services = Services.All()
    assert_table(services, "Services.All")
end)
