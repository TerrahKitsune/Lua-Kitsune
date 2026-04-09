using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

// See KitsuneEngineTests for why both classes share a single collection.
[Collection("KitsuneSequential")]

/// <summary>
/// Tests for the SQLite module.
/// In-memory tests (<see cref="FactAttribute"/>) run unconditionally.
/// File-database tests (<see cref="SQLiteFactAttribute"/>) require
/// <c>KITSUNE_SQLITE_TEST</c> to be set to the path of an existing SQLite
/// database file that the current user can open for writing.
/// </summary>
public sealed class SQLiteTests
{
    // -- In-memory ------------------------------------------------------------
    [Fact]
    public async Task SQLite_InMemory_CreateInsertSelect()
    {
        LuaValue r = await Run(@"
            local db = SQLite.Open()
            db:Query('CREATE TABLE t (id INTEGER, name TEXT)'); db:Fetch()
            db:Query([[INSERT INTO t VALUES (1, 'alice')]]); db:Fetch()
            db:Query([[INSERT INTO t VALUES (2, 'bob')]]);   db:Fetch()
            db:Query('SELECT id, name FROM t ORDER BY id')
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
    public async Task SQLite_GetRow_ByIndex_ReturnsValue()
    {
        LuaValue r = await Run(@"
            local db = SQLite.Open()
            db:Query('CREATE TABLE t (v TEXT)'); db:Fetch()
            db:Query([[INSERT INTO t VALUES ('test_val')]]); db:Fetch()
            db:Query('SELECT v FROM t')
            db:Fetch()
            local val = db:GetRow(1)
            db:Close()
            return val
        ");
        r.String.ShouldBe("test_val");
    }

    [Fact]
    public async Task SQLite_RegisterFunction_CallableFromQuery()
    {
        // SQLite returns numeric results as floats (7.0, not 7).
        LuaValue r = await Run(@"
            local db = SQLite.Open()
            db:RegisterFunction(function(a, b) return a + b end, 'add2', 2)
            db:Query('SELECT add2(3, 4) AS result')
            db:Fetch()
            local val = db:GetRow(1)
            db:Close()
            return tostring(val)
        ");
        r.String.ShouldBe("7.0");
    }

    [Fact]
    public async Task SQLite_NullValue_IsNilInLua()
    {
        LuaValue r = await Run(@"
            local db = SQLite.Open()
            db:Query('CREATE TABLE t (v TEXT)'); db:Fetch()
            db:Query('INSERT INTO t VALUES (NULL)'); db:Fetch()
            db:Query('SELECT v FROM t')
            db:Fetch()
            local val = db:GetRow(1)
            db:Close()
            return tostring(val)
        ");
        r.String.ShouldBe("nil");
    }

    [Fact]
    public async Task SQLite_IntegerColumn_RoundTrips()
    {
        LuaValue r = await Run(@"
            local db = SQLite.Open()
            db:Query('CREATE TABLE t (n INTEGER)'); db:Fetch()
            db:Query('INSERT INTO t VALUES (42)'); db:Fetch()
            db:Query('SELECT n FROM t')
            db:Fetch()
            local row = db:GetRow()
            db:Close()
            return tostring(row.n)
        ");
        r.String.ShouldBe("42");
    }

    [Fact]
    public async Task SQLite_FloatColumn_RoundTrips()
    {
        LuaValue r = await Run(@"
            local db = SQLite.Open()
            db:Query('CREATE TABLE t (f REAL)'); db:Fetch()
            db:Query('INSERT INTO t VALUES (3.14)'); db:Fetch()
            db:Query('SELECT f FROM t')
            db:Fetch()
            local row = db:GetRow()
            db:Close()
            return tostring(math.abs(row.f - 3.14) < 0.0001)
        ");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task SQLite_MultipleRows_FetchAll()
    {
        LuaValue r = await Run(@"
            local db = SQLite.Open()
            db:Query('CREATE TABLE t (n INTEGER)'); db:Fetch()
            for i = 1, 5 do
                db:Query('INSERT INTO t VALUES (' .. i .. ')'); db:Fetch()
            end
            db:Query('SELECT n FROM t ORDER BY n')
            local sum = 0
            while db:Fetch() do
                sum = sum + db:GetRow(1)
            end
            db:Close()
            return tostring(sum)
        ");
        r.String.ShouldBe("15");
    }

    [Fact]
    public async Task SQLite_ParameterizedQuery_TableBind()
    {
        LuaValue r = await Run(@"
            local db = SQLite.Open()
            db:Query('CREATE TABLE t (id INTEGER, name TEXT)'); db:Fetch()
            db:Query('INSERT INTO t VALUES (:id, :name)', {id=7, name='kitsune'}); db:Fetch()
            db:Query('SELECT name FROM t WHERE id = 7')
            db:Fetch()
            local val = db:GetRow(1)
            db:Close()
            return tostring(val)
        ");
        r.String.ShouldBe("kitsune");
    }

    [Fact]
    public async Task SQLite_InvalidQuery_ReturnsFalseAndError()
    {
        LuaValue r = await Run(@"
            local db = SQLite.Open()
            local ok, err = db:Query('THIS IS NOT SQL')
            db:Close()
            return tostring(ok == false and type(err) == 'string' and #err > 0)
        ");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task SQLite_AggregateFunction_SumCustom()
    {
        LuaValue r = await Run(@"
            local db = SQLite.Open()
            db:Query('CREATE TABLE t (n INTEGER)'); db:Fetch()
            for i = 1, 4 do
                db:Query('INSERT INTO t VALUES (' .. i .. ')'); db:Fetch()
            end
            local acc = 0
            db:RegisterAggregateFunction(function(isFinish, v)
                if isFinish then return acc end
                acc = acc + v
            end, 'mysum', 1)
            db:Query('SELECT mysum(n) FROM t')
            db:Fetch()
            local val = db:GetRow(1)
            db:Close()
            return tostring(val)
        ");
        r.String.ShouldBe("10.0");
    }

    [Fact]
    public async Task SQLite_Close_ThenQueryRaisesError()
    {
        LuaValue r = await Run(@"
            local db = SQLite.Open()
            db:Close()
            local ok, err = pcall(function() db:Query('SELECT 1') end)
            return tostring(not ok and type(err) == 'string')
        ");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task SQLite_InstanceReuse_MultipleQueries()
    {
        LuaValue r = await Run(@"
            local db = SQLite.Open()
            db:Query('CREATE TABLE t (v TEXT)'); db:Fetch()
            db:Query([[INSERT INTO t VALUES ('first')]]); db:Fetch()
            db:Query([[INSERT INTO t VALUES ('second')]]); db:Fetch()
            db:Query('SELECT COUNT(*) FROM t')
            db:Fetch()
            local count = db:GetRow(1)
            db:Query('SELECT v FROM t ORDER BY rowid LIMIT 1')
            db:Fetch()
            local first = db:GetRow(1)
            db:Close()
            return tostring(count) .. ':' .. tostring(first)
        ");
        r.String.ShouldBe("2:first");
    }

    // -- Query return values --------------------------------------------------
    [Fact]
    public async Task SQLite_Query_DML_Returns_Done()
    {
        // INSERT/UPDATE/DELETE complete without rows ? second return is "DONE".
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:Query('CREATE TABLE t (n INTEGER)'); db:Fetch()
			local ok, msg = db:Query('INSERT INTO t VALUES (1)')
			db:Close()
			return tostring(ok == true and msg == 'DONE')
		");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task SQLite_Query_Select_WithRows_Returns_Row()
    {
        // A SELECT that finds rows pre-steps once ? second return is "ROW".
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:Query('CREATE TABLE t (n INTEGER)'); db:Fetch()
			db:Query('INSERT INTO t VALUES (1)'); db:Fetch()
			local ok, msg = db:Query('SELECT n FROM t')
			db:Finish()
			db:Close()
			return tostring(ok == true and msg == 'ROW')
		");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task SQLite_Query_Select_NoRows_Returns_Done()
    {
        // A SELECT that matches nothing ? second return is "DONE".
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:Query('CREATE TABLE t (n INTEGER)'); db:Fetch()
			local ok, msg = db:Query('SELECT n FROM t WHERE n = 9999')
			db:Close()
			return tostring(ok == true and msg == 'DONE')
		");
        r.String.ShouldBe("true");
    }

    // -- Finish ---------------------------------------------------------------
    [Fact]
    public async Task SQLite_Finish_AbandonsMidQuery_AllowsNextQuery()
    {
        // Finish() finalizes the prepared statement early so the next Query can run.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:Query('CREATE TABLE t (n INTEGER)'); db:Fetch()
			for i = 1, 10 do
				db:Query('INSERT INTO t VALUES (' .. i .. ')'); db:Fetch()
			end
			db:Query('SELECT n FROM t ORDER BY n')
			db:Fetch()
			local first = db:GetRow(1)
			db:Finish()   -- abandon the remaining 9 rows
			local ok, _ = db:Query('SELECT COUNT(*) FROM t')
			db:Fetch()
			local count = db:GetRow(1)
			db:Close()
			return tostring(first == 1 and ok == true and count == 10)
		");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task SQLite_Fetch_AfterDDL_ReturnsFalse()
    {
        // After DDL the statement is finalized by Query; Fetch with no active
        // statement returns false immediately.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:Query('CREATE TABLE t (n INTEGER)')
			local fetched = db:Fetch()
			db:Close()
			return tostring(fetched == false)
		");
        r.String.ShouldBe("true");
    }

    // -- GetRow edge cases ----------------------------------------------------
    [Fact]
    public async Task SQLite_GetRow_OutOfRange_ReturnsNil()
    {
        // Requesting a column index beyond the column count returns nil.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:Query('CREATE TABLE t (a INTEGER, b INTEGER)'); db:Fetch()
			db:Query('INSERT INTO t VALUES (1, 2)'); db:Fetch()
			db:Query('SELECT a, b FROM t')
			db:Fetch()
			local col1 = db:GetRow(1)
			local col3 = db:GetRow(3)   -- only 2 columns
			db:Close()
			return tostring(col1 == 1 and col3 == nil)
		");
        r.String.ShouldBe("true");
    }

    // -- BLOB column ----------------------------------------------------------
    [Fact]
    public async Task SQLite_BlobColumn_ReturnsStreamUserdata()
    {
        // BLOB columns are returned as LuaStream userdata objects.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:Query('CREATE TABLE t (v BLOB)'); db:Fetch()
			db:Query([[INSERT INTO t VALUES (X'48454C4C4F')]]); db:Fetch()
			db:Query('SELECT v FROM t')
			db:Fetch()
			local val = db:GetRow(1)
			db:Close()
			return tostring(type(val) == 'userdata')
		");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task SQLite_BlobColumn_ContentAccessibleViaStream()
    {
        // The LuaStream wrapping a BLOB can be Read() to recover the raw bytes.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:Query('CREATE TABLE t (v BLOB)'); db:Fetch()
			db:Query([[INSERT INTO t VALUES (X'48454C4C4F')]]); db:Fetch()  -- 'HELLO'
			db:Query('SELECT v FROM t')
			db:Fetch()
			local blob = db:GetRow(1)
			local content = blob:Read()
			db:Close()
			return content
		");
        r.String.ShouldBe("HELLO");
    }

    // -- Parameter binding ----------------------------------------------------
    [Fact]
    public async Task SQLite_ParameterizedQuery_FunctionBind()
    {
        // A function can supply parameter values by name instead of a table.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:Query('CREATE TABLE t (id INTEGER, name TEXT)'); db:Fetch()
			db:Query('INSERT INTO t VALUES (:id, :name)', function(param)
				if param == 'id'   then return 42          end
				if param == 'name' then return 'fn_value'  end
			end)
			db:Fetch()
			db:Query('SELECT id, name FROM t')
				db:Fetch()
				local row = db:GetRow()
				db:Close()
				return tostring(row.id == 42 and tostring(row.name) == 'fn_value')
		");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task SQLite_ParameterizedQuery_BooleanParam_StoresAsInteger()
    {
        // Booleans bind as 1 (true) or 0 (false); SQLite has no BOOLEAN type
        // so they round-trip as integers.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:Query('CREATE TABLE t (a INTEGER, b INTEGER)'); db:Fetch()
			db:Query('INSERT INTO t VALUES (:a, :b)', {a=true, b=false}); db:Fetch()
			db:Query('SELECT a, b FROM t')
			db:Fetch()
			local row = db:GetRow()
			db:Close()
			return tostring(row.a == 1 and row.b == 0)
		");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task SQLite_ParameterizedQuery_WcharParam_RoundTrips()
    {
        // Wchar values bind as UTF-16 text; read back with ToggleWidechar(true)
        // so both sides use the matched text16/bytes16 pair.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:ToggleWidechar(true)
			db:Query('CREATE TABLE t (v TEXT)'); db:Fetch()
			db:Query('INSERT INTO t VALUES (:v)', {v = Wchar.FromUtf8('hello wchar')}); db:Fetch()
			db:Query('SELECT v FROM t')
			db:Fetch()
			local val = db:GetRow(1)   -- Wchar userdata
			db:Close()
			return tostring(val)       -- Wchar.__tostring ? ToUtf8
		");
        r.String.ShouldBe("hello wchar");
    }

    // -- Custom functions -----------------------------------------------------
    [Fact]
    public async Task SQLite_RegisterFunction_ReturnsString()
    {
        // A custom function that returns a Lua string produces a TEXT result.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:RegisterFunction(function(s) return 'prefix_' .. s end, 'prepend', 1)
			db:Query([[SELECT prepend('hello')]])
			db:Fetch()
			local val = db:GetRow(1)
			db:Close()
			return tostring(val)
		");
        r.String.ShouldBe("prefix_hello");
    }

    [Fact]
    public async Task SQLite_RegisterFunction_ReturnsNil_StoresNull()
    {
        // A custom function that returns nil produces a SQL NULL result.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:RegisterFunction(function() return nil end, 'nullfn', 0)
			db:Query('SELECT nullfn()')
			db:Fetch()
			local val = db:GetRow(1)
			db:Close()
			return tostring(val)
		");
        r.String.ShouldBe("nil");
    }

    [Fact]
    public async Task SQLite_RegisterFunction_DuplicateName_RaisesError()
    {
        // Registering a second function with the same name must raise a Lua error.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:RegisterFunction(function() return 1 end, 'myfn', 0)
			local ok, err = pcall(function()
				db:RegisterFunction(function() return 2 end, 'myfn', 0)
			end)
			db:Close()
			return tostring(not ok and type(err) == 'string')
		");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task SQLite_RegisterFunction_NegativeArgs_RaisesError()
    {
        // Negative arg count must be rejected immediately.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			local ok, err = pcall(function()
				db:RegisterFunction(function() end, 'badfn', -1)
			end)
			db:Close()
			return tostring(not ok and type(err) == 'string')
		");
        r.String.ShouldBe("true");
    }

    // -- ToggleWidechar -------------------------------------------------------
    [Fact]
    public async Task SQLite_ToggleWidechar_False_TextComesBackAsString()
    {
        // After ToggleWidechar(false) TEXT columns must be plain Lua strings.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:ToggleWidechar(false)
			db:Query('CREATE TABLE t (v TEXT)'); db:Fetch()
			db:Query([[INSERT INTO t VALUES ('hello')]]); db:Fetch()
			db:Query('SELECT v FROM t')
			db:Fetch()
			local val = db:GetRow(1)
			db:Close()
			return tostring(type(val) == 'string')
		");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task SQLite_ToggleWidechar_True_TextComesBackAsWchar()
    {
        // After ToggleWidechar(true) TEXT columns must be Wchar userdata.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:ToggleWidechar(true)
			db:Query('CREATE TABLE t (v TEXT)'); db:Fetch()
			db:Query([[INSERT INTO t VALUES ('hello')]]); db:Fetch()
			db:Query('SELECT v FROM t')
			db:Fetch()
			local val = db:GetRow(1)
			db:Close()
			return tostring(type(val) == 'userdata')
		");
        r.String.ShouldBe("true");
    }

    // -- __tostring -----------------------------------------------------------
    [Fact]
    public async Task SQLite_Tostring_InMemory_ContainsPointerAndMemoryKeyword()
    {
        // __tostring format: "SQLite: 0x… File: :memory:"
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			local s = tostring(db)
			db:Close()
			return tostring(s:find('SQLite:') ~= nil and s:find(':memory:') ~= nil)
		");
        r.String.ShouldBe("true");
    }

    // -- Lua() built-in SQL function ------------------------------------------
    [Fact]
    public async Task SQLite_LuaBuiltinFunction_CanRunScript()
    {
        // Kitsune registers Lua(script) as a built-in SQLite function that
        // evaluates a Lua chunk and returns the result via tostring.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:Query([[SELECT Lua('return 1 + 1')]])
			db:Fetch()
			local val = db:GetRow(1)
			db:Close()
			return tostring(val)
		");
        r.String.ShouldBe("2");
    }

    [Fact]
    public async Task SQLite_LuaBuiltinFunction_NilReturn_ProducesNull()
    {
        // A Lua chunk that returns nil maps to SQL NULL ? Lua nil.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			db:Query([[SELECT Lua('return nil')]])
			db:Fetch()
			local val = db:GetRow(1)
			db:Close()
			return tostring(val)
		");
        r.String.ShouldBe("nil");
    }

    [Fact]
    public async Task SQLite_LuaBuiltinFunction_CanAccessLuaGlobals()
    {
        // The Lua() function shares the engine's Lua state so globals are visible.
        LuaValue r = await Run(@"
			_G.sqliteTestGlobal = 'hello_from_lua'
			local db = SQLite.Open()
			db:Query([[SELECT Lua('return _G.sqliteTestGlobal')]])
			db:Fetch()
			local val = db:GetRow(1)
			db:Close()
			return val
		");
        r.String.ShouldBe("hello_from_lua");
    }

    // -- SetBusyHandler -------------------------------------------------------
    [Fact]
    public async Task SQLite_SetBusyHandler_SetAndClear_DoesNotCrash()
    {
        // Setting and then clearing a busy handler must not crash; with no
        // contention the handler is never invoked.
        LuaValue r = await Run(@"
			local db = SQLite.Open()
			local invoked = false
			db:SetBusyHandler(function(retries)
				invoked = true
				return false
			end)
			db:Query('CREATE TABLE t (n INTEGER)'); db:Fetch()
			db:SetBusyHandler(nil)   -- clear the handler
			db:Close()
			return tostring(invoked == false)
		");
        r.String.ShouldBe("true");
    }

    // -- Open modes -----------------------------------------------------------
    [Fact]
    public async Task SQLite_Open_Mode1_Multithread_OpensSuccessfully()
    {
        // Mode 1 (multithread) must open without error and allow queries.
        LuaValue r = await Run(@"
			local db = SQLite.Open(nil, 1)
			local ok, _ = db:Query('SELECT 1')
			db:Fetch()
			db:Close()
			return tostring(ok == true)
		");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task SQLite_Open_Mode2_Serialized_OpensSuccessfully()
    {
        // Mode 2 (serialized) must open without error and allow queries.
        LuaValue r = await Run(@"
			local db = SQLite.Open(nil, 2)
			local ok, _ = db:Query('SELECT 1')
			db:Fetch()
			db:Close()
			return tostring(ok == true)
		");
        r.String.ShouldBe("true");
    }

    // -- File database --------------------------------------------------------
    [SQLiteFact]
    public async Task SQLite_FileDatabase_OpenAndVersion()
    {
        // Opens the configured database and confirms SQLite returns a version string.
        LuaValue r = await Run(@"
            local db = SQLite.Open(os.getenv('KITSUNE_SQLITE_TEST'))
            db:Query('SELECT sqlite_version()')
            db:Fetch()
			local v = db:GetRow(1)
			db:Close()
			return tostring(v ~= nil and #tostring(v) > 0)
        ");
        r.String.ShouldBe("true");
    }

    [SQLiteFact]
    public async Task SQLite_FileDatabase_ListTables_ReturnsTable()
    {
        // sqlite_master always exists; querying it must succeed and return a table.
        LuaValue r = await Run(@"
			local db = SQLite.Open(os.getenv('KITSUNE_SQLITE_TEST'))
			local ok, err = db:Query([[SELECT name FROM sqlite_master WHERE type='table' ORDER BY name]])
			if not ok then db:Close(); return tostring(err) end
			local names = {}
			while db:Fetch() do
				table.insert(names, tostring(db:GetRow(1)))
			end
			db:Close()
			return tostring(type(names) == 'table')
		");
        r.String.ShouldBe("true");
    }

    [SQLiteFact]
    public async Task SQLite_FileDatabase_WriteAndRead_RoundTrip()
    {
        // Creates a temporary table, inserts a row, reads it back, then drops the table.
        LuaValue r = await Run(@"
            local db = SQLite.Open(os.getenv('KITSUNE_SQLITE_TEST'))
            db:Query('CREATE TABLE IF NOT EXISTS _kitsune_test (v TEXT)'); db:Fetch()
            db:Query([[INSERT INTO _kitsune_test VALUES ('round_trip')]]); db:Fetch()
            db:Query([[SELECT v FROM _kitsune_test WHERE v = 'round_trip']])
            db:Fetch()
            local val = db:GetRow(1)
            db:Query('DROP TABLE _kitsune_test'); db:Fetch()
            db:Close()
            return tostring(val)
        ");
        r.String.ShouldBe("round_trip");
    }

    [SQLiteFact]
    public async Task SQLite_FileDatabase_ParameterizedQuery_RoundTrips()
    {
        // Verifies that named parameters work against the file-backed database.
        LuaValue r = await Run(@"
			local db = SQLite.Open(os.getenv('KITSUNE_SQLITE_TEST'))
			db:Query('CREATE TABLE IF NOT EXISTS _kitsune_param (id INTEGER, name TEXT)'); db:Fetch()
			db:Query('INSERT INTO _kitsune_param VALUES (:id, :name)', {id=99, name='kitsune_file'}); db:Fetch()
			db:Query('SELECT name FROM _kitsune_param WHERE id = 99')
			db:Fetch()
			local val = db:GetRow(1)
			db:Query('DROP TABLE _kitsune_param'); db:Fetch()
			db:Close()
			return tostring(val)
		");
        r.String.ShouldBe("kitsune_file");
    }

    [SQLiteFact]
    public async Task SQLite_FileDatabase_Tostring_ContainsFilePath()
    {
        // __tostring for a file database must include the file path.
        LuaValue r = await Run(@"
			local path = os.getenv('KITSUNE_SQLITE_TEST')
			local db = SQLite.Open(path)
			local s = tostring(db)
			db:Close()
			return tostring(s:find('SQLite:') ~= nil and s:find(path, 1, true) ~= nil)
		");
        r.String.ShouldBe("true");
    }

    [SQLiteFact]
    public async Task SQLite_FileDatabase_Mode1_WAL_JournalModeIsWal()
    {
        // Mode 1 (multithread) sets WAL journal mode on the file database.
        LuaValue r = await Run(@"
			local db = SQLite.Open(os.getenv('KITSUNE_SQLITE_TEST'), 1)
			db:Query('PRAGMA journal_mode')
			db:Fetch()
			local mode = db:GetRow(1)
			db:Close()
			return tostring(tostring(mode) == 'wal')
		");
        r.String.ShouldBe("true");
    }

    private static async Task<LuaValue> Run(string lua)
    {
        var engine = new KitsuneEngine();
        LuaValue result;
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
