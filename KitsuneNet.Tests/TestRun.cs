namespace KitsuneNet.Tests;

/// <summary>
/// Per-run unique names for state that lives outside the test process: database rows and
/// tables, Redis keys, Kafka topics and groups, temp files and named mutexes. Overlapping
/// runs, and a run after a failed one, then cannot see each other's data.
/// </summary>
internal static class TestRun
{
    /// <summary>Short id shared by every test in this process.</summary>
    public static readonly string Id = Guid.NewGuid().ToString("N")[..8];

    /// <summary>
    /// A new engine whose Lua global <c>RUN_ID</c> holds <see cref="Id"/>, so Lua code can
    /// build names like <c>'kitsune_test_json_' .. RUN_ID</c>.
    /// </summary>
    public static KitsuneEngine NewEngine()
    {
        var engine = new KitsuneEngine();
        engine.SetVariable("RUN_ID", Id);
        return engine;
    }

    /// <summary><paramref name="baseName"/> with the run id appended, for names built in C#.</summary>
    public static string Name(string baseName) => $"{baseName}_{Id}";
}
