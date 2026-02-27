local helpers = require("tests.helpers")
local run = helpers.run

run("Base64 encode/decode", function()
    local encoded = Base64.Encode("hello")
    assert(type(encoded) == "string", "Base64.Encode failed")
    local decoded = Base64.Decode(encoded)
    assert(decoded == "hello", "Base64.Decode mismatch")
end)
