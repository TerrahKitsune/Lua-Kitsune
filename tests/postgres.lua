local helpers = require("tests.helpers")
local assert_type = helpers.assert_type
local assert_boolean = helpers.assert_boolean
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local pgConfig = {
    enabled = true,
    conninfo = "host=10.9.23.252 user=postgres password=a dbname=postgres connect_timeout=5"
}

run("Postgres table exists", function()
    assert_table(Postgres, "Postgres")
end)

if not pgConfig.enabled then
    skip("Postgres suite", "set pgConfig.enabled = true to run Postgres tests")
    return
end

local pg

run("Postgres.Connect returns instance", function()
    pg = Postgres.Connect(pgConfig.conninfo)
    assert(pg, "Postgres.Connect returned nil")
    assert_type(pg, "userdata", "Postgres connection")
end)

if not pg then
    skip("Postgres suite", "connection failed, skipping remaining tests")
    return
end

run("Postgres tostring", function()
    local s = tostring(pg)
    assert(type(s) == "string" and s:find("Postgres:"), "unexpected format: " .. tostring(s))
end)

run("Postgres.IsBusy returns false when idle", function()
    assert_boolean(pg:IsBusy(), "IsBusy")
    assert(pg:IsBusy() == false, "expected false when idle")
end)

-- Setup

run("Postgres.Query CREATE TABLE", function()
    local ok, err = pg:Query("CREATE TABLE IF NOT EXISTS kitsune_test (id SERIAL PRIMARY KEY, name TEXT, value DOUBLE PRECISION, flag BOOLEAN, num INT)")
    assert(ok, "Query failed: " .. tostring(err))
    local more, ferr = pg:Fetch()
    assert(not ferr, "CREATE TABLE error: " .. tostring(ferr))
end)

run("Postgres.Query INSERT", function()
    local ok, err = pg:Query("INSERT INTO kitsune_test (name, value, flag, num) VALUES ('hello', 3.14, true, 42), ('world', 2.71, false, 100)")
    assert(ok, "Query failed: " .. tostring(err))
    local more, ferr = pg:Fetch()
    assert(not ferr, "INSERT error: " .. tostring(ferr))
end)

-- Fetch + GetRow

run("Postgres Fetch iterates rows with named columns", function()
    local ok, err = pg:Query("SELECT id, name, value, flag, num FROM kitsune_test ORDER BY id")
    assert(ok, "Query failed: " .. tostring(err))

    assert(pg:Fetch(), "expected first row")
    local row = pg:GetRow()
    assert_table(row, "row")
    assert(row.name == "hello", "expected name='hello', got: " .. tostring(row.name))
    assert(type(row.value) == "number", "expected value to be number, got: " .. type(row.value))
    assert(row.flag == true, "expected flag=true, got: " .. tostring(row.flag))
    assert(row.num == 42, "expected num=42, got: " .. tostring(row.num))
    assert(type(row.id) == "number", "id should be integer")

    assert(pg:Fetch(), "expected second row")
    local row2 = pg:GetRow()
    assert(row2.name == "world", "expected 'world', got: " .. tostring(row2.name))

    assert(pg:Fetch() == false, "expected no more rows")
end)

run("Postgres GetRow with column index", function()
    local ok = pg:Query("SELECT id, name, num FROM kitsune_test WHERE name = 'hello'")
    assert(ok, "Query failed")
    assert(pg:Fetch(), "expected a row")
    local name = pg:GetRow(2)
    assert(name == "hello", "expected 'hello', got: " .. tostring(name))
    local num = pg:GetRow(3)
    assert(num == 42, "expected 42, got: " .. tostring(num))
    pg:Finish()
end)

run("Postgres GetRow with field name", function()
    local ok = pg:Query("SELECT id, name, value, flag, num FROM kitsune_test WHERE name = 'hello'")
    assert(ok, "Query failed")
    assert(pg:Fetch(), "expected a row")
    assert(pg:GetRow("name") == "hello", "expected 'hello' by field name")
    assert(pg:GetRow("num") == 42, "expected 42 by field name")
    assert(type(pg:GetRow("id")) == "number", "id should be integer by field name")
    assert(pg:GetRow("flag") == true, "expected flag=true by field name")
    assert(type(pg:GetRow("value")) == "number", "value should be number by field name")
    assert(pg:GetRow("nonexistent") == nil, "nonexistent field should return nil")
    pg:Finish()
end)

run("Postgres Fetch loop collects all rows", function()
    local ok = pg:Query("SELECT name FROM kitsune_test ORDER BY id")
    assert(ok, "Query failed")
    local names = {}
    while pg:Fetch() do
        local row = pg:GetRow()
        names[#names + 1] = row.name
    end
    assert(#names >= 2, "expected at least 2 names, got " .. tostring(#names))
    assert(names[1] == "hello", "first should be hello")
    assert(names[2] == "world", "second should be world")
end)

-- Parameterised queries

run("Postgres Query with params table", function()
    local ok, err = pg:Query("SELECT name, num FROM kitsune_test WHERE name = $1", {"hello"})
    assert(ok, "Query failed: " .. tostring(err))
    assert(pg:Fetch(), "expected a row")
    local row = pg:GetRow()
    assert(row.name == "hello", "expected 'hello', got: " .. tostring(row.name))
    assert(row.num == 42, "expected 42, got: " .. tostring(row.num))
    pg:Finish()
end)

run("Postgres Query with multiple params", function()
    local ok, err = pg:Query("SELECT name FROM kitsune_test WHERE name = $1 OR num = $2 ORDER BY id", {"world", 42})
    assert(ok, "Query failed: " .. tostring(err))
    local count = 0
    while pg:Fetch() do
        count = count + 1
        pg:GetRow()
    end
    assert(count == 2, "expected 2 rows, got " .. tostring(count))
end)

run("Postgres Query with number param", function()
    local ok = pg:Query("SELECT name FROM kitsune_test WHERE num = $1", {42})
    assert(ok, "Query failed")
    assert(pg:Fetch(), "expected a row")
    local row = pg:GetRow()
    assert(row.name == "hello", "expected 'hello', got: " .. tostring(row.name))
    pg:Finish()
end)

run("Postgres Query with nil param (SQL NULL)", function()
    local ok, err = pg:Query("INSERT INTO kitsune_test (name, value, flag, num) VALUES ($1, $2, $3, $4)", {"nulltest", nil, nil, nil})
    assert(ok, "Query failed: " .. tostring(err))
    local more, ferr = pg:Fetch()
    assert(not ferr, "INSERT with nulls error: " .. tostring(ferr))
end)

-- NULL columns

run("Postgres NULL columns are nil in GetRow", function()
    local ok = pg:Query("SELECT name, value, flag, num FROM kitsune_test WHERE name = 'nulltest'")
    assert(ok, "Query failed")
    assert(pg:Fetch(), "expected a row")
    local row = pg:GetRow()
    assert(row.name == "nulltest", "expected nulltest, got: " .. tostring(row.name))
    assert(row.value == nil, "expected nil for NULL value, got: " .. tostring(row.value))
    assert(row.flag == nil, "expected nil for NULL flag")
    assert(row.num == nil, "expected nil for NULL num")
    pg:Finish()
end)

-- Non-SELECT commands

run("Postgres non-SELECT Fetch returns false", function()
    local ok, err = pg:Query("UPDATE kitsune_test SET num = 999 WHERE name = 'world'")
    assert(ok, "Query failed: " .. tostring(err))
    local more, ferr = pg:Fetch()
    assert(more == false, "expected false from UPDATE")
    assert(ferr == nil, "expected no error, got: " .. tostring(ferr))
end)

run("Postgres empty SELECT Fetch returns false", function()
    local ok = pg:Query("SELECT * FROM kitsune_test WHERE name = 'this_name_does_not_exist'")
    assert(ok, "Query failed")
    local more, ferr = pg:Fetch()
    assert(more == false, "expected false for empty SELECT")
    assert(ferr == nil, "expected no error, got: " .. tostring(ferr))
end)

-- Finish

run("Postgres Finish discards result mid-iteration", function()
    local ok = pg:Query("SELECT name FROM kitsune_test ORDER BY id")
    assert(ok, "Query failed")
    assert(pg:Fetch(), "expected first row")
    pg:Finish()
    assert(pg:Fetch() == false, "expected false after Finish")
end)

-- INSERT RETURNING

run("Postgres INSERT RETURNING returns rows", function()
    local ok = pg:Query("INSERT INTO kitsune_test (name, num) VALUES ('returning_test', 55) RETURNING id, name")
    assert(ok, "Query failed")
    assert(pg:Fetch(), "expected a row from RETURNING")
    local row = pg:GetRow()
    assert(type(row.id) == "number", "id should be integer, got: " .. type(row.id))
    assert(row.name == "returning_test", "name mismatch: " .. tostring(row.name))
    pg:Finish()
end)

-- Sequential queries

run("Postgres sequential queries", function()
    local ok1 = pg:Query("SELECT name FROM kitsune_test WHERE name = 'hello'")
    assert(ok1, "first query failed")
    assert(pg:Fetch(), "expected row from first query")
    assert(pg:GetRow().name == "hello")
    pg:Finish()

    local ok2 = pg:Query("SELECT name FROM kitsune_test WHERE name = 'world'")
    assert(ok2, "second query failed")
    assert(pg:Fetch(), "expected row from second query")
    assert(pg:GetRow().name == "world")
    pg:Finish()
end)

-- Error handling

run("Postgres bad query Fetch returns false and errmsg", function()
    local ok = pg:Query("SELECT * FROM table_that_does_not_exist_ever")
    assert(ok, "Query dispatch failed")
    local more, ferr = pg:Fetch()
    assert(more == false, "expected false for bad query")
    assert(type(ferr) == "string", "expected error string, got: " .. type(tostring(ferr)))
end)

-- IsBusy

run("Postgres IsBusy is false after Fetch", function()
    local ok = pg:Query("SELECT 1")
    assert(ok, "Query failed")
    pg:Fetch()
    assert(pg:IsBusy() == false, "IsBusy should be false after Fetch")
end)

-- Transaction

run("Postgres transaction BEGIN ROLLBACK", function()
    local ok = pg:Query("BEGIN")
    assert(ok)
    local more, ferr = pg:Fetch()
    assert(not ferr, "BEGIN error: " .. tostring(ferr))

    pg:Query("INSERT INTO kitsune_test (name, num) VALUES ('txtest', 77)")
    pg:Fetch()

    pg:Query("SELECT name, num FROM kitsune_test WHERE name = 'txtest'")
    assert(pg:Fetch(), "expected row inside transaction")
    local row = pg:GetRow()
    assert(row.name == "txtest" and row.num == 77, "row mismatch in transaction")
    pg:Finish()

    pg:Query("ROLLBACK")
    pg:Fetch()

    pg:Query("SELECT name FROM kitsune_test WHERE name = 'txtest'")
    assert(pg:Fetch() == false, "expected 0 rows after ROLLBACK")
end)

-- EscapeValue

run("Postgres.EscapeValue escapes a string", function()
    local escaped = pg:EscapeValue("O'Reilly")
    assert(type(escaped) == "string", "EscapeValue should return string")
    assert(escaped:find("O") and escaped:find("Reilly"), "should contain original text")
    assert(escaped:sub(1, 1) == "'", "should have leading quote, got: " .. escaped)
    assert(escaped:sub(-1) == "'", "should have trailing quote, got: " .. escaped)
    assert(escaped:find("''"), "single quote should be doubled, got: " .. escaped)
end)

-- Cleanup

run("Postgres.Query DROP TABLE cleanup", function()
    local ok, err = pg:Query("DROP TABLE IF EXISTS kitsune_test")
    assert(ok, "Query failed: " .. tostring(err))
    local more, ferr = pg:Fetch()
    assert(not ferr, "DROP TABLE error: " .. tostring(ferr))
end)

run("Postgres.Close cleans up", function()
    pg:Close()
end)
