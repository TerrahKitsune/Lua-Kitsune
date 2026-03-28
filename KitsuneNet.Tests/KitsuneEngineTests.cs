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

            Task disposeTask = Task.Run(engine.Dispose);
            Task winner = await Task.WhenAny(disposeTask, Task.Delay(TimeSpan.FromSeconds(5)));
            winner.ShouldBe(disposeTask, "Dispose hung — KitsuneCleanup did not interrupt the stuck coroutine");
            await disposeTask;
        }

        // -- ExecuteString (sync) -------------------------------------------------

        [Fact]
        public void ExecuteString_Returns_PositiveId_On_Success()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("local x = 1", fireAndForget: true);
            id.ShouldBeGreaterThan(0);
            engine.Wait();
        }

        [Fact]
        public void ExecuteString_CanWaitAndGetResult()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return 'sync result'");
            engine.Wait(id);
            engine.GetResultString(id).ShouldBe("sync result");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ExecuteString_RuntimeError_CanGetError()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("error('sync boom')");
            engine.Wait(id);
            engine.GetError(id).ShouldNotBeNull();
            engine.GetError(id)!.ShouldContain("sync boom");
            engine.Cancel(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ExecuteString_SyntaxError_CanGetError()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("~~~~invalid");
            id.ShouldBeGreaterThan(0);
            // Load errors set done=1 synchronously; no Wait needed.
            engine.GetError(id).ShouldNotBeNull();
            engine.Cancel(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- GetResult / HasResult ------------------------------------------------

        [Fact]
        public void GetResult_ReturnsRawBytes()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return 'bytes'");
            engine.Wait(id);
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
            engine.GetResultString(id).ShouldBe("consume me");
            engine.GetActiveIds().ShouldBeEmpty();
            engine.GetResultString(id).ShouldBeNull();
        }

        [Fact]
        public void HasResult_ReturnsFalse_WhileRunning_TrueWhenFinished()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return 'done'");
            // Coroutine may or may not have started yet; spin until active.
            SpinUntilHasResult(engine, id);
            engine.HasResult(id).ShouldBeTrue();
            engine.HasResult(id, out nuint len).ShouldBeTrue();
            len.ShouldBe((nuint)4); // "done" is 4 bytes
            engine.Cancel(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- SetString / SetBool / SetNumber / GetVariable -------------------------

        [Fact]
        public async Task SetString_StringValue_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetString("myVar", "hello from csharp");
            string? result = await engine.ExecuteStringAsync("return Vars.myVar");
            result.ShouldBe("hello from csharp");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetString_BytesValue_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetString("bytesVar", Encoding.UTF8.GetBytes("bytes value"));
            string? result = await engine.ExecuteStringAsync("return Vars.bytesVar");
            result.ShouldBe("bytes value");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetBool_True_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetBool("boolVar", true);
            string? result = await engine.ExecuteStringAsync("return tostring(Vars.boolVar)");
            result.ShouldBe("true");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetBool_False_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetBool("boolVar", false);
            string? result = await engine.ExecuteStringAsync("return tostring(Vars.boolVar)");
            result.ShouldBe("false");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetNumber_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetNumber("numVar", 3.14);
            string? result = await engine.ExecuteStringAsync("return string.format('%.2f', Vars.numVar)");
            result.ShouldBe("3.14");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetNumber_Integer_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetNumber("intVar", 42);
            string? result = await engine.ExecuteStringAsync("return tostring(math.tointeger(Vars.intVar))");
            result.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetVariable_ScriptSetGlobal_ReturnsValue()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("Vars.testGlobal = 'get variable test'", fireAndForget: true);
            engine.Wait();
            engine.GetActiveIds().ShouldBeEmpty();
            engine.GetString("testGlobal").ShouldBe("get variable test");
        }

        [Fact]
        public void GetVariable_NonExistent_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            engine.GetString("nonExistentVar_XYZ").ShouldBeNull();
        }

        [Fact]
        public void GetVariableBytes_ScriptSetGlobal_ReturnsBytes()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("Vars.byteVar = 'bytes test'", fireAndForget: true);
            engine.Wait();
            engine.GetActiveIds().ShouldBeEmpty();
            byte[]? bytes = engine.GetStringBytes("byteVar");
            bytes.ShouldNotBeNull();
            Encoding.UTF8.GetString(bytes!).ShouldBe("bytes test");
        }

        [Fact]
        public void GetNumber_ReturnsValue()
        {
            using KitsuneEngine engine = new();
            engine.SetNumber("numVar", 2.5);
            engine.GetNumber("numVar").ShouldBe(2.5);
        }

        [Fact]
        public void GetNumber_NonExistent_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            engine.GetNumber("nonExistentNum_XYZ").ShouldBeNull();
        }

        [Fact]
        public void GetNumber_WrongType_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            engine.SetString("strVar", "not a number");
            engine.GetNumber("strVar").ShouldBeNull();
        }

        [Fact]
        public void GetBool_True_ReturnsTrue()
        {
            using KitsuneEngine engine = new();
            engine.SetBool("boolVar", true);
            engine.GetBool("boolVar").ShouldBe(true);
        }

        [Fact]
        public void GetBool_False_ReturnsFalse()
        {
            using KitsuneEngine engine = new();
            engine.SetBool("boolVar", false);
            engine.GetBool("boolVar").ShouldBe(false);
        }

        [Fact]
        public void GetBool_NonExistent_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            engine.GetBool("nonExistentBool_XYZ").ShouldBeNull();
        }

        [Fact]
        public void GetBool_WrongType_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            engine.SetString("strVar", "not a bool");
            engine.GetBool("strVar").ShouldBeNull();
        }

        [Fact]
        public void GetVariableType_String_ReturnsString()
        {
            using KitsuneEngine engine = new();
            engine.SetString("v", "hello");
            engine.GetVariableType("v").ShouldBe(LuaType.String);
        }

        [Fact]
        public void GetVariableType_Number_ReturnsNumber()
        {
            using KitsuneEngine engine = new();
            engine.SetNumber("v", 42.0);
            engine.GetVariableType("v").ShouldBe(LuaType.Number);
        }

        [Fact]
        public void GetVariableType_Bool_ReturnsBoolean()
        {
            using KitsuneEngine engine = new();
            engine.SetBool("v", true);
            engine.GetVariableType("v").ShouldBe(LuaType.Boolean);
        }

        [Fact]
        public void GetVariableType_Unset_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            engine.GetVariableType("nonExistentVar_XYZ").ShouldBe(LuaType.None);
        }

        // -- ExecuteFile ----------------------------------------------------------

        [Fact]
        public async Task ExecuteFileAsync_ReturnsResult()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "return 'from file'");
                using KitsuneEngine engine = new();
                string? result = await engine.ExecuteFileAsync(path);
                result.ShouldBe("from file");
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public async Task ExecuteFileAsync_RuntimeError_ThrowsLuaException()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "error('file boom')");
                using KitsuneEngine engine = new();
                LuaException ex = await Should.ThrowAsync<LuaException>(
                    engine.ExecuteFileAsync(path));
                ex.Message.ShouldContain("file boom");
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public async Task ExecuteFileAsync_MissingFile_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteFileAsync("nonexistent_file_xyz_99.lua"));
            ex.Message.ShouldNotBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
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
                engine.Cancel(id);
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
            engine.Cancel(id);
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
                engine.Cancel(id);
                engine.GetActiveIds().ShouldBeEmpty();
            }
        }

        // -- Concurrency ----------------------------------------------------------

        [Fact]
        public async Task ConcurrentCoroutines_AllReturnDistinctValues()
        {
            const int count = 32;
            using KitsuneEngine engine = new();
            Task<string?>[] tasks = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteStringAsync($"return 'value_{i}'"))
                .ToArray();
            string?[] results = await Task.WhenAll(tasks);
            for (int i = 0; i < count; i++)
                results[i].ShouldBe($"value_{i}");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ConcurrentCoroutines_ComputationHeavy_AllReturnCorrectSums()
        {
            const int count = 10;
            using KitsuneEngine engine = new();
            Task<string?>[] tasks = Enumerable.Range(0, count)
                .Select(i =>
                {
                    int n = (i + 1) * 1000;
                    return engine.ExecuteStringAsync($"local s = 0; for j = 1, {n} do s = s + j end; return tostring(s)");
                })
                .ToArray();
            string?[] results = await Task.WhenAll(tasks);
            for (int i = 0; i < count; i++)
            {
                int n = (i + 1) * 1000;
                long expected = (long)n * (n + 1) / 2;
                results[i].ShouldBe(expected.ToString());
            }
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ConcurrentCoroutines_MixedOutcomes_EachReportedCorrectly()
        {
            using KitsuneEngine engine = new();
            Task<string?> successTask = engine.ExecuteStringAsync("return 'ok'");
            Task<string?> errorTask   = engine.ExecuteStringAsync("error('fail')");
            Task<string?> nilTask     = engine.ExecuteStringAsync("return nil");
            Task<string?> noRetTask   = engine.ExecuteStringAsync("local x = 1 + 1");
            (await successTask).ShouldBe("ok");
            (await Should.ThrowAsync<LuaException>(errorTask)).Message.ShouldContain("fail");
            (await nilTask).ShouldBeNull();
            (await noRetTask).ShouldBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ConcurrentCoroutines_WaitedFromParallelThreads_AllComplete()
        {
            // Exercises thread-safety of the slot layer: multiple threads poll HasResult
            // simultaneously for distinct coroutine IDs via the sync Wait(id) path.
            const int count = 16;
            using KitsuneEngine engine = new();
            int[] ids = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteString($"return 'parallel_{i}'"))
                .ToArray();
            Task.WhenAll(ids.Select(id => Task.Run(() => engine.Wait(id)))).Wait();
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
                        break;
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
                engine.Cancel(id);
                engine.GetActiveIds().ShouldBeEmpty();
            }
        }

        [Fact]
        public void GetActiveIds_MultipleCoroutines_ReturnsAllIds()
        {
            const int count = 8;
            using KitsuneEngine engine = new();
            for (int i = 0; i < count; i++)
                engine.ExecuteString("Sleep(500)", fireAndForget: true);
            SpinUntilRunning(engine);
            engine.GetActiveIds().Length.ShouldBe(count);
            engine.Wait();
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
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("~~~~invalid"));
            ex.Message.ShouldNotBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteStringAsync_CancelledToken_ThrowsOperationCanceledException()
        {
            using KitsuneEngine engine = new();
            using CancellationTokenSource cts = new(TimeSpan.FromMilliseconds(100));
            await Should.ThrowAsync<OperationCanceledException>(
                engine.ExecuteStringAsync("while true do end", cts.Token));
            // Cancel is called by the handler; wait for the auto-freed slot to clear.
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetActiveIds().Length > 0 && DateTime.UtcNow < deadline)
                Thread.Sleep(1);
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

        // -- Sleep ----------------------------------------------------------------

        [Fact]
        public async Task Sleep_ReturnsResultAfterDelay()
        {
            using KitsuneEngine engine = new();
            var sw = System.Diagnostics.Stopwatch.StartNew();
            string? result = await engine.ExecuteStringAsync("Sleep(50); return 'done'");
            sw.Stop();
            result.ShouldBe("done");
            sw.ElapsedMilliseconds.ShouldBeGreaterThanOrEqualTo(40);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Sleep_DoesNotBlockOtherCoroutines()
        {
            using KitsuneEngine engine = new();
            Task<string?> sleepingTask = engine.ExecuteStringAsync("Sleep(2000); return 'slept'");
            SpinUntilRunning(engine);
            var sw = System.Diagnostics.Stopwatch.StartNew();
            string? fastResult = await engine.ExecuteStringAsync("return 'fast'");
            sw.Stop();
            sw.ElapsedMilliseconds.ShouldBeLessThan(1000,
                "Fast coroutine took too long — Sleep() may be blocking the scheduler");
            fastResult.ShouldBe("fast");
            sleepingTask.IsCompleted.ShouldBeFalse();
            (await sleepingTask).ShouldBe("slept");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Sleep_MultipleConcurrentSleeps_AllCompleteCorrectly()
        {
            using KitsuneEngine engine = new();
            Task<string?> t1 = engine.ExecuteStringAsync("Sleep(50);  return 'a'");
            Task<string?> t2 = engine.ExecuteStringAsync("Sleep(150); return 'b'");
            Task<string?> t3 = engine.ExecuteStringAsync("Sleep(100); return 'c'");
            string?[] results = await Task.WhenAll(t1, t2, t3);
            results[0].ShouldBe("a");
            results[1].ShouldBe("b");
            results[2].ShouldBe("c");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Sleep_ZeroMs_YieldsAndReturnsImmediately()
        {
            using KitsuneEngine engine = new();
            string? result = await engine.ExecuteStringAsync("Sleep(0); return 'yielded'");
            result.ShouldBe("yielded");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- ExecuteFunction ------------------------------------------------------

        [Fact]
        public async Task ExecuteFunction_NoArgs_ReturnsResult()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function greet() return 'hello' end", fireAndForget: true);
            engine.Wait();
            string? result = await engine.ExecuteFunctionAsync("greet");
            result.ShouldBe("hello");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_WithArgs_ReceivesArguments()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function echo(a, b) return a .. ',' .. b end", fireAndForget: true);
            engine.Wait();
            string? result = await engine.ExecuteFunctionAsync("echo", args: ["foo", "bar"]);
            result.ShouldBe("foo,bar");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_DoesNotSetArgsGlobal()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function checkargs(x) return tostring(ARGS) end", fireAndForget: true);
            engine.Wait();
            engine.ExecuteString("ARGS = nil", fireAndForget: true);
            engine.Wait();
            string? result = await engine.ExecuteFunctionAsync("checkargs", args: ["ignored"]);
            result.ShouldBe("nil");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_NotFound_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteFunctionAsync("nonExistentFunction_XYZ"));
            ex.Message.ShouldNotBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_RuntimeError_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function boom() error('fn error') end", fireAndForget: true);
            engine.Wait();
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteFunctionAsync("boom"));
            ex.Message.ShouldContain("fn error");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunctionAsync_ReturnsResult()
        {
            using KitsuneEngine engine = new();
            await engine.ExecuteStringAsync("function add(a, b) return tostring(tonumber(a) + tonumber(b)) end");
            string? result = await engine.ExecuteFunctionAsync("add", args: ["3", "4"]);
            result.ShouldBe("7");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Cancel ---------------------------------------------------------------

        [Fact]
        public void Cancel_RunningCoroutine_SetsErrorAndFreesSlot()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("while true do end");
            SpinUntilRunning(engine);
            engine.Cancel(id);
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetActiveIds().Contains(id) && DateTime.UtcNow < deadline)
                Thread.Sleep(1);
            engine.GetActiveIds().ShouldNotContain(id);
        }

        [Fact]
        public void Cancel_SleepingCoroutine_FreesSlotWithoutWaitingForDeadline()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("Sleep(10000); return 'never'");
            SpinUntilRunning(engine);
            engine.Cancel(id);
            var sw = System.Diagnostics.Stopwatch.StartNew();
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetActiveIds().Contains(id) && DateTime.UtcNow < deadline)
                Thread.Sleep(1);
            sw.Stop();
            sw.ElapsedMilliseconds.ShouldBeLessThan(5000,
                "Cancel did not free the sleeping coroutine before its 10 s deadline");
            engine.GetActiveIds().ShouldNotContain(id);
        }

        [Fact]
        public async Task Cancel_DoesNotAffectOtherCoroutines()
        {
            using KitsuneEngine engine = new();
            int cancelId = engine.ExecuteString("while true do end");
            SpinUntilRunning(engine);
            Task<string?> keepTask = engine.ExecuteStringAsync("Sleep(100); return 'kept'");
            engine.Cancel(cancelId);
            string? kept = await keepTask;
            kept.ShouldBe("kept");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Helpers --------------------------------------------------------------

        private static void SpinUntilRunning(KitsuneEngine engine, int timeoutMs = 2000)
        {
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (!engine.IsRunning && DateTime.UtcNow < deadline)
                Thread.Sleep(1);
        }

        private static void SpinUntilHasResult(KitsuneEngine engine, int id, int timeoutMs = 2000)
        {
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (!engine.HasResult(id) && DateTime.UtcNow < deadline)
                Thread.Sleep(1);
        }
    }
}
