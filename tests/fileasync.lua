local helpers = require("tests.helpers")
local run = helpers.run

local testDataDir = "tests/TestData"

run("FileAsync reads file", function()
    local filePath = testDataDir .. "/fileasync.txt"
    local file = io.open(filePath, "w")
    assert(file, "Unable to open test file")
    file:write("fileasync")
    file:close()

    local asyncFile = FileAsync.Open(filePath, "rb", 4096)
    assert(asyncFile, "FileAsync.Open failed")
    asyncFile:Read(0, 1024)

    local collected = ""
    while true do
        local chunk, hasMore = asyncFile:EmptyBuffer()
        if chunk and #chunk > 0 then
            collected = collected .. chunk
        end

        local busy = asyncFile:Busy(false)
        if not busy and not hasMore then
            break
        end

        if busy and (not chunk or #chunk == 0) then
            Sleep(1)
        end
    end

    asyncFile:Close()
    assert(type(collected) == "string", "FileAsync returned non-string data")
end)
