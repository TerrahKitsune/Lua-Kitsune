using KitsuneNet;
using Shouldly;
using System.Text;
using Xunit;

namespace KitsuneNet.Tests
{
    public sealed class KitsuneEngineTests
    {
        // ── Init / Dispose ───────────────────────────────────────────────────────

        [Fact]
        public void Init_CreatesEngine_WithoutThrowing()
        {
            using KitsuneEngine engine = new();
            engine.ShouldNotBeNull();
        }

        [Fact]
        public void Init_IsNotRunning_AfterCreation()
        {
            using KitsuneEngine engine = new();
            engine.IsRunning.ShouldBeFalse();
        }

        [Fact]
        public void Init_HasNoError_AfterCreation()
        {
            using KitsuneEngine engine = new();
            engine.GetError().ShouldBeNull();
        }

        [Fact]
        public void Dispose_CleansUp_WithoutThrowing()
        {
            KitsuneEngine engine = new();
            Should.NotThrow(engine.Dispose);
        }

        // ── ExecuteString – success ──────────────────────────────────────────────

        [Fact]
        public void ExecuteString_Runs_And_Returns()
        {
            const string expected = "Hello ExecuteString";
            using KitsuneEngine engine = new();
            engine.ExecuteString($"return '{expected}';").ShouldBe(0);
            engine.HasResult().ShouldBeTrue();
            engine.GetResultString().ShouldBe(expected);
        }

        [Fact]
        public void ExecuteString_NilReturn_HasNoResult()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("return nil").ShouldBe(0);
            engine.HasResult().ShouldBeFalse();
        }

        [Fact]
        public void ExecuteString_NoReturn_HasNoResult()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("local x = 1").ShouldBe(0);
            engine.HasResult().ShouldBeFalse();
        }

        // ── ExecuteString – errors ───────────────────────────────────────────────

        [Fact]
        public void ExecuteString_SyntaxError_ReturnsNonZero()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("~~~~invalid").ShouldNotBe(0);
        }

        [Fact]
        public void ExecuteString_SyntaxError_SetsError()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("~~~~invalid");
            engine.GetError().ShouldNotBeNull();
        }

        [Fact]
        public void ExecuteString_RuntimeError_HasNoResult()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("error('boom')");
            engine.HasResult().ShouldBeFalse();
        }

        // ── GetResult / HasResult ────────────────────────────────────────────────

        [Fact]
        public void GetResult_ReturnsByteArray()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("return 'bytes'");
            byte[]? result = engine.GetResult();
            result.ShouldNotBeNull();
            Encoding.UTF8.GetString(result!).ShouldBe("bytes");
        }

        [Fact]
        public void HasResult_AfterConsumed_ReturnsFalse()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("return 'consume me'");
            engine.GetResult();
            engine.HasResult().ShouldBeFalse();
        }

        // ── SetVariable / GetVariable ────────────────────────────────────────────

        [Fact]
        public void SetVariable_String_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("myVar", "hello from csharp");
            engine.ExecuteString("return Vars.myVar");
            engine.GetResultString().ShouldBe("hello from csharp");
        }

        [Fact]
        public void SetVariable_Bytes_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("bytesVar", Encoding.UTF8.GetBytes("bytes value"));
            engine.ExecuteString("return Vars.bytesVar");
            engine.GetResultString().ShouldBe("bytes value");
        }

        [Fact]
        public void GetVariable_ScriptSetGlobal_ReturnsValue()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("Vars.testGlobal = 'get variable test'");
            engine.GetVariable("testGlobal").ShouldBe("get variable test");
        }

        [Fact]
        public void GetVariable_NonExistent_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            engine.GetVariable("nonExistentVar_XYZ").ShouldBeNull();
        }

        [Fact]
        public void GetVariableBytes_ScriptSetGlobal_ReturnsBytes()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("Vars.byteVar = 'bytes test'");
            byte[]? bytes = engine.GetVariableBytes("byteVar");
            bytes.ShouldNotBeNull();
            Encoding.UTF8.GetString(bytes!).ShouldBe("bytes test");
        }

        // ── ExecuteFile ──────────────────────────────────────────────────────────

        [Fact]
        public void ExecuteFile_RunsScript_AndReturnsResult()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "return 'from file'");
                using KitsuneEngine engine = new();
                engine.ExecuteFile(path).ShouldBe(0);
                engine.GetResultString().ShouldBe("from file");
            }
            finally
            {
                File.Delete(path);
            }
        }

        // ── IsRunning / Interrupt / Wait ─────────────────────────────────────────

        [Fact]
        public async Task IsRunning_DuringExecution_ReturnsTrue()
        {
            using KitsuneEngine engine = new();
            Task execTask = Task.Run(() => engine.ExecuteString("while true do end"));
            try
            {
                SpinUntilRunning(engine);
                engine.IsRunning.ShouldBeTrue();
            }
            finally
            {
                engine.Interrupt();
                engine.Wait();
                await execTask.WaitAsync(TimeSpan.FromSeconds(2));
            }
        }

        [Fact]
        public async Task Interrupt_StopsScript()
        {
            using KitsuneEngine engine = new();
            Task execTask = Task.Run(() => engine.ExecuteString("while true do end"));
            SpinUntilRunning(engine);
            engine.Interrupt();
            engine.Wait();
            await execTask.WaitAsync(TimeSpan.FromSeconds(2));
            engine.IsRunning.ShouldBeFalse();
            engine.GetError().ShouldNotBeNull();
            engine.GetError()?.ShouldContain("interrupted");
        }

        [Fact]
        public async Task Wait_CancelledToken_ThrowsOperationCanceledException()
        {
            using KitsuneEngine engine = new();
            Task execTask = Task.Run(() => engine.ExecuteString("while true do end"));
            SpinUntilRunning(engine);
            // Create the token *after* the engine is confirmed running so the
            // 200 ms window doesn't expire during SpinUntilRunning.
            using CancellationTokenSource cts = new(TimeSpan.FromMilliseconds(200));
            try
            {
                Should.Throw<OperationCanceledException>(() => engine.Wait(cts.Token));
            }
            finally
            {
                engine.Interrupt();
                engine.Wait();
                await execTask.WaitAsync(TimeSpan.FromSeconds(2));
            }
        }

        // ── Helpers ──────────────────────────────────────────────────────────────

        private static void SpinUntilRunning(KitsuneEngine engine, int timeoutMs = 2000)
        {
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (!engine.IsRunning && DateTime.UtcNow < deadline)
                Thread.Sleep(1);
        }
    }
}
