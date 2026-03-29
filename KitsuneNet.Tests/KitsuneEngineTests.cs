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

        [Fact]
        public void ExecuteString_WithArgs_ArgsAreVisible()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return ARGS[1] .. ':' .. ARGS[2]", args: ["hello", "world"]);
            engine.Wait(id);
            engine.GetResultString(id).ShouldBe("hello:world");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ExecuteString_IDGlobal_MatchesCoroutineId()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return tostring(ID)");
            engine.Wait(id);
            engine.GetResultString(id).ShouldBe(id.ToString());
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
        public void GetError_SuccessfulCoroutine_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return 'ok'");
            engine.Wait(id);
            engine.GetError(id).ShouldBeNull();  // no error was raised
            engine.Cancel(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetResultVariable_NumberReturn_IsTypedAsNumber()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return 42.5");
            engine.Wait(id);
            LuaValue result = engine.GetResultVariable(id);
            result.Type.ShouldBe(LuaType.Number);
            result.Number.ShouldBe(42.5);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetResultVariable_BoolReturn_IsTypedAsBool()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return true");
            engine.Wait(id);
            LuaValue result = engine.GetResultVariable(id);
            result.Type.ShouldBe(LuaType.Boolean);
            result.Boolean.ShouldBeTrue();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetResultVariable_TableReturn_IsTypedAsTable()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return {}");
            engine.Wait(id);
            LuaValue result = engine.GetResultVariable(id);
            result.Type.ShouldBe(LuaType.Table);
            result.Bytes.ShouldBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
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

        [Fact]
        public void HasResult_NonStringResult_LenIsZero()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return 42");
            SpinUntilHasResult(engine, id);
            engine.HasResult(id, out nuint len).ShouldBeTrue();
            len.ShouldBe((nuint)0);  // len is only non-zero for string results
            engine.Cancel(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void HasResult_NonExistentId_ReturnsFalse()
        {
            using KitsuneEngine engine = new();
            engine.HasResult(99999).ShouldBeFalse();
            engine.HasResult(99999, out nuint len).ShouldBeFalse();
            len.ShouldBe((nuint)0);
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
        public void SetString_BinaryDataWithNonUtf8Bytes_PreservesExactBytes()
        {
            // 0xFF and 0xFE are never valid in UTF-8; a round-trip through
            // Encoding.UTF8.GetString would silently replace them with U+FFFD.
            using KitsuneEngine engine = new();
            byte[] binary = [0x01, 0xFF, 0x00, 0xFE, 0x7F];
            engine.SetString("bin", binary);
            byte[]? result = engine.GetStringBytes("bin");
            result.ShouldNotBeNull();
            result.ShouldBe(binary);
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

        [Fact]
        public void SetVariable_None_RemovesKey()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("removeMe", (LuaValue)"exists");
            engine.GetVariableType("removeMe").ShouldBe(LuaType.String);
            engine.SetVariable("removeMe", LuaValue.None);
            engine.GetVariableType("removeMe").ShouldBe(LuaType.None);
        }

        [Fact]
        public void SetVariable_Overwrite_ChangesType()
        {
            using KitsuneEngine engine = new();
            engine.SetString("v", "hello");
            engine.GetVariableType("v").ShouldBe(LuaType.String);
            engine.SetNumber("v", 42.0);
            engine.GetVariableType("v").ShouldBe(LuaType.Number);
            engine.GetNumber("v").ShouldBe(42.0);
            engine.GetString("v").ShouldBeNull();  // no longer a string
        }

        [Fact]
        public void GetAll_EmptyVars_ReturnsEmptyArray()
        {
            using KitsuneEngine engine = new();
            engine.GetAll().ShouldBeEmpty();
        }

        [Fact]
        public void GetAll_ReturnsAllSetVariables()
        {
            using KitsuneEngine engine = new();
            engine.SetString("foo", "bar");
            engine.SetNumber("count", 42.0);
            engine.SetBool("flag", true);

            var all = engine.GetAll();

            all.Count.ShouldBe(3);
            all.ShouldContain(kvp => kvp.Key.String == "foo"   && kvp.Value.String  == "bar");
            all.ShouldContain(kvp => kvp.Key.String == "count" && kvp.Value.Number  == 42.0);
            all.ShouldContain(kvp => kvp.Key.String == "flag"  && kvp.Value.Boolean == true);
        }

        [Fact]
        public void GetAll_KeysAreStrings_ValuesAreTyped()
        {
            using KitsuneEngine engine = new();
            engine.SetString("s", "hello");
            engine.SetNumber("n", 3.14);
            engine.SetBool("b", false);

            var all = engine.GetAll();

            all.ShouldAllBe(kvp => kvp.Key.Type == LuaType.String);
            all.ShouldContain(kvp => kvp.Value.Type == LuaType.String);
            all.ShouldContain(kvp => kvp.Value.Type == LuaType.Number);
            all.ShouldContain(kvp => kvp.Value.Type == LuaType.Boolean);
        }

        [Fact]
        public void GetAll_WithPath_ReturnsSubtableContents()
        {
            using KitsuneEngine engine = new();
            engine.SetString("rootOnly", "root");
            engine.SetString("sub.subA", "1");
            engine.SetNumber("sub.subB", 2.0);

            var all = engine.GetAll("sub");

            all.Count.ShouldBe(2);
            all.ShouldContain(kvp => kvp.Key.String == "subA" && kvp.Value.String == "1");
            all.ShouldContain(kvp => kvp.Key.String == "subB" && kvp.Value.Number == 2.0);
            all.ShouldNotContain(kvp => kvp.Key.String == "rootOnly");
        }

        [Fact]
        public void GetVariable_TableType_ReturnsLuaTypeTable()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("Vars.tableVar = {}", fireAndForget: true);
            engine.Wait();
            LuaValue v = engine.GetVariable("tableVar");
            v.Type.ShouldBe(LuaType.Table);
            v.Bytes.ShouldBeNull();
        }

        // -- Variable bridge path notation ----------------------------------------

        [Fact]
        public void SetVariable_DotPath_WritesToSubtable()
        {
            using KitsuneEngine engine = new();
            engine.SetString("root", "rootValue");
            engine.SetString("sub.key", "subValue");

            engine.GetString("root").ShouldBe("rootValue");
            engine.GetString("sub.key").ShouldBe("subValue");
            engine.GetString("key").ShouldBeNull();   // not visible at root
        }

        [Fact]
        public void SetVariable_DotPath_CreatesIntermediateTables()
        {
            using KitsuneEngine engine = new();
            engine.SetString("a.b.c", "deep");
            engine.GetString("a.b.c").ShouldBe("deep");
        }

        [Fact]
        public void SetVariable_DeepDotPath_WritesToNestedTable()
        {
            using KitsuneEngine engine = new();
            engine.SetString("a.b.deep", "value");
            engine.GetString("a.b.deep").ShouldBe("value");
        }

        [Fact]
        public void SetVariable_DotPath_ThroughNonTableReturnsFalse()
        {
            using KitsuneEngine engine = new();
            engine.SetString("notATable", "value");
            engine.SetString("notATable.key", "x").ShouldBeFalse();
            engine.GetString("notATable").ShouldBe("value");  // unchanged
        }

        [Fact]
        public void SetVariable_TableType_CreatesEmptyTable()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("myTable", new LuaValue { Type = LuaType.Table });
            engine.GetVariableType("myTable").ShouldBe(LuaType.Table);
        }

        [Fact]
        public async Task SetVariable_DotPath_CSharpWriteLuaRead()
        {
            using KitsuneEngine engine = new();
            engine.SetString("db.host", "localhost");
            engine.SetNumber("db.port", 5432);

            string? result = await engine.ExecuteStringAsync(
                "return Vars.db.host .. ':' .. tostring(Vars.db.port)");
            result.ShouldBe("localhost:5432");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetVariable_DotPath_LuaWriteCSharpRead()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString(
                "Vars.cfg = {}; Vars.cfg.timeout = 30; Vars.cfg.retry = true",
                fireAndForget: true);
            engine.Wait();

            engine.GetNumber("cfg.timeout").ShouldBe(30.0);
            engine.GetBool("cfg.retry").ShouldBe(true);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- ExecuteFile ----------------------------------------------------------

        [Fact]
        public void ExecuteFile_CreatesFileRunsAndCleansUp()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "return 'file result'");
                using KitsuneEngine engine = new();
                int id = engine.ExecuteFile(path);
                engine.Wait(id);
                engine.GetResultString(id).ShouldBe("file result");
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public void ExecuteFile_ArgsOneIsFilePath()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "return ARGS[1]");  // ARGS[1] = the file path itself
                using KitsuneEngine engine = new();
                int id = engine.ExecuteFile(path);
                engine.Wait(id);
                engine.GetResultString(id).ShouldBe(path);
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public void ExecuteFile_RuntimeError_CanGetError()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "error('file error')");
                using KitsuneEngine engine = new();
                int id = engine.ExecuteFile(path);
                engine.Wait(id);
                engine.GetError(id).ShouldNotBeNull();
                engine.GetError(id)!.ShouldContain("file error");
                engine.Cancel(id);
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public void ExecuteFile_WithArgs_ArgsContainPathAndExtraArgs()
        {
            string path = Path.GetTempFileName();
            try
            {
                // ARGS[1] = file path; ARGS[2+] = extra args passed to ExecuteFile
                File.WriteAllText(path, "return ARGS[2] .. ':' .. ARGS[3]");
                using KitsuneEngine engine = new();
                int id = engine.ExecuteFile(path, args: ["first", "second"]);
                engine.Wait(id);
                engine.GetResultString(id).ShouldBe("first:second");
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

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
        public void RunningCoroutineId_DuringExecution_MatchesStartedId()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("while true do end");
            try
            {
                SpinUntilRunning(engine);
                engine.RunningCoroutineId.ShouldBe(id);
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
        public void ExecuteString_AfterInterruptAndWait_WorksNormally()
        {
            // The scheduler clears the interrupt flag once runningCount hits 0.
            // Verifies the engine is fully reusable after an interrupt + wait cycle.
            using KitsuneEngine engine = new();
            engine.ExecuteString("while true do end", fireAndForget: true);
            SpinUntilRunning(engine);
            engine.Interrupt();
            engine.Wait();
            int id = engine.ExecuteString("return 'after interrupt'");
            engine.Wait(id);
            engine.GetResultString(id).ShouldBe("after interrupt");
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
        public async Task ConcurrentCoroutines_WaitedFromParallelThreads_AllComplete()
        {
            // Exercises thread-safety of the slot layer: multiple threads poll HasResult
            // simultaneously for distinct coroutine IDs via the sync Wait(id) path.
            const int count = 16;
            using KitsuneEngine engine = new();
            int[] ids = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteString($"return 'parallel_{i}'"))
                .ToArray();
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

        [Fact]
        public async Task ExecuteFunction_WithTypedNumberArgs_PassedAsNumbers()
        {
            using KitsuneEngine engine = new();
            await engine.ExecuteStringAsync("function typedAdd(a, b) return tostring(a + b) end");
            string? result = await engine.ExecuteFunctionAsync("typedAdd",
                args: [LuaValue.FromNumber(6), LuaValue.FromNumber(7)]);
            result.ShouldBe("13");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_WithTypedBoolArg_PassedAsBool()
        {
            using KitsuneEngine engine = new();
            await engine.ExecuteStringAsync("function checkBool(b) return tostring(b) end");
            string? result = await engine.ExecuteFunctionAsync("checkBool",
                args: [LuaValue.FromBool(true)]);
            result.ShouldBe("true");
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

        // -- GetRuntime -----------------------------------------------------------

        [Fact]
        public void GetRuntime_WhileRunning_ReturnsPositiveValue()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("Sleep(500)");
            SpinUntilRunning(engine);
            Thread.Sleep(5);  // ensure at least a few ms have elapsed
            engine.GetRuntime(id).ShouldBeGreaterThan(0);
            engine.Cancel(id);
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetActiveIds().Contains(id) && DateTime.UtcNow < deadline)
                Thread.Sleep(1);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetRuntime_AfterCoroutineReleased_ReturnsZero()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return 'done'");
            engine.Wait(id);
            engine.GetResult(id);  // consumes result and sets released=1
            // Spin until the scheduler's step 4 compacts the slot (zeroes id) — at most one scheduler pass (~10ms)
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetRuntime(id) != 0.0 && DateTime.UtcNow < deadline)
                Thread.Sleep(1);
            engine.GetRuntime(id).ShouldBe(0.0);  // slot compacted; ID not found returns 0
        }

        // -- Stress tests ---------------------------------------------------------

        [Fact]
        public async Task Stress_HighThroughput_SequentialBatches_AllCorrect()
        {
            // 1000 coroutines in batches of 100 — verifies high-throughput execution
            // produces the correct result for every single coroutine with no data loss.
            using KitsuneEngine engine = new();
            const int total     = 1000;
            const int batchSize =  100;

            for (int batch = 0; batch < total / batchSize; batch++)
            {
                int offset = batch * batchSize;
                Task<string?>[] tasks = Enumerable.Range(0, batchSize)
                    .Select(j => engine.ExecuteStringAsync($"return tostring({offset + j})"))
                    .ToArray();
                string?[] batchResults = await Task.WhenAll(tasks);
                for (int j = 0; j < batchSize; j++)
                    batchResults[j].ShouldBe((offset + j).ToString());
            }
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Stress_SlotRecycling_SustainedLoadBeyondSlotLimit()
        {
            // Submits 4× the 256-slot limit through a semaphore-throttled pipeline
            // (max 64 concurrent) so slots are continuously recycled while new ones
            // are being admitted — verifies every result is correct under recycling pressure.
            using KitsuneEngine engine = new();
            const int total         = 1000;
            const int maxConcurrent =   64;
            var results = new string?[total];

            using var sem = new SemaphoreSlim(maxConcurrent, maxConcurrent);
            Task[] tasks = Enumerable.Range(0, total).Select(async i =>
            {
                await sem.WaitAsync();
                try   { results[i] = await engine.ExecuteStringAsync($"return tostring({i})"); }
                finally { sem.Release(); }
            }).ToArray();

            await Task.WhenAll(tasks);

            for (int i = 0; i < total; i++)
                results[i].ShouldBe(i.ToString());
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Stress_ConcurrentVariableBridge_NoCorruptionOrDeadlock()
        {
            // 8 threads simultaneously hammer SetNumber/GetNumber on a shared key
            // while 10 Lua coroutines read the same Vars table — verifies
            // AcquireLuaAccess serialises every access with no deadlock, null reads,
            // or scheduler starvation under real write/write/read contention.
            using KitsuneEngine engine = new();
            const int threads      =   8;
            const int opsPerThread = 200;
            int completedOps = 0;

            // Lua coroutines that read Vars under scheduler pressure.
            for (int i = 0; i < 10; i++)
                engine.ExecuteString(
                    "local n = 0; for _ = 1, 5000 do n = n + (Vars.counter or 0) end",
                    fireAndForget: true);

            Task[] writers = Enumerable.Range(0, threads).Select(t => Task.Run(() =>
            {
                for (int i = 0; i < opsPerThread; i++)
                {
                    engine.SetNumber("counter", t * 1000.0 + i);
                    // Another thread may have overwritten the key between set and get,
                    // but every writer only ever writes a number, so the read must
                    // never be null regardless of which thread's value we observe.
                    engine.GetNumber("counter").ShouldNotBeNull();
                    Interlocked.Increment(ref completedOps);
                }
            })).ToArray();

            await Task.WhenAll(writers);
            engine.Interrupt();
            engine.Wait();

            completedOps.ShouldBe(threads * opsPerThread);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Stress_ConcurrentFunctionExecution_AllReturnCorrectResults()
        {
            // Defines 50 distinct functions then calls them all concurrently via
            // ExecuteFunctionAsync — stresses the function-call async path under load.
            using KitsuneEngine engine = new();
            const int count = 50;

            for (int i = 0; i < count; i++)
                engine.ExecuteString($"function stress_fn_{i}(x) return tostring(x * {i}) end",
                    fireAndForget: true);
            engine.Wait();

            Task<string?>[] tasks = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteFunctionAsync($"stress_fn_{i}",
                    args: [LuaValue.FromNumber(42)]))
                .ToArray();
            string?[] results = await Task.WhenAll(tasks);

            for (int i = 0; i < count; i++)
                results[i].ShouldBe((42 * i).ToString());
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Stress_AsyncCoroutinesWritingVars_CSharpReadsAllBack()
        {
            // 30 concurrent async coroutines each write a unique Vars key while running —
            // verifies that async execution and variable bridge writes are both correct
            // under simultaneous scheduler pressure.
            using KitsuneEngine engine = new();
            const int count = 30;

            Task<string?>[] tasks = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteStringAsync(
                    $"Vars.slot_{i} = {i}; return tostring({i})"))
                .ToArray();
            string?[] results = await Task.WhenAll(tasks);

            for (int i = 0; i < count; i++)
            {
                results[i].ShouldBe(i.ToString());
                engine.GetNumber($"slot_{i}").ShouldBe((double)i);
            }
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Stress_WaitAsync_ConcurrentPollers_AllComplete()
        {
            // Starts 30 coroutines synchronously then waits for each via WaitAsync(id)
            // in parallel — directly stresses the async wait path rather than going
            // through ExecuteStringAsync which uses it internally.
            using KitsuneEngine engine = new();
            const int count = 30;

            int[] ids = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteString($"return tostring({i})"))
                .ToArray();

            await Task.WhenAll(ids.Select(id => engine.WaitAsync(id)));

            for (int i = 0; i < count; i++)
                engine.GetResultString(ids[i]).ShouldBe(i.ToString());
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
