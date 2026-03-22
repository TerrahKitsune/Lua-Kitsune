local helpers = require("tests.helpers")
local assert_type = helpers.assert_type
local assert_boolean = helpers.assert_boolean
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local mysqlConfig = require("tests.config").mysql

run("MySQL table exists", function()
    assert_table(MySQL, "MySQL")
end)

if not mysqlConfig.enabled then
    skip("MySQL suite", "set mysqlConfig.enabled = true to run MySQL tests")
    return
end

local mysql

run("MySQL.Connect returns instance", function()
    mysql = MySQL.Connect(mysqlConfig.host, mysqlConfig.user, mysqlConfig.password, mysqlConfig.database, mysqlConfig.port)
    assert(mysql, "MySQL.Connect returned nil")
    assert_type(mysql, "userdata", "MySQL connection")
end)

if not mysql then
    skip("MySQL suite", "connection failed, skipping remaining tests")
    return
end

run("MySQL tostring", function()
    local s = tostring(mysql)
    assert(type(s) == "string" and s:find("MySQL:"), "unexpected format: " .. tostring(s))
end)

run("MySQL.IsBusy returns false when idle", function()
    assert_boolean(mysql:IsBusy(), "IsBusy")
    assert(mysql:IsBusy() == false, "expected false when idle")
end)

-- Setup

run("MySQL.Query DROP TABLE pre-cleanup", function()
    local ok, err = mysql:Query("DROP TABLE IF EXISTS kitsune_test")
    assert(ok, "Query failed: " .. tostring(err))
    mysql:Fetch()
end)

run("MySQL.Query CREATE TABLE", function()
    local ok, err = mysql:Query("CREATE TABLE kitsune_test (id INT AUTO_INCREMENT PRIMARY KEY, name VARCHAR(255), value DOUBLE, flag TINYINT(1), num INT) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4")
    assert(ok, "Query failed: " .. tostring(err))
    local more, ferr = mysql:Fetch()
    assert(not ferr, "CREATE TABLE error: " .. tostring(ferr))
end)

run("MySQL.Query INSERT", function()
    local ok, err = mysql:Query("INSERT INTO kitsune_test (name, value, flag, num) VALUES ('hello', 3.14, 1, 42), ('world', 2.71, 0, 100)")
    assert(ok, "Query failed: " .. tostring(err))
    local more, ferr = mysql:Fetch()
    assert(not ferr, "INSERT error: " .. tostring(ferr))
end)

-- Fetch + GetRow

run("MySQL Fetch iterates rows with named columns", function()
    local ok, err = mysql:Query("SELECT id, name, value, flag, num FROM kitsune_test ORDER BY id")
    assert(ok, "Query failed: " .. tostring(err))

    assert(mysql:Fetch(), "expected first row")
    local row = mysql:GetRow()
    assert_table(row, "row")
    assert(row.name == "hello", "expected name='hello', got: " .. tostring(row.name))
    assert(type(row.value) == "number", "expected value to be number, got: " .. type(row.value))
    assert(row.flag == 1, "expected flag=1, got: " .. tostring(row.flag))
    assert(row.num == 42, "expected num=42, got: " .. tostring(row.num))
    assert(type(row.id) == "number", "id should be integer")

    assert(mysql:Fetch(), "expected second row")
    local row2 = mysql:GetRow()
    assert(row2.name == "world", "expected 'world', got: " .. tostring(row2.name))

    assert(mysql:Fetch() == false, "expected no more rows")
end)

run("MySQL GetRow with column index", function()
    local ok = mysql:Query("SELECT id, name, num FROM kitsune_test WHERE name = 'hello'")
    assert(ok, "Query failed")
    assert(mysql:Fetch(), "expected a row")
    local name = mysql:GetRow(2)
    assert(name == "hello", "expected 'hello', got: " .. tostring(name))
    local num = mysql:GetRow(3)
    assert(num == 42, "expected 42, got: " .. tostring(num))
    mysql:Finish()
end)

run("MySQL GetRow with field name", function()
    local ok = mysql:Query("SELECT id, name, value, flag, num FROM kitsune_test WHERE name = 'hello'")
    assert(ok, "Query failed")
    assert(mysql:Fetch(), "expected a row")
    assert(mysql:GetRow("name") == "hello", "expected 'hello' by field name")
    assert(mysql:GetRow("num") == 42, "expected 42 by field name")
    assert(type(mysql:GetRow("id")) == "number", "id should be integer by field name")
    assert(mysql:GetRow("flag") == 1, "expected flag=1 by field name")
    assert(type(mysql:GetRow("value")) == "number", "value should be number by field name")
    assert(mysql:GetRow("nonexistent") == nil, "nonexistent field should return nil")
    mysql:Finish()
end)

run("MySQL Fetch loop collects all rows", function()
    local ok = mysql:Query("SELECT name FROM kitsune_test ORDER BY id")
    assert(ok, "Query failed")
    local names = {}
    while mysql:Fetch() do
        local row = mysql:GetRow()
        names[#names + 1] = row.name
    end
    assert(#names >= 2, "expected at least 2 names, got " .. tostring(#names))
    assert(names[1] == "hello", "first should be hello")
    assert(names[2] == "world", "second should be world")
end)

-- Parameterised queries

run("MySQL Query with params table", function()
    local ok, err = mysql:Query("SELECT name, num FROM kitsune_test WHERE name = ?", {"hello"})
    assert(ok, "Query failed: " .. tostring(err))
    assert(mysql:Fetch(), "expected a row")
    local row = mysql:GetRow()
    assert(row.name == "hello", "expected 'hello', got: " .. tostring(row.name))
    assert(row.num == 42, "expected 42, got: " .. tostring(row.num))
    mysql:Finish()
end)

run("MySQL Query with multiple params", function()
    local ok, err = mysql:Query("SELECT name FROM kitsune_test WHERE name = ? OR num = ? ORDER BY id", {"world", 42})
    assert(ok, "Query failed: " .. tostring(err))
    local count = 0
    while mysql:Fetch() do
        count = count + 1
        mysql:GetRow()
    end
    assert(count == 2, "expected 2 rows, got " .. tostring(count))
end)

run("MySQL Query with number param", function()
    local ok = mysql:Query("SELECT name FROM kitsune_test WHERE num = ?", {42})
    assert(ok, "Query failed")
    assert(mysql:Fetch(), "expected a row")
    local row = mysql:GetRow()
    assert(row.name == "hello", "expected 'hello', got: " .. tostring(row.name))
    mysql:Finish()
end)

run("MySQL Query with nil param (SQL NULL)", function()
    local ok, err = mysql:Query("INSERT INTO kitsune_test (name, value, flag, num) VALUES (?, ?, ?, ?)", {"nulltest", nil, nil, nil})
    assert(ok, "Query failed: " .. tostring(err))
    local more, ferr = mysql:Fetch()
    assert(not ferr, "INSERT with nulls error: " .. tostring(ferr))
end)

run("MySQL Query with table param is JSON-encoded", function()
    local ok, err = mysql:Query(
        "INSERT INTO kitsune_test (name, num) VALUES (?, ?)",
        {{key = "value", count = 7}, 777}
    )
    assert(ok, "Query failed: " .. tostring(err))
    local more, ferr = mysql:Fetch()
    assert(not ferr, "INSERT with table param error: " .. tostring(ferr))

    mysql:Query("SELECT name FROM kitsune_test WHERE num = ?", {777})
    assert(mysql:Fetch(), "expected a row")
    local name = mysql:GetRow("name")
    assert(type(name) == "string", "expected string from JSON-encoded table, got: " .. type(name))
    assert(name:sub(1, 1) == "{", "expected JSON object string, got: " .. name)
    local decoded = Json.Create():Decode(name)
    assert(decoded ~= nil, "expected valid JSON, got: " .. tostring(name))
    assert(decoded.key == "value", "expected key='value', got: " .. tostring(decoded and decoded.key))
    assert(decoded.count == 7, "expected count=7, got: " .. tostring(decoded and decoded.count))
    mysql:Finish()
end)

run("MySQL Query table param JSON serialization matches Postgres behaviour", function()
    local json = Json.Create()

    -- Array table
    local ok, err = mysql:Query("INSERT INTO kitsune_test (name, num) VALUES (?, ?)", {{10, 20, 30}, 901})
    assert(ok, "array INSERT failed: " .. tostring(err))
    mysql:Fetch()

    mysql:Query("SELECT name FROM kitsune_test WHERE num = ?", {901})
    assert(mysql:Fetch(), "expected array row")
    local arrJson = mysql:GetRow("name")
    local arr = json:Decode(arrJson)
    assert(type(arr) == "table", "array param should decode to table, got: " .. type(arr))
    assert(arr[1] == 10 and arr[2] == 20 and arr[3] == 30,
        "array values mismatch, got: " .. tostring(arrJson))
    mysql:Finish()

    -- Nested table
    ok, err = mysql:Query("INSERT INTO kitsune_test (name, num) VALUES (?, ?)",
        {{outer = {inner = "deep", val = 99}}, 902})
    assert(ok, "nested INSERT failed: " .. tostring(err))
    mysql:Fetch()

    mysql:Query("SELECT name FROM kitsune_test WHERE num = ?", {902})
    assert(mysql:Fetch(), "expected nested row")
    local nestJson = mysql:GetRow("name")
    local nest = json:Decode(nestJson)
    assert(type(nest) == "table" and type(nest.outer) == "table",
        "nested param should decode to nested table, got: " .. tostring(nestJson))
    assert(nest.outer.inner == "deep", "expected inner='deep', got: " .. tostring(nest.outer and nest.outer.inner))
    assert(nest.outer.val == 99, "expected val=99, got: " .. tostring(nest.outer and nest.outer.val))
    mysql:Finish()

    -- Mixed types
    ok, err = mysql:Query("INSERT INTO kitsune_test (name, num) VALUES (?, ?)",
        {{s = "text", n = 42, b = true, f = 1.5}, 903})
    assert(ok, "mixed INSERT failed: " .. tostring(err))
    mysql:Fetch()

    mysql:Query("SELECT name FROM kitsune_test WHERE num = ?", {903})
    assert(mysql:Fetch(), "expected mixed row")
    local mixJson = mysql:GetRow("name")
    local mix = json:Decode(mixJson)
    assert(mix.s == "text", "expected s='text', got: " .. tostring(mix and mix.s))
    assert(mix.n == 42, "expected n=42, got: " .. tostring(mix and mix.n))
    assert(mix.b == true, "expected b=true, got: " .. tostring(mix and mix.b))
    assert(mix.f == 1.5, "expected f=1.5, got: " .. tostring(mix and mix.f))
    mysql:Finish()
end)

run("MySQL Query with wchar param is UTF-8 encoded", function()
    local wstr = Wchar.FromUtf8("héllo wörld")
    local ok, err = mysql:Query("INSERT INTO kitsune_test (name, num) VALUES (?, ?)", {wstr, 888})
    assert(ok, "Query failed: " .. tostring(err))
    local more, ferr = mysql:Fetch()
    assert(not ferr, "INSERT with wchar param error: " .. tostring(ferr))

    mysql:Query("SELECT name FROM kitsune_test WHERE num = ?", {888})
    assert(mysql:Fetch(), "expected a row after wchar INSERT")
    local name = mysql:GetRow("name")
    assert(name == "héllo wörld", "expected wchar param to round-trip as UTF-8, got: " .. tostring(name))
    mysql:Finish()
end)

-- NULL columns

run("MySQL NULL columns are nil in GetRow", function()
    local ok = mysql:Query("SELECT name, value, flag, num FROM kitsune_test WHERE name = 'nulltest'")
    assert(ok, "Query failed")
    assert(mysql:Fetch(), "expected a row")
    local row = mysql:GetRow()
    assert(row.name == "nulltest", "expected nulltest, got: " .. tostring(row.name))
    assert(row.value == nil, "expected nil for NULL value, got: " .. tostring(row.value))
    assert(row.flag == nil, "expected nil for NULL flag")
    assert(row.num == nil, "expected nil for NULL num")
    mysql:Finish()
end)

-- Non-SELECT commands

run("MySQL non-SELECT Fetch returns false", function()
    local ok, err = mysql:Query("UPDATE kitsune_test SET num = 999 WHERE name = 'world'")
    assert(ok, "Query failed: " .. tostring(err))
    local more, ferr = mysql:Fetch()
    assert(more == false, "expected false from UPDATE")
    assert(ferr == nil, "expected no error, got: " .. tostring(ferr))
end)

run("MySQL empty SELECT Fetch returns false", function()
    local ok = mysql:Query("SELECT * FROM kitsune_test WHERE name = 'this_name_does_not_exist'")
    assert(ok, "Query failed")
    local more, ferr = mysql:Fetch()
    assert(more == false, "expected false for empty SELECT")
    assert(ferr == nil, "expected no error, got: " .. tostring(ferr))
end)

-- Finish

run("MySQL Finish discards result mid-iteration", function()
    local ok = mysql:Query("SELECT name FROM kitsune_test ORDER BY id")
    assert(ok, "Query failed")
    assert(mysql:Fetch(), "expected first row")
    mysql:Finish()
    assert(mysql:Fetch() == false, "expected false after Finish")
end)

-- Sequential queries

run("MySQL sequential queries", function()
    local ok1 = mysql:Query("SELECT name FROM kitsune_test WHERE name = 'hello'")
    assert(ok1, "first query failed")
    assert(mysql:Fetch(), "expected row from first query")
    assert(mysql:GetRow().name == "hello")
    mysql:Finish()

    local ok2 = mysql:Query("SELECT name FROM kitsune_test WHERE name = 'world'")
    assert(ok2, "second query failed")
    assert(mysql:Fetch(), "expected row from second query")
    assert(mysql:GetRow().name == "world")
    mysql:Finish()
end)

-- Error handling

run("MySQL bad query Fetch returns false and errmsg", function()
    local ok = mysql:Query("SELECT * FROM table_that_does_not_exist_ever")
    assert(ok, "Query dispatch failed")
    local more, ferr = mysql:Fetch()
    assert(more == false, "expected false for bad query")
    assert(type(ferr) == "string", "expected error string, got: " .. type(tostring(ferr)))
end)

-- IsBusy

run("MySQL IsBusy is false after Fetch", function()
    local ok = mysql:Query("SELECT 1")
    assert(ok, "Query failed")
    mysql:Fetch()
    assert(mysql:IsBusy() == false, "IsBusy should be false after Fetch")
end)

-- Transaction

run("MySQL transaction BEGIN ROLLBACK", function()
    local ok = mysql:Query("BEGIN")
    assert(ok)
    local more, ferr = mysql:Fetch()
    assert(not ferr, "BEGIN error: " .. tostring(ferr))

    mysql:Query("INSERT INTO kitsune_test (name, num) VALUES ('txtest', 77)")
    mysql:Fetch()

    mysql:Query("SELECT name, num FROM kitsune_test WHERE name = 'txtest'")
    assert(mysql:Fetch(), "expected row inside transaction")
    local row = mysql:GetRow()
    assert(row.name == "txtest" and row.num == 77, "row mismatch in transaction")
    mysql:Finish()

    mysql:Query("ROLLBACK")
    mysql:Fetch()

    mysql:Query("SELECT name FROM kitsune_test WHERE name = 'txtest'")
    assert(mysql:Fetch() == false, "expected 0 rows after ROLLBACK")
end)

-- EscapeValue

run("MySQL.EscapeValue escapes a string", function()
    local escaped = mysql:EscapeValue("O'Reilly")
    assert(type(escaped) == "string", "EscapeValue should return string")
    assert(escaped:find("O") and escaped:find("Reilly"), "should contain original text")
    assert(escaped ~= "O'Reilly", "single quote should be escaped, got unmodified string")
end)

-- Cleanup

run("MySQL.Query DROP TABLE cleanup", function()
    local ok, err = mysql:Query("DROP TABLE IF EXISTS kitsune_test")
    assert(ok, "Query failed: " .. tostring(err))
    local more, ferr = mysql:Fetch()
    assert(not ferr, "DROP TABLE error: " .. tostring(ferr))
end)

run("MySQL.Close cleans up", function()
    mysql:Close()
end)
