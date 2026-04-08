using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

[Collection("KitsuneSequential")]
public sealed class PostgresTests
{
    // ── coroutine protocol ───────────────────────────────────────────────────
    [PostgresFact]
    public async Task Helper_MethodsExistInModuleTable()
    {
        string? r = await Run(@"
			return tostring(type(Postgres.NonQuery)) .. ':' .. tostring(type(Postgres.Scalar)) .. ':' .. tostring(type(Postgres.QueryAll))
		");
        r.ShouldBe("function:function:function");
    }

    [PostgresFact]
    public async Task Connect_ValidCredentials_ReturnsUserdata()
    {
        string? r = await Run($@"
			local conn, err = {ConnectLua()}
			if not conn then error(err) end
			return tostring(conn):sub(1, 9)
		");
        r.ShouldBe("Postgres:");
    }

    [PostgresFact]
    public async Task Connect_BadCredentials_ReturnsNilAndError()
    {
        string? r = await Run(@"
			local conn, err = Postgres.Connect('host=127.0.0.1 user=bad_user password=bad_pass dbname=bad_db')
			return tostring(conn == nil) .. ':' .. tostring(type(err) == 'string')
		");
        r.ShouldBe("true:true");
    }

    [PostgresFact]
    public async Task RawSelect_CoroutineProtocol_YieldsNilThenRowcountThenRows()
    {
        string? r = await Run($@"
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
        r.ShouldBe("1:1:2:true");
    }

    [PostgresFact]
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

    [PostgresFact]
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

    [PostgresFact]
    public async Task StopFlag_MidStream_ConnNotBusy()
    {
        string? r = await Run($@"
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
        r.ShouldBe("false");
    }

    [PostgresFact]
    public async Task IsBusy_TrueWhileSuspended_FalseOnceDead()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query('SELECT 1'))
			coroutine.resume(co)
			local busy1 = conn:IsBusy()
			local ok, val = coroutine.resume(co)
			while ok and val == nil and coroutine.status(co) == 'suspended' do
				ok, val = coroutine.resume(co)
			end
			ok, val = coroutine.resume(co)
			while ok and val ~= nil do ok, val = coroutine.resume(co) end
			local busy2 = conn:IsBusy()
			return tostring(busy1) .. ':' .. tostring(busy2)
		");
        r.ShouldNotBeNull();
        r!.ShouldEndWith(":false");
    }

    [PostgresFact]
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

    // ── CRUD ─────────────────────────────────────────────────────────────────
    [PostgresFact]
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
			local _, c0 = exec('DELETE FROM test WHERE string = $1', {{'crud_insert'}})
			coroutine.resume(c0, true)
			local rowcount, c1 = exec('INSERT INTO test (string) VALUES ($1)', {{'crud_insert'}})
			coroutine.resume(c1, true)
			return tostring(rowcount)
		");
        r.ShouldBe("1");
    }

    [PostgresFact]
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
			local _, c0 = exec('DELETE FROM test WHERE string = $1', {{'crud_sel'}})
			coroutine.resume(c0, true)
			local _, c1 = exec('INSERT INTO test (string) VALUES ($1)', {{'crud_sel'}})
			coroutine.resume(c1, true)
			local _, co = exec('SELECT string FROM test WHERE string = $1', {{'crud_sel'}})
			local ok, row = coroutine.resume(co)
			coroutine.resume(co, true)
			return tostring(row[1])
		");
        r.ShouldBe("crud_sel");
    }

    [PostgresFact]
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
			local _, c0 = exec('DELETE FROM test WHERE string IN ($1, $2)', {{'crud_upd_old', 'crud_upd_new'}})
			coroutine.resume(c0, true)
			local _, c1 = exec('INSERT INTO test (string) VALUES ($1)', {{'crud_upd_old'}})
			coroutine.resume(c1, true)
			local affected, c2 = exec('UPDATE test SET string = $1 WHERE string = $2', {{'crud_upd_new', 'crud_upd_old'}})
			coroutine.resume(c2, true)
			local _, co = exec('SELECT string FROM test WHERE string = $1', {{'crud_upd_new'}})
			local ok, row = coroutine.resume(co)
			coroutine.resume(co, true)
			return tostring(affected) .. ':' .. tostring(row[1])
		");
        r.ShouldBe("1:crud_upd_new");
    }

    [PostgresFact]
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
			local _, c0 = exec('DELETE FROM test WHERE string = $1', {{'crud_del'}})
			coroutine.resume(c0, true)
			local _, c1 = exec('INSERT INTO test (string) VALUES ($1)', {{'crud_del'}})
			coroutine.resume(c1, true)
			local _, c2 = exec('DELETE FROM test WHERE string = $1', {{'crud_del'}})
			coroutine.resume(c2, true)
			local rowcount, co = exec('SELECT id FROM test WHERE string = $1', {{'crud_del'}})
			local ok, row = coroutine.resume(co)
			coroutine.resume(co, true)
			return tostring(rowcount) .. ':' .. tostring(row == nil)
		");
        r.ShouldBe("0:true");
    }

    // ── type mapping ──────────────────────────────────────────────────────────
    [PostgresFact]
    public async Task Types_IntegersReturnedAsInteger()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query('SELECT 1::int2, 2::int4, 3::int8'))
			local ok, v = coroutine.resume(co)
			while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
			ok, v = coroutine.resume(co)
			coroutine.resume(co, true)
			return tostring(math.type(v[1])) .. ':' .. tostring(math.type(v[2])) .. ':' .. tostring(math.type(v[3]))
		");
        r.ShouldBe("integer:integer:integer");
    }

    [PostgresFact]
    public async Task Types_FloatsReturnedAsNumber()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query('SELECT 1.5::float4, 2.5::float8, 3.0::numeric'))
			local ok, v = coroutine.resume(co)
			while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
			ok, v = coroutine.resume(co)
			coroutine.resume(co, true)
			return tostring(type(v[1])) .. ':' .. tostring(type(v[2])) .. ':' .. tostring(type(v[3]))
		");
        r.ShouldBe("number:number:number");
    }

    [PostgresFact]
    public async Task Types_BoolReturnedAsBoolean()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query('SELECT true::bool, false::bool'))
			local ok, v = coroutine.resume(co)
			while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
			ok, v = coroutine.resume(co)
			coroutine.resume(co, true)
			return tostring(type(v[1])) .. ':' .. tostring(v[1]) .. ':' .. tostring(v[2])
		");
        r.ShouldBe("boolean:true:false");
    }

    [PostgresFact]
    public async Task Types_TextReturnedAsString()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query(""SELECT 'hello'::text, 'world'::varchar""))
			local ok, v = coroutine.resume(co)
			while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
			ok, v = coroutine.resume(co)
			coroutine.resume(co, true)
			return tostring(type(v[1])) .. ':' .. tostring(v[1]) .. ':' .. tostring(v[2])
		");
        r.ShouldBe("string:hello:world");
    }

    [PostgresFact]
    public async Task Types_NullReturnedAsNil()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			local co = assert(conn:Query('SELECT NULL::int, NULL::text'))
			local ok, v = coroutine.resume(co)
			while ok and v == nil and coroutine.status(co) == 'suspended' do ok, v = coroutine.resume(co) end
			ok, v = coroutine.resume(co)
			coroutine.resume(co, true)
			return tostring(v[1] == nil) .. ':' .. tostring(v[2] == nil)
		");
        r.ShouldBe("true:true");
    }

    // ── NonQuery ──────────────────────────────────────────────────────────────
    [PostgresFact]
    public async Task NonQuery_Insert_ReturnsAffectedCount()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			conn:NonQuery('DELETE FROM test WHERE string = $1', {{'helper_nq'}})
			local ok, n = conn:NonQuery('INSERT INTO test (string) VALUES ($1)', {{'helper_nq'}})
			conn:NonQuery('DELETE FROM test WHERE string = $1', {{'helper_nq'}})
			return tostring(ok) .. ':' .. tostring(n)
		");
        r.ShouldBe("true:1");
    }

    [PostgresFact]
    public async Task NonQuery_InvalidSql_ReturnsFalseAndError()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			local ok, err = conn:NonQuery('NOT VALID SQL AT ALL')
			return tostring(ok == false and type(err) == 'string' and #err > 0)
		");
        r.ShouldBe("true");
    }

    [PostgresFact]
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
    [PostgresFact]
    public async Task Scalar_ReturnsFirstColumnOfFirstRow()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			local ok, v = conn:Scalar('SELECT 42::int')
			return tostring(ok) .. ':' .. tostring(v)
		");
        r.ShouldBe("true:42");
    }

    [PostgresFact]
    public async Task Scalar_NoMatchingRow_ReturnsNil()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			local ok, v = conn:Scalar('SELECT 1 WHERE 1 = 0')
			return tostring(ok) .. ':' .. tostring(v)
		");
        r.ShouldBe("true:nil");
    }

    [PostgresFact]
    public async Task Scalar_InvalidSql_ReturnsFalseAndError()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			local ok, err = conn:Scalar('NOT VALID SQL')
			return tostring(ok == false and type(err) == 'string')
		");
        r.ShouldBe("true");
    }

    [PostgresFact]
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
    [PostgresFact]
    public async Task QueryAll_MultipleRows_ReturnsAllRows()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			local ok, rows = conn:QueryAll('SELECT generate_series(1,3)')
			return tostring(ok) .. ':' .. tostring(#rows)
		");
        r.ShouldBe("true:3");
    }

    [PostgresFact]
    public async Task QueryAll_EmptyResult_ReturnsEmptyTable()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			local ok, rows = conn:QueryAll('SELECT 1 WHERE 1 = 0')
			return tostring(ok) .. ':' .. tostring(#rows)
		");
        r.ShouldBe("true:0");
    }

    [PostgresFact]
    public async Task QueryAll_RowValuesAccessible()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			local ok, rows = conn:QueryAll('SELECT 10::int, 20::int, 30::int')
			return tostring(ok and rows[1][1] == 10 and rows[1][2] == 20 and rows[1][3] == 30)
		");
        r.ShouldBe("true");
    }

    [PostgresFact]
    public async Task QueryAll_InvalidSql_ReturnsFalseAndError()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			local ok, err = conn:QueryAll('NOT VALID SQL')
			return tostring(ok == false and type(err) == 'string')
		");
        r.ShouldBe("true");
    }

    [PostgresFact]
    public async Task QueryAll_ConnectionNotBusy_AfterCompletion()
    {
        string? r = await Run($@"
			local conn = assert({ConnectLua()})
			conn:QueryAll('SELECT 1')
			return tostring(conn:IsBusy())
		");
        r.ShouldBe("false");
    }

    // Reads KITSUNE_POSTGRES_TEST=<libpq conninfo>
    private static string ConnectLua()
    {
        var conninfo = Environment.GetEnvironmentVariable("KITSUNE_POSTGRES_TEST")!;
        return $"Postgres.Connect('{conninfo}')";
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
