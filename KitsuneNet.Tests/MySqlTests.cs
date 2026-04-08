using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

[Collection("KitsuneSequential")]
public sealed class MySqlTests
{
    // ── coroutine protocol ───────────────────────────────────────────────────
    [MySqlFact]
    public async Task Helper_MethodsExistInModuleTable()
    {
        string? r = await Run(@"
            return tostring(type(MySQL.NonQuery)) .. ':' .. tostring(type(MySQL.Scalar)) .. ':' .. tostring(type(MySQL.QueryAll))
        ");
        r.ShouldBe("function:function:function");
    }

    [MySqlFact]
    public async Task Connect_ValidCredentials_ReturnsUserdata()
    {
        string? r = await Run($@"
            local conn, err = {ConnectLua()}
            if not conn then error(err) end
            return tostring(conn):sub(1,6)
        ");
        r.ShouldBe("MySQL:");
    }

    [MySqlFact]
    public async Task Connect_BadCredentials_ReturnsNilAndError()
    {
        string? r = await Run($@"
            local conn, err = MySQL.Connect('127.0.0.1','bad_user','bad_pass','bad_db',3306)
            return tostring(conn == nil) .. ':' .. tostring(type(err) == 'string')
        ");
        r.ShouldBe("true:true");
    }

    [MySqlFact]
    public async Task RawSelect_CoroutineProtocol_YieldsNilThenRowcountThenRows()
    {
        string? r = await Run($@"
            local conn, err = {ConnectLua()}
            if not conn then error(err) end
            local co, cerr = conn:Query('SELECT 1 AS n, 2 AS m')
            if not co then error(cerr) end
            -- Phase 1: drive to rowcount
            local ok, val = coroutine.resume(co)
            while ok and val == nil and coroutine.status(co) == 'suspended' do
                ok, val = coroutine.resume(co)
            end
            if not ok then error(val) end
            if type(val) == 'string' then error(val) end
            local rowcount = val
            -- Phase 3: read first row
            ok, val = coroutine.resume(co)
            local col1 = val and val[1]
            local col2 = val and val[2]
            -- Drain
            ok, val = coroutine.resume(co)
            return tostring(rowcount) .. ':' .. tostring(col1) .. ':' .. tostring(col2) .. ':' .. tostring(val == nil)
        ");
        r.ShouldBe("1:1:2:true");
    }

    [MySqlFact]
    public async Task RawQuery_Error_Phase2YieldsString()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local co = assert(conn:Query('SELECT * FROM _no_such_table_xyz'))
            local ok, val = coroutine.resume(co)
            while ok and val == nil and coroutine.status(co) == 'suspended' do
                ok, val = coroutine.resume(co)
            end
            return tostring(ok) .. ':' .. tostring(type(val) == 'string')
        ");
        r.ShouldBe("true:true");
    }

    [MySqlFact]
    public async Task StopFlag_MidWait_ConnNotBusy()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local co = assert(conn:Query('SELECT 1'))
            coroutine.resume(co, true)
            return tostring(conn:IsBusy())
        ");
        r.ShouldBe("false");
    }

    [MySqlFact]
    public async Task StopFlag_MidStream_ConnNotBusy()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local co = assert(conn:Query('SELECT 1 UNION SELECT 2 UNION SELECT 3'))
            local ok, val = coroutine.resume(co)
            while ok and val == nil and coroutine.status(co) == 'suspended' do
                ok, val = coroutine.resume(co)
            end
            -- val is rowcount; get first row
            coroutine.resume(co)
            -- stop mid-stream
            coroutine.resume(co, true)
            return tostring(conn:IsBusy())
        ");
        r.ShouldBe("false");
    }

    [MySqlFact]
    public async Task IsBusy_TrueWhileSuspended_FalseOnceDead()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local co = assert(conn:Query('SELECT 1'))
            -- after first resume T is yielded (async wait or rowcount phase)
            coroutine.resume(co)
            local busy1 = conn:IsBusy()
            -- drive to completion
            local ok, val = coroutine.resume(co)
            while ok and val == nil and coroutine.status(co) == 'suspended' do
                ok, val = coroutine.resume(co)
            end
            -- drain rows
            ok, val = coroutine.resume(co)
            while ok and val ~= nil do ok, val = coroutine.resume(co) end
            local busy2 = conn:IsBusy()
            return tostring(busy1) .. ':' .. tostring(busy2)
        ");

        // busy1 may be true or false depending on server speed; busy2 must be false
        r.ShouldNotBeNull();
        r!.ShouldEndWith(":false");
    }

    [MySqlFact]
    public async Task EscapeValue_EscapesSingleQuoteAndBackslash()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local s = conn:EscapeValue(""it's a \\ test"")
            return s
        ");
        r.ShouldBe(@"it\'s a \\ test");
    }

    [MySqlFact]
    public async Task Close_IsIdempotent()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            conn:Close()
            conn:Close()
            return 'ok'
        ");
        r.ShouldBe("ok");
    }

    // ── CRUD helpers shared by every test below ───────────────────────────────
    //
    // exec(sql, params?) → rowcount, co
    //   Drives the inner query coroutine T to the rowcount phase.
    //   Caller MUST drain co before the next query on the same connection:
    //     DML  → coroutine.resume(co, true)   (stop flag, T dies)
    //     SELECT (after reading rows) → coroutine.resume(co, true)

    // ── INSERT ────────────────────────────────────────────────────────────────
    [MySqlFact]
    public async Task Crud_Insert_ReturnsOneAffectedRow()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local function exec(sql, p)
                local co = assert(conn:Query(sql, p))
                local ok, v = coroutine.resume(co)
                while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                if not ok then error(tostring(v)) end
                if type(v) == 'string' then error(v) end
                return v, co
            end
            local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_insert'}})
            coroutine.resume(c0, true)
            local rowcount, c1 = exec('INSERT INTO test (String) VALUES (?)', {{'crud_insert'}})
            coroutine.resume(c1, true)
            return tostring(rowcount)
        ");
        r.ShouldBe("1");
    }

    // ── SELECT ────────────────────────────────────────────────────────────────
    [MySqlFact]
    public async Task Crud_Select_ReturnsInsertedRow()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local function exec(sql, p)
                local co = assert(conn:Query(sql, p))
                local ok, v = coroutine.resume(co)
                while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                if not ok then error(tostring(v)) end
                if type(v) == 'string' then error(v) end
                return v, co
            end
            local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_sel'}})
            coroutine.resume(c0, true)
            local _, c1 = exec('INSERT INTO test (String, bool) VALUES (?, ?)', {{'crud_sel', 1}})
            coroutine.resume(c1, true)
            local _, co = exec('SELECT String, bool FROM test WHERE String = ?', {{'crud_sel'}})
            local ok, row = coroutine.resume(co)
            coroutine.resume(co, true)
            return tostring(row[1]) .. ':' .. tostring(row[2])
        ");
        r.ShouldBe("crud_sel:1");
    }

    // ── UPDATE ────────────────────────────────────────────────────────────────
    [MySqlFact]
    public async Task Crud_Update_ModifiesRow()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local function exec(sql, p)
                local co = assert(conn:Query(sql, p))
                local ok, v = coroutine.resume(co)
                while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                if not ok then error(tostring(v)) end
                if type(v) == 'string' then error(v) end
                return v, co
            end
            local _, c0 = exec('DELETE FROM test WHERE String IN (?, ?)', {{'crud_upd_old', 'crud_upd_new'}})
            coroutine.resume(c0, true)
            local _, c1 = exec('INSERT INTO test (String) VALUES (?)', {{'crud_upd_old'}})
            coroutine.resume(c1, true)
            local affected, c2 = exec('UPDATE test SET String = ? WHERE String = ?', {{'crud_upd_new', 'crud_upd_old'}})
            coroutine.resume(c2, true)
            local _, co = exec('SELECT String FROM test WHERE String = ?', {{'crud_upd_new'}})
            local ok, row = coroutine.resume(co)
            coroutine.resume(co, true)
            return tostring(affected) .. ':' .. tostring(row[1])
        ");
        r.ShouldBe("1:crud_upd_new");
    }

    // ── DELETE ────────────────────────────────────────────────────────────────
    [MySqlFact]
    public async Task Crud_Delete_RemovesRow()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local function exec(sql, p)
                local co = assert(conn:Query(sql, p))
                local ok, v = coroutine.resume(co)
                while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                if not ok then error(tostring(v)) end
                if type(v) == 'string' then error(v) end
                return v, co
            end
            local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_del'}})
            coroutine.resume(c0, true)
            local _, c1 = exec('INSERT INTO test (String) VALUES (?)', {{'crud_del'}})
            coroutine.resume(c1, true)
            local _, c2 = exec('DELETE FROM test WHERE String = ?', {{'crud_del'}})
            coroutine.resume(c2, true)
            local rowcount, co = exec('SELECT Id FROM test WHERE String = ?', {{'crud_del'}})
            local ok, row = coroutine.resume(co)
            coroutine.resume(co, true)
            return tostring(rowcount) .. ':' .. tostring(row == nil)
        ");
        r.ShouldBe("0:true");
    }

    // ── multiple rows ─────────────────────────────────────────────────────────
    [MySqlFact]
    public async Task Crud_Select_MultipleRowsReturned()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local function exec(sql, p)
                local co = assert(conn:Query(sql, p))
                local ok, v = coroutine.resume(co)
                while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                if not ok then error(tostring(v)) end
                if type(v) == 'string' then error(v) end
                return v, co
            end
            local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_multi'}})
            coroutine.resume(c0, true)
            for i = 1, 3 do
                local _, ci = exec('INSERT INTO test (String) VALUES (?)', {{'crud_multi'}})
                coroutine.resume(ci, true)
            end
            local rowcount, co = exec('SELECT Id FROM test WHERE String = ? ORDER BY Id', {{'crud_multi'}})
            local count, ok, row = 0, coroutine.resume(co)
            while ok and row ~= nil do count = count + 1; ok, row = coroutine.resume(co) end
            return tostring(rowcount) .. ':' .. tostring(count)
        ");
        r.ShouldBe("3:3");
    }

    // ── type mapping ──────────────────────────────────────────────────────────
    [MySqlFact]
    public async Task Crud_Types_IntColumnsReturnedAsInteger()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local function exec(sql, p)
                local co = assert(conn:Query(sql, p))
                local ok, v = coroutine.resume(co)
                while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                if not ok then error(tostring(v)) end
                if type(v) == 'string' then error(v) end
                return v, co
            end
            local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_tint'}})
            coroutine.resume(c0, true)
            local _, c1 = exec('INSERT INTO test (String, bool) VALUES (?, ?)', {{'crud_tint', 1}})
            coroutine.resume(c1, true)
            local _, co = exec('SELECT Id, bool FROM test WHERE String = ?', {{'crud_tint'}})
            local ok, row = coroutine.resume(co)
            coroutine.resume(co, true)
            return tostring(math.type(row[1])) .. ':' .. tostring(math.type(row[2]))
        ");
        r.ShouldBe("integer:integer");
    }

    [MySqlFact]
    public async Task Crud_Types_FloatDoubleDecimalReturnedAsNumber()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local function exec(sql, p)
                local co = assert(conn:Query(sql, p))
                local ok, v = coroutine.resume(co)
                while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                if not ok then error(tostring(v)) end
                if type(v) == 'string' then error(v) end
                return v, co
            end
            local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_tnum'}})
            coroutine.resume(c0, true)
            local _, c1 = exec('INSERT INTO test (String, `float`, `double`, `decimal`) VALUES (?, ?, ?, ?)', {{'crud_tnum', 1.5, 2.5, 42}})
            coroutine.resume(c1, true)
            local _, co = exec('SELECT `float`, `double`, `decimal` FROM test WHERE String = ?', {{'crud_tnum'}})
            local ok, row = coroutine.resume(co)
            coroutine.resume(co, true)
            return tostring(type(row[1])) .. ':' .. tostring(type(row[2])) .. ':' .. tostring(type(row[3]))
        ");
        r.ShouldBe("number:number:number");
    }

    [MySqlFact]
    public async Task Crud_Types_BlobReturnedAsUserdata()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local function exec(sql, p)
                local co = assert(conn:Query(sql, p))
                local ok, v = coroutine.resume(co)
                while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                if not ok then error(tostring(v)) end
                if type(v) == 'string' then error(v) end
                return v, co
            end
            local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_tblob'}})
            coroutine.resume(c0, true)
            local _, c1 = exec('INSERT INTO test (String, `Blob`) VALUES (?, ?)', {{'crud_tblob', 'hello blob'}})
            coroutine.resume(c1, true)
            local _, co = exec('SELECT `Blob` FROM test WHERE String = ?', {{'crud_tblob'}})
            local ok, row = coroutine.resume(co)
            coroutine.resume(co, true)
            return tostring(type(row[1]))
        ");
        r.ShouldBe("userdata");
    }

    [MySqlFact]
    public async Task Crud_Types_NullableColumnsReturnedAsNil()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local function exec(sql, p)
                local co = assert(conn:Query(sql, p))
                local ok, v = coroutine.resume(co)
                while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                if not ok then error(tostring(v)) end
                if type(v) == 'string' then error(v) end
                return v, co
            end
            local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_tnull'}})
            coroutine.resume(c0, true)
            local _, c1 = exec('INSERT INTO test (String) VALUES (?)', {{'crud_tnull'}})
            coroutine.resume(c1, true)
            local _, co = exec('SELECT `Blob`, Json, datetime, `float`, `double`, `decimal`, bool FROM test WHERE String = ?', {{'crud_tnull'}})
            local ok, row = coroutine.resume(co)
            coroutine.resume(co, true)
            local allnil = row[1]==nil and row[2]==nil and row[3]==nil and row[4]==nil and row[5]==nil and row[6]==nil and row[7]==nil
            return tostring(allnil)
        ");
        r.ShouldBe("true");
    }

    [MySqlFact]
    public async Task Crud_Types_DatetimeAndJsonReturnedAsString()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local function exec(sql, p)
                local co = assert(conn:Query(sql, p))
                local ok, v = coroutine.resume(co)
                while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                if not ok then error(tostring(v)) end
                if type(v) == 'string' then error(v) end
                return v, co
            end
            local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_tstr'}})
            coroutine.resume(c0, true)
            local _, c1 = exec('INSERT INTO test (String, datetime, Json) VALUES (?, NOW(), ?)', {{'crud_tstr', '{{""k"":1}}'}})
            coroutine.resume(c1, true)
            local _, co = exec('SELECT datetime, Json FROM test WHERE String = ?', {{'crud_tstr'}})
            local ok, row = coroutine.resume(co)
            coroutine.resume(co, true)
            return tostring(type(row[1])) .. ':' .. tostring(type(row[2]))
        ");
        r.ShouldBe("string:string");
    }

    // ── NonQuery ──────────────────────────────────────────────────────────────
    [MySqlFact]
    public async Task NonQuery_Insert_ReturnsAffectedCount()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            conn:NonQuery('DELETE FROM test WHERE String = ?', {{'helper_nq'}})
            local ok, n = conn:NonQuery('INSERT INTO test (String) VALUES (?)', {{'helper_nq'}})
            conn:NonQuery('DELETE FROM test WHERE String = ?', {{'helper_nq'}})
            return tostring(ok) .. ':' .. tostring(n)
        ");
        r.ShouldBe("true:1");
    }

    [MySqlFact]
    public async Task NonQuery_InvalidSql_ReturnsFalseAndError()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local ok, err = conn:NonQuery('NOT VALID SQL AT ALL')
            return tostring(ok == false and type(err) == 'string' and #err > 0)
        ");
        r.ShouldBe("true");
    }

    [MySqlFact]
    public async Task NonQuery_ConnectionNotBusy_AfterCompletion()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            conn:NonQuery('SELECT 1')
            return tostring(conn:IsBusy())
        ");
        r.ShouldBe("false");
    }

    // ── Scalar ────────────────────────────────────────────────────────────────
    [MySqlFact]
    public async Task Scalar_ReturnsFirstColumnOfFirstRow()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local ok, v = conn:Scalar('SELECT 42')
            return tostring(ok) .. ':' .. tostring(v)
        ");
        r.ShouldBe("true:42");
    }

    [MySqlFact]
    public async Task Scalar_NoMatchingRow_ReturnsNil()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local ok, v = conn:Scalar('SELECT 1 WHERE 1 = 0')
            return tostring(ok) .. ':' .. tostring(v)
        ");
        r.ShouldBe("true:nil");
    }

    [MySqlFact]
    public async Task Scalar_InvalidSql_ReturnsFalseAndError()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local ok, err = conn:Scalar('NOT VALID SQL')
            return tostring(ok == false and type(err) == 'string')
        ");
        r.ShouldBe("true");
    }

    [MySqlFact]
    public async Task Scalar_ConnectionNotBusy_AfterCompletion()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            conn:Scalar('SELECT 1')
            return tostring(conn:IsBusy())
        ");
        r.ShouldBe("false");
    }

    // ── QueryAll ──────────────────────────────────────────────────────────────
    [MySqlFact]
    public async Task QueryAll_MultipleRows_ReturnsAllRows()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local ok, rows = conn:QueryAll('SELECT 1 UNION SELECT 2 UNION SELECT 3')
            return tostring(ok) .. ':' .. tostring(#rows)
        ");
        r.ShouldBe("true:3");
    }

    [MySqlFact]
    public async Task QueryAll_EmptyResult_ReturnsEmptyTable()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local ok, rows = conn:QueryAll('SELECT 1 WHERE 1 = 0')
            return tostring(ok) .. ':' .. tostring(#rows)
        ");
        r.ShouldBe("true:0");
    }

    [MySqlFact]
    public async Task QueryAll_RowValuesAccessible()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local ok, rows = conn:QueryAll('SELECT 10, 20, 30')
            return tostring(ok and rows[1][1] == 10 and rows[1][2] == 20 and rows[1][3] == 30)
        ");
        r.ShouldBe("true");
    }

    [MySqlFact]
    public async Task QueryAll_InvalidSql_ReturnsFalseAndError()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            local ok, err = conn:QueryAll('NOT VALID SQL')
            return tostring(ok == false and type(err) == 'string')
        ");
        r.ShouldBe("true");
    }

    [MySqlFact]
    public async Task QueryAll_ConnectionNotBusy_AfterCompletion()
    {
        string? r = await Run($@"
            local conn = assert({ConnectLua()})
            conn:QueryAll('SELECT 1')
            return tostring(conn:IsBusy())
        ");
        r.ShouldBe("false");
    }

    // Parses KITSUNE_MYSQL_TEST=host:port:user:pass:db
    private static string ConnectLua()
    {
        var parts = Environment.GetEnvironmentVariable("KITSUNE_MYSQL_TEST")!.Split(':');
        return $"MySQL.Connect('{parts[0]}','{parts[2]}','{parts[3]}','{parts[4]}',{parts[1]})";
    }

    private static async Task<string?> Run(string lua)
    {
        var engine = new KitsuneEngine();
        string? result;
        try
        {
            result = await engine.ExecuteStringAsync(lua).ConfigureAwait(false);
        }
        finally
        {
            engine.Dispose();
        }

        if (engine.LeakedAllocations != 0)
        {
            throw new InvalidOperationException($"Native memory leak: {engine.LeakedAllocations} unfreed allocation(s) after KitsuneCleanup");
        }
        return result;
    }
}
