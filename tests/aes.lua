local helpers = require("tests.helpers")
local run = helpers.run

run("Aes encrypt/decrypt", function()
    local aes = Aes.Create("0123456789abcdef0123456789abcdef")
    local encrypted = aes:Encrypt("secret")
    local decrypted = aes:Decrypt(encrypted)
    assert(decrypted == "secret", "Aes decrypt mismatch")
end)
