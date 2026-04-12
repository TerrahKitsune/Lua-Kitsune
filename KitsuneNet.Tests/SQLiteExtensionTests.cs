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
}
