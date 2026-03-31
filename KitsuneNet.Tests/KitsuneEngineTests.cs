using KitsuneNet;
using Shouldly;
using System.Text;
using Xunit;

namespace KitsuneNet.Tests
{
    // KitsuneEngine.dll uses a process-wide global state: all KitsuneEngine instances
    // in the same process share the same native scheduler and Lua state. Both test
    // classes must therefore run sequentially rather than in parallel.
    [Collection("KitsuneSequential")]
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
            engine.ReleaseResult(id);
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
            engine.ReleaseResult(id);
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
            engine.ReleaseResult(id);
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
            engine.ReleaseResult(id);
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
            engine.ReleaseResult(id);
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

        [Fact]
        public void HasResult_AfterResultConsumed_ReturnsFalse()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return 'done'");
            engine.Wait(id);
            engine.HasResult(id).ShouldBeTrue();
            engine.GetResultVariable(id);  // consumes + sets released=1
            // Spin until the scheduler compacts the slot on its next pass.
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.HasResult(id) && DateTime.UtcNow < deadline)
                Thread.Sleep(1);
            engine.HasResult(id).ShouldBeFalse();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- SetString / SetBool / SetNumber / GetVariable -------------------------

        [Fact]
        public async Task SetString_StringValue_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetString("myVar", "hello from csharp");
            string? result = await engine.ExecuteStringAsync("return myVar");
            result.ShouldBe("hello from csharp");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetString_BytesValue_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetString("bytesVar", Encoding.UTF8.GetBytes("bytes value"));
            string? result = await engine.ExecuteStringAsync("return bytesVar");
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
            string? result = await engine.ExecuteStringAsync("return tostring(boolVar)");
            result.ShouldBe("true");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetBool_False_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetBool("boolVar", false);
            string? result = await engine.ExecuteStringAsync("return tostring(boolVar)");
            result.ShouldBe("false");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetNumber_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetNumber("numVar", 3.14);
            string? result = await engine.ExecuteStringAsync("return string.format('%.2f', numVar)");
            result.ShouldBe("3.14");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetNumber_Integer_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetNumber("intVar", 42);
            string? result = await engine.ExecuteStringAsync("return tostring(math.tointeger(intVar))");
            result.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetVariable_ScriptSetGlobal_ReturnsValue()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("testGlobal = 'get variable test'", fireAndForget: true);
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
            engine.ExecuteString("byteVar = 'bytes test'", fireAndForget: true);
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
        public void GetNumber_FromIntegerType_ReturnsAsDouble()
        {
            // GetNumber bridges both float (LuaType.Number) and integer (LuaType.Integer) for convenience.
            using KitsuneEngine engine = new();
            engine.SetInt64("n", 7);
            engine.GetNumber("n").ShouldBe(7.0);
        }

        [Fact]
        public void GetNumber_NonExistent_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            engine.GetNumber("nonExistentNum_XYZ").ShouldBeNull();
        }

        [Fact]
        public void SetInt64_GetInt64_RoundTrip()
        {
            using KitsuneEngine engine = new();
            engine.SetInt64("n", 1234567890123L);
            engine.GetInt64("n").ShouldBe(1234567890123L);
        }

        [Fact]
        public void GetInt64_FromFloatType_ReturnsAsLong()
        {
            // GetInt64 bridges both integer and float types for convenience.
            using KitsuneEngine engine = new();
            engine.SetNumber("n", 42.0);
            engine.GetInt64("n").ShouldBe(42L);
        }

        [Fact]
        public void GetVariableType_Integer_ReturnsInteger()
        {
            using KitsuneEngine engine = new();
            engine.SetInt64("v", 99);
            engine.GetVariableType("v").ShouldBe(LuaType.Integer);
        }

        [Fact]
        public async Task SetInt64_IsVisibleInScriptAsInteger()
        {
            using KitsuneEngine engine = new();
            engine.SetInt64("n", 42);
            // Lua sees a proper integer (math.type returns "integer", not "float").
            string? result = await engine.ExecuteStringAsync("return math.type(n)");
            result.ShouldBe("integer");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetInt64_LuaIntegerAssignment_ReturnsIntegerType()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("n = 42", fireAndForget: true);
            engine.Wait();
            engine.GetVariableType("n").ShouldBe(LuaType.Integer);
            engine.GetInt64("n").ShouldBe(42L);
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
        public void GetAll_EmptySubtable_ReturnsEmpty()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("empty", new LuaValue { Type = LuaType.Table });
            engine.GetAll("empty").ShouldBeEmpty();
        }

        [Fact]
        public void GetAll_ReturnsAllSetVariables()
        {
            using KitsuneEngine engine = new();
            engine.SetString("data.foo", "bar");
            engine.SetNumber("data.count", 42.0);
            engine.SetBool("data.flag", true);

            var all = engine.GetAll("data");

            all.Count.ShouldBe(3);
            all.ShouldContain(kvp => kvp.Key.String == "foo"   && kvp.Value.String  == "bar");
            all.ShouldContain(kvp => kvp.Key.String == "count" && kvp.Value.Number  == 42.0);
            all.ShouldContain(kvp => kvp.Key.String == "flag"  && kvp.Value.Boolean == true);
        }

        [Fact]
        public void GetAll_KeysAreStrings_ValuesAreTyped()
        {
            using KitsuneEngine engine = new();
            engine.SetString("data.s", "hello");
            engine.SetNumber("data.n", 3.14);
            engine.SetBool("data.b", false);

            var all = engine.GetAll("data");

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
        public void GetAll_EmptyStringPath_IteratesGlobalEnv()
        {
            using KitsuneEngine engine = new();
            engine.SetString("testIterRoot_xyz", "hello");
            var all = engine.GetAll("");  // "" iterates _G itself
            all.ShouldContain(kvp => kvp.Key.String == "testIterRoot_xyz" && kvp.Value.String == "hello");
        }

        [Fact]
        public void GetAll_NonExistentPath_ReturnsEmpty()
        {
            using KitsuneEngine engine = new();
            engine.SetString("other", "value");
            engine.GetAll("doesNotExist").ShouldBeEmpty();
        }

        [Fact]
        public void GetAll_PathTargetsNonTable_ReturnsEmpty()
        {
            using KitsuneEngine engine = new();
            engine.SetString("x", "a string not a table");
            engine.GetAll("x").ShouldBeEmpty();
        }

        [Fact]
        public void GetVariable_TableType_ReturnsLuaTypeTable()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("tableVar = {}", fireAndForget: true);
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
        public void GetVariable_DotPath_FinalKeyAbsent_ReturnsNone()
        {
            using KitsuneEngine engine = new();
            engine.SetString("tbl.existing", "value");
            engine.GetVariable("tbl.nonexistent").Type.ShouldBe(LuaType.None);
            engine.GetString("tbl.nonexistent").ShouldBeNull();
        }

        [Fact]
        public void SetVariable_TableType_CreatesEmptyTable()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("myTable", new LuaValue { Type = LuaType.Table });
            engine.GetVariableType("myTable").ShouldBe(LuaType.Table);
        }

        [Fact]
        public async Task SetVariable_TableType_AtNestedPath()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("parent.child", new LuaValue { Type = LuaType.Table });
            engine.GetVariableType("parent.child").ShouldBe(LuaType.Table);
            string? result = await engine.ExecuteStringAsync(
                "parent.child.x = 'yes'; return parent.child.x");
            result.ShouldBe("yes");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetVariable_DotPath_CSharpWriteLuaRead()
        {
            using KitsuneEngine engine = new();
            engine.SetString("db.host", "localhost");
            engine.SetInt64("db.port", 5432);

            string? result = await engine.ExecuteStringAsync(
                "return db.host .. ':' .. tostring(db.port)");
            result.ShouldBe("localhost:5432");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetVariable_DotPath_LuaWriteCSharpRead()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString(
                "cfg = {}; cfg.timeout = 30; cfg.retry = true",
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
                engine.ReleaseResult(id);
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
                engine.ReleaseResult(id);
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
                engine.ReleaseResult(id);
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
            engine.ReleaseResult(id);
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
                engine.ReleaseResult(id);
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
            engine.SetBool("ShouldWait", true);
            while (true)
            {
                try
                {
                    Task t = engine.ExecuteStringAsync($"while ShouldWait do end return 'parallel_{tasks.Count}'");
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
            engine.SetBool("ShouldWait", false);
            await Task.WhenAll(tasks);
            engine.GetActiveIds().ShouldBeEmpty();
            for (int i = 0; i < tasks.Count; i++)
            {
                string expected = $"parallel_{i}";
                string? result = await (Task<string?>)tasks[i];
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
                engine.ReleaseResult(id);
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
                args: [LuaValue.FromInt64(6), LuaValue.FromInt64(7)]);
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

        // -- Dot-path notation for ExecuteFunction and RegisterFunction -----------

        [Fact]
        public async Task ExecuteFunction_DotPath_CallsNestedFunction()
        {
            // ExecuteFunction("Ns.Foo") should find _G.Ns.Foo, not a global named "Ns.Foo".
            using KitsuneEngine engine = new();
            await engine.ExecuteStringAsync("Ns = {}; function Ns.greet() return 'hi' end");
            string? result = await engine.ExecuteFunctionAsync("Ns.greet");
            result.ShouldBe("hi");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_DotPath_WithArgs_PassedCorrectly()
        {
            using KitsuneEngine engine = new();
            await engine.ExecuteStringAsync("Math = {}; function Math.add(a, b) return tostring(a + b) end");
            string? result = await engine.ExecuteFunctionAsync("Math.add",
                args: [LuaValue.FromInt64(10), LuaValue.FromInt64(32)]);
            result.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_DeepDotPath_CallsFunction()
        {
            using KitsuneEngine engine = new();
            await engine.ExecuteStringAsync("A = {}; A.B = {}; function A.B.fn() return 'deep' end");
            string? result = await engine.ExecuteFunctionAsync("A.B.fn");
            result.ShouldBe("deep");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_DotPath_NotFound_ThrowsLuaException()
        {
            // Intermediate table exists but the function key is absent.
            using KitsuneEngine engine = new();
            await engine.ExecuteStringAsync("Ns = {}");
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteFunctionAsync("Ns.missingFn"));
            ex.Message.ShouldNotBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_DotPath_IntermediateTableMissing_ThrowsLuaException()
        {
            // Navigating through a non-existent intermediate table should report "function not found".
            using KitsuneEngine engine = new();
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteFunctionAsync("NoSuchNs.fn"));
            ex.Message.ShouldNotBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_DotPath_LuaCanCallIt()
        {
            // RegisterFunction("Ns.Foo") should create the intermediate table and register the function.
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Kitsune.Multiply", args =>
                LuaValue.FromInt64(args[0].AsInt64 * args[1].AsInt64));
            string? result = await engine.ExecuteStringAsync(
                "return tostring(Kitsune.Multiply(6, 7))");
            result.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_DotPath_ExecuteFunction_CallsIt()
        {
            // A function registered at a dot-path should also be callable via ExecuteFunction.
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Util.Double", args =>
                (LuaValue)$"{args.First().AsInt64 * 2}");
            string? result = await engine.ExecuteFunctionAsync("Util.Double",
                args: [LuaValue.FromInt64(21)]);
            result.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_DeepDotPath_LuaCanCallIt()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("A.B.C.fn", _ => (LuaValue)"deep");
            string? result = await engine.ExecuteStringAsync("return A.B.C.fn()");
            result.ShouldBe("deep");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task DotPath_SetVariable_GetVariable_And_ExecuteFunction_ConsistentNamespace()
        {
            // SetVariable, RegisterFunction, and ExecuteFunction all share the same _G namespace.
            using KitsuneEngine engine = new();
            engine.SetString("Config.name", "kitsune");
            // Capture the value outside the callback to stay within the constraint that
            // GetVariable must not be called from within a registered function.
            string capturedName = engine.GetString("Config.name") ?? "";
            engine.RegisterFunction("Config.getName", _ => (LuaValue)capturedName);
            string? result = await engine.ExecuteFunctionAsync("Config.getName");
            result.ShouldBe("kitsune");
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

        // -- GetStatus ------------------------------------------------------------

        [Fact]
        public void GetStatus_NonExistentId_ReturnsNone()
        {
            using KitsuneEngine engine = new();
            engine.GetStatus(99999).ShouldBe(CoroutineStatus.None);
        }

        [Fact]
        public void GetStatus_TwoConcurrentCoroutines_OneIsIdle()
        {
            // The Ticker yields every 1000 instructions when runningCount > 1,
            // so at any moment one coroutine is in lua_resume and the other is Idle.
            using KitsuneEngine engine = new();
            int idA = engine.ExecuteString("while true do end");
            int idB = engine.ExecuteString("while true do end");
            try
            {
                DateTime deadline = DateTime.UtcNow.AddSeconds(5);
                bool sawIdle = false;
                while (!sawIdle && DateTime.UtcNow < deadline)
                    sawIdle = engine.GetStatus(idA) == CoroutineStatus.Idle
                           || engine.GetStatus(idB) == CoroutineStatus.Idle;
                sawIdle.ShouldBeTrue();
            }
            finally
            {
                engine.Cancel(idA);
                engine.Cancel(idB);
                engine.Wait();
            }
        }

        [Fact]
        public void GetStatus_DuringSleep_ReturnsSleeping()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("Sleep(60000)");
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            CoroutineStatus status;
            do status = engine.GetStatus(id);
            while (status != CoroutineStatus.Sleeping && DateTime.UtcNow < deadline);
            status.ShouldBe(CoroutineStatus.Sleeping);
            engine.Cancel(id);
            engine.Wait();
        }

        [Fact]
        public void GetStatus_WhileRunning_ReturnsRunning()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("while true do end");
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            CoroutineStatus status;
            do status = engine.GetStatus(id);
            while (status != CoroutineStatus.Running && DateTime.UtcNow < deadline);
            status.ShouldBe(CoroutineStatus.Running);
            engine.Cancel(id);
            engine.Wait();
        }

        [Fact]
        public void GetStatus_AfterSuccess_ReturnsDone()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return 42");
            engine.Wait();
            engine.GetStatus(id).ShouldBe(CoroutineStatus.Done);
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetStatus_AfterRuntimeError_ReturnsFaulted()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("error('test error')");
            engine.Wait();
            engine.GetStatus(id).ShouldBe(CoroutineStatus.Faulted);
            (engine.GetError(id) ?? "").ShouldContain("test error");
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetStatus_AfterCancelOnSleepingCoroutine_ReturnsCancelling()
        {
            // Cancelling: KitsuneCancel sets interrupted=1 synchronously, but the scheduler
            // processes it on its next cycle. Using a sleeping coroutine maximises the
            // window — the scheduler was in WaitForSingleObject and needs one full cycle
            // (wakeup + Step1 + Step2) before it can set done=1.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("Sleep(60000)");
            DateTime readyDeadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetStatus(id) != CoroutineStatus.Sleeping && DateTime.UtcNow < readyDeadline)
                Thread.Sleep(1);

            engine.Cancel(id);

            // Spin until the status changes from Sleeping — must be Cancelling before done=1 is set.
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            CoroutineStatus status;
            do status = engine.GetStatus(id);
            while (status == CoroutineStatus.Sleeping && DateTime.UtcNow < deadline);
            status.ShouldBe(CoroutineStatus.Cancelling);

            engine.Wait();
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
                    "local n = 0; for _ = 1, 5000 do n = n + (counter or 0) end",
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
                    args: [LuaValue.FromInt64(42)]))
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
                    $"slot_{i} = {i}; return tostring({i})"))
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

        // -- RegisterFunction -----------------------------------------------------

        [Fact]
        public async Task RegisterFunction_ReturnsString_LuaReceivesIt()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Greet", _ => "hello from C#");
            string? result = await engine.ExecuteStringAsync("return Greet()");
            result.ShouldBe("hello from C#");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_ReturnsNumber_LuaReceivesIt()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("FortyTwo", _ => (LuaValue)42.5);
            string? result = await engine.ExecuteStringAsync("return tostring(FortyTwo())");
            result.ShouldBe("42.5");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_ReturnsBool_LuaReceivesIt()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Yes", _ => (LuaValue)true);
            string? result = await engine.ExecuteStringAsync("return tostring(Yes())");
            result.ShouldBe("true");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_ReturnsNone_LuaReceivesNil()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Void", _ => LuaValue.None);
            string? result = await engine.ExecuteStringAsync(
                "local x = Void(); return tostring(x)");
            result.ShouldBe("nil");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_StringArg_ReceivedCorrectly()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Echo", args => args.First());
            string? result = await engine.ExecuteStringAsync("return Echo('ping')");
            result.ShouldBe("ping");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_NumberArg_ReceivedCorrectly()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Double", args =>
                LuaValue.FromInt64(args.First().AsInt64 * 2));
            string? result = await engine.ExecuteStringAsync("return tostring(Double(7))");
            result.ShouldBe("14");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_BoolArg_ReceivedCorrectly()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Not", args => LuaValue.FromBool(!args.First().Boolean));
            string? result = await engine.ExecuteStringAsync("return tostring(Not(true))");
            result.ShouldBe("false");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_MultipleArgs_AllReceivedInOrder()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Concat", args =>
                LuaValue.FromString(string.Join("-", args.Select(a => a.String ?? "?"))));
            string? result = await engine.ExecuteStringAsync(
                "return Concat('a', 'b', 'c')");
            result.ShouldBe("a-b-c");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_NoArgs_ReceivesEmptyCollection()
        {
            using KitsuneEngine engine = new();
            int receivedCount = -1;
            engine.RegisterFunction("CountArgs", args =>
            {
                receivedCount = args.Count;
                return LuaValue.None;
            });
            await engine.ExecuteStringAsync("CountArgs()");
            receivedCount.ShouldBe(0);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RegisterFunction_ThrowsLuaException_RaisesLuaErrorWithMessage()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Boom", _ => throw new LuaException("custom kaboom"));
            int id = engine.ExecuteString("return Boom()");
            engine.Wait(id);
            string? err = engine.GetError(id);
            err.ShouldNotBeNull();
            err.ShouldContain("custom kaboom");
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RegisterFunction_ThrowsOtherException_RaisesLuaErrorWithMessage()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Crash", _ => throw new InvalidOperationException("managed crash"));
            int id = engine.ExecuteString("return Crash()");
            engine.Wait(id);
            string? err = engine.GetError(id);
            err.ShouldNotBeNull();
            err.ShouldContain("managed crash");
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_ErrorCaughtByPcall_DoesNotAbortCoroutine()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Boom", _ => throw new LuaException("pcall me"));
            string? result = await engine.ExecuteStringAsync(
                "local ok, err = pcall(Boom); return tostring(ok) .. ':' .. err");
            result.ShouldNotBeNull();
            result.ShouldStartWith("false:");
            result.ShouldContain("pcall me");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_MultipleFunctions_EachCallsCorrectHandler()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("GetA", _ => "A");
            engine.RegisterFunction("GetB", _ => "B");
            engine.RegisterFunction("GetC", _ => "C");
            string? result = await engine.ExecuteStringAsync(
                "return GetA() .. GetB() .. GetC()");
            result.ShouldBe("ABC");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_ClosureCapture_MutatesAndReadsState()
        {
            using KitsuneEngine engine = new();
            int counter = 0;
            engine.RegisterFunction("Increment", _ =>
            {
                counter++;
                return LuaValue.FromInt64(counter);
            });
            string? result = await engine.ExecuteStringAsync(
                "return tostring(Increment()) .. ',' .. tostring(Increment())");
            result.ShouldBe("1,2");
            counter.ShouldBe(2);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_CalledRepeatedly_CorrectResultEveryTime()
        {
            using KitsuneEngine engine = new();
            int calls = 0;
            engine.RegisterFunction("Tick", _ => { calls++; return LuaValue.None; });
            await engine.ExecuteStringAsync(
                "for _ = 1, 10 do Tick() end");
            calls.ShouldBe(10);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_ConcurrentCoroutinesCalling_AllReceiveCorrectResult()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Square", args => {
                long x = args.First().AsInt64;
                return LuaValue.FromInt64(x * x);
            });

            const int count = 20;
            Task<string?>[] tasks = Enumerable.Range(1, count)
                .Select(i => engine.ExecuteStringAsync($"return tostring(Square({i}))"))
                .ToArray();
            string?[] results = await Task.WhenAll(tasks);

            for (int i = 0; i < count; i++)
                results[i].ShouldBe(((i + 1.0) * (i + 1.0)).ToString(
                    System.Globalization.CultureInfo.InvariantCulture));
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RegisterFunction_Dispose_DoesNotCrash()
        {
            // Verifies that GCHandles for registered functions are freed cleanly on Dispose.
            KitsuneEngine engine = new();
            engine.RegisterFunction("Noop", _ => LuaValue.None);
            engine.RegisterFunction("Echo", args => args.FirstOrDefault());
            Should.NotThrow(engine.Dispose);
        }

        // -- Shallow / deep table bridge ------------------------------------------

        [Fact]
        public void GetVariable_TableValue_IsOpaqueWithNoContents()
        {
            // GetVariable is shallow: a table value returns type=Table but Table==null.
            using KitsuneEngine engine = new();
            engine.ExecuteString("t = {x=1, y=2}", fireAndForget: true);
            engine.Wait();

            LuaValue v = engine.GetVariable("t");
            v.Type.ShouldBe(LuaType.Table);
            v.Table.ShouldBeNull();
        }

        [Fact]
        public void GetAll_NestedTableValue_IsOpaque()
        {
            // GetAll is shallow: iterating a table whose values include a sub-table yields
            // an opaque Table entry (type=Table, Table==null) for that value.
            using KitsuneEngine engine = new();
            engine.ExecuteString("outer = { scalar = 42, inner = {a=1, b=2} }", fireAndForget: true);
            engine.Wait();

            var all = engine.GetAll("outer");
            all.ShouldContain(kvp => kvp.Key.String == "scalar" && kvp.Value.AsDouble == 42);
            var innerEntry = all.Single(kvp => kvp.Key.String == "inner");
            innerEntry.Value.Type.ShouldBe(LuaType.Table);
            innerEntry.Value.Table.ShouldBeNull();  // opaque: contents not captured
        }

        [Fact]
        public void GetResult_TableReturn_ContainsFullContents()
        {
            // GetResult is deep: a table returned from a script is fully converted.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return {x=10, y=20}");
            engine.Wait(id);

            LuaValue result = engine.GetResultVariable(id);
            result.Type.ShouldBe(LuaType.Table);
            result.Table.ShouldNotBeNull();
            result.Table!.ShouldContain(kvp => kvp.Key.String == "x" && kvp.Value.AsDouble == 10);
            result.Table!.ShouldContain(kvp => kvp.Key.String == "y" && kvp.Value.AsDouble == 20);
        }

        [Fact]
        public void RegisterFunction_TableArg_ReceivedWithFullContents()
        {
            // Function args are deep: a table passed from Lua carries its full contents.
            using KitsuneEngine engine = new();
            LuaValue? received = null;
            engine.RegisterFunction("Capture", args =>
            {
                received = args[0];
                return LuaValue.None;
            });
            int id = engine.ExecuteString("Capture({a='hello', b=99})");
            engine.Wait(id);

            received.ShouldNotBeNull();
            received!.Value.Type.ShouldBe(LuaType.Table);
            received.Value.Table.ShouldNotBeNull();
            received.Value.Table!.ShouldContain(kvp => kvp.Key.String == "a" && kvp.Value.String == "hello");
            received.Value.Table!.ShouldContain(kvp => kvp.Key.String == "b" && kvp.Value.AsDouble == 99);
        }

        // -- Table args passed to execute functions (C# → Lua) --------------------

        [Fact]
        public void ExecuteString_TableArg_ContentAccessibleFromARGS()
        {
            // A table passed as an arg to ExecuteString is accessible as ARGS[n] with full contents.
            using KitsuneEngine engine = new();
            var tableArg = LuaValue.FromTable(new List<KeyValuePair<LuaValue, LuaValue>>
            {
                new(LuaValue.FromString("name"),  LuaValue.FromString("alice")),
                new(LuaValue.FromString("score"), LuaValue.FromInt64(99)),
            }.AsReadOnly());

            int id = engine.ExecuteString(
                "return ARGS[1].name .. ':' .. tostring(ARGS[1].score)",
                args: [tableArg]);
            engine.Wait(id);
            engine.GetResultString(id).ShouldBe("alice:99");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ExecuteFile_TableArg_ContentAccessibleFromARGS()
        {
            // For ExecuteFile ARGS[1] = file path; extra args start at ARGS[2].
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "return ARGS[2].x .. ':' .. tostring(ARGS[2].y)");
                using KitsuneEngine engine = new();
                var tableArg = LuaValue.FromTable(new List<KeyValuePair<LuaValue, LuaValue>>
                {
                    new(LuaValue.FromString("x"), LuaValue.FromString("hello")),
                    new(LuaValue.FromString("y"), LuaValue.FromInt64(7)),
                }.AsReadOnly());

                int id = engine.ExecuteFile(path, args: [tableArg]);
                engine.Wait(id);
                engine.GetResultString(id).ShouldBe("hello:7");
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally { File.Delete(path); }
        }

        [Fact]
        public async Task ExecuteFunction_TableArg_ContentAccessibleAsParameter()
        {
            // A table arg passed to ExecuteFunction arrives as a direct function parameter.
            using KitsuneEngine engine = new();
            engine.ExecuteString(
                "function process(t) return t.key .. ':' .. tostring(t.val) end",
                fireAndForget: true);
            engine.Wait();

            var tableArg = LuaValue.FromTable(new List<KeyValuePair<LuaValue, LuaValue>>
            {
                new(LuaValue.FromString("key"), LuaValue.FromString("test")),
                new(LuaValue.FromString("val"), LuaValue.FromInt64(42)),
            }.AsReadOnly());

            string? result = await engine.ExecuteFunctionAsync("process", args: [tableArg]);
            result.ShouldBe("test:42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_NestedTableArg_NestedContentsAccessible()
        {
            // A table arg containing a nested table is fully pushed; nested keys are reachable.
            using KitsuneEngine engine = new();
            engine.ExecuteString(
                "function getDeep(t) return t.outer.inner end",
                fireAndForget: true);
            engine.Wait();

            var inner = LuaValue.FromTable(new List<KeyValuePair<LuaValue, LuaValue>>
            {
                new(LuaValue.FromString("inner"), LuaValue.FromString("deep value")),
            }.AsReadOnly());
            var outer = LuaValue.FromTable(new List<KeyValuePair<LuaValue, LuaValue>>
            {
                new(LuaValue.FromString("outer"), inner),
            }.AsReadOnly());

            string? result = await engine.ExecuteFunctionAsync("getDeep", args: [outer]);
            result.ShouldBe("deep value");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Lua coroutine interop -------------------------------------------------

        [Fact]
        public async Task Coroutine_SubCoroutine_YieldAndResume_WorksCorrectly()
        {
            // coroutine.create/resume/yield inside a Kitsune-managed coroutine must work;
            // the Ticker hook is only installed on the Kitsune thread, not on sub-threads
            // created by user Lua code, so sub-coroutines run without interference.
            using KitsuneEngine engine = new();
            string? result = await engine.ExecuteStringAsync(@"
                local co = coroutine.create(function()
                    coroutine.yield(10)
                    return 20
                end)
                local ok1, v1 = coroutine.resume(co)
                local ok2, v2 = coroutine.resume(co)
                return tostring(v1) .. ':' .. tostring(v2)
            ");
            result.ShouldBe("10:20");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_Wrap_Generator_ProducesCorrectSequence()
        {
            // coroutine.wrap (the generator idiom) must work inside a Kitsune coroutine.
            using KitsuneEngine engine = new();
            string? result = await engine.ExecuteStringAsync(@"
                local gen = coroutine.wrap(function()
                    coroutine.yield(1)
                    coroutine.yield(2)
                    coroutine.yield(3)
                end)
                return tostring(gen()) .. ':' .. tostring(gen()) .. ':' .. tostring(gen())
            ");
            result.ShouldBe("1:2:3");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_DirectYield_FromKitsuneCoroutine_ResumesNormally()
        {
            // coroutine.yield() called directly from a Kitsune-managed coroutine yields to
            // the scheduler, which re-resumes it with zero args on the next pass.
            // The state after the yield must be unchanged; the resume must complete normally.
            using KitsuneEngine engine = new();
            string? result = await engine.ExecuteStringAsync(@"
                local x = 1
                coroutine.yield()
                x = x + 1
                return tostring(x)
            ");
            result.ShouldBe("2");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_InfiniteGenerator_ProducesCorrectValues()
        {
            // An infinite generator (sub-coroutine that never returns) must behave
            // correctly when its parent Kitsune coroutine calls it a fixed number of times.
            using KitsuneEngine engine = new();
            string? result = await engine.ExecuteStringAsync(@"
                local function counter(start)
                    return coroutine.wrap(function()
                        local n = start
                        while true do coroutine.yield(n); n = n + 1 end
                    end)
                end
                local c = counter(5)
                return tostring(c()) .. ':' .. tostring(c()) .. ':' .. tostring(c())
            ");
            result.ShouldBe("5:6:7");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_ConcurrentKitsuneCoroutines_EachWithOwnSubCoroutine_AreIndependent()
        {
            // Multiple concurrent Kitsune coroutines each owning their own sub-coroutine;
            // sub-coroutine state must not bleed between Kitsune-managed threads.
            using KitsuneEngine engine = new();
            Task<string?>[] tasks = Enumerable.Range(1, 5).Select(i =>
                engine.ExecuteStringAsync($@"
                    local co = coroutine.create(function()
                        coroutine.yield({i} * 10)
                        return {i} * 100
                    end)
                    local _, v1 = coroutine.resume(co)
                    local _, v2 = coroutine.resume(co)
                    return tostring(v1) .. ':' .. tostring(v2)
                ")).ToArray();
            string?[] results = await Task.WhenAll(tasks);
            for (int i = 0; i < 5; i++)
                results[i].ShouldBe($"{(i + 1) * 10}:{(i + 1) * 100}");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_YieldWithValue_ValueIsDiscardedByScheduler()
        {
            // The scheduler calls lua_pop(T, nresults) on LUA_YIELD, so any value passed
            // to coroutine.yield() is silently discarded. The result comes from return, not yield.
            using KitsuneEngine engine = new();
            string? result = await engine.ExecuteStringAsync("coroutine.yield('hello')");
            result.ShouldBeNull();  // no return statement → result is nil/none
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_YieldWithValue_ResumeReceivesNil()
        {
            // The scheduler always resumes with 0 args (nstart=0 after a yield),
            // so coroutine.yield() always returns nil inside a Kitsune-managed coroutine.
            using KitsuneEngine engine = new();
            string? result = await engine.ExecuteStringAsync(@"
                local v = coroutine.yield('discarded')
                return tostring(v)
            ");
            result.ShouldBe("nil");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_YieldIsNotReturn_ResultComesFromReturnOnly()
        {
            // Reinforces that yield value != result; only return sets the coroutine result.
            using KitsuneEngine engine = new();
            string? result = await engine.ExecuteStringAsync(@"
                coroutine.yield('not the result')
                return 'the real result'
            ");
            result.ShouldBe("the real result");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Wchar bridge ---------------------------------------------------------

        [Fact]
        public async Task Wchar_ReturnedFromScript_HasWcharType()
        {
            // A Lua Wchar returned by a coroutine is surfaced as LuaType.Wchar, not LuaType.String.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return Wchar.FromUtf8('hello wchar')");
            engine.Wait(id);
            LuaValue v = engine.GetResultVariable(id);
            v.Type.ShouldBe(LuaType.Wchar);
            v.String.ShouldBe("hello wchar");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Wchar_ReturnedFromScript_StringAccessible()
        {
            // GetResultString decodes the UTF-8 bytes regardless of String vs Wchar type.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return Wchar.FromUtf8('kitsune wchar')");
            engine.Wait(id);
            engine.GetResultString(id).ShouldBe("kitsune wchar");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Wchar_SetVariable_PushesWcharIntoLua()
        {
            // Setting a Wchar variable pushes a Lua Wchar object; Lua can call Wchar methods on it.
            using KitsuneEngine engine = new();
            engine.SetVariable("wv", LuaValue.FromWchar("hello"));
            string? result = await engine.ExecuteStringAsync(
                "return tostring(type(wv) == 'userdata' and wv:ToUtf8() == 'hello')");
            result.ShouldBe("true");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Wchar_SetVariable_LuaCanCallWcharMethods()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("greeting", LuaValue.FromWchar("Hello World"));
            string? result = await engine.ExecuteStringAsync(
                "return greeting:ToUpper():ToUtf8()");
            result.ShouldBe("HELLO WORLD");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Wchar_GetVariable_FromLuaWcharGlobal_ReturnsWcharType()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("myWchar = Wchar.FromUtf8('bridge test')", fireAndForget: true);
            engine.Wait();
            LuaValue v = engine.GetVariable("myWchar");
            v.Type.ShouldBe(LuaType.Wchar);
            v.String.ShouldBe("bridge test");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Wchar_RoundTrip_SetAndGet_PreservesContent()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("wRound", LuaValue.FromWchar("round trip \u00e9"));  // é is non-ASCII
            LuaValue back = engine.GetVariable("wRound");
            back.Type.ShouldBe(LuaType.Wchar);
            back.String.ShouldBe("round trip \u00e9");
        }

        [Fact]
        public async Task Wchar_InTable_ReturnedWithWcharType()
        {
            // A Wchar inside a returned table is also tagged as LuaType.Wchar.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return { w = Wchar.FromUtf8('in table') }");
            engine.Wait(id);
            LuaValue result = engine.GetResultVariable(id);
            result.Type.ShouldBe(LuaType.Table);
            result.Table.ShouldNotBeNull();
            var entry = result.Table!.Single(kvp => kvp.Key.String == "w");
            entry.Value.Type.ShouldBe(LuaType.Wchar);
            entry.Value.String.ShouldBe("in table");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Wchar_RegisterFunction_WcharArgReceivedAsWcharType()
        {
            // A Wchar passed to a registered C# function arrives with LuaType.Wchar.
            using KitsuneEngine engine = new();
            LuaValue? received = null;
            engine.RegisterFunction("CaptureWchar", args => { received = args[0]; return LuaValue.None; });
            int id = engine.ExecuteString("CaptureWchar(Wchar.FromUtf8('from lua'))");
            engine.Wait(id);
            received.ShouldNotBeNull();
            received!.Value.Type.ShouldBe(LuaType.Wchar);
            received.Value.String.ShouldBe("from lua");
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Wchar_RegisterFunction_ReturnWchar_LuaReceivesWcharObject()
        {
            // A C# function returning LuaType.Wchar pushes a Lua Wchar object; Lua can call methods on it.
            using KitsuneEngine engine = new();
            engine.RegisterFunction("MakeWchar", _ => LuaValue.FromWchar("from csharp"));
            string? result = await engine.ExecuteStringAsync(
                "local w = MakeWchar(); return tostring(type(w)=='userdata' and w:ToUpper():ToUtf8())");
            result.ShouldBe("FROM CSHARP");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Userdata __name bridge ------------------------------------------------

        [Fact]
        public async Task Userdata_RegisterFunction_ArgHasTypeNameInBytes()
        {
            // An unrecognised userdata passed as an argument to a registered C# function
            // arrives with Type == Userdata and Bytes holding the metatable __name.
            // Json is used because it is in-memory and has no external dependencies.
            using KitsuneEngine engine = new();
            LuaValue? received = null;
            engine.RegisterFunction("CaptureJson", args => { received = args[0]; return LuaValue.None; });
            int id = engine.ExecuteString("CaptureJson(Json.Create())");
            engine.Wait(id);
            received.ShouldNotBeNull();
            received!.Value.Type.ShouldBe(LuaType.Userdata);
            received.Value.Bytes.ShouldNotBeNull();
            received.Value.String.ShouldBe("LUAJSON");
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Userdata_CoroutineResult_TypeNameInBytes()
        {
            // An unrecognised userdata returned from a coroutine arrives via GetResultVariable
            // with Type == Userdata and Bytes holding the metatable __name.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return Json.Create()");
            engine.Wait(id);
            LuaValue v = engine.GetResultVariable(id);
            v.Type.ShouldBe(LuaType.Userdata);
            v.Bytes.ShouldNotBeNull();
            v.String.ShouldBe("LUAJSON");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Userdata_DifferentType_TypeNameMatchesMetatable()
        {
            // Verifies the __name lookup is not hard-coded: CSV.Create() carries a
            // different metatable name ("LUACSV") from Json.Create() ("LUAJSON").
            using KitsuneEngine engine = new();
            LuaValue? received = null;
            engine.RegisterFunction("CaptureCsv", args => { received = args[0]; return LuaValue.None; });
            int id = engine.ExecuteString("CaptureCsv(CSV.Create())");
            engine.Wait(id);
            received.ShouldNotBeNull();
            received!.Value.Type.ShouldBe(LuaType.Userdata);
            received.Value.String.ShouldBe("LUACSV");
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }
    }
}
