using Xunit;

namespace KitsuneNet.Tests;

/// <summary>
/// Marks a test that spawns brief visible console windows on Windows (e.g. Process tests
/// started without <c>noconsole=true</c>).  The test runs normally unless the environment
/// variable <c>KITSUNE_SKIP_ANNOYING</c> is set to <c>1</c>, in which case it is skipped.
/// <para>
/// To suppress these tests, add the following to your <c>.runsettings</c> file:
/// <code>
/// &lt;KITSUNE_SKIP_ANNOYING&gt;1&lt;/KITSUNE_SKIP_ANNOYING&gt;
/// </code>
/// </para>
/// </summary>
public sealed class AnnoyingFactAttribute : FactAttribute
{
    public AnnoyingFactAttribute()
    {
        if (Environment.GetEnvironmentVariable("KITSUNE_SKIP_ANNOYING") == "1")
        {
            Skip = "Skipped: set KITSUNE_SKIP_ANNOYING=0 (or remove it) to run tests that spawn brief console windows";
        }
    }
}
