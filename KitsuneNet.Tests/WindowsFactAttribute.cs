using Xunit;

namespace KitsuneNet.Tests;

/// <summary>
/// A fact that is skipped automatically on non-Windows platforms.
/// Use this for tests that require a Windows-only module (e.g. Llama).
/// </summary>
public sealed class WindowsFactAttribute : FactAttribute
{
    public WindowsFactAttribute()
    {
        if (!OperatingSystem.IsWindows())
        {
            Skip = "Windows-only test (Llama is not available on this platform)";
        }
    }
}
