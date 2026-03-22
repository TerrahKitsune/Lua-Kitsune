local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local redisConfig = require("tests.config").redis

run("Redis table exists", function()
    assert_table(Redis, "Redis")
end)

if not redisConfig.enabled then
    skip("Redis suite", "set config.redis.enabled = true to run Redis tests")
    return
end

run("Redis.Open connects", function()
    local redis = Redis.Open(redisConfig.host, redisConfig.port, redisConfig.useTls, redisConfig.timeout, redisConfig.sslOptions)
    assert(redis, "Redis.Open failed")
end)
