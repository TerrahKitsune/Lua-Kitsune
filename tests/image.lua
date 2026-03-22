local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local imageConfig = require("tests.config").image

run("Image table exists", function()
    assert_table(Image, "Image")
end)

if not imageConfig.enabled then
    skip("Image suite", "set config.image.enabled = true to run image tests")
    return
end
