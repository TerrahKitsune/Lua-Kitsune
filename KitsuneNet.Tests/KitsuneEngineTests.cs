using KitsuneNet;
using Shouldly;
using System.Text;
using Xunit;

namespace KitsuneNet.Tests
{
    public sealed class KitsuneEngineTests
    {
        // -- Init / Dispose -------------------------------------------------------

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
        public void Dispose_CleansUp_WithoutThrowing()
        {
            KitsuneEngine engine = new();
            Should.NotThrow(engine.Dispose);
        }

        [Fact]
        public async Task Dispose_WithRunningCoroutine_InterruptsAndCompletes()
        {
            KitsuneEngine engine = new();
            engine.ExecuteString("while true do end", fireAndForget: true);
            SpinUntilRunning(engine);

            // KitsuneCleanup must interrupt running coroutines before waiting on the
            // scheduler thread, so Dispose must return well within a generous timeout.
            Task disposeTask = Task.Run(engine.Dispose);
            Task winner = await Task.WhenAny(disposeTask, Task.Delay(TimeSpan.FromSeconds(5)));
            winner.ShouldBe(disposeTask, "Dispose hung — KitsuneCleanup did not interrupt the stuck coroutine");
            await disposeTask; // propagate any exception from Dispose itself
        }

        // -- ExecuteString – success ----------------------------------------------

        [Fact]
        public void ExecuteString_Runs_And_Returns()
        {
            const string expected = "Hello ExecuteString";
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString($"return '{expected}';");
            id.ShouldBeGreaterThan(0);
            engine.Wait(id);
            engine.GetActiveIds().ShouldContain(id);
            engine.GetResultString(id).ShouldBe(expected);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ExecuteString_NilReturn_HasNoResultData()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return nil");
            engine.Wait(id);
            engine.HasResult(id, out nuint len).ShouldBeTrue();
            len.ShouldBe((nuint)0);
            engine.GetActiveIds().ShouldContain(id);
            engine.ReleaseCoroutine(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ExecuteString_NoReturn_HasNoResultData()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("local x = 1");
            engine.Wait(id);
            engine.GetActiveIds().ShouldContain(id);
            engine.GetResultString(id).ShouldBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- ExecuteString – errors -----------------------------------------------

        [Fact]
        public void ExecuteString_SyntaxError_SetsError()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("~~~~invalid");
            id.ShouldBeGreaterThan(0);
            // Load errors set done=1 synchronously; no Wait needed.
            engine.GetError(id).ShouldNotBeNull();
            engine.GetActiveIds().ShouldContain(id);
            engine.ReleaseCoroutine(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ExecuteString_RuntimeError_SetsError()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("error('boom')");
            engine.Wait(id);
            engine.GetError(id).ShouldNotBeNull();
            engine.GetActiveIds().ShouldContain(id);
            engine.ReleaseCoroutine(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ExecuteString_RuntimeError_HasNoResultData()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("error('boom')");
            engine.Wait(id);
            engine.GetActiveIds().ShouldContain(id);
            engine.GetResult(id).ShouldBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- GetResult / HasResult ------------------------------------------------

        [Fact]
        public void GetResult_ReturnsByteArray()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return 'bytes'");
            engine.Wait(id);
            engine.GetActiveIds().ShouldContain(id);
            byte[]? result = engine.GetResult(id);
            result.ShouldNotBeNull();
            Encoding.UTF8.GetString(result!).ShouldBe("bytes");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetResult_ConsumedTwice_ReturnsNullSecondTime()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return 'consume me'");
            engine.Wait(id);
            engine.GetActiveIds().ShouldContain(id);
            engine.GetResultString(id).ShouldBe("consume me");
            engine.GetActiveIds().ShouldBeEmpty();
            engine.GetResultString(id).ShouldBeNull();
        }

        // -- SetString / SetBool / SetNumber / GetVariable -------------------------

        [Fact]
        public void SetString_StringValue_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetString("myVar", "hello from csharp");
            int id = engine.ExecuteString("return Vars.myVar");
            engine.Wait(id);
            engine.GetActiveIds().ShouldContain(id);
            engine.GetResultString(id).ShouldBe("hello from csharp");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void SetString_BytesValue_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetString("bytesVar", Encoding.UTF8.GetBytes("bytes value"));
            int id = engine.ExecuteString("return Vars.bytesVar");
            engine.Wait(id);
            engine.GetActiveIds().ShouldContain(id);
            engine.GetResultString(id).ShouldBe("bytes value");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void SetBool_True_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetBool("boolVar", true);
            int id = engine.ExecuteString("return tostring(Vars.boolVar)");
            engine.Wait(id);
            engine.GetResultString(id).ShouldBe("true");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void SetBool_False_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetBool("boolVar", false);
            int id = engine.ExecuteString("return tostring(Vars.boolVar)");
            engine.Wait(id);
            engine.GetResultString(id).ShouldBe("false");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void SetNumber_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetNumber("numVar", 3.14);
            int id = engine.ExecuteString("return string.format('%.2f', Vars.numVar)");
            engine.Wait(id);
            engine.GetResultString(id).ShouldBe("3.14");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void SetNumber_Integer_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetNumber("intVar", 42);
            int id = engine.ExecuteString("return tostring(math.tointeger(Vars.intVar))");
            engine.Wait(id);
            engine.GetResultString(id).ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetVariable_ScriptSetGlobal_ReturnsValue()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("Vars.testGlobal = 'get variable test'");
            engine.Wait(id);
            engine.GetActiveIds().ShouldContain(id);
            engine.ReleaseCoroutine(id);
            engine.GetActiveIds().ShouldBeEmpty();
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
            int id = engine.ExecuteString("Vars.byteVar = 'bytes test'");
            engine.Wait(id);
            engine.GetActiveIds().ShouldContain(id);
            engine.ReleaseCoroutine(id);
            engine.GetActiveIds().ShouldBeEmpty();
            byte[]? bytes = engine.GetVariableBytes("byteVar");
            bytes.ShouldNotBeNull();
            Encoding.UTF8.GetString(bytes!).ShouldBe("bytes test");
        }

        // -- ExecuteFile ----------------------------------------------------------

        [Fact]
        public void ExecuteFile_RunsScript_AndReturnsResult()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "return 'from file'");
                using KitsuneEngine engine = new();
                int id = engine.ExecuteFile(path);
                id.ShouldBeGreaterThan(0);
                engine.Wait(id);
                engine.GetActiveIds().ShouldContain(id);
                engine.GetResultString(id).ShouldBe("from file");
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        // -- IsRunning / Interrupt / Wait -----------------------------------------

        [Fact]
        public void IsRunning_DuringExecution_ReturnsTrue()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("while true do end");
            try
            {
                SpinUntilRunning(engine);
                engine.IsRunning.ShouldBeTrue();
                engine.GetActiveIds().ShouldContain(id);
            }
            finally
            {
                engine.Interrupt();
                engine.Wait();
                engine.ReleaseCoroutine(id);
                engine.GetActiveIds().ShouldBeEmpty();
            }
        }

        [Fact]
        public void Interrupt_StopsScript()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("while true do end");
            SpinUntilRunning(engine);
            engine.Interrupt();
            engine.Wait();
            engine.IsRunning.ShouldBeFalse();
            engine.GetError(id).ShouldNotBeNull();
            engine.GetError(id)?.ShouldContain("interrupted");
            engine.GetActiveIds().ShouldContain(id);
            engine.ReleaseCoroutine(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Wait_CancelledToken_ThrowsOperationCanceledException()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("while true do end");
            SpinUntilRunning(engine);
            using CancellationTokenSource cts = new(TimeSpan.FromMilliseconds(200));
            try
            {
                Should.Throw<OperationCanceledException>(() => engine.Wait(cts.Token));
            }
            finally
            {
                engine.Interrupt();
                engine.Wait();
                engine.GetActiveIds().ShouldContain(id);
                engine.ReleaseCoroutine(id);
                engine.GetActiveIds().ShouldBeEmpty();
            }
        }

        // -- Concurrency ----------------------------------------------------------

        [Fact]
        public void ConcurrentCoroutines_AllReturnDistinctValues()
        {
            const int count = 32;
            using KitsuneEngine engine = new();

            int[] ids = new int[count];
            for (int i = 0; i < count; i++)
            {
                ids[i] = engine.ExecuteString($"return 'value_{i}'");
                ids[i].ShouldBeGreaterThan(0);
            }

            foreach (int id in ids)
                engine.Wait(id);

            for (int i = 0; i < count; i++)
                engine.GetResultString(ids[i]).ShouldBe($"value_{i}");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ConcurrentCoroutines_ComputationHeavy_AllReturnCorrectSums()
        {
            // Each coroutine loops (i+1)*1000 iterations; the Ticker hook (LUA_MASKCOUNT=1000)
            // yields to other coroutines every 1000 instructions, so these run genuinely interleaved.
            const int count = 10;
            using KitsuneEngine engine = new();

            int[] ids = new int[count];
            for (int i = 0; i < count; i++)
            {
                int n = (i + 1) * 1000;
                ids[i] = engine.ExecuteString($"local s = 0; for j = 1, {n} do s = s + j end; return tostring(s)");
                ids[i].ShouldBeGreaterThan(0);
            }

            foreach (int id in ids)
                engine.Wait(id);

            for (int i = 0; i < count; i++)
            {
                int n = (i + 1) * 1000;
                long expected = (long)n * (n + 1) / 2;
                engine.GetResultString(ids[i]).ShouldBe(expected.ToString());
            }
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ConcurrentCoroutines_MixedOutcomes_EachReportedCorrectly()
        {
            using KitsuneEngine engine = new();

            int successId = engine.ExecuteString("return 'ok'");
            int errorId = engine.ExecuteString("error('fail')");
            int nilId = engine.ExecuteString("return nil");
            int noRetId = engine.ExecuteString("local x = 1 + 1");

            engine.Wait(successId);
            engine.Wait(errorId);
            engine.Wait(nilId);
            engine.Wait(noRetId);

            engine.GetActiveIds().ShouldContain(successId);
            engine.GetActiveIds().ShouldContain(errorId);
            engine.GetActiveIds().ShouldContain(nilId);
            engine.GetActiveIds().ShouldContain(noRetId);
            engine.GetResultString(successId).ShouldBe("ok");
            engine.GetError(errorId).ShouldNotBeNull();
            engine.ReleaseCoroutine(errorId);
            engine.GetResultString(nilId).ShouldBeNull();
            engine.GetResultString(noRetId).ShouldBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ConcurrentCoroutines_WaitedFromParallelTasks_AllComplete()
        {
            // Multiple .NET thread-pool tasks each poll HasResult on their own coroutine ID
            // simultaneously, exercising the thread-safety guarantees of the slot layer.
            const int count = 16;
            using KitsuneEngine engine = new();

            int[] ids = new int[count];
            for (int i = 0; i < count; i++)
                ids[i] = engine.ExecuteString($"return 'parallel_{i}'");

            await Task.WhenAll(ids.Select(id => Task.Run(() => engine.Wait(id))));

            for (int i = 0; i < count; i++)
                engine.GetResultString(ids[i]).ShouldBe($"parallel_{i}");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ConcurrentCoroutines_SuperConcurrency_AllComplete()
        {
            using KitsuneEngine engine = new();

            List<Task> tasks = new List<Task>();

            engine.SetBool("Wait", true);

            while (true)
            {
                try
                {
                    Task t = engine.ExecuteStringAsync($"while Vars.Wait do end return 'parallel_{tasks.Count}'");
                    if (t.Status == TaskStatus.Faulted)
                    {
                        break;
                    }
                    tasks.Add(t);
                }
                catch (Exception)
                {
                    break;
                }
            }

            engine.GetActiveIds().Length.ShouldBeGreaterThan(0);

            engine.SetBool("Wait", false);

            await Task.WhenAll(tasks);

            engine.GetActiveIds().ShouldBeEmpty();

            for (int i = 0; i < tasks.Count; i++)
            {
                string expected = $"parallel_{i}";
                string result = await ((Task<string>)tasks[i]);
                result.ShouldBe(expected);
            }
        }


        // -- GetActiveIds ---------------------------------------------------------

        [Fact]
        public void GetActiveIds_NoCoroutines_ReturnsEmpty()
        {
            using KitsuneEngine engine = new();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetActiveIds_WhileRunning_ContainsId()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("while true do end");
            try
            {
                SpinUntilRunning(engine);
                engine.GetActiveIds().ShouldContain(id);
            }
            finally
            {
                engine.Interrupt();
                engine.Wait();
                engine.ReleaseCoroutine(id);
            }
        }

        [Fact]
        public void GetActiveIds_FinishedButUnreleased_ContainsId()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return 'pending'");
            engine.Wait(id);
            // Result is ready but not yet consumed — slot is still unreleased.
            engine.GetActiveIds().ShouldContain(id);
            engine.GetResultString(id);
            // After consuming the result the slot is released.
            engine.GetActiveIds().ShouldNotContain(id);
        }

        [Fact]
        public void GetActiveIds_MultipleCoroutines_ReturnsAllIds()
        {
            const int count = 8;
            using KitsuneEngine engine = new();
            int[] ids = new int[count];
            for (int i = 0; i < count; i++)
                ids[i] = engine.ExecuteString($"return 'x{i}'");

            foreach (int id in ids)
                engine.Wait(id);

            int[] active = engine.GetActiveIds();
            foreach (int id in ids)
                active.ShouldContain(id);

            // Release all and verify they disappear.
            foreach (int id in ids)
                engine.ReleaseCoroutine(id);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Async execution ------------------------------------------------------

        [Fact]
        public async Task ExecuteStringAsync_ReturnsResult()
        {
            using KitsuneEngine engine = new();
            string? result = await engine.ExecuteStringAsync("return 'async result'");
            result.ShouldBe("async result");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteStringAsync_NilReturn_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            string? result = await engine.ExecuteStringAsync("return nil");
            result.ShouldBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteStringAsync_NoReturn_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            string? result = await engine.ExecuteStringAsync("local x = 1 + 1");
            result.ShouldBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteStringAsync_RuntimeError_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("error('async boom')"));
            ex.Message.ShouldContain("async boom");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteStringAsync_SyntaxError_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("~~~~invalid"));
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFileAsync_ReturnsResult()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "return 'async from file'");
                using KitsuneEngine engine = new();
                string? result = await engine.ExecuteFileAsync(path);
                result.ShouldBe("async from file");
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public async Task ExecuteStringAsync_CancelledToken_ThrowsOperationCanceledException()
        {
            using KitsuneEngine engine = new();
            using CancellationTokenSource cts = new(TimeSpan.FromMilliseconds(100));
            await Should.ThrowAsync<OperationCanceledException>(
                engine.ExecuteStringAsync("while true do end", cts.Token));
            // The coroutine is still running; there is no per-coroutine interrupt, so the
            // slot stays alive until it finishes naturally or the caller cleans up explicitly.
            engine.GetActiveIds().ShouldNotBeEmpty();
            engine.Interrupt();
            engine.Wait();
            foreach (int orphanId in engine.GetActiveIds())
                engine.ReleaseCoroutine(orphanId);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteStringAsync_MultipleConcurrent_AllReturnCorrectResults()
        {
            const int count = 8;
            using KitsuneEngine engine = new();
            Task<string?>[] tasks = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteStringAsync($"return 'async_{i}'"))
                .ToArray();
            string?[] results = await Task.WhenAll(tasks);
            for (int i = 0; i < count; i++)
                results[i].ShouldBe($"async_{i}");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task WaitAsync_CompletesWhenCoroutineFinishes()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return 'waited'");
            await engine.WaitAsync(id);
            engine.GetActiveIds().ShouldContain(id);
            engine.GetResultString(id).ShouldBe("waited");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Sleep ----------------------------------------------------------------

        [Fact]
        public void Sleep_ReturnsResultAfterDelay()
        {
            using KitsuneEngine engine = new();
            var sw = System.Diagnostics.Stopwatch.StartNew();
            int id = engine.ExecuteString("Sleep(50); return 'done'");
            engine.Wait(id);
            sw.Stop();
            engine.GetResultString(id).ShouldBe("done");
            sw.ElapsedMilliseconds.ShouldBeGreaterThanOrEqualTo(40);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Sleep_DoesNotBlockOtherCoroutines()
        {
            using KitsuneEngine engine = new();
            // sleepingId runs first so it is definitely in-flight before fastId is created.
            int sleepingId = engine.ExecuteString("Sleep(2000); return 'slept'");
            SpinUntilRunning(engine); // ensure the sleeping coroutine has started and yielded
            int fastId = engine.ExecuteString("return 'fast'");

            // fastId has no sleep — it must finish well before the 2 s deadline.
            var sw = System.Diagnostics.Stopwatch.StartNew();
            engine.Wait(fastId);
            sw.Stop();

            sw.ElapsedMilliseconds.ShouldBeLessThan(1000,
                "Wait(fastId) took too long — Sleep() may be blocking the scheduler");
            engine.GetResultString(fastId).ShouldBe("fast");

            // Verify the sleeping coroutine is still sleeping.
            engine.HasResult(sleepingId).ShouldBeFalse();

            engine.Wait(sleepingId);
            engine.GetResultString(sleepingId).ShouldBe("slept");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Sleep_MultipleConcurrentSleeps_AllCompleteCorrectly()
        {
            using KitsuneEngine engine = new();
            int id1 = engine.ExecuteString("Sleep(50);  return 'a'");
            int id2 = engine.ExecuteString("Sleep(150); return 'b'");
            int id3 = engine.ExecuteString("Sleep(100); return 'c'");

            engine.Wait(id1);
            engine.Wait(id2);
            engine.Wait(id3);

            engine.GetResultString(id1).ShouldBe("a");
            engine.GetResultString(id2).ShouldBe("b");
            engine.GetResultString(id3).ShouldBe("c");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Sleep_ZeroMs_YieldsAndReturnsImmediately()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("Sleep(0); return 'yielded'");
            engine.Wait(id);
            engine.GetResultString(id).ShouldBe("yielded");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Helpers --------------------------------------------------------------

        private static void SpinUntilRunning(KitsuneEngine engine, int timeoutMs = 2000)
        {
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (!engine.IsRunning && DateTime.UtcNow < deadline)
                Thread.Sleep(1);
        }
    }
}
