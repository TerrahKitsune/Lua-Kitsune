local helpers = require("tests.helpers")
local run = helpers.run
local skip = helpers.skip

local hashingConfig = require("tests.config").hashing

if not hashingConfig.enabled then
    skip("Hashing suite", "set config.hashing.enabled = true to run hashing tests")
    return
end

local function read_file(path)
    local file = io.open(path, "rb")
    assert(file, "Unable to open " .. path)
    local data = file:read("*a")
    file:close()
    return data
end

local function to_uint64(value)
    if value < 0 then
        value = value + 0x10000000000000000
    end
    return value
end

run("SHA1 hash", function()
    local sha1 = SHA1.New()
    SHA1.Update(sha1, "hello")
    local hex = SHA1.Finish(sha1)
    assert(type(hex) == "string" and #hex > 0, "SHA1.Finish failed")
end)

run("SHA256 hash", function()
    local sha = SHA256.New()
    SHA256.Update(sha, "hello")
    local hex = SHA256.Finish(sha)
    assert(type(hex) == "string" and #hex > 0, "SHA256.Finish failed")
end)

run("MD5 hash", function()
    local md5 = MD5.New()
    MD5.Update(md5, "hello")
    local hex = MD5.Finish(md5)
    assert(type(hex) == "string" and #hex > 0, "MD5.Finish failed")
end)

run("CRC64 '123'", function()
    local crc64 = to_uint64(CRC64("123"))
    assert(string.format("%016X", crc64) == "4001B32000000000", "CRC64 '123' mismatch")
end)

run("Hash data.txt", function()
    local data = read_file("tests/data.txt")

    local sha256 = SHA256.New()
    SHA256.Update(sha256, data)
    local sha256Hex = SHA256.Finish(sha256)

    local sha1 = SHA1.New()
    SHA1.Update(sha1, data)
    local sha1Hex = SHA1.Finish(sha1)

    local md5 = MD5.New()
    MD5.Update(md5, data)
    local md5Hex = MD5.Finish(md5)

    local crc32 = CRC32(data)

    assert(string.lower(sha256Hex) == "a665a45920422f9d417e4867efdc4fb8a04a1f3fff1fa07e998e86f7f7a27ae3", "SHA256 mismatch")
    assert(string.lower(sha1Hex) == "40bd001563085fc35165329ea1ff5c5ecbdbbeef", "SHA1 mismatch")
    assert(string.upper(md5Hex) == "202CB962AC59075B964B07152D234B70", "MD5 mismatch")
    assert(string.format("%08X", crc32) == "884863D2", "CRC32 mismatch")
end)
