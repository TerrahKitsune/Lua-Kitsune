local helpers = require("tests.helpers")
local run = helpers.run
local skip = helpers.skip

local aesConfig = require("tests.config").aes

if not aesConfig.enabled then
    skip("Aes suite", "set config.aes.enabled = true to run AES tests")
    return
end

run("Aes encrypt/decrypt", function()
    local aes = Aes.Create("0123456789abcdef0123456789abcdef")
    local encrypted = aes:Encrypt("secret")
    local decrypted = aes:Decrypt(encrypted)
    assert(decrypted == "secret", "Aes decrypt mismatch")
end)
