local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local servicesConfig = require("tests.config").services

run("Services table exists", function()
    assert_table(Services, "Services")
end)

if not servicesConfig.enabled then
    skip("Services suite", "set config.services.enabled = true to enumerate services")
    return
end

run("Services.All returns list", function()
    local services = Services.All()
    assert_table(services, "Services.All")
end)
