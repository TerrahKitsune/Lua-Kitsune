using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

[Collection("KitsuneSequential")]
public sealed class MySqlTests
{
    // coroutine protocol
    [MySqlFact]
    public async Task Helper_MethodsExistInModuleTable()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            local types = tostring(type(MySQL.NonQuery)) .. ':' .. tostring(type(MySQL.Scalar)) .. ':' .. tostring(type(MySQL.QueryAll))
            local ok, v = MySQL.Scalar(conn, 'SELECT 42')
            conn:Close()
            return types .. ':' .. tostring(ok) .. ':' .. tostring(v)
        ");
        r.String.ShouldBe("function:function:function:true:42");
    }

    [MySqlFact]
    public async Task Connect_ValidCredentials_ReturnsUserdata()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn, err = {ConnectLua()}
            if not conn then error(err) end
            return tostring(conn):sub(1,6)
        ");
        r.String.ShouldBe("MySQL:");
    }

    [MySqlFact]
    public async Task Connect_BadCredentials_ReturnsNilAndError()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn, err = MySQL.Connect('127.0.0.1','bad_user','bad_pass','bad_db',3306)
            return tostring(conn == nil) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("true:true");
    }

    [MySqlFact]
    public async Task RawSelect_CoroutineProtocol_YieldsNilThenRowcountThenRows()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
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
        r.String.ShouldBe("1:1:2:true");
    }

    [MySqlFact]
    public async Task IsBusy_TrueWhileSuspended_FalseOnceDead()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            local co = assert(conn:Query('SELECT 1'))
            -- after first resume T is yielded (async wait or rowcount phase)
            coroutine.resume(co)
            local status1 = coroutine.status(co)
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
            return status1 .. ':' .. tostring(busy1) .. ':' .. tostring(busy2)
        ");

        // IsBusy is documented as true while the query coroutine is alive (suspended).
        r.String.ShouldBe("suspended:true:false");
    }

    [MySqlFact]
    public async Task EscapeValue_EscapesSingleQuoteAndBackslash()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            local s = conn:EscapeValue(""it's a \\ test"")
            return s
        ");
        r.ShouldBe(@"it\'s a \\ test");
    }

    [MySqlFact]
    public async Task Close_IsIdempotent()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            conn:Close()
            conn:Close()
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    // CRUD helpers shared by every test below
    //
    // exec(sql, params?) ? rowcount, co
    //   Drives the inner query coroutine T to the rowcount phase.
    //   Caller MUST drain co before the next query on the same connection:
    //     DML  ? coroutine.resume(co, true)   (stop flag, T dies)
    //     SELECT (after reading rows) ? coroutine.resume(co, true)

    // INSERT
    [MySqlFact]
    public async Task Crud_Insert_ReturnsOneAffectedRow()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        try
        {
            LuaValue r = await engine.ExecuteStringAsync($@"
                local conn = assert({ConnectLua()})
                local function exec(sql, p)
                    local co = assert(conn:Query(sql, p))
                    local ok, v = coroutine.resume(co)
                    while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                    if not ok then error(tostring(v)) end
                    if type(v) == 'string' then error(v) end
                    return v, co
                end
                local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_insert_' .. RUN_ID}})
                coroutine.resume(c0, true)
                local rowcount, c1 = exec('INSERT INTO test (String) VALUES (?)', {{'crud_insert_' .. RUN_ID}})
                coroutine.resume(c1, true)
                return tostring(rowcount)
            ");
            r.String.ShouldBe("1");
        }
        finally
        {
            await DeleteTestRowsAsync(engine, "crud_insert");
        }
    }

    // SELECT
    [MySqlFact]
    public async Task Crud_Select_ReturnsInsertedRow()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        try
        {
            LuaValue r = await engine.ExecuteStringAsync($@"
                local conn = assert({ConnectLua()})
                local function exec(sql, p)
                    local co = assert(conn:Query(sql, p))
                    local ok, v = coroutine.resume(co)
                    while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                    if not ok then error(tostring(v)) end
                    if type(v) == 'string' then error(v) end
                    return v, co
                end
                local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_sel_' .. RUN_ID}})
                coroutine.resume(c0, true)
                local _, c1 = exec('INSERT INTO test (String, bool) VALUES (?, ?)', {{'crud_sel_' .. RUN_ID, 1}})
                coroutine.resume(c1, true)
                local _, co = exec('SELECT String, bool FROM test WHERE String = ?', {{'crud_sel_' .. RUN_ID}})
                local ok, row = coroutine.resume(co)
                coroutine.resume(co, true)
                return tostring(row[1]) .. ':' .. tostring(row[2])
            ");
            r.String.ShouldBe(TestRun.Name("crud_sel") + ":1");
        }
        finally
        {
            await DeleteTestRowsAsync(engine, "crud_sel");
        }
    }

    // UPDATE
    [MySqlFact]
    public async Task Crud_Update_ModifiesRow()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        try
        {
            LuaValue r = await engine.ExecuteStringAsync($@"
                local conn = assert({ConnectLua()})
                local function exec(sql, p)
                    local co = assert(conn:Query(sql, p))
                    local ok, v = coroutine.resume(co)
                    while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                    if not ok then error(tostring(v)) end
                    if type(v) == 'string' then error(v) end
                    return v, co
                end
                local _, c0 = exec('DELETE FROM test WHERE String IN (?, ?)', {{'crud_upd_old_' .. RUN_ID, 'crud_upd_new_' .. RUN_ID}})
                coroutine.resume(c0, true)
                local _, c1 = exec('INSERT INTO test (String) VALUES (?)', {{'crud_upd_old_' .. RUN_ID}})
                coroutine.resume(c1, true)
                local affected, c2 = exec('UPDATE test SET String = ? WHERE String = ?', {{'crud_upd_new_' .. RUN_ID, 'crud_upd_old_' .. RUN_ID}})
                coroutine.resume(c2, true)
                local _, co = exec('SELECT String FROM test WHERE String = ?', {{'crud_upd_new_' .. RUN_ID}})
                local ok, row = coroutine.resume(co)
                coroutine.resume(co, true)
                return tostring(affected) .. ':' .. tostring(row[1])
            ");
            r.String.ShouldBe("1:" + TestRun.Name("crud_upd_new"));
        }
        finally
        {
            await DeleteTestRowsAsync(engine, "crud_upd_old", "crud_upd_new");
        }
    }

    // DELETE
    [MySqlFact]
    public async Task Crud_Delete_RemovesRow()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        try
        {
            LuaValue r = await engine.ExecuteStringAsync($@"
                local conn = assert({ConnectLua()})
                local function exec(sql, p)
                    local co = assert(conn:Query(sql, p))
                    local ok, v = coroutine.resume(co)
                    while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                    if not ok then error(tostring(v)) end
                    if type(v) == 'string' then error(v) end
                    return v, co
                end
                local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_del_' .. RUN_ID}})
                coroutine.resume(c0, true)
                local _, c1 = exec('INSERT INTO test (String) VALUES (?)', {{'crud_del_' .. RUN_ID}})
                coroutine.resume(c1, true)
                local _, c2 = exec('DELETE FROM test WHERE String = ?', {{'crud_del_' .. RUN_ID}})
                coroutine.resume(c2, true)
                local rowcount, co = exec('SELECT Id FROM test WHERE String = ?', {{'crud_del_' .. RUN_ID}})
                local ok, row = coroutine.resume(co)
                coroutine.resume(co, true)
                return tostring(rowcount) .. ':' .. tostring(row == nil)
            ");
            r.String.ShouldBe("0:true");
        }
        finally
        {
            await DeleteTestRowsAsync(engine, "crud_del");
        }
    }

    // multiple rows
    [MySqlFact]
    public async Task Crud_Select_MultipleRowsReturned()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        try
        {
            LuaValue r = await engine.ExecuteStringAsync($@"
                local conn = assert({ConnectLua()})
                local function exec(sql, p)
                    local co = assert(conn:Query(sql, p))
                    local ok, v = coroutine.resume(co)
                    while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                    if not ok then error(tostring(v)) end
                    if type(v) == 'string' then error(v) end
                    return v, co
                end
                local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_multi_' .. RUN_ID}})
                coroutine.resume(c0, true)
                for i = 1, 3 do
                    local _, ci = exec('INSERT INTO test (String) VALUES (?)', {{'crud_multi_' .. RUN_ID}})
                    coroutine.resume(ci, true)
                end
                local rowcount, co = exec('SELECT Id FROM test WHERE String = ? ORDER BY Id', {{'crud_multi_' .. RUN_ID}})
                local count, ok, row = 0, coroutine.resume(co)
                while ok and row ~= nil do count = count + 1; ok, row = coroutine.resume(co) end
                return tostring(rowcount) .. ':' .. tostring(count)
            ");
            r.String.ShouldBe("3:3");
        }
        finally
        {
            await DeleteTestRowsAsync(engine, "crud_multi");
        }
    }

    // type mapping
    [MySqlFact]
    public async Task Crud_Types_IntColumnsReturnedAsInteger()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        try
        {
            LuaValue r = await engine.ExecuteStringAsync($@"
                local conn = assert({ConnectLua()})
                local function exec(sql, p)
                    local co = assert(conn:Query(sql, p))
                    local ok, v = coroutine.resume(co)
                    while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                    if not ok then error(tostring(v)) end
                    if type(v) == 'string' then error(v) end
                    return v, co
                end
                local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_tint_' .. RUN_ID}})
                coroutine.resume(c0, true)
                local _, c1 = exec('INSERT INTO test (String, bool) VALUES (?, ?)', {{'crud_tint_' .. RUN_ID, 1}})
                coroutine.resume(c1, true)
                local _, co = exec('SELECT Id, bool FROM test WHERE String = ?', {{'crud_tint_' .. RUN_ID}})
                local ok, row = coroutine.resume(co)
                coroutine.resume(co, true)
                return tostring(math.type(row[1])) .. ':' .. tostring(math.type(row[2])) .. ':' .. tostring(row[1] > 0) .. ':' .. tostring(row[2])
            ");
            r.String.ShouldBe("integer:integer:true:1");
        }
        finally
        {
            await DeleteTestRowsAsync(engine, "crud_tint");
        }
    }

    [MySqlFact]
    public async Task Crud_Types_FloatDoubleAsNumber_DecimalAsDecimal()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        try
        {
            LuaValue r = await engine.ExecuteStringAsync($@"
                local conn = assert({ConnectLua()})
                local function exec(sql, p)
                    local co = assert(conn:Query(sql, p))
                    local ok, v = coroutine.resume(co)
                    while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                    if not ok then error(tostring(v)) end
                    if type(v) == 'string' then error(v) end
                    return v, co
                end
                local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_tnum_' .. RUN_ID}})
                coroutine.resume(c0, true)
                local _, c1 = exec('INSERT INTO test (String, `float`, `double`, `decimal`) VALUES (?, ?, ?, ?)', {{'crud_tnum_' .. RUN_ID, 1.5, 2.5, 42}})
                coroutine.resume(c1, true)
                local _, co = exec('SELECT `float`, `double`, `decimal` FROM test WHERE String = ?', {{'crud_tnum_' .. RUN_ID}})
                local ok, row = coroutine.resume(co)
                coroutine.resume(co, true)
                return tostring(type(row[1])) .. ':' .. tostring(type(row[2])) .. ':' .. tostring(type(row[3])) .. ':' .. tostring(row[1] == 1.5) .. ':' .. tostring(row[2] == 2.5) .. ':' .. tostring(row[3]:ToNumber() == 42)
            ");
            r.String.ShouldBe("number:number:userdata:true:true:true");
        }
        finally
        {
            await DeleteTestRowsAsync(engine, "crud_tnum");
        }
    }

    [MySqlFact]
    public async Task Crud_Types_NullableColumnsReturnedAsNil()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        try
        {
            LuaValue r = await engine.ExecuteStringAsync($@"
                local conn = assert({ConnectLua()})
                local function exec(sql, p)
                    local co = assert(conn:Query(sql, p))
                    local ok, v = coroutine.resume(co)
                    while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                    if not ok then error(tostring(v)) end
                    if type(v) == 'string' then error(v) end
                    return v, co
                end
                local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_tnull_' .. RUN_ID}})
                coroutine.resume(c0, true)
                local _, c1 = exec('INSERT INTO test (String) VALUES (?)', {{'crud_tnull_' .. RUN_ID}})
                coroutine.resume(c1, true)
                local _, co = exec('SELECT `Blob`, Json, datetime, `float`, `double`, `decimal`, bool FROM test WHERE String = ?', {{'crud_tnull_' .. RUN_ID}})
                local ok, row = coroutine.resume(co)
                coroutine.resume(co, true)
                local allnil = row[1]==nil and row[2]==nil and row[3]==nil and row[4]==nil and row[5]==nil and row[6]==nil and row[7]==nil
                return tostring(allnil)
            ");
            r.String.ShouldBe("true");
        }
        finally
        {
            await DeleteTestRowsAsync(engine, "crud_tnull");
        }
    }

    [MySqlFact]
    public async Task Crud_Types_DatetimeAsDateTime_JsonAsString()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        try
        {
            LuaValue r = await engine.ExecuteStringAsync($@"
                local conn = assert({ConnectLua()})
                local function exec(sql, p)
                    local co = assert(conn:Query(sql, p))
                    local ok, v = coroutine.resume(co)
                    while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
                    if not ok then error(tostring(v)) end
                    if type(v) == 'string' then error(v) end
                    return v, co
                end
                local _, c0 = exec('DELETE FROM test WHERE String = ?', {{'crud_tstr_' .. RUN_ID}})
                coroutine.resume(c0, true)
                local _, c1 = exec('INSERT INTO test (String, datetime, Json) VALUES (?, NOW(), ?)', {{'crud_tstr_' .. RUN_ID, '{{""k"":1}}'}})
                coroutine.resume(c1, true)
                local _, co = exec('SELECT datetime, Json FROM test WHERE String = ?', {{'crud_tstr_' .. RUN_ID}})
                local ok, row = coroutine.resume(co)
                coroutine.resume(co, true)
                local nearNow = math.abs(DateTime.UnixSeconds(row[1]) - os.time()) < 172800
                return tostring(type(row[1])) .. ':' .. tostring(type(row[2])) .. ':' .. tostring(nearNow) .. ':' .. tostring(Json.New():Decode(row[2]).k)
            ");
            r.String.ShouldBe("userdata:string:true:1");
        }
        finally
        {
            await DeleteTestRowsAsync(engine, "crud_tstr");
        }
    }

    // NonQuery
    [MySqlFact]
    public async Task NonQuery_Insert_ReturnsAffectedCount()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        try
        {
            LuaValue r = await engine.ExecuteStringAsync($@"
                local conn = assert({ConnectLua()})
                conn:NonQuery('DELETE FROM test WHERE String = ?', {{'helper_nq_' .. RUN_ID}})
                local ok, n = conn:NonQuery('INSERT INTO test (String) VALUES (?)', {{'helper_nq_' .. RUN_ID}})
                conn:NonQuery('DELETE FROM test WHERE String = ?', {{'helper_nq_' .. RUN_ID}})
                return tostring(ok) .. ':' .. tostring(n)
            ");
            r.String.ShouldBe("true:1");
        }
        finally
        {
            await DeleteTestRowsAsync(engine, "helper_nq");
        }
    }

    [MySqlFact]
    public async Task NonQuery_ConnectionNotBusy_AfterCompletion()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            conn:NonQuery('SELECT 1')
            return tostring(conn:IsBusy())
        ");
        r.String.ShouldBe("false");
    }

    // Scalar
    [MySqlFact]
    public async Task Scalar_ReturnsFirstColumnOfFirstRow()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            local ok, v = conn:Scalar('SELECT 42')
            return tostring(ok) .. ':' .. tostring(v)
        ");
        r.String.ShouldBe("true:42");
    }

    [MySqlFact]
    public async Task Scalar_NoMatchingRow_ReturnsNil()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            local ok, v = conn:Scalar('SELECT 1 WHERE 1 = 0')
            return tostring(ok) .. ':' .. tostring(v)
        ");
        r.String.ShouldBe("true:nil");
    }

    [MySqlFact]
    public async Task Scalar_InvalidSql_ReturnsFalseAndError()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            local ok, err = conn:Scalar('NOT VALID SQL')
            return tostring(ok == false and type(err) == 'string')
        ");
        r.String.ShouldBe("true");
    }

    [MySqlFact]
    public async Task Scalar_ConnectionNotBusy_AfterCompletion()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            conn:Scalar('SELECT 1')
            return tostring(conn:IsBusy())
        ");
        r.String.ShouldBe("false");
    }

    // QueryAll
    [MySqlFact]
    public async Task QueryAll_MultipleRows_ReturnsAllRows()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            local ok, rows = conn:QueryAll('SELECT 1 UNION SELECT 2 UNION SELECT 3')
            return tostring(ok) .. ':' .. tostring(#rows)
        ");
        r.String.ShouldBe("true:3");
    }

    [MySqlFact]
    public async Task QueryAll_EmptyResult_ReturnsEmptyTable()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            local ok, rows = conn:QueryAll('SELECT 1 WHERE 1 = 0')
            return tostring(ok) .. ':' .. tostring(#rows)
        ");
        r.String.ShouldBe("true:0");
    }

    [MySqlFact]
    public async Task QueryAll_RowValuesAccessible()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            local ok, rows = conn:QueryAll('SELECT 10, 20, 30')
            return tostring(ok and rows[1][1] == 10 and rows[1][2] == 20 and rows[1][3] == 30)
        ");
        r.String.ShouldBe("true");
    }

    [MySqlFact]
    public async Task QueryAll_InvalidSql_ReturnsFalseAndError()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            local ok, err = conn:QueryAll('NOT VALID SQL')
            return tostring(ok == false and type(err) == 'string')
        ");
        r.String.ShouldBe("true");
    }

    [MySqlFact]
    public async Task QueryAll_ConnectionNotBusy_AfterCompletion()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            conn:QueryAll('SELECT 1')
            return tostring(conn:IsBusy())
        ");
        r.String.ShouldBe("false");
    }

    // AliveToken integration
    [MySqlFact]
    public async Task MySQL_SetAliveToken_DoesNotRaise()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            local token = AliveToken.New()
            local attached = pcall(conn.SetAliveToken, conn, token)
            local ok, v = conn:Scalar('SELECT 7')
            conn:Close()
            return tostring(attached) .. ':' .. tostring(ok) .. ':' .. tostring(v)
        ");
        r.String.ShouldBe("true:true:7");
    }

    [MySqlFact]
    public async Task MySQL_SetAliveToken_NilDetaches()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            local token = AliveToken.New()
            conn:SetAliveToken(token)
            conn:SetAliveToken(nil)
            token:Dispose()
            local ok, v = conn:Scalar('SELECT 5')
            conn:Close()
            return tostring(ok) .. ':' .. tostring(v)
        ");
        r.String.ShouldBe("true:5");
    }

    [MySqlFact]
    public async Task MySQL_AliveToken_LiveToken_NonQueryCompletes()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            local token = AliveToken.New()
            conn:SetAliveToken(token)
            local ok, n = conn:NonQuery('SELECT 1')
            conn:Close()
            return tostring(ok)
        ");
        r.String.ShouldBe("true");
    }

    [MySqlFact]
    public async Task MySQL_AliveToken_DisposedBeforeCall_ReturnsCancelled()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            local token = AliveToken.New()
            conn:SetAliveToken(token)
            token:Dispose()
            local ok, err = conn:NonQuery('SELECT 1')
            conn:Close()
            return tostring(ok) .. '|' .. tostring(err)
        ");
        r.String.ShouldBe("false|cancelled");
    }

    private static async Task DeleteTestRowsAsync(KitsuneEngine engine, params string[] markers)
    {
        var markerList = string.Join(", ", markers.Select(m => $"'{m}'"));
        await engine.ExecuteStringAsync($@"
            local conn = assert({ConnectLua()})
            for _, m in ipairs({{{markerList}}}) do
                conn:NonQuery('DELETE FROM test WHERE String = ?', {{m .. '_' .. RUN_ID}})
            end
            conn:Close()
        ");
    }

    // Parses KITSUNE_MYSQL_TEST=host:port:user:pass:db
    private static string ConnectLua()
    {
        var parts = Environment.GetEnvironmentVariable("KITSUNE_MYSQL_TEST")!.Split(':');
        return $"MySQL.Connect('{parts[0]}','{parts[2]}','{parts[3]}','{parts[4]}',{parts[1]})";
    }
}
