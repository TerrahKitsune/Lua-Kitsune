local helpers = require("tests.helpers")
local run = helpers.run
local skip = helpers.skip

local base64Config = require("tests.config").base64

if not base64Config.enabled then
    skip("Base64 suite", "set config.base64.enabled = true to run Base64 tests")
    return
end

run("Base64 encode/decode", function()
    local encoded = Base64.Encode("hello")
    assert(type(encoded) == "string", "Base64.Encode failed")
    local decoded = Base64.Decode(encoded)
    assert(decoded == "hello", "Base64.Decode mismatch")
end)
