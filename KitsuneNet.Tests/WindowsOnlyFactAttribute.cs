using System.Runtime.InteropServices;
using Xunit;

namespace KitsuneNet.Tests;

public sealed class WindowsOnlyFactAttribute : FactAttribute
{
    public WindowsOnlyFactAttribute()
    {
        if (!RuntimeInformation.IsOSPlatform(OSPlatform.Windows))
        {
            Skip = "This test only runs on Windows";
        }
    }
}
