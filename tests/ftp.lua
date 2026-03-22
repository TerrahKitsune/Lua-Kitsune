local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local ftpConfig = require("tests.config").ftp

run("FTP table exists", function()
    assert_table(FTP, "FTP")
end)

if not ftpConfig.enabled then
    skip("FTP suite", "set config.ftp.enabled = true to run FTP tests")
    return
end

run("FTP.Open connects", function()
    local ftp = FTP.Open(ftpConfig.host, ftpConfig.port)
    assert(ftp, "FTP.Open failed")
end)
