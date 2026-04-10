using Xunit;

namespace KitsuneNet.Tests;

/// <summary>
/// Skips the test unless <c>KITSUNE_SQLITE_TEST</c> is set to the path of an
/// existing SQLite database file that the current user can open for writing.
/// On Linux, Windows-style paths (e.g. <c>R:\test.sqlite</c>) are automatically
/// translated to <c>/tmp/&lt;filename&gt;</c> and the environment variable is
/// updated so that Lua's <c>os.getenv</c> also receives the translated path.
/// </summary>
public sealed class SQLiteFactAttribute : FactAttribute
{
    private static readonly string? _path = ResolvedPath();

    public SQLiteFactAttribute()
    {
        if (string.IsNullOrEmpty(_path))
        {
            Skip = "Set KITSUNE_SQLITE_TEST to the path of a SQLite database file to run SQLite file-database tests";
        }
    }

    private static string? ResolvedPath()
    {
        string? raw = Environment.GetEnvironmentVariable("KITSUNE_SQLITE_TEST");
        if (string.IsNullOrEmpty(raw))
        {
            return null;
        }

        // Translate Windows drive-letter paths (e.g. R:\test.sqlite) when running
        // on Linux so that file-database tests can run without a separate runsettings.
        if (!OperatingSystem.IsWindows() && raw.Length >= 2 && raw[1] == ':')
        {
            string translated = Path.Combine("/tmp", Path.GetFileName(raw));
            Environment.SetEnvironmentVariable("KITSUNE_SQLITE_TEST", translated);
            return translated;
        }

        return raw;
    }
}
