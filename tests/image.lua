local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local enableImageTests = false

run("Image table exists", function()
    assert_table(Image, "Image")
end)

if not enableImageTests then
    skip("Image suite", "set enableImageTests = true to run image tests")
    return
end
