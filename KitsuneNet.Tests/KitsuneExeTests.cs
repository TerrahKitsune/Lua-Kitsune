using Shouldly;
using System.Diagnostics;
using Xunit;

namespace KitsuneNet.Tests;

/// <summary>
/// Tests for what Kitsune.exe reports to its caller: the process exit code, and that it
/// prints nothing of its own. Each case runs a script in a Kitsune.exe child process.
/// </summary>
[Collection("KitsuneSequential")]
public sealed class KitsuneExeTests
{
    [KitsuneExeFact]
    public async Task ExitCode_ScriptWithoutReturnValue_IsZeroAndNothingIsPrinted()
    {
        ExeResult result = await RunScriptAsync("local x = 1");
        result.ShouldBe(new ExeResult(0, string.Empty, string.Empty));
    }

    [KitsuneExeFact]
    public async Task ExitCode_ScriptReturnsNumber_IsThatNumber()
    {
        (await RunScriptAsync("return 3")).ShouldBe(new ExeResult(3, string.Empty, string.Empty));
        (await RunScriptAsync("return 2.9")).ShouldBe(new ExeResult(2, string.Empty, string.Empty));
    }

    [KitsuneExeFact]
    public async Task ExitCode_ScriptReturnsOtherValue_IsZeroAndTheValueIsNotPrinted()
    {
        (await RunScriptAsync("return 'hello'")).ShouldBe(new ExeResult(0, string.Empty, string.Empty));
        (await RunScriptAsync("return true")).ShouldBe(new ExeResult(0, string.Empty, string.Empty));
        (await RunScriptAsync("return {}")).ShouldBe(new ExeResult(0, string.Empty, string.Empty));
    }

    [KitsuneExeFact]
    public async Task ExitCode_UncaughtError_IsMinusOneWithTheErrorOnStderr()
    {
        ExeResult result = await RunScriptAsync("error('kaboom')");
        result.ExitCode.ShouldBe(-1);
        result.Stdout.ShouldBeEmpty();
        result.Stderr.ShouldEndWith(":1: kaboom");
    }

    [KitsuneExeFact]
    public async Task ExitCode_OsExit_IsTheRequestedCodeAndLaterCodeDoesNotRun()
    {
        ExeResult result = await RunScriptAsync("print('before') os.exit(5) print('after')");
        result.ShouldBe(new ExeResult(5, "before", string.Empty));
    }

    [KitsuneExeFact]
    public async Task ExitCode_OsExitFromATask_EndsTheMainScriptWithThatCode()
    {
        ExeResult result = await RunScriptAsync(@"
            Tasks.New(function() Sleep(50) os.exit(7) end)
            while true do Sleep(10) end");
        result.ShouldBe(new ExeResult(7, string.Empty, string.Empty));
    }

    [KitsuneExeFact]
    public async Task ExitCode_OsExitInExpression_SkipsTheScript()
    {
        ExeResult result = await RunAsync("-e \"os.exit(6)\" -e \"print('second')\"", "print('script')");
        result.ShouldBe(new ExeResult(6, string.Empty, string.Empty));
    }

    [KitsuneExeFact]
    public async Task ExitCode_ErrorInExpression_IsMinusOne()
    {
        ExeResult result = await RunAsync("-e \"error('in -e')\"", "print('script')");
        result.ExitCode.ShouldBe(-1);
        result.Stdout.ShouldBeEmpty();
        result.Stderr.ShouldEndWith("in -e");
    }

    private static Task<ExeResult> RunScriptAsync(string script) => RunAsync(string.Empty, script);

    private static async Task<ExeResult> RunAsync(string leadingArguments, string script)
    {
        string exe = KitsuneExeFactAttribute.ExePath;
        string path = Path.Combine(Path.GetTempPath(), "_kitsune_exe_" + Guid.NewGuid().ToString("N") + ".lua");
        await File.WriteAllTextAsync(path, script);

        var psi = new ProcessStartInfo(exe, $"{leadingArguments} \"{path}\"")
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            WorkingDirectory = Path.GetDirectoryName(exe)!,
        };
        using Process process = Process.Start(psi)!;
        try
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(20));
            Task<string> stdout = process.StandardOutput.ReadToEndAsync(timeout.Token);
            Task<string> stderr = process.StandardError.ReadToEndAsync(timeout.Token);
            await process.WaitForExitAsync(timeout.Token);
            return new ExeResult(process.ExitCode, (await stdout).Trim(), (await stderr).Trim());
        }
        finally
        {
            if (!process.HasExited)
            {
                process.Kill(true);
            }

            File.Delete(path);
        }
    }

    private sealed record ExeResult(int ExitCode, string Stdout, string Stderr);
}
