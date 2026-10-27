using System.Runtime.InteropServices;

namespace KitsuneNet.Tests;

/// <summary>
/// Runs LeakSanitizer's check inside the test host after the last test when <c>KITSUNE_LSAN</c> is <c>1</c>
/// (set by <c>build-linux-asan-and-test.ps1</c>, which preloads the sanitizer runtime). The test runner ends
/// the test host without a normal exit, so LeakSanitizer's own exit-time check never runs there. Leaks are
/// written to the sanitizer log, which the script reads.
/// </summary>
public sealed class LeakSanitizerCheck : IDisposable
{
    private delegate int LeakCheck();

    public void Dispose()
    {
        if (Environment.GetEnvironmentVariable("KITSUNE_LSAN") != "1")
        {
            return;
        }

        if (!NativeLibrary.TryGetExport(NativeLibrary.GetMainProgramHandle(), "__lsan_do_recoverable_leak_check", out IntPtr check))
        {
            throw new InvalidOperationException("KITSUNE_LSAN=1 but the LeakSanitizer runtime is not loaded");
        }

        Marshal.GetDelegateForFunctionPointer<LeakCheck>(check)();
    }
}
