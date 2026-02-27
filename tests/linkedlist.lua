local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run

run("LinkedList add and count", function()
    assert_table(LinkedList, "LinkedList")
    local list = LinkedList.New()
    list:AddFirst("first")
    list:AddLast("last")
    local count = list:Count()
    assert(type(count) == "number" and count >= 2, "LinkedList.Count mismatch")
end)
