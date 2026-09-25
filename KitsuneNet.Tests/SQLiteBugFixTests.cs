using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

// See KitsuneEngineTests for why both classes share a single collection.
[Collection("KitsuneSequential")]

/// <summary>
/// Regression tests for SQLite module fixes: the function-array growth in
/// RegisterFunction, parameter binding of unsupported value types, and the
/// busy handler's return value.
/// </summary>
public sealed class SQLiteBugFixTests
{
    // -- RegisterFunction array growth ---------------------------------------
    [Fact]
    public async Task SQLite_RegisterFunction_Two_BothCallable()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
			local db = SQLite.Open()
			db:RegisterFunction(function(a) return 'f1_' .. a end, 'f1', 1)
			db:RegisterFunction(function(a) return 'f2_' .. a end, 'f2', 1)
			db:Query([[SELECT f1('x'), f2('y')]])
			local a, b = db:GetRow(1), db:GetRow(2)
			db:Close()
			return tostring(a) .. ',' .. tostring(b)
		");
        r.String.ShouldBe("f1_x,f2_y");
    }

    [Fact]
    public async Task SQLite_RegisterFunction_ThreeWithAggregate_AllCallable()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
			local db = SQLite.Open()
			db:RegisterFunction(function(a) return 'f1_' .. a end, 'f1', 1)
			db:RegisterFunction(function(a) return 'f2_' .. a end, 'f2', 1)
			local sum = 0
			db:RegisterAggregateFunction(function(done, v)
				if done then local s = sum; sum = 0; return s end
				sum = sum + v
			end, 'mysum', 1)
			db:RegisterFunction(function(a) return 'f3_' .. a end, 'f3', 1)

			db:Query([[SELECT f1('a'), f2('b'), f3('c')]])
			local out = { tostring(db:GetRow(1)), tostring(db:GetRow(2)), tostring(db:GetRow(3)) }
			db:Finish()

			db:Query('CREATE TABLE t (v INTEGER)')
			db:Query('INSERT INTO t VALUES (1), (2), (3)')
			db:Query('SELECT mysum(v) FROM t')
			out[#out + 1] = tostring(db:GetRow(1) == 6)
			db:Close()
			return table.concat(out, ',')
		");
        r.String.ShouldBe("f1_a,f2_b,f3_c,true");
    }

    // -- Parameter binding of unsupported types -------------------------------
    [Fact]
    public async Task SQLite_Query_ParamsTable_UnsupportedValues_BindNullWithoutShift()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
			local db = SQLite.Open()
			db:Query('CREATE TABLE t (a, b, c, d, e, f)')
			local ok, msg = db:Query('INSERT INTO t VALUES (:a, :b, :c, :d, :e, :f)',
				{ a = {}, b = 2, c = function() end, d = 4, e = Json.Null, f = 'six' })
			if not ok then db:Close(); return 'insert failed: ' .. tostring(msg) end
			db:Query('SELECT a, b, c, d, e, f FROM t')
			local out = {}
			for i = 1, 6 do out[i] = tostring(db:GetRow(i)) end
			db:Close()
			return table.concat(out, ',')
		");
        r.String.ShouldBe("nil,2,nil,4,nil,six");
    }

    [Fact]
    public async Task SQLite_Query_ParamsFunction_UnsupportedValues_BindNullWithoutShift()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
			local db = SQLite.Open()
			db:Query('CREATE TABLE t (a, b, c, d, e, f)')
			local values = {
				a = {}, b = 2, c = function() end,
				d = 4, e = coroutine.create(function() end), f = 'six'
			}
			local ok, msg = db:Query('INSERT INTO t VALUES (:a, :b, :c, :d, :e, :f)',
				function(name) return values[name] end)
			if not ok then db:Close(); return 'insert failed: ' .. tostring(msg) end
			db:Query('SELECT a, b, c, d, e, f FROM t')
			local out = {}
			for i = 1, 6 do out[i] = tostring(db:GetRow(i)) end
			db:Close()
			return table.concat(out, ',')
		");
        r.String.ShouldBe("nil,2,nil,4,nil,six");
    }

    // -- Busy handler return value --------------------------------------------
    // Two connections on the same temp file: A holds an exclusive lock, B tries
    // to write. Each handler also raises after 50 calls so a regression cannot
    // hang the test run.

    private const string BusySetup = @"
			local path = FileSystem.GetTempFileName()   -- empty file: SQLite treats it as a new database
			local A = SQLite.Open(path)
			A:Query('PRAGMA journal_mode=DELETE')  -- new databases default to WAL; these tests need rollback locking
			A:Finish()
			A:Query('CREATE TABLE t (n INTEGER)')
			local B = SQLite.Open(path)
			B:Query('SELECT count(*) FROM t')   -- load the schema on B
			B:Finish()                           -- release B's read lock
			local locked = A:Query('BEGIN EXCLUSIVE')
			local function cleanup()
				A:Query('COMMIT')
				A:Close()
				B:Close()
				FileSystem.Delete(path)
				FileSystem.Delete(path .. '-journal')
			end
";

    [Fact]
    public async Task SQLite_SetBusyHandler_ReturnFalse_GivesUpWithBusyError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(BusySetup + @"
			local calls, firstArg, firstRetry = 0, nil, nil
			B:SetBusyHandler(function(sqlite, retries)
				calls = calls + 1
				if calls == 1 then firstArg, firstRetry = sqlite, retries end
				if calls > 50 then error('busy handler kept being called') end
				return false
			end)
			local ok, msg = B:Query('INSERT INTO t VALUES (1)')
			cleanup()
			return tostring(locked) .. ',' .. tostring(ok) .. ',' ..
				tostring(string.find(tostring(msg), 'locked', 1, true) ~= nil) .. ',' ..
				tostring(calls >= 1 and calls <= 5) .. ',' ..
				tostring(rawequal(firstArg, B)) .. ',' .. tostring(firstRetry)
		");
        r.String.ShouldBe("true,false,true,true,true,0");
    }

    [Fact]
    public async Task SQLite_SetBusyHandler_ReturnTrue_RetriesUntilFalse()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(BusySetup + @"
			local calls = 0
			B:SetBusyHandler(function(sqlite, retries)
				calls = calls + 1
				if calls > 50 then error('busy handler kept being called') end
				return retries < 2
			end)
			local ok = B:Query('INSERT INTO t VALUES (1)')
			cleanup()
			return tostring(locked) .. ',' .. tostring(ok) .. ',' .. tostring(calls >= 3 and calls <= 10)
		");
        r.String.ShouldBe("true,false,true");
    }

    [Fact]
    public async Task SQLite_SetBusyHandler_HandlerError_GivesUp()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(BusySetup + @"
			local calls = 0
			B:SetBusyHandler(function()
				calls = calls + 1
				error('give up')
			end)
			local ok, msg = B:Query('INSERT INTO t VALUES (1)')
			cleanup()
			return tostring(locked) .. ',' .. tostring(ok) .. ',' ..
				tostring(string.find(tostring(msg), 'locked', 1, true) ~= nil) .. ',' ..
				tostring(calls >= 1 and calls <= 5)
		");
        r.String.ShouldBe("true,false,true,true");
    }
}
