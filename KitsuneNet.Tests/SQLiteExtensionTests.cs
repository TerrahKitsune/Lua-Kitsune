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
    private static string ExtensionPath =>
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
            db:Query("SELECT AddOne(68)")
            db:Fetch()
            local r = db:GetRow(1)
            db:Close()
            DB = nil; -- Free
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
}
