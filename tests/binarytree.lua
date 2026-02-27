local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run

run("BinaryTree add/get/delete", function()
    assert_table(BinaryTree, "BinaryTree")
    local tree = BinaryTree.Create()
    assert(tree:Add(1, "one"), "BinaryTree.Add failed")
    local value = tree:Get(1)
    assert(value == "one", "BinaryTree.Get mismatch")
    assert(tree:Delete(1), "BinaryTree.Delete failed")
end)
