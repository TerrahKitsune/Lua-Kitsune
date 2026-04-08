using KitsuneNet;
using Shouldly;
using System.Text;
using System.Text.Json.Nodes;
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
            {
                Thread.Sleep(1);
            }
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
            all.ShouldContain(kvp => kvp.Key.String == "foo" && kvp.Value.String == "bar");
            all.ShouldContain(kvp => kvp.Key.String == "count" && kvp.Value.Number == 42.0);
            all.ShouldContain(kvp => kvp.Key.String == "flag" && kvp.Value.Boolean == true);
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

            var all = engine.GetAll(string.Empty);  // "" iterates _G itself
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
            {
                results[i].ShouldBe($"value_{i}");
            }

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
            Task<string?> errorTask = engine.ExecuteStringAsync("error('fail')");
            Task<string?> nilTask = engine.ExecuteStringAsync("return nil");
            Task<string?> noRetTask = engine.ExecuteStringAsync("local x = 1 + 1");
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
            {
                engine.GetResultString(ids[i]).ShouldBe($"parallel_{i}");
            }

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
            {
                engine.ExecuteString("Sleep(500)", fireAndForget: true);
            }

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
            {
                Thread.Sleep(1);
            }

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
            {
                results[i].ShouldBe($"async_{i}");
            }

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
            string capturedName = engine.GetString("Config.name") ?? string.Empty;
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
            {
                Thread.Sleep(1);
            }

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
            {
                Thread.Sleep(1);
            }

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
            // With two concurrent coroutines, the scheduler alternates between them,
            // so at any moment one is running and the other is Idle.
            using KitsuneEngine engine = new();
            int idA = engine.ExecuteString("while true do end");
            int idB = engine.ExecuteString("while true do end");
            try
            {
                DateTime deadline = DateTime.UtcNow.AddSeconds(5);
                bool sawIdle = false;
                while (!sawIdle && DateTime.UtcNow < deadline)
                {
                    sawIdle = engine.GetStatus(idA) == CoroutineStatus.Idle
                           || engine.GetStatus(idB) == CoroutineStatus.Idle;
                }

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
            do
            {
                status = engine.GetStatus(id);
            }
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
            do
            {
                status = engine.GetStatus(id);
            }
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
            (engine.GetError(id) ?? string.Empty).ShouldContain("test error");
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetStatus_AfterCancelOnSleepingCoroutine_ReturnsCancelled()
        {
            // After Cancel, GetStatus returns Cancelled until the slot is freed.
            // On fast schedulers (e.g. Linux) the slot may already be freed, returning None.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("Sleep(60000)");
            DateTime readyDeadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetStatus(id) != CoroutineStatus.Sleeping && DateTime.UtcNow < readyDeadline)
            {
                Thread.Sleep(1);
            }

            engine.GetStatus(id).ShouldBe(CoroutineStatus.Sleeping);

            engine.Cancel(id);

            var statusAfterCancel = engine.GetStatus(id);
            (statusAfterCancel == CoroutineStatus.Cancelled || statusAfterCancel == CoroutineStatus.None)
                .ShouldBeTrue($"Expected Cancelled or None after Cancel but got {statusAfterCancel}");

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
            {
                Thread.Sleep(1);
            }

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
            {
                Thread.Sleep(1);
            }

            engine.GetRuntime(id).ShouldBe(0.0);  // slot compacted; ID not found returns 0
        }

        // -- Stress tests ---------------------------------------------------------
        [Fact]
        public async Task Stress_HighThroughput_SequentialBatches_AllCorrect()
        {
            // 1000 coroutines in batches of 100 — verifies high-throughput execution
            // produces the correct result for every single coroutine with no data loss.
            using KitsuneEngine engine = new();
            const int total = 1000;
            const int batchSize = 100;

            for (int batch = 0; batch < total / batchSize; batch++)
            {
                int offset = batch * batchSize;
                Task<string?>[] tasks = Enumerable.Range(0, batchSize)
                    .Select(j => engine.ExecuteStringAsync($"return tostring({offset + j})"))
                    .ToArray();
                string?[] batchResults = await Task.WhenAll(tasks);
                for (int j = 0; j < batchSize; j++)
                {
                    batchResults[j].ShouldBe((offset + j).ToString());
                }
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
            const int total = 1000;
            const int maxConcurrent = 64;
            var results = new string?[total];

            using var sem = new SemaphoreSlim(maxConcurrent, maxConcurrent);
            Task[] tasks = Enumerable.Range(0, total).Select(async i =>
            {
                await sem.WaitAsync().ConfigureAwait(false);
                try
                {
                    results[i] = await engine.ExecuteStringAsync($"return tostring({i})").ConfigureAwait(false);
                }
                finally
                {
                    sem.Release();
                }
            }).ToArray();

            await Task.WhenAll(tasks);

            for (int i = 0; i < total; i++)
            {
                results[i].ShouldBe(i.ToString());
            }

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
            const int threads = 8;
            const int opsPerThread = 200;
            int completedOps = 0;

            // Lua coroutines that read Vars under scheduler pressure.
            for (int i = 0; i < 10; i++)
            {
                engine.ExecuteString(
                    "local n = 0; for _ = 1, 5000 do n = n + (counter or 0) end",
                    fireAndForget: true);
            }

            Task[] writers = Enumerable.Range(0, threads).Select(t => Task.Run(() =>
            {
                for (int i = 0; i < opsPerThread; i++)
                {
                    engine.SetNumber("counter", (t * 1000.0) + i);

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
            {
                engine.ExecuteString($"function stress_fn_{i}(x) return tostring(x * {i}) end",
                    fireAndForget: true);
            }

            engine.Wait();

            Task<string?>[] tasks = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteFunctionAsync($"stress_fn_{i}",
                    args: [LuaValue.FromInt64(42)]))
                .ToArray();
            string?[] results = await Task.WhenAll(tasks);

            for (int i = 0; i < count; i++)
            {
                results[i].ShouldBe((42 * i).ToString());
            }

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
            {
                engine.GetResultString(ids[i]).ShouldBe(i.ToString());
            }

            engine.GetActiveIds().ShouldBeEmpty();
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
            engine.RegisterFunction("Tick", _ =>
            {
                calls++;
                return LuaValue.None;
            });
            await engine.ExecuteStringAsync(
                "for _ = 1, 10 do Tick() end");
            calls.ShouldBe(10);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_ConcurrentCoroutinesCalling_AllReceiveCorrectResult()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Square", args =>
            {
                long x = args.First().AsInt64;
                return LuaValue.FromInt64(x * x);
            });

            const int count = 20;
            Task<string?>[] tasks = Enumerable.Range(1, count)
                .Select(i => engine.ExecuteStringAsync($"return tostring(Square({i}))"))
                .ToArray();
            string?[] results = await Task.WhenAll(tasks);

            for (int i = 0; i < count; i++)
            {
                results[i].ShouldBe(((i + 1.0) * (i + 1.0)).ToString(
                    System.Globalization.CultureInfo.InvariantCulture));
            }

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

        // -- Table args passed to execute functions (C# ? Lua) --------------------
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
            finally
            {
                File.Delete(path);
            }
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
            {
                results[i].ShouldBe($"{(i + 1) * 10}:{(i + 1) * 100}");
            }

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_YieldWithValue_ValueIsDiscardedByScheduler()
        {
            // Any value passed to coroutine.yield() in a Kitsune coroutine is silently
            // discarded by the scheduler. The result comes from return, not yield.
            using KitsuneEngine engine = new();
            string? result = await engine.ExecuteStringAsync("coroutine.yield('hello')");
            result.ShouldBeNull();  // no return statement ? result is nil/none
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_YieldWithValue_ResumeReceivesNil()
        {
            // The scheduler always resumes with 0 args after a yield,
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
            v.Type.ShouldBe(LuaType.Char16);
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
            v.Type.ShouldBe(LuaType.Char16);
            v.String.ShouldBe("bridge test");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Wchar_RoundTrip_SetAndGet_PreservesContent()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("wRound", LuaValue.FromWchar("round trip \u00e9"));  // é is non-ASCII
            LuaValue back = engine.GetVariable("wRound");
            back.Type.ShouldBe(LuaType.Char16);
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
            entry.Value.Type.ShouldBe(LuaType.Char16);
            entry.Value.String.ShouldBe("in table");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Wchar_RegisterFunction_WcharArgReceivedAsWcharType()
        {
            // A Wchar passed to a registered C# function arrives with LuaType.Wchar.
            using KitsuneEngine engine = new();
            LuaValue? received = null;
            engine.RegisterFunction("CaptureWchar", args =>
            {
                received = args[0];
                return LuaValue.None;
            });
            int id = engine.ExecuteString("CaptureWchar(Wchar.FromUtf8('from lua'))");
            engine.Wait(id);
            received.ShouldNotBeNull();
            received!.Value.Type.ShouldBe(LuaType.Char16);
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
            engine.RegisterFunction("CaptureJson", args =>
            {
                received = args[0];
                return LuaValue.None;
            });
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
            // Verifies the __name lookup is not hard-coded: Stream.Create() carries a
            // different metatable name ("STREAM") from Json.Create() ("LUAJSON").
            using KitsuneEngine engine = new();
            LuaValue? received = null;
            engine.RegisterFunction("CaptureStream", args =>
            {
                received = args[0];
                return LuaValue.None;
            });
            int id = engine.ExecuteString("CaptureStream(Stream.Create())");
            engine.Wait(id);
            received.ShouldNotBeNull();
            received!.Value.Type.ShouldBe(LuaType.Userdata);
            received.Value.Bytes.ShouldNotBeNull();
            received.Value.String.ShouldBe("STREAM");
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Nohook / yield safety ------------------------------------------------
        [Fact]
        public void NohookCallback_ErrorCaughtByPcall_HookRestoredAndCancelStillWorks()
        {
            // Regression: if a nohook callback error was caught by pcall, the coroutine
            // could continue running without the Ticker hook, making Cancel ineffective.
            // Verifies the hook is always restored before the error unwinds.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString(@"
                -- CSV.DecodeFromFunction calls the supplier via lua_call_nohook only
                -- when the iterator is advanced (iter()).  Calling iter() inside the
                -- pcall triggers the supplier error through lua_call_nohook; the hook
                -- must be restored before re-raising so the outer pcall leaves the
                -- coroutine fully hooked.
                local ok, err = pcall(function()
                    local iter = CSV.New():DecodeFromFunction(function() error('supplier error') end)
                    iter()   -- advances the iterator ? calls supplier ? errors via lua_call_nohook
                end)
                -- If the hook was lost the Ticker never fires and Cancel hangs here forever.
                while true do end
            ");
            SpinUntilRunning(engine);
            engine.Cancel(id);
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetActiveIds().Contains(id) && DateTime.UtcNow < deadline)
            {
                Thread.Sleep(1);
            }

            engine.GetActiveIds().ShouldNotContain(id,
                "Cancel timed out — Ticker hook may have been lost after pcall caught the nohook callback error");
        }

        [Fact]
        public async Task NohookCallback_SleepInsideSupplier_CompletesWithoutCrash()
        {
            // Regression: Sleep inside a nohook callback must not attempt to yield;
            // it falls back to a blocking sleep when the call stack is non-yieldable.
            using KitsuneEngine engine = new();
            string? result = await engine.ExecuteStringAsync(@"
                local count = 0
                local iter = CSV.New():DecodeFromFunction(function()
                    count = count + 1
                    if count > 2 then return nil end
                    Sleep(1)   -- inside lua_call_nohook: must use blocking sleep, not lua_yieldk
                    return 'a,b\n'
                end)
                local rows = 0
                local row = iter()
                while row do rows = rows + 1; row = iter() end
                return tostring(rows)
            ");
            result.ShouldBe("2");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task MetamethodFromCCode_WithConcurrentCoroutines_NoCrash()
        {
            // Regression: the Ticker must not yield when a non-yieldable C-call boundary
            // is on the stack (e.g. when __tostring is dispatched via tostring()).
            using KitsuneEngine engine = new();

            // Runs concurrently to keep the scheduler active.
            Task<string?> bgTask = engine.ExecuteStringAsync(
                "local n = 0; for _ = 1, 1000000 do n = n + 1 end; return tostring(n)");

            // Calls tostring() on an object with __tostring; dispatched via a non-yieldable
            // C boundary — the Ticker must handle this without crashing.
            Task<string?> fgTask = engine.ExecuteStringAsync(@"
                local obj = setmetatable({}, {
                    __tostring = function()
                        local s = 0
                        for i = 1, 500 do s = s + i end
                        return 'obj:' .. tostring(s)
                    end
                })
                local r = ''
                for _ = 1, 200 do r = tostring(obj) end
                return r
            ");

            string?[] results = await Task.WhenAll(bgTask, fgTask);
            results[0].ShouldBe("1000000");
            results[1].ShouldBe("obj:125250");  // sum(1..500) = 125250
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Json bridge ----------------------------------------------------------
        [Fact]
        public void Json_FromJson_Null_ReturnsNone()
        {
            // null JsonNode produces LuaValue.None — nothing is pushed to Lua.
            LuaValue.FromJson(null).Type.ShouldBe(LuaType.None);
        }

        [Fact]
        public async Task Json_SetVariable_ObjectNode_LuaReadsFields()
        {
            // A C# JsonNode set via SetVariable arrives in Lua as a table decoded
            // by the bridge Json instance; all fields are accessible by name.
            using KitsuneEngine engine = new();
            engine.SetVariable("jv", LuaValue.FromJson(JsonNode.Parse("""{"name":"alice","score":99}""")));
            string? result = await engine.ExecuteStringAsync(
                "return jv.name .. ':' .. tostring(jv.score)");
            result.ShouldBe("alice:99");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Json_SetVariable_NestedObject_LuaReadsDeeply()
        {
            // Nested JSON objects become nested Lua tables; deep path access works.
            using KitsuneEngine engine = new();
            engine.SetVariable("jv", LuaValue.FromJson(JsonNode.Parse("""{"outer":{"inner":"deep"}}""")));
            string? result = await engine.ExecuteStringAsync("return jv.outer.inner");
            result.ShouldBe("deep");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Json_SetVariable_ArrayNode_LuaUsesOneBasedKeys()
        {
            // Json:Decode maps JSON arrays to Lua tables with 1-based integer keys.
            using KitsuneEngine engine = new();
            engine.SetVariable("jv", LuaValue.FromJson(JsonNode.Parse("[10,20,30]")));
            string? result = await engine.ExecuteStringAsync(
                "return tostring(jv[1]) .. ':' .. tostring(jv[2]) .. ':' .. tostring(jv[3])");
            result.ShouldBe("10:20:30");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Json_ExecuteStringArg_LuaReadsFields()
        {
            // A JsonNode passed as an ARGS element is accessible as a table in the script.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString(
                "return tostring(ARGS[1].x + ARGS[1].y)",
                args: [LuaValue.FromJson(JsonNode.Parse("""{"x":7,"y":8}"""))]);
            engine.Wait(id);
            engine.GetResultString(id).ShouldBe("15");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Json_TableResult_AsJsonNode_ProducesJsonObject()
        {
            // Lua returns a string-keyed table; AsJsonNode() converts the LuaType.Table
            // linked list to a JsonObject — no native-side JSON encoding involved.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return {name='bob', score=42}");
            engine.Wait(id);
            LuaValue result = engine.GetResultVariable(id);
            result.Type.ShouldBe(LuaType.Table);
            JsonNode? node = result.AsJsonNode();
            node.ShouldBeAssignableTo<JsonObject>();
            ((JsonObject)node!)["name"]!.GetValue<string>().ShouldBe("bob");
            ((JsonObject)node)["score"]!.GetValue<long>().ShouldBe(42L);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Json_TableResult_AsJsonNode_SequentialIntKeys_ProducesJsonArray()
        {
            // Lua returns a sequential table; AsJsonNode() detects 1-based integer keys
            // and produces a JsonArray with elements in order.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return {10, 20, 30}");
            engine.Wait(id);
            LuaValue result = engine.GetResultVariable(id);
            result.Type.ShouldBe(LuaType.Table);
            JsonNode? node = result.AsJsonNode();
            node.ShouldBeAssignableTo<JsonArray>();
            var arr = (JsonArray)node!;
            arr.Count.ShouldBe(3);
            arr[0]!.GetValue<long>().ShouldBe(10L);
            arr[1]!.GetValue<long>().ShouldBe(20L);
            arr[2]!.GetValue<long>().ShouldBe(30L);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Json_AsJsonNode_OnScalarLuaValues()
        {
            // AsJsonNode() wraps scalar LuaValues in the appropriate JsonValue.
            LuaValue.FromInt64(42).AsJsonNode()!.GetValue<long>().ShouldBe(42L);
            LuaValue.FromNumber(3.14).AsJsonNode()!.GetValue<double>().ShouldBe(3.14);
            LuaValue.FromBool(true).AsJsonNode()!.GetValue<bool>().ShouldBeTrue();
            LuaValue.FromString("hello").AsJsonNode()!.GetValue<string>().ShouldBe("hello");
            LuaValue.None.AsJsonNode().ShouldBeNull();
            new LuaValue { Type = LuaType.Nil }.AsJsonNode().ShouldBeNull();
        }

        [Fact]
        public void Json_AsJsonNode_OnJsonType_ReturnsSameNode()
        {
            // When Type == Json, AsJsonNode() returns the stored node directly — no re-parse.
            var node = JsonNode.Parse("""{"direct":true}""")!;
            LuaValue v = LuaValue.FromJson(node);
            v.Type.ShouldBe(LuaType.Json);
            ReferenceEquals(v.AsJsonNode(), node).ShouldBeTrue();
        }

        [Fact]
        public void Json_ImplicitOperator_WorksFromJsonNode()
        {
            // The implicit operator allows a JsonNode to be assigned to LuaValue directly.
            LuaValue v = JsonNode.Parse("""{"k":1}""");
            v.Type.ShouldBe(LuaType.Json);
            v.JsonNode.ShouldNotBeNull();
        }

        [Fact]
        public async Task Json_RegisterFunction_TableArgConvertedToJsonNode()
        {
            // When Lua passes a table (originally decoded from a JsonNode) to a C# registered
            // function, it arrives as LuaType.Table; AsJsonNode() converts it correctly.
            using KitsuneEngine engine = new();
            JsonNode? captured = null;
            engine.RegisterFunction("CaptureJson", args =>
            {
                captured = args[0].AsJsonNode();
                return LuaValue.None;
            });
            engine.SetVariable("jv", LuaValue.FromJson(JsonNode.Parse("""{"a":1,"b":2}""")));
            int id = engine.ExecuteString("CaptureJson(jv)");
            engine.Wait(id);
            captured.ShouldNotBeNull();
            captured.ShouldBeAssignableTo<JsonObject>();
            ((JsonObject)captured!)["a"]!.GetValue<long>().ShouldBe(1L);
            ((JsonObject)captured)["b"]!.GetValue<long>().ShouldBe(2L);
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Json_RegisterFunction_ReturnsJsonNode_LuaReadsAsTable()
        {
            // A C# function returning LuaValue.FromJson produces a Lua table in the caller.
            using KitsuneEngine engine = new();
            engine.RegisterFunction("MakeJson", _ =>
                LuaValue.FromJson(JsonNode.Parse("""{"status":"ok","code":200}""")));
            string? result = await engine.ExecuteStringAsync(
                "local t = MakeJson(); return t.status .. ':' .. tostring(t.code)");
            result.ShouldBe("ok:200");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Json_MultipleRoundTrips_NoCrashOrLeak()
        {
            // 50 iterations of JsonNode ? bridge Json decode ? Lua reads ? result.
            // The bridge Json instance in the Lua registry is reused each time;
            // correct results on every iteration confirm no corruption or resource leak.
            using KitsuneEngine engine = new();
            for (int i = 0; i < 50; i++)
            {
                engine.SetVariable("j", LuaValue.FromJson(
                    JsonNode.Parse($"{{\"n\":{i},\"s\":\"{i}\"}}")));
                string? result = await engine.ExecuteStringAsync(
                    "return j.s .. ':' .. tostring(j.n)");
                result.ShouldBe($"{i}:{i}", $"round-trip failed at iteration {i}");
            }
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Stream bridge (LuaType.Stream / KITSUNE_TSTREAM) ---------------------
        [Fact]
        public void Stream_LuaReturnsStream_TypeIsStream()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return Stream.Create('hello')");
            engine.Wait(id);
            LuaValue v = engine.GetResultVariable(id);
            using var stream = v.StreamValue as LuaStream;
            v.Type.ShouldBe(LuaType.Stream);
            stream.ShouldNotBeNull();
        }

        [Fact]
        public void Stream_LuaReturnsStream_ContainsCorrectBytes()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return Stream.Create('hello bridge')");
            engine.Wait(id);
            using LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;
            Encoding.UTF8.GetString(stream.ToArray()).ShouldBe("hello bridge");
        }

        [Fact]
        public void Stream_LuaReturnsStream_StreamApiReads()
        {
            // LuaStream : UnmanagedMemoryStream — no copy; Length and Read use native memory.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return Stream.Create('bridge test')");
            engine.Wait(id);
            using LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;
            stream.Length.ShouldBe(11L);
            byte[] buf = new byte[stream.Length];
            stream.ReadExactly(buf);
            Encoding.UTF8.GetString(buf).ShouldBe("bridge test");
        }

        [Fact]
        public void Stream_LuaReturnsStream_SeekAndReadPartial()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return Stream.Create('ABCDEFGHIJ')");
            engine.Wait(id);
            using LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;
            stream.Seek(5, System.IO.SeekOrigin.Begin);
            byte[] buf = new byte[5];
            stream.ReadExactly(buf);
            Encoding.UTF8.GetString(buf).ShouldBe("FGHIJ");
        }

        [Fact]
        public void Stream_LuaReturnsStream_ToArray_PreservesPosition()
        {
            // ToArray() must not move the read cursor.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return Stream.Create('hello')");
            engine.Wait(id);
            using LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;
            stream.Seek(3, System.IO.SeekOrigin.Begin);
            byte[] bytes = stream.ToArray();
            Encoding.UTF8.GetString(bytes).ShouldBe("hello");
            stream.Position.ShouldBe(3L);  // position must be restored
        }

        [Fact]
        public async Task Stream_CSharpSendsStream_LuaReadsIt()
        {
            // C# ? Lua: FromStream(byte[]) wraps bytes in a native block;
            // Lua receives a readable stream and Read() returns the exact bytes.
            using KitsuneEngine engine = new();
            engine.SetVariable("data", LuaValue.FromStream(Encoding.UTF8.GetBytes("hello from csharp")));
            string? result = await engine.ExecuteStringAsync("return data:Read()");
            result.ShouldBe("hello from csharp");
        }

        [Fact]
        public async Task Stream_CSharpSendsStream_LuaCanSeek()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("data", LuaValue.FromStream(Encoding.UTF8.GetBytes("ABCDEFGHIJ")));
            string? result = await engine.ExecuteStringAsync("data:Seek(5); return data:Read()");
            result.ShouldBe("FGHIJ");
        }

        [Fact]
        public async Task Stream_CSharpSendsStream_FromSystemIOStream()
        {
            // FromStream(System.IO.Stream) should also work.
            using KitsuneEngine engine = new();
            using var ms = new System.IO.MemoryStream(Encoding.UTF8.GetBytes("from memorystream"));
            engine.SetVariable("data", LuaValue.FromStream(ms));
            string? result = await engine.ExecuteStringAsync("return data:Read()");
            result.ShouldBe("from memorystream");
        }

        [Fact]
        public async Task Stream_CSharpSendsStream_AsArg_LuaReadsIt()
        {
            // Passing a stream as a coroutine argument; the engine places it in ARGS[1]
            // (string/file coroutines receive args via the ARGS table, not via varargs).
            using KitsuneEngine engine = new();
            string? result = await engine.ExecuteStringAsync(
                "local s = ARGS[1]; return s:Read()",
                args: [LuaValue.FromStream(Encoding.UTF8.GetBytes("arg stream"))]);
            result.ShouldBe("arg stream");
        }

        [Fact]
        public void Stream_LuaReturnsStream_Dispose_DoesNotThrow()
        {
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return Stream.Create('dispose me')");
            engine.Wait(id);
            LuaValue v = engine.GetResultVariable(id);
            LuaStream stream = (LuaStream)v.StreamValue!;
            Should.NotThrow(stream.Dispose);
        }

        [Fact]
        public void Stream_LuaReturnsStream_DoubleDispose_DoesNotThrow()
        {
            // The Interlocked guard must prevent the close callback from being invoked twice.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return Stream.Create('double dispose')");
            engine.Wait(id);
            LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;
            stream.Dispose();
            Should.NotThrow(stream.Dispose);
        }

        [Fact]
        public async Task Stream_MultipleRoundTrips_NoCrashOrLeak()
        {
            // 50 iterations of C# bytes ? Lua stream ? Lua reads ? string result.
            using KitsuneEngine engine = new();
            for (int i = 0; i < 50; i++)
            {
                engine.SetVariable("s", LuaValue.FromStream(Encoding.UTF8.GetBytes($"iter{i}")));
                string? result = await engine.ExecuteStringAsync("return s:Read()");
                result.ShouldBe($"iter{i}", $"round-trip failed at iteration {i}");
            }
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Stream lifetime and sync ---------------------------------------------
        [Fact]
        public void Stream_LuaReturnsStream_DataConsistentAfterGcCollect()
        {
            // The Lua registry anchor must prevent premature collection; the native
            // block must still be addressable after multiple forced GC cycles.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return Stream.Create('keep me alive')");
            engine.Wait(id);
            using LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;

            GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking: true);
            GC.WaitForPendingFinalizers();
            GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking: true);

            Encoding.UTF8.GetString(stream.ToArray()).ShouldBe("keep me alive");
        }

        [Fact]
        public void Stream_LuaReturnsStream_MultipleReadsReturnIdenticalData()
        {
            // The underlying native memory must not change between reads during the
            // stream's lifetime — ToArray must produce the same bytes every call.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return Stream.Create('stable data')");
            engine.Wait(id);
            using LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;

            byte[] first = stream.ToArray();
            byte[] second = stream.ToArray();

            first.ShouldBe(second);
            Encoding.UTF8.GetString(first).ShouldBe("stable data");
        }

        [Fact]
        public async Task Stream_LuaReturnsStream_RemainsReadableAfterConcurrentEngineUse()
        {
            // Running additional coroutines (which trigger scheduler cycles and may
            // drain the pending-unref queue) must not invalidate a held LuaStream.
            var engine = new KitsuneEngine();
            try
            {
                int id = engine.ExecuteString("return Stream.Create('concurrent test')");
                engine.Wait(id);
                using LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;

                for (int i = 0; i < 10; i++)
                {
                    (await engine.ExecuteStringAsync("return 'ping'")).ShouldBe("ping");
                }

                Encoding.UTF8.GetString(stream.ToArray()).ShouldBe("concurrent test");
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        // -- Stream disposal ------------------------------------------------------
        [Fact]
        public void Stream_LuaReturnsStream_AfterDispose_CanReadReturnsFalse()
        {
            var engine = new KitsuneEngine();
            try
            {
                int id = engine.ExecuteString("return Stream.Create('will close')");
                engine.Wait(id);
                LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;

                stream.CanRead.ShouldBeTrue();
                stream.Dispose();
                stream.CanRead.ShouldBeFalse();
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public void Stream_LuaReturnsStream_AfterDispose_ReadThrowsObjectDisposedException()
        {
            var engine = new KitsuneEngine();
            try
            {
                int id = engine.ExecuteString("return Stream.Create('closed')");
                engine.Wait(id);
                LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;
                stream.Dispose();

                byte[] buf = new byte[1];
                Should.Throw<ObjectDisposedException>(() => stream.Read(buf, 0, 1));
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public void Stream_LuaReturnsStream_AfterDispose_LengthThrowsObjectDisposedException()
        {
            var engine = new KitsuneEngine();
            try
            {
                int id = engine.ExecuteString("return Stream.Create('len test')");
                engine.Wait(id);
                LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;
                stream.Dispose();

                Should.Throw<ObjectDisposedException>(() => _ = stream.Length);
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public void Stream_LuaReturnsStream_AfterDispose_SeekThrowsObjectDisposedException()
        {
            var engine = new KitsuneEngine();
            try
            {
                int id = engine.ExecuteString("return Stream.Create('seek test')");
                engine.Wait(id);
                LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;
                stream.Dispose();

                Should.Throw<ObjectDisposedException>(() => stream.Seek(0, System.IO.SeekOrigin.Begin));
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public async Task Stream_LuaReturnsStream_AfterDispose_EngineStillFunctional()
        {
            // Disposing the LuaStream sets ACCESSOR_DISPOSED on the block; the engine's ticker
            // sweeps the block list and frees it once Lua's GC also sets OWNER_DISPOSED.
            // Subsequent coroutines must succeed with no corruption or deadlock.
            var engine = new KitsuneEngine();
            try
            {
                int id = engine.ExecuteString("return Stream.Create('dispose me')");
                engine.Wait(id);
                ((LuaStream)engine.GetResultVariable(id).StreamValue!).Dispose();

                for (int i = 0; i < 5; i++)
                {
                    (await engine.ExecuteStringAsync("return 'alive'")).ShouldBe("alive");
                }

                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public void Stream_LuaReturnsStream_ManyDisposes_NoResourceLeak()
        {
            // 100 streams created, read, and disposed — if the close callback never
            // fired the Lua registry would fill up and later coroutines would fail.
            var engine = new KitsuneEngine();
            try
            {
                for (int i = 0; i < 100; i++)
                {
                    int id = engine.ExecuteString($"return Stream.Create('block{i}')");
                    engine.Wait(id);
                    using LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;
                    Encoding.UTF8.GetString(stream.ToArray()).ShouldBe($"block{i}");
                }
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        // -- Shared-memory concurrent access -------------------------------------
        [Fact]
        public void SharedMemory_ConcurrentLuaAndCSharpAccess_CooperativeLockedFlag()
        {
            // Verifies concurrent access between Lua (scheduler thread) and C# (test thread)
            // on the same shared-memory block, and that the LOCKED flag is observable from C#.
            //
            // Block layout (data starts at struct offset 32):
            //   data[0] – counter incremented by Lua each iteration (wraps at 256)
            //   data[1..3] – sentinel 0xAA, must never be written to
            //
            // The LOCKED flag is a cooperative advisory signal, not a mutex. C# must spin
            // until the flag clears before reading. The test verifies that sentinel bytes
            // are not corrupted and that LOCKED is correctly set during every access.
            var engine = new KitsuneEngine();
            try
            {
                const byte FlagLocked = 0x01;   // KITSUNE_SHARED_MEMORY_FLAG_LOCKED = (1 << 0)
                const byte Sentinel = 0xAA;
                const int Iterations = 1000;

                LuaStream stream = engine.CreateStream(4);
                stream.Write([0, Sentinel, Sentinel, Sentinel]);
                stream.Seek(0, System.IO.SeekOrigin.Begin);

                engine.SetVariable("shmem", LuaValue.FromStream(stream));

                // Lua coroutine: read data[0], increment, write back — 1000× with Sleep(0) to yield.
                int id = engine.ExecuteString($$"""
                local n = 0
                for i = 1, {{Iterations}} do
                    shmem:Seek(0)
                    local chunk = shmem:Read(1)
                    local val   = chunk and string.byte(chunk, 1) or 0
                    shmem:Seek(0)
                    shmem:Write(string.char((val + 1) % 256))
                    n = n + 1
                    Sleep(0)
                end
                return n
                """);

                // C# test thread: poll stream.Flags and sentinel bytes while the coroutine runs.
                // Cooperative protocol: spin until LOCKED clears before reading the sentinels.
                int lockedObservations = 0;
                bool sentinelCorrupted = false;

                while (!engine.HasResult(id))
                {
                    byte flags;
                    do
                    {
                        flags = stream.Flags;
                        if ((flags & FlagLocked) != 0)
                        {
                            lockedObservations++;
                        }
                    }
                    while ((flags & FlagLocked) != 0);

                    stream.Seek(1, System.IO.SeekOrigin.Begin);
                    if (stream.ReadByte() != Sentinel ||
                        stream.ReadByte() != Sentinel ||
                        stream.ReadByte() != Sentinel)
                    {
                        sentinelCorrupted = true;
                        break;
                    }
                }

                engine.Wait(id);
                long luaCount = engine.GetResultVariable(id).Int64;

                sentinelCorrupted.ShouldBeFalse("sentinel bytes were overwritten — Lua wrote outside data[0]");
                luaCount.ShouldBe(Iterations, "Lua must complete all iterations without error");

                // lockedObservations may be zero when the LOCKED window is too narrow to
                // catch reliably; not a hard assertion.
                _ = lockedObservations;

                // Safe to dispose C# side after handoff; the block stays alive until Lua's GC releases it.
                stream.Dispose();
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        // -- New lifecycle: flag-based block management ---------------------------
        [Fact]
        public void LuaStream_FlagConstants_HaveCorrectBitValues()
        {
            LuaStream.FlagLocked.ShouldBe((byte)0x01);
            LuaStream.FlagReadOnly.ShouldBe((byte)0x04);
            LuaStream.FlagOwnerDisposed.ShouldBe((byte)0x10);
            LuaStream.FlagAccessorDisposed.ShouldBe((byte)0x20);
            LuaStream.FlagLuaReferenced.ShouldBe((byte)0x40);
        }

        [Fact]
        public void CreateStream_FreshBlock_AccessorDisposedFlagIsCleared()
        {
            // ACCESSOR_DISPOSED starts at 1 in the engine; the LuaStream constructor
            // clears it to signal C# has taken the accessor role.
            var engine = new KitsuneEngine();
            try
            {
                using LuaStream stream = engine.CreateStream(8);
                (stream.Flags & LuaStream.FlagAccessorDisposed).ShouldBe((byte)0,
                    "ACCESSOR_DISPOSED must be 0 while C# holds the stream");
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public void CreateStream_FreshBlock_LuaReferencedFlagIsNotSet()
        {
            // LUA_REFERENCED is set only when lua_push_sharedmemory_stream is called.
            // A newly allocated block that has never been passed to Lua must not have it.
            var engine = new KitsuneEngine();
            try
            {
                using LuaStream stream = engine.CreateStream(8);
                (stream.Flags & LuaStream.FlagLuaReferenced).ShouldBe((byte)0,
                    "FlagLuaReferenced must not be set before the block is passed to Lua");
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public void CreateStream_AfterSetVariable_LuaReferencedFlagIsSet()
        {
            // FlagLuaReferenced is set when the block is passed to Lua, so Dispose
            // knows Lua's GC will eventually release it.
            var engine = new KitsuneEngine();
            try
            {
                LuaStream stream = engine.CreateStream(4);
                engine.SetVariable("s", LuaValue.FromStream(stream));
                engine.Wait();

                // After SetVariable, FlagLuaReferenced must be set.
                (stream.Flags & LuaStream.FlagLuaReferenced).ShouldNotBe((byte)0,
                    "FlagLuaReferenced must be set after the block is passed to Lua");
                stream.Dispose();
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public async Task CreateStream_NotPassedToLua_DisposeIsSafe_EngineFunctional()
        {
            // Disposing a CreateStream block that was never given to Lua must be safe.
            // The engine must remain fully functional afterward.
            var engine = new KitsuneEngine();
            try
            {
                LuaStream stream = engine.CreateStream(16);
                stream.Write(Encoding.UTF8.GetBytes("never used"));
                stream.Dispose();  // should set both flags, not crash

                for (int i = 0; i < 3; i++)
                {
                    (await engine.ExecuteStringAsync($"return 'ok{i}'")).ShouldBe($"ok{i}");
                }

                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public async Task CreateStream_PassedToLua_DisposeSafeAfterHandoff()
        {
            // After passing to Lua, C# can Dispose its side; the block stays alive
            // until Lua's GC releases it.
            var engine = new KitsuneEngine();
            try
            {
                byte[] payload = Encoding.UTF8.GetBytes("csharp writes");
                LuaStream stream = engine.CreateStream(payload.Length);
                stream.Write(payload);

                engine.SetVariable("s", LuaValue.FromStream(stream));

                // C# disposes its side — ACCESSOR_DISPOSED is set; block stays alive for Lua.
                stream.Dispose();

                // Lua can still read from the block.
                string? result = await engine.ExecuteStringAsync("return s:Read()");
                result.ShouldBe("csharp writes");

                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public async Task CreateStream_CanBeCalledFromRegisterFunctionCallback()
        {
            // CreateStream may be called from inside a RegisterFunction callback.
            var engine = new KitsuneEngine();
            try
            {
                engine.RegisterFunction("MakeStream", args =>
                {
                    using LuaStream s = engine.CreateStream(5);
                    s.Write(Encoding.UTF8.GetBytes("inner"));

                    // Copy bytes to a plain byte[] before the stream is disposed by the
                    // using block, so FillNativeVariable doesn't read from a closed stream.
                    return LuaValue.FromStream(s.ToArray());
                });

                string? result = await engine.ExecuteStringAsync("return MakeStream():Read()");
                result.ShouldBe("inner");
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public void LuaStream_AfterDispose_FlagsReturnsZero()
        {
            // After Dispose, _blockPtr is cleared to IntPtr.Zero, so Flags returns 0.
            var engine = new KitsuneEngine();
            try
            {
                int id = engine.ExecuteString("return Stream.Create('test flags')");
                engine.Wait(id);
                LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;
                stream.Flags.ShouldNotBe((byte)0, "Flags must be non-zero before dispose");
                stream.Dispose();
                stream.Flags.ShouldBe((byte)0, "Flags must return 0 after dispose (_blockPtr cleared)");
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public void LuaStream_InboundFromLua_HasLuaReferencedFlagSet()
        {
            // A stream returned from a Lua coroutine must have FlagLuaReferenced set.
            var engine = new KitsuneEngine();
            try
            {
                int id = engine.ExecuteString("return Stream.Create('flag check')");
                engine.Wait(id);
                using LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;
                (stream.Flags & LuaStream.FlagLuaReferenced).ShouldNotBe((byte)0,
                    "FlagLuaReferenced must be set on blocks that came from Lua's outbound stream");
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public void LuaStream_InboundFromLua_AccessorDisposedFlagIsCleared()
        {
            // The LuaStream constructor clears ACCESSOR_DISPOSED when C# wraps the block.
            var engine = new KitsuneEngine();
            try
            {
                int id = engine.ExecuteString("return Stream.Create('accessor flag')");
                engine.Wait(id);
                using LuaStream stream = (LuaStream)engine.GetResultVariable(id).StreamValue!;
                (stream.Flags & LuaStream.FlagAccessorDisposed).ShouldBe((byte)0,
                    "ACCESSOR_DISPOSED must be 0 while C# holds the stream");
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public async Task CreateStream_ManyAllocationsDisposes_NoCrashOrLeak()
        {
            // Stress the new lifecycle: allocate many CreateStream blocks, some passed to
            // Lua, some not, all disposed. Engine must remain clean throughout.
            var engine = new KitsuneEngine();
            try
            {
                for (int i = 0; i < 50; i++)
                {
                    byte[] data = Encoding.UTF8.GetBytes($"iter{i:D3}");
                    LuaStream stream = engine.CreateStream(data.Length);
                    stream.Write(data);

                    if (i % 2 == 0)
                    {
                        // Pass to Lua, read back, dispose C# side.
                        engine.SetVariable("s", LuaValue.FromStream(stream));
                        string? r = await engine.ExecuteStringAsync("return s:Read()");
                        r.ShouldBe($"iter{i:D3}");
                        stream.Dispose();
                    }
                    else
                    {
                        // Never passed to Lua — dispose sets both flags.
                        stream.Dispose();
                    }
                }
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public async Task CreateStream_MultipleGcCycles_BlockSurvivesWhileCSharpHolds()
        {
            // Multiple forced GC cycles must not corrupt a held CreateStream block
            // that was also passed to Lua. The two-flag scheme keeps it alive.
            var engine = new KitsuneEngine();
            try
            {
                byte[] payload = Encoding.UTF8.GetBytes("survive gc");
                LuaStream stream = engine.CreateStream(payload.Length);
                stream.Write(payload);
                engine.SetVariable("s", LuaValue.FromStream(stream));

                for (int i = 0; i < 3; i++)
                {
                    GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking: true);
                    GC.WaitForPendingFinalizers();

                    // Run coroutines so the scheduler GCs Lua objects between rounds.
                    await engine.ExecuteStringAsync("return 'ping'");
                }

                // Block must still be readable from C# even after GC cycles and coroutine runs.
                Encoding.UTF8.GetString(stream.ToArray()).ShouldBe("survive gc");
                stream.Dispose();

                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        // -- RegisterSession ------------------------------------------------------
        [Fact]
        public async Task RegisterSession_MakesSessionTableAvailable()
        {
            // Session is not registered by default; RegisterSession must create the global.
            using KitsuneEngine engine = new();
            engine.RegisterSession();
            string? r = await engine.ExecuteStringAsync("return type(Session)");
            r.ShouldBe("table");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterSession_SessionConsoleHasPutFunction()
        {
            // Session.Console.Put is cross-platform; must be callable after RegisterSession.
            using KitsuneEngine engine = new();
            engine.RegisterSession();
            string? r = await engine.ExecuteStringAsync("return type(Session.Console.Put)");
            r.ShouldBe("function");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterSession_SessionDisplayHasGetScreenSize()
        {
            using KitsuneEngine engine = new();
            engine.RegisterSession();
            string? r = await engine.ExecuteStringAsync("return type(Session.Display.GetScreenSize)");
            r.ShouldBe("function");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterSession_SessionClipboardHasSetAndGet()
        {
            using KitsuneEngine engine = new();
            engine.RegisterSession();
            string? r = await engine.ExecuteStringAsync(
                "return tostring(type(Session.Clipboard.Set) == 'function' and type(Session.Clipboard.Get) == 'function')");
            r.ShouldBe("true");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- RunString / RunFile / RunFunction (sync blocking) --------------------
        [Fact]
        public void RunString_ReturnsStringResult()
        {
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return 'sync result'");
            result.Type.ShouldBe(LuaType.String);
            result.String.ShouldBe("sync result");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RunString_ReturnsNoneOnNoReturn()
        {
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("local x = 1");
            result.Type.ShouldBe(LuaType.None);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RunString_RuntimeError_ReturnsNone()
        {
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("error('sync boom')");
            result.Type.ShouldBe(LuaType.None);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RunString_WithArgs_ArgsAreVisible()
        {
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return ARGS[1] .. ':' .. ARGS[2]", "hello", "world");
            result.Type.ShouldBe(LuaType.String);
            result.String.ShouldBe("hello:world");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RunFile_ReturnsStringResult()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "return 'file sync result'");
                using KitsuneEngine engine = new();
                LuaValue result = engine.RunFile(path);
                result.Type.ShouldBe(LuaType.String);
                result.String.ShouldBe("file sync result");
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public void RunFunction_ReturnsStringResult()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function syncTarget() return 'fn sync result' end", fireAndForget: true);
            engine.Wait();
            LuaValue result = engine.RunFunction("syncTarget");
            result.Type.ShouldBe(LuaType.String);
            result.String.ShouldBe("fn sync result");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RunFunction_WithArgs_ReturnsResult()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function syncAdd(a, b) return a + b end", fireAndForget: true);
            engine.Wait();
            LuaValue result = engine.RunFunction("syncAdd", LuaValue.FromInt64(10), LuaValue.FromInt64(32));
            result.AsInt64.ShouldBe(42L);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Non-recursive guard: Execute* rejected inside registered functions ---
        [Fact]
        public void RegisterFunction_CallingRunString_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("TryRunString", _ =>
            {
                engine.RunString("return 'nested'");
                return LuaValue.None;
            });
            int id = engine.ExecuteString("TryRunString()");
            engine.Wait(id);
            (engine.GetError(id) ?? string.Empty).ShouldContain("registered function");
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RegisterFunction_CallingRunFile_ThrowsLuaException()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "return 'nested file'");
                using KitsuneEngine engine = new();
                engine.RegisterFunction("TryRunFile", _ =>
                {
                    engine.RunFile(path);
                    return LuaValue.None;
                });
                int id = engine.ExecuteString("TryRunFile()");
                engine.Wait(id);
                (engine.GetError(id) ?? string.Empty).ShouldContain("registered function");
                engine.ReleaseResult(id);
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public void RegisterFunction_CallingRunFunction_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function syncTarget() return 'target' end", fireAndForget: true);
            engine.Wait();
            engine.RegisterFunction("TryRunFunction", _ =>
            {
                engine.RunFunction("syncTarget");
                return LuaValue.None;
            });
            int id = engine.ExecuteString("TryRunFunction()");
            engine.Wait(id);
            (engine.GetError(id) ?? string.Empty).ShouldContain("registered function");
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RegisterFunction_CallingExecuteString_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("TryExecuteString", _ =>
            {
                engine.ExecuteString("return 'nested'");
                return LuaValue.None;
            });
            int id = engine.ExecuteString("TryExecuteString()");
            engine.Wait(id);
            (engine.GetError(id) ?? string.Empty).ShouldContain("registered function");
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RegisterFunction_CallingExecuteFile_ThrowsLuaException()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "return 'nested file'");
                using KitsuneEngine engine = new();
                engine.RegisterFunction("TryExecuteFile", _ =>
                {
                    engine.ExecuteFile(path);
                    return LuaValue.None;
                });
                int id = engine.ExecuteString("TryExecuteFile()");
                engine.Wait(id);
                (engine.GetError(id) ?? string.Empty).ShouldContain("registered function");
                engine.ReleaseResult(id);
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public void RegisterFunction_CallingExecuteFunction_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function syncTarget() return 'target' end", fireAndForget: true);
            engine.Wait();
            engine.RegisterFunction("TryExecuteFunction", _ =>
            {
                engine.ExecuteFunction("syncTarget");
                return LuaValue.None;
            });
            int id = engine.ExecuteString("TryExecuteFunction()");
            engine.Wait(id);
            (engine.GetError(id) ?? string.Empty).ShouldContain("registered function");
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Function results (LuaType.Function) -----------------------------------------
        // These tests exercise:
        //   (a) the explicit LUA_TFUNCTION branch in KitsuneGetResult that zeroes slot->result.integer
        //   (b) the pendingResults[] array in scheduler step 4 that defers FreeVariableData outside slotsLock
        //   (c) the KitsuneVariableChain deferred-free queue used by KitsuneVariableFree on non-scheduler threads

        [Fact]
        public void GetResultVariable_FunctionReturn_HasFunctionType()
        {
            // A coroutine that returns a Lua function must produce a result with LuaType.Function.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return function() return 42 end");
            engine.Wait(id);
            LuaValue result = engine.GetResultVariable(id);
            result.Type.ShouldBe(LuaType.Function);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetResultVariable_TableContainingFunction_FunctionEntryHasFunctionType()
        {
            // A table result containing a function value must preserve the entry as LuaType.Function.
            // This exercises FillKitsuneVariableFromStack + TableToLinkedList for function-valued nodes.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return { callback = function() return 99 end, n = 1 }");
            engine.Wait(id);
            LuaValue result = engine.GetResultVariable(id);
            result.Type.ShouldBe(LuaType.Table);
            result.Table.ShouldNotBeNull();
            result.Table!.ShouldContain(kvp => kvp.Key.String == "n" && kvp.Value.AsDouble == 1);
            result.Table!.Single(kvp => kvp.Key.String == "callback").Value.Type.ShouldBe(LuaType.Function);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RegisterFunction_ReceivesFunctionArg_HasFunctionType()
        {
            // A Lua function passed as an arg to a RegisterFunction callback arrives as LuaType.Function.
            // The registry ref is anchored by FillKitsuneVariableFromStack and freed by FreeVariableData
            // in LuaCFunctionWrapper after the callback returns (scheduler-thread fast path, no deferred queue).
            using KitsuneEngine engine = new();
            LuaValue? received = null;
            engine.RegisterFunction("CaptureFunc", args =>
            {
                received = args[0];
                return LuaValue.None;
            });
            int id = engine.ExecuteString("CaptureFunc(function() return 99 end)");
            engine.Wait(id);
            received.ShouldNotBeNull();
            received!.Value.Type.ShouldBe(LuaType.Function);
            engine.ReleaseResult(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ReleaseResult_FunctionResult_SchedulerFreesViaPendingResults_NoLeak()
        {
            // ReleaseResult marks the slot for scheduler cleanup without the C# side consuming
            // the result.  The scheduler's step-4 pendingResults array takes ownership of the
            // TFUNCTION result and calls FreeVariableData (→ luaL_unref) in phase 2 outside
            // slotsLock.  This is the specific code path changed by the pendingResults fix.
            var engine = new KitsuneEngine();
            try
            {
                for (int i = 0; i < 20; i++)
                {
                    int id = engine.ExecuteString("return function() end");
                    engine.Wait(id);
                    engine.HasResult(id).ShouldBeTrue();
                    engine.ReleaseResult(id);
                    DateTime deadline = DateTime.UtcNow.AddSeconds(5);
                    while (engine.HasResult(id) && DateTime.UtcNow < deadline)
                        Thread.Sleep(1);
                }
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public void ReleaseResult_TableWithFunctionResult_SchedulerFreesViaPendingResults_NoLeak()
        {
            // A table result containing function entries: FreeKVNode must luaL_unref every
            // nested function ref.  With the pendingResults fix this happens outside slotsLock
            // (phase 2), so concurrent KitsuneCancel / KitsuneGetActiveIds callers are not blocked.
            var engine = new KitsuneEngine();
            try
            {
                for (int i = 0; i < 20; i++)
                {
                    int id = engine.ExecuteString("return { f1 = function() end, f2 = function() end, n = 1 }");
                    engine.Wait(id);
                    engine.ReleaseResult(id);
                    DateTime deadline = DateTime.UtcNow.AddSeconds(5);
                    while (engine.HasResult(id) && DateTime.UtcNow < deadline)
                        Thread.Sleep(1);
                }
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public async Task DeferredFree_FunctionResultsFreedWhileSchedulerActive_NoLeak()
        {
            // Exercises the KitsuneVariableChain deferred-free queue: function results are
            // obtained via GetResultVariable while the scheduler is running a background coroutine,
            // then GC is forced so any LuaFunctionRef finalizers run and enqueue the native
            // variables.  ExecuteStringAsync guarantees at least one scheduler drain cycle before
            // Dispose so all queued luaL_unref calls complete while the Lua state is still live.
            var engine = new KitsuneEngine();
            try
            {
                int bgId = engine.ExecuteString("Sleep(2000)");
                SpinUntilRunning(engine);

                for (int i = 0; i < 10; i++)
                {
                    int id = engine.ExecuteString("return function() end");
                    engine.Wait(id);
                    _ = engine.GetResultVariable(id);
                }

                GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking: true);
                GC.WaitForPendingFinalizers();

                engine.Cancel(bgId);
                engine.Wait();
                // One scheduler cycle after GC drains anything enqueued by finalizers.
                await engine.ExecuteStringAsync("return 'drain'");

                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        [Fact]
        public async Task Stress_FunctionResults_ManyReleasedViaScheduler_NoLeak()
        {
            // 50 coroutines each returning a function released via ReleaseResult under concurrent
            // pressure — stresses pendingResults compaction across many scheduler cycles.
            var engine = new KitsuneEngine();
            try
            {
                const int count = 50;
                Task[] tasks = Enumerable.Range(0, count).Select(_ => Task.Run(() =>
                {
                    int id = engine.ExecuteString("return function() end");
                    engine.Wait(id);
                    engine.ReleaseResult(id);
                })).ToArray();
                await Task.WhenAll(tasks);

                DateTime deadline = DateTime.UtcNow.AddSeconds(10);
                while (engine.GetActiveIds().Length > 0 && DateTime.UtcNow < deadline)
                    Thread.Sleep(1);

                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine.Dispose();
            }
            ThrowIfLeaked(engine);
        }

        // -- Boundary / edge cases --------------------------------------------------------

        [Fact]
        public void GetError_NonExistentId_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            engine.GetError(99999).ShouldBeNull();
        }

        [Fact]
        public void GetResult_WhileCoroutineStillRunning_ReturnsNull()
        {
            // KitsuneGetResult returns NULL when done==0; the C# wrapper must surface this as null.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("while true do end");
            SpinUntilRunning(engine);
            try
            {
                engine.GetResult(id).ShouldBeNull();
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
        public void ReleaseResult_NonExistentId_IsNoOp()
        {
            using KitsuneEngine engine = new();
            Should.NotThrow(() => engine.ReleaseResult(99999));
        }

        [Fact]
        public void GetRuntime_NonExistentId_ReturnsZero()
        {
            using KitsuneEngine engine = new();
            engine.GetRuntime(99999).ShouldBe(0.0);
        }

        [Fact]
        public void GetResultVariable_FunctionResult_SlotReleasedAfterConsume()
        {
            // After GetResultVariable consumes the function result (sets released=1), the scheduler
            // must compact the slot.  The explicit LUA_TFUNCTION branch in KitsuneGetResult zeroes
            // slot->result.integer so no stale ref lingers after transfer.
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return function() end");
            engine.Wait(id);
            engine.HasResult(id).ShouldBeTrue();
            _ = engine.GetResultVariable(id);  // consumes result, sets released=1

            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.HasResult(id) && DateTime.UtcNow < deadline)
                Thread.Sleep(1);

            engine.HasResult(id).ShouldBeFalse();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetResultVariable_FunctionReturn_ConsumedTwice_ReturnsNoneSecondTime()
        {
            // Consuming a function result twice must not double-unref the registry entry.
            // The second call must return LuaType.None (slot already released).
            using KitsuneEngine engine = new();
            int id = engine.ExecuteString("return function() end");
            engine.Wait(id);
            LuaValue first = engine.GetResultVariable(id);
            first.Type.ShouldBe(LuaType.Function);
            LuaValue second = engine.GetResultVariable(id);
            second.Type.ShouldBe(LuaType.None);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        private static void SpinUntilRunning(KitsuneEngine engine, int timeoutMs = 2000)
        {
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (!engine.IsRunning && DateTime.UtcNow < deadline)
            {
                Thread.Sleep(1);
            }
        }

        private static void SpinUntilHasResult(KitsuneEngine engine, int id, int timeoutMs = 2000)
        {
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (!engine.HasResult(id) && DateTime.UtcNow < deadline)
            {
                Thread.Sleep(1);
            }
        }

        private static void ThrowIfLeaked(KitsuneEngine engine)
        {
            if (engine.LeakedAllocations != 0)
            {
                throw new InvalidOperationException($"Native memory leak: {engine.LeakedAllocations} unfreed allocation(s) after KitsuneCleanup");
            }
        }
    }
}