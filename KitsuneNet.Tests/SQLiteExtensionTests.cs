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

    // Diagnostic test:
    // load_extension failure throws a SqliteException with the exact error message,
    // rather than silently returning null through the Lua path.
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
}
