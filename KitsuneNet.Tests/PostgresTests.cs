using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

[Collection("KitsuneSequential")]
public sealed class PostgresTests
{
    // Coroutine protocol
    [PostgresFact]
    public async Task Helper_MethodsExistInModuleTable()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local types = tostring(type(Postgres.NonQuery)) .. ':' .. tostring(type(Postgres.Scalar)) .. ':' .. tostring(type(Postgres.QueryAll))
			local ok, v = Postgres.Scalar(conn, 'SELECT 42::int')
			conn:Close()
			return types .. ':' .. tostring(ok) .. ':' .. tostring(v)
		");
        r.String.ShouldBe("function:function:function:true:42");
    }

    [PostgresFact]
    public async Task Connect_ValidCredentials_ReturnsUserdata()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn, err = {ConnectLua()}
			if not conn then error(err) end
			return tostring(conn):sub(1, 9)
		");
        r.String.ShouldBe("Postgres:");
    }

    [PostgresFact]
    public async Task Connect_BadCredentials_ReturnsNilAndError()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync(@"
			local conn, err = Postgres.Connect('host=127.0.0.1 user=bad_user password=bad_pass dbname=bad_db')
			return tostring(conn == nil) .. ':' .. tostring(type(err) == 'string')
		");
        r.String.ShouldBe("true:true");
    }

    [PostgresFact]
    public async Task RawSelect_CoroutineProtocol_YieldsNilThenRowcountThenRows()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn, err = {ConnectLua()}
			if not conn then error(err) end
			local co, cerr = conn:Query('SELECT 1::int AS n, 2::int AS m')
			if not co then error(cerr) end
			local ok, val = coroutine.resume(co)
			while ok and val == nil and coroutine.status(co) == 'suspended' do
				ok, val = coroutine.resume(co)
			end
			if not ok then error(val) end
			if type(val) == 'string' then error(val) end
			local rowcount = val
			ok, val = coroutine.resume(co)
			local col1 = val and val[1]
			local col2 = val and val[2]
			ok, val = coroutine.resume(co)
			return tostring(rowcount) .. ':' .. tostring(col1) .. ':' .. tostring(col2) .. ':' .. tostring(val == nil)
		");
        r.String.ShouldBe("1:1:2:true");
    }

    [PostgresFact]
    public async Task RawQuery_Error_Phase2YieldsString()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query('SELECT * FROM _no_such_table_xyz'))
			local ok, val = coroutine.resume(co)
			while ok and val == nil and coroutine.status(co) == 'suspended' do
				ok, val = coroutine.resume(co)
			end
			return tostring(ok) .. ':' .. tostring(type(val) == 'string')
		");
        r.String.ShouldBe("true:true");
    }

    [PostgresFact]
    public async Task StopFlag_MidWait_ConnNotBusy()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query('SELECT 1'))
			coroutine.resume(co, true)
			return tostring(conn:IsBusy())
		");
        r.String.ShouldBe("false");
    }

    [PostgresFact]
    public async Task StopFlag_AfterQuerySent_CancelsAndConnectionIsReusable()
    {
        // Stopping once the statement is on the server must cancel it and drain its
        // result; otherwise the next query fails with "another command is already in
        // progress" (or waits for the whole pg_sleep).
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query('SELECT pg_sleep(10)'))
			coroutine.resume(co)          -- sends the statement
			coroutine.resume(co)          -- polling: the server is still sleeping
			local t0 = Time()
			coroutine.resume(co, true)    -- stop mid-poll
			local stopMs = Time() - t0
			local ok, v = conn:Scalar('SELECT 42')
			conn:Close()
			return tostring(ok == true and v == 42 and stopMs < 8000) .. ' ' .. tostring(v) .. ' ' .. stopMs
		");
        r.String.ShouldStartWith("true");
    }

    [PostgresFact]
    public async Task Timestamptz_WholeHourAndMinuteOffsets_ReturnDateTime()
    {
        // Postgres prints whole-hour offsets as "+00" / "-05"; they must still
        // come back as DateTime.
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local out = {{}}
			for _, tz in ipairs({{ 'UTC', 'America/New_York', 'Asia/Kolkata' }}) do
				assert(conn:NonQuery(""SET TIME ZONE '"" .. tz .. ""'""))
				local ok, v = conn:Scalar(""SELECT '2024-01-01 12:00:00.250Z'::timestamptz"")
				out[#out + 1] = type(v) .. '/' .. (type(v) == 'userdata'
					and (v:OffsetMinutes() .. '/' .. v:UnixMilliseconds()) or tostring(v))
			end
			conn:Close()
			return table.concat(out, ',')
		");
        r.String.ShouldBe("userdata/0/1704110400250,userdata/-300/1704110400250,userdata/330/1704110400250");
    }

    [PostgresFact]
    public async Task Dates_WithUnparsedSuffix_FallBackToString()
    {
        // A BC date or a pre-1900 local-mean-time offset with seconds must come back
        // as a string; parsing it as DateTime would silently ignore the suffix.
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			assert(conn:NonQuery(""SET TIME ZONE 'Europe/Stockholm'""))
			local _, bc  = conn:Scalar(""SELECT '0044-03-15 BC'::date"")
			local _, lmt = conn:Scalar(""SELECT '1850-01-01 12:00:00'::timestamptz"")
			local _, ok  = conn:Scalar(""SELECT '2024-01-01 12:00:00'::timestamptz"")
			conn:Close()
			-- The exact LMT offset depends on the server's tzdata; it always has seconds.
			local lmtShape = type(lmt) == 'string' and lmt:match('[+-]%d%d:%d%d:%d%d$') ~= nil
			return type(bc) .. ':' .. tostring(bc) .. ',' .. tostring(lmtShape) .. ',' .. type(ok)
		");
        r.String.ShouldBe("string:0044-03-15 BC,true,userdata");
    }

    [PostgresFact]
    public async Task StopFlag_MidStream_ConnNotBusy()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query('SELECT generate_series(1,3)'))
			local ok, val = coroutine.resume(co)
			while ok and val == nil and coroutine.status(co) == 'suspended' do
				ok, val = coroutine.resume(co)
			end
			coroutine.resume(co)
			coroutine.resume(co, true)
			return tostring(conn:IsBusy())
		");
        r.String.ShouldBe("false");
    }

    [PostgresFact]
    public async Task IsBusy_TrueWhileSuspended_FalseOnceDead()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query('SELECT 1'))
			coroutine.resume(co)
			local status1 = coroutine.status(co)
			local busy1 = conn:IsBusy()
			local ok, val = coroutine.resume(co)
			while ok and val == nil and coroutine.status(co) == 'suspended' do
				ok, val = coroutine.resume(co)
			end
			ok, val = coroutine.resume(co)
			while ok and val ~= nil do ok, val = coroutine.resume(co) end
			local busy2 = conn:IsBusy()
			return status1 .. ':' .. tostring(busy1) .. ':' .. tostring(busy2)
		");

        // IsBusy is documented as true while the query coroutine is alive (suspended).
        r.String.ShouldBe("suspended:true:false");
    }

    [PostgresFact]
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

    // CRUD
    [PostgresFact]
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
				local _, c0 = exec('DELETE FROM test WHERE string = $1', {{'crud_insert_' .. RUN_ID}})
				coroutine.resume(c0, true)
				local rowcount, c1 = exec('INSERT INTO test (string) VALUES ($1)', {{'crud_insert_' .. RUN_ID}})
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

    [PostgresFact]
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
				local _, c0 = exec('DELETE FROM test WHERE string = $1', {{'crud_sel_' .. RUN_ID}})
				coroutine.resume(c0, true)
				local _, c1 = exec('INSERT INTO test (string) VALUES ($1)', {{'crud_sel_' .. RUN_ID}})
				coroutine.resume(c1, true)
				local _, co = exec('SELECT string FROM test WHERE string = $1', {{'crud_sel_' .. RUN_ID}})
				local ok, row = coroutine.resume(co)
				coroutine.resume(co, true)
				return tostring(row[1])
			");
            r.String.ShouldBe(TestRun.Name("crud_sel"));
        }
        finally
        {
            await DeleteTestRowsAsync(engine, "crud_sel");
        }
    }

    [PostgresFact]
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
				local _, c0 = exec('DELETE FROM test WHERE string IN ($1, $2)', {{'crud_upd_old_' .. RUN_ID, 'crud_upd_new_' .. RUN_ID}})
				coroutine.resume(c0, true)
				local _, c1 = exec('INSERT INTO test (string) VALUES ($1)', {{'crud_upd_old_' .. RUN_ID}})
				coroutine.resume(c1, true)
				local affected, c2 = exec('UPDATE test SET string = $1 WHERE string = $2', {{'crud_upd_new_' .. RUN_ID, 'crud_upd_old_' .. RUN_ID}})
				coroutine.resume(c2, true)
				local _, co = exec('SELECT string FROM test WHERE string = $1', {{'crud_upd_new_' .. RUN_ID}})
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

    [PostgresFact]
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
				local _, c0 = exec('DELETE FROM test WHERE string = $1', {{'crud_del_' .. RUN_ID}})
				coroutine.resume(c0, true)
				local _, c1 = exec('INSERT INTO test (string) VALUES ($1)', {{'crud_del_' .. RUN_ID}})
				coroutine.resume(c1, true)
				local _, c2 = exec('DELETE FROM test WHERE string = $1', {{'crud_del_' .. RUN_ID}})
				coroutine.resume(c2, true)
				local rowcount, co = exec('SELECT id FROM test WHERE string = $1', {{'crud_del_' .. RUN_ID}})
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

    // Type mapping
    [PostgresFact]
    public async Task Types_IntegersReturnedAsInteger()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query('SELECT 1::int2, 2::int4, 3::int8'))
			local ok, v = coroutine.resume(co)
			while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
			ok, v = coroutine.resume(co)
			coroutine.resume(co, true)
			return tostring(math.type(v[1])) .. ':' .. tostring(math.type(v[2])) .. ':' .. tostring(math.type(v[3])) .. ':' .. v[1] .. ':' .. v[2] .. ':' .. v[3]
		");
        r.String.ShouldBe("integer:integer:integer:1:2:3");
    }

    [PostgresFact]
    public async Task Types_FloatsAsNumber_NumericAsDecimal()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query('SELECT 1.5::float4, 2.5::float8, 3.0::numeric'))
			local ok, v = coroutine.resume(co)
			while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
			ok, v = coroutine.resume(co)
			coroutine.resume(co, true)
			return tostring(type(v[1])) .. ':' .. tostring(type(v[2])) .. ':' .. tostring(type(v[3])) .. ':' .. tostring(v[1] == 1.5) .. ':' .. tostring(v[2] == 2.5) .. ':' .. tostring(v[3]:ToNumber() == 3)
		");
        r.String.ShouldBe("number:number:userdata:true:true:true");
    }

    [PostgresFact]
    public async Task Types_BoolReturnedAsBoolean()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query('SELECT true::bool, false::bool'))
			local ok, v = coroutine.resume(co)
			while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
			ok, v = coroutine.resume(co)
			coroutine.resume(co, true)
			return tostring(type(v[1])) .. ':' .. tostring(v[1]) .. ':' .. tostring(v[2])
		");
        r.String.ShouldBe("boolean:true:false");
    }

    [PostgresFact]
    public async Task Types_TextReturnedAsString()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query(""SELECT 'hello'::text, 'world'::varchar""))
			local ok, v = coroutine.resume(co)
			while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
			ok, v = coroutine.resume(co)
			coroutine.resume(co, true)
			return tostring(type(v[1])) .. ':' .. tostring(v[1]) .. ':' .. tostring(v[2])
		");
        r.String.ShouldBe("string:hello:world");
    }

    [PostgresFact]
    public async Task Types_NullReturnedAsNil()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query('SELECT NULL::int, NULL::text'))
			local ok, v = coroutine.resume(co)
			while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
			ok, v = coroutine.resume(co)
			coroutine.resume(co, true)
			return tostring(v[1] == nil) .. ':' .. tostring(v[2] == nil)
		");
        r.String.ShouldBe("true:true");
    }

    // NonQuery
    [PostgresFact]
    public async Task NonQuery_Insert_ReturnsAffectedCount()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        try
        {
            LuaValue r = await engine.ExecuteStringAsync($@"
				local conn = assert({ConnectLua()})
				conn:NonQuery('DELETE FROM test WHERE string = $1', {{'helper_nq_' .. RUN_ID}})
				local ok, n = conn:NonQuery('INSERT INTO test (string) VALUES ($1)', {{'helper_nq_' .. RUN_ID}})
				conn:NonQuery('DELETE FROM test WHERE string = $1', {{'helper_nq_' .. RUN_ID}})
				return tostring(ok) .. ':' .. tostring(n)
			");
            r.String.ShouldBe("true:1");
        }
        finally
        {
            await DeleteTestRowsAsync(engine, "helper_nq");
        }
    }

    [PostgresFact]
    public async Task NonQuery_InvalidSql_ReturnsFalseAndError()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local ok, err = conn:NonQuery('NOT VALID SQL AT ALL')
			return tostring(ok == false and type(err) == 'string' and #err > 0)
		");
        r.String.ShouldBe("true");
    }

    [PostgresFact]
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
    [PostgresFact]
    public async Task Scalar_ReturnsFirstColumnOfFirstRow()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local ok, v = conn:Scalar('SELECT 42::int')
			return tostring(ok) .. ':' .. tostring(v)
		");
        r.String.ShouldBe("true:42");
    }

    [PostgresFact]
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

    [PostgresFact]
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

    [PostgresFact]
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
    [PostgresFact]
    public async Task QueryAll_MultipleRows_ReturnsAllRows()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local ok, rows = conn:QueryAll('SELECT generate_series(1,3)')
			return tostring(ok) .. ':' .. tostring(#rows)
		");
        r.String.ShouldBe("true:3");
    }

    [PostgresFact]
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

    [PostgresFact]
    public async Task QueryAll_RowValuesAccessible()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local ok, rows = conn:QueryAll('SELECT 10::int, 20::int, 30::int')
			return tostring(ok and rows[1][1] == 10 and rows[1][2] == 20 and rows[1][3] == 30)
		");
        r.String.ShouldBe("true");
    }

    [PostgresFact]
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

    [PostgresFact]
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
    [PostgresFact]
    public async Task Postgres_SetAliveToken_DoesNotRaise()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local token = AliveToken.New()
			local attached = pcall(conn.SetAliveToken, conn, token)
			local ok, v = conn:Scalar('SELECT 7::int')
			conn:Close()
			return tostring(attached) .. ':' .. tostring(ok) .. ':' .. tostring(v)
		");
        r.String.ShouldBe("true:true:7");
    }

    [PostgresFact]
    public async Task Postgres_SetAliveToken_NilDetaches()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
			local conn = assert({ConnectLua()})
			local token = AliveToken.New()
			conn:SetAliveToken(token)
			conn:SetAliveToken(nil)
			token:Dispose()
			local ok, v = conn:Scalar('SELECT 5::int')
			conn:Close()
			return tostring(ok) .. ':' .. tostring(v)
		");
        r.String.ShouldBe("true:5");
    }

    [PostgresFact]
    public async Task Postgres_AliveToken_LiveToken_NonQueryCompletes()
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

    [PostgresFact]
    public async Task Postgres_AliveToken_DisposedBeforeCall_ReturnsCancelled()
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
				conn:NonQuery('DELETE FROM test WHERE string = $1', {{m .. '_' .. RUN_ID}})
			end
			conn:Close()
		");
    }

    // Reads KITSUNE_POSTGRES_TEST=<libpq conninfo>
    private static string ConnectLua()
    {
        var conninfo = Environment.GetEnvironmentVariable("KITSUNE_POSTGRES_TEST")!;
        return $"Postgres.Connect('{conninfo}')";
    }
}
