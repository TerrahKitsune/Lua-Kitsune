local tests = {
    "tests.utils",
    "tests.console",
    "tests.mutex",
    "tests.macro",
    "tests.redis",
    "tests.csv",
    "tests.kafka",
    "tests.fileasync",
    "tests.ftp",
    "tests.sound",
    "tests.odbc",
    "tests.archive",
    "tests.stream",
    "tests.tts",
    "tests.env",
    "tests.zip",
    "tests.server",
    "tests.client",
    "tests.pipe",
    "tests.base64",
    "tests.services",
    "tests.aes",
    "tests.process",
    "tests.imgui",
    "tests.http",
    "tests.hashing",
    "tests.mysql",
    "tests.postgres",
    "tests.timer",
    "tests.sqlite",
    "tests.image",
    "tests.json",
    "tests.wchar",
    "tests.filesystem",
    "tests.twoda",
    "tests.tlk",
    "tests.erf",
    "tests.gff"
}

local failures = 0

for _, moduleName in ipairs(tests) do
    local ok, err = pcall(require, moduleName)
    if not ok then
        failures = failures + 1
        print("[FAIL]", moduleName, err)
    end
end

if failures > 0 then
    error(string.format("%d test module(s) failed", failures))
end
