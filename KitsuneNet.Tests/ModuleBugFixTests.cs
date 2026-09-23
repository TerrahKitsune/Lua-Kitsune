using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

/// <summary>
/// Regression tests for module-level bug fixes:
/// CSV module-level Decode/Encode/DecodeFromFunction, TaskStatus.Waiting,
/// Redis RESP3 BOOL replies and unsigned RedisString bytes, and MySQL
/// connection release after an early stop flag or a query-level error.
/// </summary>
[Collection("KitsuneSequential")]
public sealed class ModuleBugFixTests
{
    // -- CSV: module-level forms ------------------------------------------------
    [Fact]
    public async Task Csv_ModuleDecode_DefaultDelimiterIsComma()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local t = CSV.Decode('a,b\n1,2')
            local t2 = CSV.Decode('a;b\n1;2')
            return tostring(#t.Rows == 2 and t.Rows[1][1] == 'a' and t.Rows[2][2] == '2'
                and #t2.Rows[1] == 1 and t2.Rows[1][1] == 'a;b')
        ");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Csv_ModuleDecode_ExplicitAndAutoDelimiter()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local t1 = CSV.Decode('a;b\n1;2', ';')
            local t2 = CSV.Decode('a|b\n1|2', 'auto')
            local t3 = CSV.Decode('a;b', string.byte(';'))
            return tostring(t1.Rows[1][2] == 'b' and t1.Rows[2][1] == '1'
                and t2.Rows[1][2] == 'b' and t2.Rows[2][2] == '2'
                and t3.Rows[1][2] == 'b')
        ");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Csv_ModuleDecode_NoArgument_RaisesArgumentError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local ok, err = pcall(CSV.Decode)
            return tostring(ok) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("false:true");
    }

    [Fact]
    public async Task Csv_ModuleEncode_DefaultAndExplicitDelimiter()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local rows = { {'a', 'b'}, {'c', 'd'} }
            return CSV.Encode(rows) .. '|' .. CSV.Encode(rows, ';')
        ");
        r.String.ShouldBe("a,b\nc,d|a;b\nc;d");
    }

    [Fact]
    public async Task Csv_ModuleDecodeFromFunction_DefaultAndExplicitDelimiter()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local function supplier(chunks)
                local i = 0
                return function() i = i + 1; return chunks[i] end
            end
            local out = {}
            for row in CSV.DecodeFromFunction(supplier({'a,b\n', '1,2'})) do
                out[#out + 1] = table.concat(row, '+')
            end
            for row in CSV.DecodeFromFunction(supplier({'x;y\n3;', '4'}), ';') do
                out[#out + 1] = table.concat(row, '+')
            end
            return table.concat(out, ' ')
        ");
        r.String.ShouldBe("a+b 1+2 x+y 3+4");
    }

    [Fact]
    public async Task Csv_ModuleDecodeFromFunction_NonFunction_RaisesError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local ok = pcall(CSV.DecodeFromFunction, 42)
            return tostring(ok)
        ");
        r.String.ShouldBe("false");
    }

    [Fact]
    public async Task Csv_InstanceForms_StillWork()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local sc = CSV.New(';')
            local t = sc:Decode('a;b\n1;2')
            local s = sc:Encode({ {'x', 'y'} })
            local rows = {}
            local done = false
            for row in sc:DecodeFromFunction(function()
                if done then return nil end
                done = true
                return 'p;q'
            end) do
                rows[#rows + 1] = table.concat(row, '+')
            end
            local auto = CSV.New():Decode('a|b')
            return tostring(t.Rows[2][2] == '2') .. ':' .. s .. ':' .. table.concat(rows, ' ')
                .. ':' .. tostring(auto.Rows[1][2] == 'b')
        ");
        r.String.ShouldBe("true:x;y:p+q:true");
    }

    [Fact]
    public async Task Csv_ModuleAndInstanceDecode_ShareTheSameFunction()
    {
        // CSV.Decode is also the instance method (module table is __index).
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local csv = CSV.New(';')
            local a = CSV.Decode('a;b', ';')
            local b = csv:Decode('a;b')
            local c = CSV.Decode(csv, 'a;b')
            return tostring(csv.Decode == CSV.Decode) .. ':' .. a.Rows[1][2] .. b.Rows[1][2] .. c.Rows[1][2]
        ");
        r.String.ShouldBe("true:bbb");
    }

    // -- Tasks: TaskStatus.Waiting ---------------------------------------------
    [Fact]
    public async Task TaskStatus_Waiting_IsNine()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            return tostring(TaskStatus.Waiting)
        ");
        r.String.ShouldBe("9");
    }

    [Fact]
    public async Task TaskStatus_TaskSuspendedInWait_ReportsWaiting()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local blocker = Tasks.New(function() Pause() end)
            for i = 1, 2000 do
                if blocker:GetStatus() == TaskStatus.Paused then break end
                Sleep(1)
            end
            local waiter = Tasks.New(function() blocker:Wait() end)
            local seen = false
            for i = 1, 2000 do
                if waiter:GetStatus() == TaskStatus.Waiting then seen = true; break end
                Sleep(1)
            end
            local finished = waiter:Finished()
            blocker:Resume()
            waiter:Wait()
            blocker:Dispose()
            waiter:Dispose()
            return tostring(seen) .. ':' .. tostring(finished)
        ");
        r.String.ShouldBe("true:false");
    }

    // -- Redis -----------------------------------------------------------------
    [RedisFact]
    public async Task Redis_Resp3BoolReply_IsLuaBoolean()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            local redis = {{RedisOpen()}}
            redis:Command('HELLO', '3')
            local t = redis:Command('EVAL', 'redis.setresp(3); return true', '0')
            local f = redis:Command('EVAL', 'redis.setresp(3); return false', '0')
            return tostring(t.Type) .. ':' .. type(t.Value) .. ':' .. tostring(t.Value)
                .. ':' .. tostring(f.Type) .. ':' .. tostring(f.Value)
            """);
        r.String.ShouldBe("8:boolean:true:8:false");
    }

    [RedisFact]
    public async Task Redis_StringAt_ByteAbove127_IsUnsigned()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            local redis = {{RedisOpen()}}
            local key = 'kitsune_test_bugfix_bytes'
            redis:Command('SET', key, string.char(200, 65, 255))
            local s = redis:GetString(key)
            local at = s:At(1)
            local idx = s[3]
            local it = {}
            for _, b in pairs(s) do it[#it + 1] = tostring(b) end
            redis:Command('DEL', key)
            return tostring(at) .. ':' .. tostring(idx) .. ':' .. table.concat(it, ',')
            """);
        r.String.ShouldBe("200:255:200,65,255");
    }

    // -- MySQL -----------------------------------------------------------------
    [MySqlFact]
    public async Task MySql_StopFlagBeforeRowcount_ConnectionReusable()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            local conn = assert({{MySqlConnect()}})
            local co = assert(conn:Query('SELECT SLEEP(0.2)'))
            coroutine.resume(co)          -- sends the query; still in flight
            coroutine.resume(co, true)    -- stop before the rowcount arrives
            local status = coroutine.status(co)
            local ok1, n = conn:NonQuery('DO 1')
            local ok2, v = conn:Scalar('SELECT 42')
            return status .. ':' .. tostring(conn:IsBusy()) .. ':' .. tostring(ok1) .. ':'
                .. tostring(ok2) .. ':' .. tostring(v)
            """);
        r.String.ShouldBe("dead:false:true:true:42");
    }

    [MySqlFact]
    public async Task MySql_StopFlagInEveryPhase_ConnectionReusable()
    {
        // Stop after 0..6 plain resumes, covering: before start, query in
        // flight, result store pending, and rows streaming.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            local conn = assert({{MySqlConnect()}})
            for k = 0, 6 do
                local co, err = conn:Query('SELECT 1 UNION SELECT 2 UNION SELECT 3')
                if not co then return 'query failed at ' .. k .. ': ' .. tostring(err) end
                for i = 1, k do
                    if coroutine.status(co) == 'dead' then break end
                    coroutine.resume(co)
                end
                if coroutine.status(co) == 'suspended' then coroutine.resume(co, true) end
                local ok, v = conn:Scalar('SELECT 42')
                if not ok or v ~= 42 then return 'fail at ' .. k .. ': ' .. tostring(v) end
            end
            return 'ok'
            """);
        r.String.ShouldBe("ok");
    }

    [MySqlFact]
    public async Task MySql_RawQueryError_ConnectionReusableWithoutExtraResume()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            local conn = assert({{MySqlConnect()}})
            local co = assert(conn:Query('SELECT * FROM _no_such_table_xyz'))
            local ok, val = coroutine.resume(co)
            while ok and val == nil and coroutine.status(co) == 'suspended' do
                ok, val = coroutine.resume(co)
            end
            local status = coroutine.status(co)
            local busy = conn:IsBusy()
            local ok2, v = conn:Scalar('SELECT 7')
            return type(val) .. ':' .. status .. ':' .. tostring(busy) .. ':'
                .. tostring(ok2) .. ':' .. tostring(v)
            """);
        r.String.ShouldBe("string:dead:false:true:7");
    }

    [MySqlFact]
    public async Task MySql_HelperQueryError_ConnectionReusable()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            local conn = assert({{MySqlConnect()}})
            local ok1, err = conn:NonQuery('THIS IS NOT SQL')
            local ok2, v = conn:Scalar('SELECT 5')
            return tostring(ok1) .. ':' .. tostring(type(err) == 'string') .. ':'
                .. tostring(ok2) .. ':' .. tostring(v)
            """);
        r.String.ShouldBe("false:true:true:5");
    }

    [MySqlFact]
    public async Task MySql_TextIsString_BlobIsStream_BitIsInteger()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            local conn = assert({{MySqlConnect()}})
            assert(conn:NonQuery('CREATE TEMPORARY TABLE kt_types (t TEXT, b BLOB, b1 BIT(1), b8 BIT(8), b64 BIT(64))'))
            assert(conn:NonQuery("INSERT INTO kt_types VALUES ('h\u{00E9}llo', 'bin', b'1', b'10000001', b'1111111111111111111111111111111111111111111111111111111111111111')"))
            local ok, rows = conn:QueryAll('SELECT t, b, b1, b8, b64 FROM kt_types')
            assert(ok, rows)
            local row = rows[1]
            return table.concat({
                type(row[1]), row[1] == 'h\u{00E9}llo' and 'eq' or 'ne',
                type(row[2]), tostring(row[2]),
                math.type(row[3]), row[3], row[4], tostring(row[5])}, ',')
            """);
        r.String.ShouldBe("string,eq,userdata,bin,integer,1,129,18446744073709551615");
    }

    // -- helpers ---------------------------------------------------------------
    private static string RedisOpen()
    {
        var parts = Environment.GetEnvironmentVariable("KITSUNE_REDIS_TEST")!.Split(':');
        var host = parts[0];
        var port = parts.Length > 1 && int.TryParse(parts[1], out int p) ? p : 6379;
        var passLua = parts.Length > 2 && parts[2].Length > 0 ? $"'{parts[2]}'" : "nil";
        return $"Redis.Open('{host}', {port}, false, 10, nil, {passLua})";
    }

    private static string MySqlConnect()
    {
        var parts = Environment.GetEnvironmentVariable("KITSUNE_MYSQL_TEST")!.Split(':');
        return $"MySQL.Connect('{parts[0]}','{parts[2]}','{parts[3]}','{parts[4]}',{parts[1]})";
    }
}
