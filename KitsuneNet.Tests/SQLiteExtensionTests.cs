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

    // ---- RegisterVirtualTable tests -----------------------------------------

    // Reader returns {name, points} rows; SELECT returns expected results (2-field).
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_ReadOnly_TwoField()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            SQLiteExt.RegisterVirtualTable("RVT2", {"Name", "Points"}, function(ctx, nth)
                local data = {{"alice", 100}, {"bob", 200}, {"carol", 300}}
                return data[nth]
            end)
            local rows = SQLiteExt.Query("SELECT Name, Points FROM RVT2 ORDER BY Name")
            db:Close()
            return #rows .. "|" .. rows[1]["Name"] .. "|" .. rows[2]["Points"] .. "|" .. rows[3]["Name"]
            """);
        result.String.ShouldBe("3|alice|200|carol");
    }

    // Reader returns {id, first, last} rows; SELECT returns expected results (3-field).
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_ReadOnly_ThreeField()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            SQLiteExt.RegisterVirtualTable("RVT3", {"Id", "First", "Last"}, function(ctx, nth)
                local data = {{1, "John", "Doe"}, {2, "Jane", "Smith"}}
                return data[nth]
            end)
            local rows = SQLiteExt.Query("SELECT Id, First, Last FROM RVT3 ORDER BY Id")
            db:Close()
            return rows[1]["First"] .. " " .. rows[1]["Last"] .. "|" ..
                   rows[2]["First"] .. " " .. rows[2]["Last"]
            """);
        result.String.ShouldBe("John Doe|Jane Smith");
    }

    // nth is 1 on the first reader call and increments on each subsequent call.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_NthCounter()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local calls = {}
            SQLiteExt.RegisterVirtualTable("NthVT", {"Id", "Val"}, function(ctx, nth)
                calls[#calls + 1] = nth
                if nth > 3 then return nil end
                return {nth, nth * 10}
            end)
            SQLiteExt.Query("SELECT * FROM NthVT")
            db:Close()
            return calls[1] .. "|" .. calls[2] .. "|" .. calls[3] .. "|" .. calls[4]
            """);
        result.String.ShouldBe("1|2|3|4");
    }

    // All cursors on the same vtable share the vtable-level context.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_ContextPerCursor()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local finalCount = 0
            SQLiteExt.RegisterVirtualTable("CtxVT", {"Id", "Val"}, function(ctx, nth)
                if nth == 1 then
                    ctx.openCount = (ctx.openCount or 0) + 1
                    finalCount = ctx.openCount
                end
                if nth > 1 then return nil end
                return {nth, nth}
            end)
            -- Cross join opens two cursors; both share the same vtable context.
            SQLiteExt.Query("SELECT a.Id FROM CtxVT a, CtxVT b LIMIT 4")
            db:Close()
            return finalCount
            """);
        // Each of the two cursors calls xFilter once (nth=1 increments the shared counter).
        result.AsInt64.ShouldBe(2L);
    }

    // No update function provided; INSERT returns an error containing "Readonly".
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_ReadOnly_RejectWrite()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            SQLiteExt.RegisterVirtualTable("RdOnlyVT", {"Id", "Val"}, function(ctx, nth)
                if nth > 1 then return nil end
                return {1, "one"}
            end)
            local ok, err = db:Query("INSERT INTO RdOnlyVT VALUES(2, 'two')")
            db:Close()
            if ok then return "unexpected_success" end
            return err or "(no message)"
            """);
        result.String.ShouldNotBeNull();
        result.String.ShouldContain("Readonly");
    }

    // Update function called with pk=nil on INSERT; new row visible in reader output.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_Insert()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local store = {{"a", 1}, {"b", 2}}
            SQLiteExt.RegisterVirtualTable("InsertVT", {"Key", "Val"},
                function(ctx, nth) return store[nth] end,
                nil,
                function(ctx, pk, data)
                    if pk == nil then store[#store + 1] = {data[1], data[2]} end
                end
            )
            local ok, err = db:Query("INSERT INTO InsertVT VALUES('c', 3)")
            assert(ok, err)
            db:Fetch()
            local rows = SQLiteExt.Query("SELECT Key, Val FROM InsertVT ORDER BY Key")
            db:Close()
            return #rows .. "|" .. rows[3]["Key"] .. "|" .. tostring(rows[3]["Val"])
            """);
        result.String.ShouldBe("3|c|3");
    }

    // Update function called with data=nil on DELETE; row gone from subsequent reads.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_Delete()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local store = {{"a", 1}, {"b", 2}, {"c", 3}}
            SQLiteExt.RegisterVirtualTable("DeleteVT", {"Key", "Val"},
                function(ctx, nth) return store[nth] end,
                nil,
                function(ctx, pk, data)
                    if data == nil then
                        for i, row in ipairs(store) do
                            if row[1] == pk then table.remove(store, i); return end
                        end
                    end
                end
            )
            local ok, err = db:Query("DELETE FROM DeleteVT WHERE Key='b'")
            assert(ok, err)
            db:Fetch()
            local rows = SQLiteExt.Query("SELECT Key FROM DeleteVT ORDER BY Key")
            db:Close()
            return #rows .. "|" .. rows[1]["Key"] .. "|" .. rows[2]["Key"]
            """);
        result.String.ShouldBe("2|a|c");
    }

    // data[1] == pk (same PK); non-PK field values updated.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_Update_SamePK()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local store = {{"a", 100}, {"b", 200}}
            SQLiteExt.RegisterVirtualTable("UpdateSameVT", {"Key", "Val"},
                function(ctx, nth) return store[nth] end,
                nil,
                function(ctx, pk, data)
                    if pk ~= nil and data ~= nil then
                        for _, row in ipairs(store) do
                            if row[1] == pk then row[2] = data[2]; return end
                        end
                    end
                end
            )
            local ok, err = db:Query("UPDATE UpdateSameVT SET Val=999 WHERE Key='a'")
            assert(ok, err)
            db:Fetch()
            local rows = SQLiteExt.Query("SELECT Key, Val FROM UpdateSameVT ORDER BY Key")
            db:Close()
            return rows[1]["Key"] .. "|" .. tostring(rows[1]["Val"]) .. "|" ..
                   rows[2]["Key"] .. "|" .. tostring(rows[2]["Val"])
            """);
        result.String.ShouldBe("a|999|b|200");
    }

    // data[1] ~= pk (PK rename); PK and field values both change.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_Update_RenamePK()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local store = {{"a", 100}, {"b", 200}}
            SQLiteExt.RegisterVirtualTable("RenameVT", {"Key", "Val"},
                function(ctx, nth) return store[nth] end,
                nil,
                function(ctx, pk, data)
                    if pk ~= nil and data ~= nil then
                        for i, row in ipairs(store) do
                            if row[1] == pk then store[i] = {data[1], data[2]}; return end
                        end
                    end
                end
            )
            local ok, err = db:Query("UPDATE RenameVT SET Key='z', Val=777 WHERE Key='a'")
            assert(ok, err)
            db:Fetch()
            local rows = SQLiteExt.Query("SELECT Key, Val FROM RenameVT ORDER BY Key")
            db:Close()
            return #rows .. "|" .. rows[1]["Key"] .. "|" .. tostring(rows[1]["Val"]) .. "|" .. rows[2]["Key"]
            """);
        result.String.ShouldBe("2|b|200|z");
    }

    // Update function calls error(); SQLite surfaces that message as an error.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_UpdateError()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            SQLiteExt.RegisterVirtualTable("ErrVT", {"Id", "Val"},
                function(ctx, nth)
                    if nth > 1 then return nil end
                    return {1, "one"}
                end,
                nil,
                function(ctx, pk, data) error("Forbidden key") end
            )
            local ok, err = db:Query("INSERT INTO ErrVT VALUES(2, 'two')")
            db:Close()
            if ok then return "unexpected_success" end
            return err or "(no message)"
            """);
        result.String.ShouldNotBeNull();
        result.String.ShouldContain("Forbidden key");
    }

    // Index function returns true (unique) for EQ on Key; reader receives index, no full scan.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_Index_EqLookup()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local gotIndex = false
            local lookupMap = {a=100, b=200, c=300}
            SQLiteExt.RegisterVirtualTable("IdxEqVT", {"Key", "Val"},
                function(ctx, nth, index)
                    if index then
                        gotIndex = true
                        if nth > 1 then return nil end
                        local k = index[1].Value
                        if lookupMap[k] then return {k, lookupMap[k]} end
                        return nil
                    end
                    local keys = {"a", "b", "c"}
                    local k = keys[nth]
                    if not k then return nil end
                    return {k, lookupMap[k]}
                end,
                function(ctx, op, col)
                    if col == "Key" and op == "=" then return true end
                    return nil
                end
            )
            local rows = SQLiteExt.Query("SELECT Val FROM IdxEqVT WHERE Key='b'")
            db:Close()
            return tostring(gotIndex) .. "|" .. tostring(rows[1]["Val"])
            """);
        result.String.ShouldBe("true|200");
    }

    // Index function returns nil; full scan is used and reader receives nil index.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_Index_FallbackFullScan()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local wasFullScan = false
            SQLiteExt.RegisterVirtualTable("IdxFallbackVT", {"Key", "Val"},
                function(ctx, nth, index)
                    if nth == 1 then wasFullScan = (index == nil) end
                    local data = {{"a", 1}, {"b", 2}, {"c", 3}}
                    return data[nth]
                end,
                function(ctx, op, col) return nil end
            )
            local rows = SQLiteExt.Query("SELECT Val FROM IdxFallbackVT WHERE Key='b'")
            db:Close()
            return tostring(wasFullScan) .. "|" .. #rows .. "|" .. tostring(rows[1]["Val"])
            """);
        result.String.ShouldBe("true|1|2");
    }

    // Returning 0 from indexFunc is treated as decline; scan falls back to full scan.
    // This verifies 0 and negative are NOT treated as unique — only true is unique.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_Index_ZeroCostDeclines()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local wasFullScan = false
            SQLiteExt.RegisterVirtualTable("ZeroCostVT", {"Key", "Val"},
                function(ctx, nth, index)
                    if nth == 1 then wasFullScan = (index == nil) end
                    local data = {{"a", 1}, {"b", 2}, {"c", 3}}
                    return data[nth]
                end,
                function(ctx, op, col)
                    return 0  -- 0 = decline, not unique
                end
            )
            local rows = SQLiteExt.Query("SELECT Val FROM ZeroCostVT WHERE Key='b'")
            db:Close()
            return tostring(wasFullScan) .. "|" .. #rows
            """);
        result.String.ShouldBe("true|1");
    }

    // Returning a negative integer from indexFunc is also treated as decline.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_Index_NegativeCostDeclines()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local wasFullScan = false
            SQLiteExt.RegisterVirtualTable("NegCostVT", {"Key", "Val"},
                function(ctx, nth, index)
                    if nth == 1 then wasFullScan = (index == nil) end
                    local data = {{"a", 1}, {"b", 2}, {"c", 3}}
                    return data[nth]
                end,
                function(ctx, op, col)
                    return -5  -- negative integer = decline
                end
            )
            local rows = SQLiteExt.Query("SELECT Val FROM NegCostVT WHERE Key='b'")
            db:Close()
            return tostring(wasFullScan) .. "|" .. #rows
            """);
        result.String.ShouldBe("true|1");
    }

    // Returning a positive float from indexFunc is accepted as a cost (KITSUNE_TNUMBER path).
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_Index_FloatCostAccepted()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local gotIndex = false
            local lookupMap = {a=1, b=2, c=3}
            SQLiteExt.RegisterVirtualTable("FloatCostVT", {"Key", "Val"},
                function(ctx, nth, index)
                    if index then
                        gotIndex = true
                        if nth > 1 then return nil end
                        local k = index[1].Value
                        return {k, lookupMap[k]}
                    end
                    local keys = {"a", "b", "c"}
                    if not keys[nth] then return nil end
                    return {keys[nth], lookupMap[keys[nth]]}
                end,
                function(ctx, op, col)
                    if col == "Key" and op == "=" then return 0.5 end  -- float cost, not unique
                    return nil
                end
            )
            local rows = SQLiteExt.Query("SELECT Val FROM FloatCostVT WHERE Key='c'")
            db:Close()
            return tostring(gotIndex) .. "|" .. tostring(rows[1]["Val"])
            """);
        result.String.ShouldBe("true|3");
    }

    // index sub-table entries have the correct Column, Op, and Value fields.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_Index_ConstraintShape()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local captured = nil
            SQLiteExt.RegisterVirtualTable("ShapeVT", {"Key", "Val"},
                function(ctx, nth, index)
                    if nth == 1 and index then captured = index end
                    if index then
                        if nth > 1 then return nil end
                        return {index[1].Value, 99}
                    end
                    if nth > 1 then return nil end
                    return {"a", 99}
                end,
                function(ctx, op, col)
                    if col == "Key" and op == "=" then return 10 end
                    return nil
                end
            )
            SQLiteExt.Query("SELECT * FROM ShapeVT WHERE Key='testkey'")
            db:Close()
            if not captured then return "no_index" end
            local c = captured[1]
            return c.Column .. "|" .. c.Op .. "|" .. tostring(c.Value)
            """);
        result.String.ShouldBe("Key|=|testkey");
    }

    // Context table persists across xFilter re-scans (JOIN); nth resets to 1 each scan.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_ContextPreservedOnRescan()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            -- Outer vtable: 2 rows, forces inner to be rescanned twice.
            SQLiteExt.RegisterVirtualTable("RescanOuter", {"N", "V"}, function(ctx, nth)
                if nth > 2 then return nil end
                return {nth, nth}
            end)
            -- Inner vtable: increments ctx.scanCount on each new scan (nth==1).
            -- ctx.scanCount accumulates across rescans, confirming context survives.
            local lastScanCount = 0
            SQLiteExt.RegisterVirtualTable("RescanInner", {"N", "V"}, function(ctx, nth)
                if nth == 1 then
                    ctx.scanCount = (ctx.scanCount or 0) + 1
                    lastScanCount = ctx.scanCount
                end
                if nth > 1 then return nil end
                return {nth, nth}
            end)
            -- Cross join: outer has 2 rows → inner xFilter called twice.
            SQLiteExt.Query("SELECT * FROM RescanOuter, RescanInner")
            db:Close()
            return lastScanCount
            """);
        result.AsInt64.ShouldBe(2L);
    }

    // Reader returns nil on the very first call; SELECT returns zero rows.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_EmptyReader_ReturnsNoRows()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            SQLiteExt.RegisterVirtualTable("EmptyRVT", {"Id", "Val"}, function(ctx, nth)
                return nil
            end)
            local rows = SQLiteExt.Query("SELECT * FROM EmptyRVT") or {}
            db:Close()
            return #rows
            """);
        result.AsInt64.ShouldBe(0L);
    }

    // Calling RegisterVirtualTable twice with the same name replaces the first reader.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_Reregistration_ReplacesReader()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            SQLiteExt.RegisterVirtualTable("ReregVT", {"Id", "Val"}, function(ctx, nth)
                if nth > 1 then return nil end
                return {1, "first"}
            end)
            local r1 = SQLiteExt.Query("SELECT Val FROM ReregVT")
            -- Re-register with a different reader under the same name.
            SQLiteExt.RegisterVirtualTable("ReregVT", {"Id", "Val"}, function(ctx, nth)
                if nth > 1 then return nil end
                return {2, "second"}
            end)
            local r2 = SQLiteExt.Query("SELECT Val FROM ReregVT")
            db:Close()
            return r1[1]["Val"] .. "|" .. r2[1]["Val"]
            """);
        result.String.ShouldBe("first|second");
    }

    // 3-field INSERT: xUpdate builds a 3-element data table; update function receives
    // data[1]=PK, data[2]=col1, data[3]=col2.
    [WindowsOnlyFact]
    public async Task SQLiteExtension_RegisterVirtualTable_ThreeField_Insert()
    {
        using KitsuneEngine engine = new();
        engine.SetString("extPath", ExtensionPath);
        LuaValue result = await engine.ExecuteStringAsync("""
            local db = SQLite.Open()
            db:Query("SELECT load_extension('" .. extPath .. "')")
            db:Fetch()
            local store = {}
            SQLiteExt.RegisterVirtualTable("Insert3VT", {"Id", "First", "Last"},
                function(ctx, nth) return store[nth] end,
                nil,
                function(ctx, pk, data)
                    if pk == nil then
                        store[#store + 1] = {data[1], data[2], data[3]}
                    end
                end
            )
            local ok, err = db:Query("INSERT INTO Insert3VT VALUES(7, 'Hans', 'Mueller')")
            assert(ok, err)
            db:Fetch()
            local rows = SQLiteExt.Query("SELECT Id, First, Last FROM Insert3VT")
            db:Close()
            return tostring(rows[1]["Id"]) .. "|" .. rows[1]["First"] .. " " .. rows[1]["Last"]
            """);
        result.String.ShouldBe("7|Hans Mueller");
    }
}

