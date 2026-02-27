local helpers = require("tests.helpers")
local run = helpers.run

local testDataDir = "tests/TestData"

run("Json encode/decode", function()
    local json = Json.Create()

    local payload = {
        stringValue = "hello",
        numberValue = 42,
        floatValue = 3.14,
        boolTrue = true,
        boolFalse = false,
        arrayValue = { 1, 2, 3 },
        objectValue = { nested = "value" },
        emptyObject = {},
        emptyArray = {},
    }

    local encoded = json:Encode(payload)
    local decoded = json:Decode(encoded)

    assert(decoded.stringValue == "hello", "Json string mismatch")
    assert(decoded.numberValue == 42, "Json number mismatch")
    assert(decoded.floatValue == 3.14, "Json float mismatch")
    assert(decoded.boolTrue == true, "Json bool true mismatch")
    assert(decoded.boolFalse == false, "Json bool false mismatch")
    assert(type(decoded.arrayValue) == "table" and #decoded.arrayValue == 3, "Json array mismatch")
    assert(type(decoded.objectValue) == "table" and decoded.objectValue.nested == "value", "Json object mismatch")

    json:SetNullValue("__NULL__")
    local withNull = { value = "__NULL__" }
    local nullEncoded = json:Encode(withNull)
    local nullDecoded = json:Decode(nullEncoded)
    assert(nullDecoded.value == "__NULL__", "Json null value mismatch")

    local filePath = testDataDir .. "/json_test.json"
    json:EncodeToFile(filePath, payload)
    local fileDecoded = json:DecodeFromFile(filePath)
    assert(fileDecoded.stringValue == "hello", "Json file decode mismatch")
end)
