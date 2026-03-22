local helpers = require("tests.helpers")
local run = helpers.run

local testDataDir = "tests/TestData"

-- ────────────────────────────────────────────────────────────
-- Encode / Decode  (basic type coverage)
-- ────────────────────────────────────────────────────────────

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

-- ────────────────────────────────────────────────────────────
-- SetNullValue  (null sentinel)
-- ────────────────────────────────────────────────────────────

run("Json null sentinel decoded from raw JSON string", function()
    local json = Json.Create()
    json:SetNullValue("__NULL__")
    local decoded = json:Decode('{"v":null}')
    assert(decoded.v == "__NULL__", "null in raw JSON should decode as sentinel, got: " .. tostring(decoded.v))
end)

run("Json SetNullValue clear restores nil for null", function()
    local json = Json.Create()
    json:SetNullValue("__NULL__")
    json:SetNullValue(nil)
    local decoded = json:Decode('{"v":null}')
    assert(decoded.v == nil, "null should decode as nil after sentinel cleared, got: " .. tostring(decoded.v))
end)

run("Json SetNullValue returns previous value", function()
    local json = Json.Create()
    local prev1 = json:SetNullValue("first")
    assert(prev1 == nil, "first SetNullValue should return nil, got: " .. tostring(prev1))
    local prev2 = json:SetNullValue("second")
    assert(prev2 == "first", "second SetNullValue should return 'first', got: " .. tostring(prev2))
    local prev3 = json:SetNullValue(nil)
    assert(prev3 == "second", "clearing should return 'second', got: " .. tostring(prev3))
end)

-- ────────────────────────────────────────────────────────────
-- Number encoding
-- ────────────────────────────────────────────────────────────

run("Json float encoding is compact", function()
    local json = Json.Create()
    local encoded = json:Encode({v = 3.5})
    assert(not encoded:find("3%.5000"), "float 3.5 should not have trailing zeros, got: " .. encoded)
    assert(json:Decode(encoded).v == 3.5, "3.5 should round-trip correctly")
end)

run("Json negative numbers round-trip", function()
    local json = Json.Create()
    local decoded = json:Decode(json:Encode({i = -42, f = -3.5}))
    assert(decoded.i == -42, "negative integer should round-trip, got: " .. tostring(decoded.i))
    assert(decoded.f == -3.5, "negative float should round-trip, got: " .. tostring(decoded.f))
end)

run("Json NaN encodes as null and decodes as nil", function()
    local json = Json.Create()
    local encoded = json:Encode({v = 0/0})
    assert(encoded:find('"v":null'), "NaN should encode as null, got: " .. encoded)
    local decoded = json:Decode(encoded)
    assert(decoded.v == nil, "NaN-as-null should decode as nil, got: " .. tostring(decoded.v))
end)

run("Json Infinity encodes as large number and decodes back", function()
    local json = Json.Create()
    local encoded = json:Encode({pos = math.huge, neg = -math.huge})
    assert(encoded:find("1e%+9999"), "positive infinity should encode as 1e+9999, got: " .. encoded)
    assert(encoded:find("%-1e%+9999"), "negative infinity should encode as -1e+9999, got: " .. encoded)
    local decoded = json:Decode(encoded)
    assert(decoded.pos == math.huge, "positive infinity should decode back, got: " .. tostring(decoded.pos))
    assert(decoded.neg == -math.huge, "negative infinity should decode back, got: " .. tostring(decoded.neg))
end)

-- ────────────────────────────────────────────────────────────
-- String encoding
-- ────────────────────────────────────────────────────────────

run("Json empty string round-trip", function()
    local json = Json.Create()
    local decoded = json:Decode(json:Encode({s = ""}))
    assert(decoded.s == "", "empty string should round-trip, got: " .. tostring(decoded.s))
end)

run("Json escape sequences in strings round-trip", function()
    local json = Json.Create()
    local s = "line1\nline2\ttab\"quote"
    local decoded = json:Decode(json:Encode({s = s}))
    assert(decoded.s == s, "escape sequences should round-trip, got: " .. tostring(decoded.s))
end)

run("Json UTF-8 string round-trip", function()
    local json = Json.Create()
    local decoded = json:Decode(json:Encode({s = "héllo wörld"}))
    assert(decoded.s == "héllo wörld", "UTF-8 string should round-trip, got: " .. tostring(decoded.s))
end)

run("Json non-ASCII strings pass through as raw UTF-8", function()
    local json = Json.Create()
    local encoded = json:Encode({s = "héllo"})
    assert(not encoded:find("\\u"), "no chars should be \\uXXXX escaped, got: " .. encoded)
    assert(json:Decode(encoded).s == "héllo", "string should round-trip correctly")
end)

run("Json decodes \\uXXXX escape sequences as UTF-8", function()
    local json = Json.Create()
    -- ASCII range
    local d1 = json:Decode('{"s":"\\u0048\\u0065\\u006c\\u006c\\u006f"}')
    assert(d1.s == "Hello", "ASCII \\uXXXX mismatch, got: " .. tostring(d1.s))
    -- Latin Extended (U+00E9 = é, UTF-8: 0xC3 0xA9)
    local d2 = json:Decode('{"s":"\\u00e9"}')
    assert(d2.s == "é", "\\u00e9 should be é, got: " .. tostring(d2.s))
    -- CJK (U+4F60 = 你, UTF-8: 0xE4 0xBD 0xA0)
    local d3 = json:Decode('{"s":"\\u4f60"}')
    assert(d3.s == "你", "\\u4f60 should be 你, got: " .. tostring(d3.s))
end)

run("Json \\u00XX decodes as Unicode code point not raw byte", function()
    local json = Json.Create()
    -- U+00C3 = Ã (Latin capital A with tilde)
    -- Old decoder: appended single byte 0xC3 (incomplete UTF-8 sequence)
    -- New decoder: code point U+00C3 -> UTF-8 0xC3 0x83 = "Ã"
    local decoded = json:Decode('{"s":"\\u00c3"}')
    assert(decoded.s == "Ã", "\\u00c3 should be Ã (U+00C3), got: " .. tostring(decoded.s))
end)

run("Json surrogate pair decodes to 4-byte UTF-8", function()
    local json = Json.Create()
    -- U+1F600 (😀) = \uD83D\uDE00 as JSON surrogate pair
    -- UTF-8: 0xF0 0x9F 0x98 0x80
    local decoded = json:Decode('{"s":"\\uD83D\\uDE00"}')
    assert(decoded.s == "😀", "surrogate pair \\uD83D\\uDE00 should decode as 😀, got: " .. tostring(decoded.s))
end)

run("Json UTF-8 strings round-trip across multiple scripts", function()
    local json = Json.Create()
    local payload = {
        latin    = "café",
        greek    = "Ελλάδα",
        cyrillic = "Привет",
        cjk      = "你好世界",
        emoji    = "😀",
        mixed    = "héllo 世界 😀",
    }
    local decoded = json:Decode(json:Encode(payload))
    for k, v in pairs(payload) do
        assert(decoded[k] == v, k .. " mismatch, got: " .. tostring(decoded[k]))
    end
end)

run("Json \\uXXXX mixed with raw text in same string", function()
    local json = Json.Create()
    -- \u006c = 'l' (U+006C), \u4f60 = '你' (U+4F60), \u597d = '好' (U+597D)
    local decoded = json:Decode('{"s":"hel\\u006co \\u4f60\\u597d"}')
    assert(decoded.s == "hello 你好", "mixed \\u and raw text mismatch, got: " .. tostring(decoded.s))
end)

run("Json control characters below U+0020 are still escaped by encoder", function()
    local json = Json.Create()
    -- \t (U+0009) has no dedicated escape so falls to \u0009
    local encoded = json:Encode({s = "\t"})
    assert(encoded:find("\\u0009"), "tab should encode as \\u0009, got: " .. encoded)
    -- \u0009 decodes back to \t
    assert(json:Decode(encoded).s == "\t", "\\u0009 should decode back to tab")
    -- full round-trip of a string with mixed control chars
    local s = "a\tb\nc"
    assert(json:Decode(json:Encode({s = s})).s == s, "control char string should round-trip")
end)

run("Json forward slash escape \\/ decodes to /", function()
    local json = Json.Create()
    local decoded = json:Decode('{"url":"http:\\/\\/example.com"}')
    assert(decoded.url == "http://example.com", "\\/ should decode to /, got: " .. tostring(decoded.url))
end)

run("Json null byte \\u0000 produces embedded null with correct length", function()
    local json = Json.Create()
    local decoded = json:Decode('{"s":"before\\u0000after"}')
    assert(#decoded.s == 12, "string should be 12 bytes (6+1+5), got: " .. tostring(#decoded.s))
    assert(decoded.s:byte(7) == 0, "byte 7 should be null, got: " .. tostring(decoded.s:byte(7)))
    assert(decoded.s:sub(1, 6) == "before" and decoded.s:sub(8) == "after",
        "content around null byte incorrect, got: " .. tostring(decoded.s:sub(1, 6)))
end)

run("Json bare quoted string decodes at root level", function()
    local json = Json.Create()
    local result = json:Decode('"hello world"')
    assert(result == "hello world", "bare quoted string should decode, got: " .. tostring(result))
end)

run("Json bare array decodes at root level", function()
    local json = Json.Create()
    local result = json:Decode('[1,2,3]')
    assert(type(result) == "table" and #result == 3, "bare array should decode, got: " .. tostring(result))
    assert(result[1] == 1 and result[2] == 2 and result[3] == 3, "array elements mismatch")
end)

run("Json trailing commas in objects and arrays are tolerated", function()
    local json = Json.Create()
    local obj = json:Decode('{"a":1,"b":2,}')
    assert(obj.a == 1 and obj.b == 2, "object with trailing comma should decode")
    local arr = json:Decode('[10,20,30,]')
    assert(#arr == 3 and arr[2] == 20, "array with trailing comma should decode")
end)

run("Json DecodeFromFunction returns error on incomplete JSON", function()
    local json = Json.Create()
    local called = 0
    local ok, err = pcall(function()
        json:DecodeFromFunction(function()
            called = called + 1
            if called == 1 then return '{"a":' end
            return nil
        end)
    end)
    assert(not ok, "incomplete JSON from function should error")
    assert(type(err) == "string" and err:find("Read end"), "error should mention read end, got: " .. tostring(err))
end)

run("Json Encode Wchar value as UTF-8 string", function()
    local json = Json.Create()
    local wstr = Wchar.FromUtf8("wchar value")
    local decoded = json:Decode(json:Encode({s = wstr}))
    assert(decoded.s == "wchar value", "Wchar should encode as UTF-8 string, got: " .. tostring(decoded.s))
end)

-- ────────────────────────────────────────────────────────────
-- Collections
-- ────────────────────────────────────────────────────────────

run("Json large array round-trips correctly", function()
    local json = Json.Create()
    local arr = {}
    for i = 1, 100 do arr[i] = i end
    local decoded = json:Decode(json:Encode({arr = arr}))
    assert(type(decoded.arr) == "table", "decoded arr should be a table")
    assert(#decoded.arr == 100, "decoded arr should have 100 elements, got: " .. tostring(#decoded.arr))
    assert(decoded.arr[1] == 1 and decoded.arr[100] == 100, "array boundary values should be correct")
end)

run("Json deeply nested table round-trips", function()
    local json = Json.Create()
    local payload = {a = {b = {c = {d = {e = "deep"}}}}}
    local decoded = json:Decode(json:Encode(payload))
    assert(decoded.a.b.c.d.e == "deep", "deeply nested value should round-trip, got: " ..
        tostring(decoded.a and decoded.a.b and decoded.a.b.c and decoded.a.b.c.d and decoded.a.b.c.d.e))
end)

run("Json Encode recursive table is detected and errors", function()
    local json = Json.Create()
    local t = {a = 1}
    t.self = t  -- circular reference
    local ok, err = pcall(function() json:Encode(t) end)
    assert(not ok, "encoding a recursive table should error")
    assert(type(err) == "string" and err:find("Recursion"), "error should mention recursion, got: " .. tostring(err))
end)

run("Json Encode respects __pairs metamethod", function()
    local json = Json.Create()
    -- __pairs overrides what gets iterated; when it returns an empty iterator,
    -- the encoder should encode nothing (pairs() is called, not lua_next)
    local t = setmetatable({x = 1, y = 2}, {
        __pairs = function() return function() end, nil, nil end
    })
    local decoded = json:Decode(json:Encode(t))
    assert(decoded.x == nil, "__pairs override should hide x, got: " .. tostring(decoded.x))
    assert(decoded.y == nil, "__pairs override should hide y, got: " .. tostring(decoded.y))
end)

run("Json Encode respects __pairs providing custom object data", function()
    local json = Json.Create()
    -- Raw table is empty but __pairs injects virtual key-value pairs
    local entries = {{"x", 42}, {"y", "hello"}, {"z", true}}
    local t = setmetatable({}, {
        __pairs = function(tbl)
            local i = 0
            return function()
                i = i + 1
                if entries[i] then return entries[i][1], entries[i][2] end
            end, tbl, nil
        end
    })
    local decoded = json:Decode(json:Encode(t))
    assert(decoded.x == 42,      "__pairs x mismatch, got: " .. tostring(decoded.x))
    assert(decoded.y == "hello",  "__pairs y mismatch, got: " .. tostring(decoded.y))
    assert(decoded.z == true,     "__pairs z mismatch, got: " .. tostring(decoded.z))
end)

run("Json Encode respects __len and __index for array encoding", function()
    local json = Json.Create()
    -- Raw table is empty; __len advertises length 3 and __index provides the values
    -- lua_len() calls __len, lua_geti() calls __index
    local t = setmetatable({}, {
        __len   = function() return 3 end,
        __index = function(_, k)
            if type(k) == "number" and k >= 1 and k <= 3 then
                return "item" .. k
            end
        end,
    })
    local decoded = json:Decode(json:Encode(t))
    assert(type(decoded) == "table", "should decode as table, got: " .. type(decoded))
    assert(#decoded == 3, "should have 3 elements, got: " .. tostring(#decoded))
    assert(decoded[1] == "item1", "item1 mismatch: " .. tostring(decoded[1]))
    assert(decoded[2] == "item2", "item2 mismatch: " .. tostring(decoded[2]))
    assert(decoded[3] == "item3", "item3 mismatch: " .. tostring(decoded[3]))
end)

-- ────────────────────────────────────────────────────────────
-- Create(pretty)
-- ────────────────────────────────────────────────────────────

run("Json Create with pretty=true adds whitespace", function()
    local json = Json.Create(true)
    local encoded = json:Encode({a = 1, b = "hello"})
    assert(encoded:find("\n") or encoded:find("\t"), "pretty output should contain newlines or tabs, got: " .. encoded)
    local decoded = json:Decode(encoded)
    assert(decoded.a == 1 and decoded.b == "hello", "pretty JSON should decode correctly")
end)

-- ────────────────────────────────────────────────────────────
-- EncodeToFile / DecodeFromFile
-- ────────────────────────────────────────────────────────────

run("Json EncodeToFile and DecodeFromFile full round-trip", function()
    local json = Json.Create()
    local filePath = testDataDir .. "/json_full_test.json"
    local payload = {num = 99, str = "file-test", arr = {10, 20, 30}, nested = {flag = true}}
    json:EncodeToFile(filePath, payload)
    local decoded = json:DecodeFromFile(filePath)
    assert(decoded.num == 99, "file num mismatch: " .. tostring(decoded.num))
    assert(decoded.str == "file-test", "file str mismatch: " .. tostring(decoded.str))
    assert(type(decoded.arr) == "table" and #decoded.arr == 3, "file arr mismatch")
    assert(decoded.arr[2] == 20, "file arr[2] mismatch: " .. tostring(decoded.arr[2]))
    assert(type(decoded.nested) == "table" and decoded.nested.flag == true, "file nested mismatch")
end)

run("Json EncodeToFile preserves null sentinel through file", function()
    local json = Json.Create()
    json:SetNullValue("__NULL__")
    local filePath = testDataDir .. "/json_null_file_test.json"
    json:EncodeToFile(filePath, {v = "__NULL__", name = "test"})
    local decoded = json:DecodeFromFile(filePath)
    assert(decoded.v == "__NULL__", "null sentinel should survive file round-trip, got: " .. tostring(decoded.v))
    assert(decoded.name == "test", "other values should survive file round-trip")
end)

-- ────────────────────────────────────────────────────────────
-- EncodeToFunction / DecodeFromFunction
-- ────────────────────────────────────────────────────────────

run("Json EncodeToFunction streams JSON to callback", function()
    local json = Json.Create()
    local chunks = {}
    json:EncodeToFunction(function(chunk) chunks[#chunks + 1] = chunk end, {a = 1, b = "hello"})
    local encoded = table.concat(chunks)
    assert(#encoded > 0, "EncodeToFunction should produce output")
    local decoded = json:Decode(encoded)
    assert(decoded.a == 1, "EncodeToFunction a mismatch: " .. tostring(decoded.a))
    assert(decoded.b == "hello", "EncodeToFunction b mismatch: " .. tostring(decoded.b))
end)

run("Json DecodeFromFunction assembles JSON from chunked reader", function()
    local json = Json.Create()
    local s = '{"x":42,"y":"world","flag":true}'
    local pos = 1
    local decoded = json:DecodeFromFunction(function()
        if pos > #s then return nil end
        local chunk = s:sub(pos, math.min(pos + 5, #s))
        pos = pos + 6
        return chunk
    end)
    assert(decoded.x == 42, "DecodeFromFunction x mismatch: " .. tostring(decoded.x))
    assert(decoded.y == "world", "DecodeFromFunction y mismatch: " .. tostring(decoded.y))
    assert(decoded.flag == true, "DecodeFromFunction flag mismatch: " .. tostring(decoded.flag))
end)

-- ────────────────────────────────────────────────────────────
-- Coroutine encode
-- ────────────────────────────────────────────────────────────

run("Json Encode flat object via coroutine", function()
    local json = Json.Create()
    local encoded = json:Encode(coroutine.create(function()
        coroutine.yield(nil, {})    -- first yield must be a table to trigger table encoding
        coroutine.yield("n", 42)
        coroutine.yield("s", "hello")
        coroutine.yield(nil, nil)
    end))
    local decoded = json:Decode(encoded)
    assert(decoded.n == 42, "coroutine n mismatch, got: " .. tostring(decoded.n))
    assert(decoded.s == "hello", "coroutine s mismatch, got: " .. tostring(decoded.s))
end)

run("Json Encode array via coroutine", function()
    local json = Json.Create()
    local encoded = json:Encode(coroutine.create(function()
        coroutine.yield(1, {})      -- integer key + table triggers array encoding
        coroutine.yield(1, "a")
        coroutine.yield(2, "b")
        coroutine.yield(3, "c")
        coroutine.yield(nil, nil)
    end))
    local decoded = json:Decode(encoded)
    assert(type(decoded) == "table", "coroutine array should decode to table, got: " .. type(decoded))
    assert(decoded[1] == "a" and decoded[2] == "b" and decoded[3] == "c",
        "coroutine array values mismatch, got: " .. tostring(encoded))
end)

-- ────────────────────────────────────────────────────────────
-- Iterator
-- ────────────────────────────────────────────────────────────

run("Json Iterator returns a coroutine and yields key-value pairs", function()
    local json = Json.Create()
    local s = '{"a":1,"b":"hello"}'
    local pos = 1
    local co = json:Iterator(function()
        if pos > 1 then return nil end
        pos = pos + 1
        return s
    end)
    assert(type(co) == "thread", "Iterator should return a coroutine, got: " .. type(co))

    local result = {}
    while true do
        local ok, key, value = coroutine.resume(co)
        if not ok or key == nil then break end
        if type(value) ~= "table" then
            result[key] = value
        end
    end
    assert(result.a == 1, "Iterator should yield a=1, got: " .. tostring(result.a))
    assert(result.b == "hello", "Iterator should yield b='hello', got: " .. tostring(result.b))
end)

-- ────────────────────────────────────────────────────────────
-- Dispose
-- ────────────────────────────────────────────────────────────

run("Json Dispose resets context and allows reuse", function()
    local json = Json.Create()
    json:SetNullValue("__NULL__")
    local before = json:Encode({v = 1})
    assert(type(before) == "string", "Encode should work before Dispose")
    json:Dispose()
    -- Dispose resets the context; the object is still usable
    local after = json:Encode({v = 2})
    assert(type(after) == "string", "Encode should work after Dispose")
    assert(json:Decode(after).v == 2, "Decode after Dispose should work")
end)
