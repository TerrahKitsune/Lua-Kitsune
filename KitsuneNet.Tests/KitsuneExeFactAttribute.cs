using Xunit;

namespace KitsuneNet.Tests;

/// <summary>
/// A fact that needs the prebuilt Windows app <c>x64\&lt;Configuration&gt;\Kitsune.exe</c>.
/// Skipped on other platforms and when the app has not been built for this configuration.
/// </summary>
public sealed class KitsuneExeFactAttribute : FactAttribute
{
    // bin\{Configuration}\net10.0\ -> repo root
    public static readonly string ExePath = Path.GetFullPath(Path.Combine(
        AppContext.BaseDirectory, "..", "..", "..", "..", "x64", Configuration, "Kitsune.exe"));

#if DEBUG
    private const string Configuration = "Debug";
#else
    private const string Configuration = "Release";
#endif

    public KitsuneExeFactAttribute()
    {
        if (!OperatingSystem.IsWindows())
        {
            Skip = "Windows-only test";
        }
        else if (!File.Exists(ExePath))
        {
            Skip = $"Kitsune.exe is not built: {ExePath}";
        }
    }
}
