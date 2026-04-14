using KitsuneNet;
using Microsoft.Data.Sqlite;
using Shouldly;
using SQLitePCL;
using System.IO;
using Xunit;

namespace KitsuneNet.Tests;

// See KitsuneEngineTests for why both classes share a single collection.
[Collection("KitsuneSequential")]
public sealed class SQLiteExtensionTests
{
    // The DLL is copied into the test output directory by the CopyNativeDlls
    // target in KitsuneNet.csproj. Omit the file extension so SQLite appends
    // the platform-appropriate suffix (.dll on Windows).
    private static readonly string ExtensionPath =
        Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "SQLiteKitsune")
            .Replace('\\', '/');

    // This test verifies that the sqlite extension can be loaded and called from the kitsune engine
    // Kitsune engine owns the sqlite extension
    // kitsune engine -> sqlite -> kitsune engine
    [WindowsOnlyFact]
    public async Task SQLiteKitsuneExtension_KitsuneVersion_ReturnsVersionString()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            db:Query("SELECT KitsuneVersion()")
            db:Fetch()
            local version = db:GetRow(1)
            db:Close()
            return version
            """);
        result.String.ShouldNotBeNull();
        result.String.ShouldStartWith("1.0.0.");
    }

    // This tests verifies that the kitsune engine can be loaded from the sqlite extension
    // Sqlite owns the kitsuneengine
    // sqlite -> kitsune engine
    [WindowsOnlyFact]
    public async Task SQLiteKitsuneExtension_CSharpConnection_KitsuneVersion_ReturnsVersionString()
    {
        string? version = await Task.Run(() =>
        {
            using var connection = new SqliteConnection("Data Source=:memory:");
            connection.Open();
            raw.sqlite3_enable_load_extension(connection.Handle, 1);
            using (var cmd = connection.CreateCommand())
            {
#pragma warning disable CA2100 // Review SQL queries for security vulnerabilities
                cmd.CommandText = $"SELECT load_extension('{ExtensionPath}')";
#pragma warning restore CA2100 // Review SQL queries for security vulnerabilities
                cmd.ExecuteScalar();
            }

            using (var cmd = connection.CreateCommand())
            {
                cmd.CommandText = "SELECT KitsuneVersion()";
                return cmd.ExecuteScalar() as string;
            }
        });
        version.ShouldNotBeNull();
        version.ShouldStartWith("1.0.0.");
    }

    [WindowsOnlyFact]
    public async Task SQLiteKitsuneExtension_Luastring_Works()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            db:Query("SELECT LuaString('return ARGS[1]..ARGS[2];', '123', 'abc')")
            db:Fetch()
            local result = db:GetRow(1)
            db:Close()
            return result
            """);
        result.String.ShouldNotBeNull();
        result.String.ShouldBe("123abc");
    }

    // Exercises the g_inlineExecution re-entrant path: RunString (blocking, RunInline on
    // calling thread) → SQLite → LuaString → KitsuneExecuteString. Without the
    // g_inlineExecution condition this returns "cannot call Execute from this context".
    [WindowsOnlyFact]
    public void SQLiteKitsuneExtension_LuaString_Works_Sync()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = engine.RunString("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            db:Query("SELECT LuaString('return ARGS[1]..ARGS[2];', '456', 'xyz')")
            db:Fetch()
            local result = db:GetRow(1)
            db:Close()
            return result
            """);
        result.String.ShouldNotBeNull();
        result.String.ShouldBe("456xyz");
    }

    // Verifies that sqlite_register_kitsune_functions auto-executes extension.lua
    // from the same directory as the database file. A unique temp directory is used
    // so this test never interferes with other tests sharing the system temp folder.
    [WindowsOnlyFact]
    public async Task SQLiteKitsuneExtension_ExtensionLua_AutoExecutedOnLoad()
    {
        string tempDir = Path.Combine(Path.GetTempPath(), Path.GetRandomFileName());
        Directory.CreateDirectory(tempDir);
        try
        {
            string dbPath = Path.Combine(tempDir, "test.db").Replace('\\', '/');
            File.WriteAllText(Path.Combine(tempDir, "extension.lua"), "extensionLuaRan = true");

            using KitsuneEngine engine = new();
            engine.SetString("extPath", ExtensionPath);
            engine.SetString("dbPath", dbPath);
            await engine.ExecuteStringAsync("""
                local db = SQLite.Open(dbPath)
                db:Query("SELECT load_extension('" .. extPath .. "')")
                db:Fetch()
                db:Close()
                """);

            engine.GetBool("extensionLuaRan").ShouldBe(true);
        }
        finally
        {
            Directory.Delete(tempDir, recursive: true);
        }
    }

    // DoFile(path, args...) — KitsuneExecuteFile convention: ARGS[1]=path, ARGS[2..n]=extra args.
    [WindowsOnlyFact]
    public async Task SQLiteKitsuneExtension_DoFile_ExecutesFileWithArgsAndReturnsResult()
    {
        string tempFile = Path.Combine(Path.GetTempPath(), Path.GetRandomFileName() + ".lua");
        try
        {
            File.WriteAllText(tempFile, "return ARGS[2] .. ARGS[3]");
            string luaPath = tempFile.Replace('\\', '/');

            using KitsuneEngine engine = new();
            engine.SetString("extPath", ExtensionPath);
            engine.SetString("luaPath", luaPath);
            LuaValue result = await engine.ExecuteStringAsync("""
                local db = SQLite.Open()
                db:Query("SELECT load_extension('" .. extPath .. "')")
                db:Fetch()
                db:Query("SELECT LuaFile('" .. luaPath .. "', 'hello', 'world')")
                db:Fetch()
                local r = db:GetRow(1)
                db:Close()
                return r
                """);
            result.String.ShouldBe("helloworld");
        }
        finally
        {
            if (File.Exists(tempFile))
            {
                File.Delete(tempFile);
            }
        }
    }

    // DoFunction(name, args...) — args are direct function parameters (no ARGS table).
    // The target function must already exist as a Lua global when DoFunction is called.
    [WindowsOnlyFact]
    public async Task SQLiteKitsuneExtension_DoFunction_CallsGlobalFunctionWithArgsAndReturnsResult()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            function sqlConcat(a, b) return a .. b end
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            db:Query("SELECT LuaFunction('sqlConcat', 'foo', 'bar')")
            db:Fetch()
            local r = db:GetRow(1)
            db:Close()
            return r
            """);
        result.String.ShouldBe("foobar");
    }

    // RegisterFunction(name, fn) — registers a Lua closure as a SQLite scalar function.
    // The function persists for the lifetime of the extension and is freed on DLL_PROCESS_DETACH.
    [WindowsOnlyFact]
    public async Task SQLiteKitsuneExtension_RegisterFunction_LuaClosureCallableFromSQL()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            SQLiteExt.RegisterFunction("AddOne", function(n) return n + 1 end)
            db:Query("SELECT AddOne(41)")
            db:Fetch()
            local r = db:GetRow(1)
            DB = db; -- Keep alive
            return r
            """);
        result.AsInt64.ShouldBe(42L);

        engine.CollectGarbage();

        // Do it again
        result = await engine.ExecuteStringAsync("""
            local db = DB
            DB = nil
            local ok, r = pcall(function()
                db:Query("SELECT AddOne(68)")
                db:Fetch()
                return db:GetRow(1)
            end)
            db:Close()
            if not ok then error(r) end
            return r
            """);
        result.AsInt64.ShouldBe(69L);
    }

    // SQLiteExt.Query(sql) — returns all rows as { [1]={col=val,...}, [2]={...}, ... }.
    [WindowsOnlyFact]
    public async Task SQLiteKitsuneExtension_Query_ReturnsFullResultSet()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            db:Query("CREATE TABLE t(id INTEGER, name TEXT)")
            db:Fetch()
            db:Query("INSERT INTO t VALUES(1,'hello')")
            db:Fetch()
            db:Query("INSERT INTO t VALUES(2,'world')")
            db:Fetch()
            local rows = SQLiteExt.Query("SELECT id, name FROM t ORDER BY id")
            db:Close()
            return rows[2]["name"]
            """);
        result.String.ShouldBe("world");
    }

    // SQLiteExt.Query(sql, params) — @paramName bindings from the params table.
    [WindowsOnlyFact]
    public async Task SQLiteKitsuneExtension_Query_WithPreparedParams()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            db:Query("CREATE TABLE t(id INTEGER, name TEXT)")
            db:Fetch()
            db:Query("INSERT INTO t VALUES(1,'hello')")
            db:Fetch()
            db:Query("INSERT INTO t VALUES(2,'world')")
            db:Fetch()
            local rows = SQLiteExt.Query("SELECT name FROM t WHERE id=@id", {id=1})
            db:Close()
            return rows[1]["name"]
            """);
        result.String.ShouldBe("hello");
    }

    // SQLiteExt.Scalar(sql) — returns the first column of the first row as a single value.
    [WindowsOnlyFact]
    public async Task SQLiteKitsuneExtension_Scalar_ReturnsSingleValue()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            db:Query("CREATE TABLE t(id INTEGER, name TEXT)")
            db:Fetch()
            db:Query("INSERT INTO t VALUES(1,'hello')")
            db:Fetch()
            db:Query("INSERT INTO t VALUES(2,'world')")
            db:Fetch()
            local name = SQLiteExt.Scalar("SELECT name FROM t WHERE id=2")
            db:Close()
            return name
            """);
        result.String.ShouldBe("world");
    }

    // SQLiteExt.Scalar with @param binding.
    [WindowsOnlyFact]
    public async Task SQLiteKitsuneExtension_Scalar_WithPreparedParams()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            db:Query("CREATE TABLE t(id INTEGER, name TEXT)")
            db:Fetch()
            db:Query("INSERT INTO t VALUES(1,'hello')")
            db:Fetch()
            local name = SQLiteExt.Scalar("SELECT name FROM t WHERE id=@id", {id=1})
            db:Close()
            return name
            """);
        result.String.ShouldBe("hello");
    }

    // SQLiteExt.RegisterAggregate(name, fn) — fn(isFinished, args...); state via closure.
    [WindowsOnlyFact]
    public async Task SQLiteKitsuneExtension_RegisterAggregate_AccumulatesRowsAndReturnsResult()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            db:Query("CREATE TABLE t(v INTEGER)")
            db:Fetch()
            for i = 1, 5 do
                db:Query("INSERT INTO t VALUES(" .. i .. ")")
                db:Fetch()
            end
            local total = 0
            SQLiteExt.RegisterAggregate("LuaSum", function(isFinished, val)
                if isFinished then local r = total; total = 0; return r end
                total = total + (val or 0)
            end)
            db:Query("SELECT LuaSum(v) FROM t")
            db:Fetch()
            local r = db:GetRow(1)
            db:Close()
            return r
            """);
        result.AsInt64.ShouldBe(15L);
    }

    // RegisterTable — 2-field table, string PKs, full scan returns all rows in PK order.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_TwoField_StringPKs_FullScan()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t["a"] = 1
            t["b"] = 2
            t["c"] = 3
            SQLiteExt.RegisterTable("TwoFieldStr", {"Id", "Data"}, t)
            local rows = SQLiteExt.Query("SELECT Id, Data FROM TwoFieldStr ORDER BY Id")
            db:Close()
            return #rows .. "|" .. rows[1]["Id"] .. "|" .. rows[2]["Id"] .. "|" .. rows[3]["Id"]
            """);
        result.String.ShouldBe("3|a|b|c");
    }

    // RegisterTable — 3-field table, integer PKs, full scan returns correct column values.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_ThreeField_IntegerPKs_FullScan()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t[1] = {"John", "Doe"}
            t[2] = {"Jane", "Smith"}
            SQLiteExt.RegisterTable("ThreeFieldInt", {"Id", "FirstName", "LastName"}, t)
            local rows = SQLiteExt.Query("SELECT Id, FirstName, LastName FROM ThreeFieldInt ORDER BY Id")
            db:Close()
            return rows[1]["FirstName"] .. " " .. rows[1]["LastName"] .. "|" ..
                   rows[2]["FirstName"] .. " " .. rows[2]["LastName"]
            """);
        result.String.ShouldBe("John Doe|Jane Smith");
    }

    // RegisterTable — PK equality index uses the fast single-row lookup path (string PK).
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_PKEqualityIndex_StringPK()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t["abc"] = {"John", "Doe"}
            t["xyz"] = {"Jane", "Smith"}
            t["zzz"] = {"Bob", "Jones"}
            SQLiteExt.RegisterTable("PKStringTest", {"Id", "FirstName", "LastName"}, t)
            local rows = SQLiteExt.Query("SELECT FirstName, LastName FROM PKStringTest WHERE Id='xyz'")
            db:Close()
            return #rows .. "|" .. rows[1]["FirstName"] .. " " .. rows[1]["LastName"]
            """);
        result.String.ShouldBe("1|Jane Smith");
    }

    // RegisterTable — PK equality index uses the fast single-row lookup path (integer PK).
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_PKEqualityIndex_IntegerPK()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t[1] = {"John", "Doe"}
            t[2] = {"Jane", "Smith"}
            t[3] = {"Bob", "Jones"}
            SQLiteExt.RegisterTable("PKIntTest", {"Id", "FirstName", "LastName"}, t)
            local rows = SQLiteExt.Query("SELECT FirstName FROM PKIntTest WHERE Id=2")
            db:Close()
            return #rows .. "|" .. rows[1]["FirstName"]
            """);
        result.String.ShouldBe("1|Jane");
    }

    // RegisterTable — table value in 2-field schema is serialized as JSON.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_TableValue_SerializedAsJson_TwoField()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t["a"] = {Name="James"}
            SQLiteExt.RegisterTable("JsonTwoField", {"Id", "Data"}, t)
            local rows = SQLiteExt.Query("SELECT Data FROM JsonTwoField WHERE Id='a'")
            db:Close()
            return rows[1]["Data"]
            """);
        result.String.ShouldNotBeNull();
        result.String.ShouldContain("James");
    }

    // RegisterTable — nested table value in multi-field schema is serialized as JSON.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_TableValue_SerializedAsJson_MultiField()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t[1] = {"John", "Doe", {Bla=42}}
            SQLiteExt.RegisterTable("JsonMultiField", {"Id", "FirstName", "LastName", "Extra"}, t)
            local rows = SQLiteExt.Query("SELECT FirstName, Extra FROM JsonMultiField WHERE Id=1")
            db:Close()
            return rows[1]["FirstName"] .. "|" .. rows[1]["Extra"]
            """);
        result.String.ShouldNotBeNull();
        result.String!.ShouldStartWith("John|");
        result.String.ShouldContain("42");
    }

    // RegisterTable — empty data table produces zero rows.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_EmptyTable_ReturnsNoRows()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            SQLiteExt.RegisterTable("EmptyTest", {"Id", "Data"}, t)
            local rows = SQLiteExt.Query("SELECT * FROM EmptyTest") or {}
            db:Close()
            return #rows
            """);
        result.AsInt64.ShouldBe(0L);
    }

    // RegisterTable — row whose value is a scalar (not a sub-table) in a 3-field schema:
    // first non-PK column returns the scalar, remaining columns return nil.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_MalformedRow_ScalarInMultiFieldSchema()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t["good"] = {"John", "Doe"}
            t["bad"]  = "scalar"
            SQLiteExt.RegisterTable("MalformedTest", {"Id", "FirstName", "LastName"}, t)
            local rows = SQLiteExt.Query("SELECT FirstName, LastName FROM MalformedTest WHERE Id='bad'")
            db:Close()
            return rows[1]["FirstName"] .. "|" .. tostring(rows[1]["LastName"])
            """);
        result.String.ShouldBe("scalar|nil");
    }

    // RegisterTable — running SELECT twice on the same virtual table (re-scan) returns
    // correct results both times.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_Rescan_BothScansReturnCorrectResults()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t[1] = "alpha"
            t[2] = "beta"
            SQLiteExt.RegisterTable("RescanTest", {"Id", "Word"}, t)
            local r1 = SQLiteExt.Query("SELECT Word FROM RescanTest ORDER BY Id")
            local r2 = SQLiteExt.Query("SELECT Word FROM RescanTest ORDER BY Id")
            db:Close()
            return r1[1]["Word"] .. "|" .. r1[2]["Word"] .. "|" ..
                   r2[1]["Word"] .. "|" .. r2[2]["Word"]
            """);
        result.String.ShouldBe("alpha|beta|alpha|beta");
    }

    // RegisterTable V2 — INSERT into a 2-field virtual table; new row visible in SELECT.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_Insert_TwoField_AddsRow()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t["a"] = 1
            t["b"] = 2
            SQLiteExt.RegisterTable("InsertTwo", {"Id", "Data"}, t)
            local ok, err = db:Query("INSERT INTO InsertTwo VALUES('d', 99)")
            assert(ok, err)
            db:Fetch()
            local rows = SQLiteExt.Query("SELECT Id, Data FROM InsertTwo ORDER BY Id")
            db:Close()
            return #rows .. "|" .. rows[3]["Id"] .. "|" .. tostring(rows[3]["Data"])
            """);
        result.String.ShouldBe("3|d|99");
    }

    // RegisterTable V2 — INSERT into a 3-field virtual table; new row visible in SELECT.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_Insert_ThreeField_AddsRow()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t[1] = {"John", "Doe"}
            t[2] = {"Jane", "Smith"}
            SQLiteExt.RegisterTable("InsertThree", {"Id", "FirstName", "LastName"}, t)
            local ok, err = db:Query("INSERT INTO InsertThree VALUES(4,'Hans','Mueller')")
            assert(ok, err)
            db:Fetch()
            local rows = SQLiteExt.Query("SELECT FirstName, LastName FROM InsertThree WHERE Id=4")
            db:Close()
            return rows[1]["FirstName"] .. " " .. rows[1]["LastName"]
            """);
        result.String.ShouldBe("Hans Mueller");
    }

    // RegisterTable V2 — INSERT with a duplicate PK raises a constraint error
    // containing "Duplicate key" from xUpdate.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_Insert_DuplicateKey_ReturnsError()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t["a"] = 1
            SQLiteExt.RegisterTable("InsertDup", {"Id", "Data"}, t)
            local ok, err = db:Query("INSERT INTO InsertDup VALUES('a', 99)")
            db:Close()
            if ok then return "unexpected success" end
            return err or "(no message)"
            """);
        result.String.ShouldNotBeNull();
        result.String.ShouldContain("Duplicate");
    }

    // RegisterTable V2 — DELETE removes the row from subsequent SELECT results.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_Delete_RemovesRow()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t["a"] = 1
            t["b"] = 2
            t["c"] = 3
            SQLiteExt.RegisterTable("DeleteTest", {"Id", "Data"}, t)
            local ok, err = db:Query("DELETE FROM DeleteTest WHERE Id='b'")
            assert(ok, err)
            db:Fetch()
            local rows = SQLiteExt.Query("SELECT Id FROM DeleteTest ORDER BY Id")
            db:Close()
            return #rows .. "|" .. rows[1]["Id"] .. "|" .. rows[2]["Id"]
            """);
        result.String.ShouldBe("2|a|c");
    }

    // RegisterTable V2 — UPDATE changes the scalar value for an existing row.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_Update_ScalarValue()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t["a"] = 1
            t["b"] = 2
            SQLiteExt.RegisterTable("UpdateScalar", {"Id", "Data"}, t)
            local ok, err = db:Query("UPDATE UpdateScalar SET Data=42 WHERE Id='a'")
            assert(ok, err)
            db:Fetch()
            local rows = SQLiteExt.Query("SELECT Data FROM UpdateScalar WHERE Id='a'")
            db:Close()
            return tostring(rows[1]["Data"])
            """);
        result.String.ShouldBe("42");
    }

    // RegisterTable V2 — UPDATE with PK rename: old key absent, new key present.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_Update_PKRename()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t["a"] = 10
            t["b"] = 20
            SQLiteExt.RegisterTable("RenameTest", {"Id", "Data"}, t)
            local ok, err = db:Query("UPDATE RenameTest SET Id='z' WHERE Id='a'")
            assert(ok, err)
            db:Fetch()
            local rows = SQLiteExt.Query("SELECT Id, Data FROM RenameTest ORDER BY Id")
            db:Close()
            -- a gone, b unchanged, z present with a's old value
            return #rows .. "|" .. rows[1]["Id"] .. "|" .. rows[2]["Id"] .. "|" .. tostring(rows[2]["Data"])
            """);
        result.String.ShouldBe("2|b|z|10");
    }

    // RegisterTable V2 — UPDATE with a WHERE that matches nothing returns SQL success
    // (SQLite handles the WHERE filter before xUpdate; 0 rows affected is not an error).
    // Verifies the existing row is untouched.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_Update_KeyNotFound_ReturnsSuccess()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t["a"] = 1
            SQLiteExt.RegisterTable("UpdateMissing", {"Id", "Data"}, t)
            SQLiteExt.Query("UPDATE UpdateMissing SET Data=99 WHERE Id='nope'")
            local rows = SQLiteExt.Query("SELECT * FROM UpdateMissing")
            db:Close()
            -- original row unchanged, no error raised
            return #rows .. "|" .. tostring(rows[1]["Data"])
            """);
        result.String.ShouldBe("1|1");
    }

    // RegisterTable V2 — UPDATE non-PK columns in a 3-field table; exercises
    // set_row_value's KITSUNE_TTABLECONTENTS path (fieldCount > 2) for UPDATE.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_Update_ThreeField_UpdatesSubTableValues()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t[1] = {"John", "Doe"}
            t[2] = {"Jane", "Smith"}
            SQLiteExt.RegisterTable("UpdateThree", {"Id", "FirstName", "LastName"}, t)
            local ok, err = db:Query("UPDATE UpdateThree SET FirstName='Hans', LastName='Mueller' WHERE Id=1")
            assert(ok, err)
            db:Fetch()
            local rows = SQLiteExt.Query("SELECT FirstName, LastName FROM UpdateThree ORDER BY Id")
            db:Close()
            -- row 1 updated; row 2 unchanged
            return rows[1]["FirstName"] .. " " .. rows[1]["LastName"] .. "|" ..
                   rows[2]["FirstName"] .. " " .. rows[2]["LastName"]
            """);
        result.String.ShouldBe("Hans Mueller|Jane Smith");
    }

    // RegisterTable V2 — UPDATE with PK rename on a 3-field table:
    // exercises sub-table creation AND old-key deletion in the same xUpdate call.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterTable_Update_PKRename_ThreeField()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local t = {}
            t[1] = {"John", "Doe"}
            t[2] = {"Jane", "Smith"}
            SQLiteExt.RegisterTable("PKRenameThree", {"Id", "FirstName", "LastName"}, t)
            local ok, err = db:Query("UPDATE PKRenameThree SET Id=9, FirstName='Hans', LastName='Mueller' WHERE Id=1")
            assert(ok, err)
            db:Fetch()
            local rows = SQLiteExt.Query("SELECT Id, FirstName, LastName FROM PKRenameThree ORDER BY Id")
            db:Close()
            -- old key 1 gone, new key 9 present; row 2 unchanged
            return #rows .. "|" .. rows[1]["Id"] .. "|" .. rows[1]["FirstName"] .. "|" ..
                   rows[2]["Id"] .. "|" .. rows[2]["FirstName"]
            """);
        result.String.ShouldBe("2|2|Jane|9|Hans");
    }
}

