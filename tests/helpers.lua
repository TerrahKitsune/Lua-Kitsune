local function assert_type(value, expected, name)
    local actual = type(value)
    assert(actual == expected, string.format("%s expected %s, got %s", name, expected, actual))
end

local function assert_boolean(value, name)
    assert_type(value, "boolean", name)
end

local function assert_table(value, name)
    assert_type(value, "table", name)
end

local function assert_string_or_wchar(value, name)
    local valueType = type(value)
    if valueType ~= "string" and valueType ~= "userdata" and value ~= nil then
        assert(false, string.format("%s expected string, wchar, or nil", name))
    end
end

local function run(name, func)
    local ok, err = pcall(func)
    if ok then
        print("[PASS]", name)
    else
        print("[FAIL]", name, err)
    end
end

local function skip(name, reason)
    if reason and #reason > 0 then
        print("[SKIP]", name, reason)
    else
        print("[SKIP]", name)
    end
end

return {
    assert_type = assert_type,
    assert_boolean = assert_boolean,
    assert_table = assert_table,
    assert_string_or_wchar = assert_string_or_wchar,
    run = run,
    skip = skip
}
