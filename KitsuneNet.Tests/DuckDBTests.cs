using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

// See KitsuneEngineTests for why both classes share a single collection.
[Collection("KitsuneSequential")]

/// <summary>
/// Tests for the DuckDB module. All tests use an in-memory database.
/// </summary>
public sealed class DuckDBTests
{
    [Fact]
    public async Task DuckDB_InMemory_CreateInsertSelect()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local db = DuckDB.Open()
            db:Execute('CREATE TABLE t (id INTEGER, name VARCHAR)')
            db:Execute([[INSERT INTO t VALUES (1, 'alice')]])
            db:Execute([[INSERT INTO t VALUES (2, 'bob')]])
            db:Execute('SELECT id, name FROM t ORDER BY id')
            local out = {}
            while db:Fetch() do
                local row = db:GetRow()
                table.insert(out, tostring(row.id) .. ':' .. tostring(row.name))
            end
            db:Close()
            return table.concat(out, ',')
        ");
        r.String.ShouldBe("1:alice,2:bob");
    }

    [Fact]
    public async Task DuckDB_GetRow_ByIndex_ReturnsValue()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local db = DuckDB.Open()
            db:Execute('CREATE TABLE t (v VARCHAR)')
            db:Execute([[INSERT INTO t VALUES ('test_val')]])
            db:Execute('SELECT v FROM t')
            db:Fetch()
            local val = db:GetRow(1)
            db:Close()
            return val
        ");
        r.String.ShouldBe("test_val");
    }

    [Fact]
    public async Task DuckDB_NullValue_IsNilInLua()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local db = DuckDB.Open()
            db:Execute('CREATE TABLE t (v VARCHAR)')
            db:Execute('INSERT INTO t VALUES (NULL)')
            db:Execute('SELECT v FROM t')
            db:Fetch()
            local val = db:GetRow(1)
            db:Close()
            return tostring(val == nil)
        ");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task DuckDB_IntegerColumn_ReturnsInteger()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local db = DuckDB.Open()
            db:Execute('CREATE TABLE t (n BIGINT)')
            db:Execute('INSERT INTO t VALUES (42)')
            db:Execute('SELECT n FROM t')
            db:Fetch()
            local val = db:GetRow(1)
            db:Close()
            return tostring(val)
        ");
        r.String.ShouldBe("42");
    }

    [Fact]
    public async Task DuckDB_DoubleColumn_ReturnsNumber()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local db = DuckDB.Open()
            db:Execute('CREATE TABLE t (v DOUBLE)')
            db:Execute('INSERT INTO t VALUES (3.14)')
            db:Execute('SELECT v FROM t')
            db:Fetch()
            local val = db:GetRow(1)
            db:Close()
            return tostring(math.floor(val * 100) == 314)
        ");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task DuckDB_ParameterBinding_Table()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local db = DuckDB.Open()
            db:Execute('CREATE TABLE t (id INTEGER, name VARCHAR)')
            db:Execute('INSERT INTO t VALUES ($1, $2)', {99, 'charlie'})
            db:Execute('SELECT name FROM t WHERE id = $1', {99})
            db:Fetch()
            local val = db:GetRow(1)
            db:Close()
            return val
        ");
        r.String.ShouldBe("charlie");
    }

    [Fact]
    public async Task DuckDB_ParameterBinding_Function()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local db = DuckDB.Open()
            db:Execute('CREATE TABLE t (id INTEGER, name VARCHAR)')
            db:Execute('INSERT INTO t VALUES ($1, $2)', function(i)
                if i == 1 then return 7 end
                return 'dave'
            end)
            db:Execute('SELECT name FROM t WHERE id = $1', {7})
            db:Fetch()
            local val = db:GetRow(1)
            db:Close()
            return val
        ");
        r.String.ShouldBe("dave");
    }

    [Fact]
    public async Task DuckDB_Finish_AllowsNewQuery()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local db = DuckDB.Open()
            db:Execute('CREATE TABLE t (v INTEGER)')
            db:Execute('INSERT INTO t VALUES (1)')
            db:Execute('INSERT INTO t VALUES (2)')
            db:Execute('SELECT v FROM t')
            db:Fetch()
            db:Finish()
            db:Execute('SELECT COUNT(*) AS c FROM t')
            db:Fetch()
            local val = db:GetRow(1)
            db:Close()
            return tostring(val)
        ");
        r.String.ShouldBe("2");
    }

    [Fact]
    public async Task DuckDB_Query_AliasWorks()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local db = DuckDB.Open()
            db:Query('CREATE TABLE t (v INTEGER)')
            db:Query('INSERT INTO t VALUES (5)')
            db:Query('SELECT v FROM t')
            db:Fetch()
            local val = db:GetRow(1)
            db:Close()
            return tostring(val)
        ");
        r.String.ShouldBe("5");
    }

    [Fact]
    public async Task DuckDB_BadSQL_ReturnsFalseAndError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local db = DuckDB.Open()
            local ok, err = db:Execute('THIS IS NOT SQL')
            db:Close()
            return tostring(ok == false and type(err) == 'string')
        ");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task DuckDB_ToString_ContainsDuckDB()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local db = DuckDB.Open()
            local s = tostring(db)
            db:Close()
            return tostring(s:find('DuckDB') ~= nil)
        ");
        r.String.ShouldBe("true");
    }
}
