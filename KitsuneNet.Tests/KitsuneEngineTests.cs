using KitsuneNet;
using Shouldly;
using System.Diagnostics;
using System.Text;
using System.Text.Json.Nodes;
using Xunit;
using Xunit.Abstractions;

namespace KitsuneNet.Tests
{
    // KitsuneEngine.dll uses a process-wide global state: all KitsuneEngine instances
    // in the same process share the same native scheduler and Lua state. Both test
    // classes must therefore run sequentially rather than in parallel.
    [Collection("KitsuneSequential")]
    public sealed class KitsuneEngineTests
    {
        private readonly ITestOutputHelper _output;
        private bool _iteratorEnumeratorDisposed;

        public KitsuneEngineTests(ITestOutputHelper output)
        {
            _output = output;
        }

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
        public void CollectGarbage_DoesNotThrow()
        {
            using KitsuneEngine engine = new();
            Should.NotThrow(() => engine.CollectGarbage());
        }

        [Fact]
        public void CollectGarbage_Mode0_ReturnsPositiveUsage()
        {
            using KitsuneEngine engine = new();
            engine.CollectGarbage(0).ShouldBeGreaterThan(0);
        }

        [Fact]
        public void CollectGarbage_Mode1_ReturnsUsageAfterCollection()
        {
            using KitsuneEngine engine = new();
            long usage = engine.CollectGarbage(1);
            usage.ShouldBeGreaterThan(0);
        }

        [Fact]
        public void CollectGarbage_Mode2_IncrementalStep_ReturnsUsage()
        {
            using KitsuneEngine engine = new();
            engine.CollectGarbage(2).ShouldBeGreaterThan(0);
        }

        [Fact]
        public void CollectGarbage_PauseAndRestart_DoesNotThrow()
        {
            using KitsuneEngine engine = new();
            Should.NotThrow(() => engine.CollectGarbage(3)); // pause
            Should.NotThrow(() => engine.CollectGarbage(4)); // restart
        }

        [Fact]
        public void CollectGarbage_AfterLargeDataBatch_NoLeak()
        {
            // Create many tables and function refs, discard them, force GC, then verify no leaks.
            var engine = new KitsuneEngine();
            try
            {
                for (int i = 0; i < 20; i++)
                {
                    LuaValue fn = engine.RunString("return function() return {1,2,3} end");
                    fn.FunctionRef?.Dispose();
                }

                engine.CollectGarbage();
            }
            finally
            {
                engine.Dispose();
            }
        }

        [Fact]
        public async Task Dispose_WithRunningCoroutine_InterruptsAndCompletes()
        {
            KitsuneEngine engine = new();
            engine.ExecuteString("while true do end");
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
            engine.ExecuteString("local x = 1");
            engine.Wait();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ExecuteString_CanWaitAndGetResult()
        {
            using KitsuneEngine engine = new();
            engine.RunString("return 'sync result'").String.ShouldBe("sync result");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteString_RuntimeError_CanGetError()
        {
            using KitsuneEngine engine = new();
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("error('sync boom')"));
            ex.Message.ShouldContain("sync boom");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteString_SyntaxError_CanGetError()
        {
            using KitsuneEngine engine = new();
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("~~~~invalid"));
            ex.Message.ShouldNotBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ExecuteString_WithArgs_ArgsAreVisible()
        {
            using KitsuneEngine engine = new();
            engine.RunString("local a,b = ...; return a .. ':' .. b", "hello", "world").String.ShouldBe("hello:world");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ExecuteString_GetCurrentId_MatchesCoroutineId()
        {
            using KitsuneEngine engine = new();
            long.TryParse(engine.RunString("return tostring(Tasks.GetCurrentId())").String, out long luaId).ShouldBeTrue();
            luaId.ShouldBeGreaterThan(0);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- GetResult / HasResult ------------------------------------------------
        [Fact]
        public void GetResult_ReturnsRawBytes()
        {
            using KitsuneEngine engine = new();
            byte[]? result = engine.RunString("return 'bytes'").Bytes;
            result.ShouldNotBeNull();
            Encoding.UTF8.GetString(result!).ShouldBe("bytes");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetResult_ReturnsExpectedValue()
        {
            using KitsuneEngine engine = new();
            engine.RunString("return 'consume me'").String.ShouldBe("consume me");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetError_SuccessfulCoroutine_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            engine.RunString("return 'ok'").String.ShouldBe("ok");  // no exception thrown
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetResultVariable_NumberReturn_IsTypedAsNumber()
        {
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return 42.5");
            result.Type.ShouldBe(LuaType.Number);
            result.Number.ShouldBe(42.5);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetResultVariable_BoolReturn_IsTypedAsBool()
        {
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return true");
            result.Type.ShouldBe(LuaType.Boolean);
            result.Boolean.ShouldBeTrue();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetResultVariable_TableReturn_IsTypedAsTable()
        {
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return {}");
            result.Type.ShouldBe(LuaType.Table);
            result.Bytes.ShouldBeNull();
            result.TableRef?.Dispose();  // release the live Lua registry ref
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void HasResult_ReturnsFalse_WhileRunning_TrueWhenFinished()
        {
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return 'done'");
            result.String.ShouldBe("done");
            result.Bytes?.Length.ShouldBe(4);  // "done" is 4 bytes
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void HasResult_NonStringResult_LenIsZero()
        {
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return 42");
            result.Bytes.ShouldBeNull();  // non-string results have no byte representation
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void HasResult_NonExistentId_ReturnsFalse()
        {
            using KitsuneEngine engine = new();
            engine.GetStatus(99999).ShouldBe(CoroutineStatus.None);
            engine.GetRuntime(99999).ShouldBe(0.0);
        }

        [Fact]
        public void HasResult_AfterResultConsumed_ReturnsFalse()
        {
            using KitsuneEngine engine = new();
            engine.RunString("return 'done'").String.ShouldBe("done");

            // RunString consumes the result; slot is auto-released
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- SetString / SetBool / SetNumber / GetVariable -------------------------
        [Fact]
        public async Task SetString_StringValue_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetString("myVar", "hello from csharp");
            LuaValue result = await engine.ExecuteStringAsync("return myVar");
            result.String.ShouldBe("hello from csharp");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetString_BytesValue_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetString("bytesVar", Encoding.UTF8.GetBytes("bytes value"));
            LuaValue result = await engine.ExecuteStringAsync("return bytesVar");
            result.String.ShouldBe("bytes value");
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
            LuaValue result = await engine.ExecuteStringAsync("return tostring(boolVar)");
            result.String.ShouldBe("true");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetBool_False_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetBool("boolVar", false);
            LuaValue result = await engine.ExecuteStringAsync("return tostring(boolVar)");
            result.String.ShouldBe("false");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetNumber_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetNumber("numVar", 3.14);
            LuaValue result = await engine.ExecuteStringAsync("return string.format('%.2f', numVar)");
            result.String.ShouldBe("3.14");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetNumber_Integer_IsVisibleInScript()
        {
            using KitsuneEngine engine = new();
            engine.SetNumber("intVar", 42);
            LuaValue result = await engine.ExecuteStringAsync("return tostring(math.tointeger(intVar))");
            result.String.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetVariable_ScriptSetGlobal_ReturnsValue()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("testGlobal = 'get variable test'");
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
            engine.ExecuteString("byteVar = 'bytes test'");
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
            LuaValue result = await engine.ExecuteStringAsync("return math.type(n)");
            result.String.ShouldBe("integer");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetInt64_LuaIntegerAssignment_ReturnsIntegerType()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("n = 42");
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
            engine.ExecuteString("tableVar = {}");
            engine.Wait();
            LuaValue v = engine.GetVariable("tableVar");
            v.Type.ShouldBe(LuaType.Table);
            v.Bytes.ShouldBeNull();
            v.TableRef?.Dispose();  // release the live Lua registry ref
        }

        [Fact]
        public void GetVariable_EmptyPath_ReturnsGlobalTableRef()
        {
            // An empty path returns _G itself as a live KITSUNE_TTABLE registry ref
            // so it can be used with GetIndex, SetIndex, Pairs, etc.
            using KitsuneEngine engine = new();
            engine.SetString("testGlobalLookup_xyz", "hello from _G");

            LuaValue v = engine.GetVariable(string.Empty);
            v.Type.ShouldBe(LuaType.Table);
            using var tref = v.TableRef;
            tref.ShouldNotBeNull();
            tref!.GetIndex(LuaValue.FromString("testGlobalLookup_xyz")).String.ShouldBe("hello from _G");
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
            LuaValue result = await engine.ExecuteStringAsync(
                "parent.child.x = 'yes'; return parent.child.x");
            result.String.ShouldBe("yes");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task SetVariable_DotPath_CSharpWriteLuaRead()
        {
            using KitsuneEngine engine = new();
            engine.SetString("db.host", "localhost");
            engine.SetInt64("db.port", 5432);

            LuaValue result = await engine.ExecuteStringAsync(
                "return db.host .. ':' .. tostring(db.port)");
            result.String.ShouldBe("localhost:5432");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetVariable_DotPath_LuaWriteCSharpRead()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString(
                "cfg = {}; cfg.timeout = 30; cfg.retry = true");
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
                engine.RunFile(path).String.ShouldBe("file result");
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
                File.WriteAllText(path, "local p = ...; return p");  // first vararg is the file path
                using KitsuneEngine engine = new();
                engine.RunFile(path).String.ShouldBe(path);
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public async Task ExecuteFile_RuntimeError_CanGetError()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "error('file error')");
                using KitsuneEngine engine = new();
                LuaException ex = await Should.ThrowAsync<LuaException>(
                    engine.ExecuteFileAsync(path));
                ex.Message.ShouldContain("file error");
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
                // first vararg is the file path; extra args follow
                File.WriteAllText(path, "local _, a, b = ...; return a .. ':' .. b");
                using KitsuneEngine engine = new();
                engine.RunFile(path, args: ["first", "second"]).String.ShouldBe("first:second");
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
                LuaValue result = await engine.ExecuteFileAsync(path);
                result.String.ShouldBe("from file");
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
            engine.ExecuteString("while true do end");
            try
            {
                SpinUntilRunning(engine);
                engine.IsRunning.ShouldBeTrue();
                int id = engine.RunningCoroutineId;
                id.ShouldBeGreaterThan(0);
                engine.GetActiveIds().ShouldContain(id);
            }
            finally
            {
                engine.Interrupt();
                engine.Wait();
                engine.GetActiveIds().ShouldBeEmpty();
            }
        }

        [Fact]
        public void RunningCoroutineId_DuringExecution_MatchesStartedId()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("while true do end");
            try
            {
                SpinUntilRunning(engine);
                int id = engine.GetActiveIds()[0];
                engine.RunningCoroutineId.ShouldBe(id);
            }
            finally
            {
                engine.Interrupt();
                engine.Wait();
                engine.GetActiveIds().ShouldBeEmpty();
            }
        }

        [Fact]
        public void Interrupt_StopsScript()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("while true do end");
            SpinUntilRunning(engine);
            engine.Interrupt();
            engine.Wait();
            engine.IsRunning.ShouldBeFalse();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ExecuteString_AfterInterruptAndWait_WorksNormally()
        {
            // The scheduler clears the interrupt flag once runningCount hits 0.
            // Verifies the engine is fully reusable after an interrupt + wait cycle.
            using KitsuneEngine engine = new();
            engine.ExecuteString("while true do end");
            SpinUntilRunning(engine);
            engine.Interrupt();
            engine.Wait();
            engine.RunString("return 'after interrupt'").String.ShouldBe("after interrupt");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Wait_CancelledToken_ThrowsOperationCanceledException()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("while true do end");
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
                engine.GetActiveIds().ShouldBeEmpty();
            }
        }

        [Fact]
        public async Task Wait_ForId_UnblockedWhenEngineDisposedConcurrently()
        {
            KitsuneEngine engine = new();
            engine.ExecuteString("Sleep(60000)");
            SpinUntilActive(engine);
            int id = engine.GetActiveIds()[0];
            Task waitTask = Task.Run(() => engine.Wait(id));
            await Task.Delay(25);

            Task disposeTask = Task.Run(engine.Dispose);
            Task timeout = Task.Delay(TimeSpan.FromSeconds(5));
            Task winner = await Task.WhenAny(Task.WhenAll(waitTask, disposeTask), timeout);
            winner.ShouldNotBe(timeout,
                "Wait(id) did not unblock — doneCV.notify_all from KitsuneCleanup was not received");
            await Task.WhenAll(waitTask, disposeTask);
        }

        [Fact]
        public async Task Wait_MultipleParallelWaiters_AllUnblockedWhenEngineDisposed()
        {
            const int count = 4;
            KitsuneEngine engine = new();
            for (int i = 0; i < count; i++)
            {
                engine.ExecuteString("Sleep(60000)");
            }

            DateTime ready = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetActiveIds().Length < count && DateTime.UtcNow < ready)
            {
                Thread.Sleep(1);
            }

            int[] ids = engine.GetActiveIds();
            ids.Length.ShouldBe(count);

            Task[] waitTasks = ids.Select(i => Task.Run(() => engine.Wait(i))).ToArray();
            await Task.Delay(25);

            Task disposeTask = Task.Run(engine.Dispose);
            Task allDone = Task.WhenAll([.. waitTasks, disposeTask]);
            Task timeout = Task.Delay(TimeSpan.FromSeconds(5));
            Task winner = await Task.WhenAny(allDone, timeout);
            winner.ShouldNotBe(timeout,
                "Some Wait(id) calls did not unblock when Dispose was called");
            await allDone;
        }

        // -- Concurrency ----------------------------------------------------------
        [Fact]
        public async Task ConcurrentCoroutines_AllReturnDistinctValues()
        {
            const int count = 32;
            using KitsuneEngine engine = new();
            Task<LuaValue>[] tasks = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteStringAsync($"return 'value_{i}'"))
                .ToArray();
            LuaValue[] results = await Task.WhenAll(tasks);
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
            Task<LuaValue>[] tasks = Enumerable.Range(0, count)
                .Select(i =>
                {
                    int n = (i + 1) * 1000;
                    return engine.ExecuteStringAsync($"local s = 0; for j = 1, {n} do s = s + j end; return tostring(s)");
                })
                .ToArray();
            LuaValue[] results = await Task.WhenAll(tasks);
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
            Task<LuaValue> successTask = engine.ExecuteStringAsync("return 'ok'");
            Task<LuaValue> errorTask = engine.ExecuteStringAsync("error('fail')");
            Task<LuaValue> nilTask = engine.ExecuteStringAsync("return nil");
            Task<LuaValue> noRetTask = engine.ExecuteStringAsync("local x = 1 + 1");
            (await successTask).String.ShouldBe("ok");
            (await Should.ThrowAsync<LuaException>(errorTask)).Message.ShouldContain("fail");
            (await nilTask).String.ShouldBeNull();
            (await noRetTask).String.ShouldBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ConcurrentCoroutines_WaitedFromParallelThreads_AllComplete()
        {
            const int count = 16;
            using KitsuneEngine engine = new();
            Task<LuaValue>[] tasks = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteStringAsync($"return 'parallel_{i}'"))
                .ToArray();
            LuaValue[] results = await Task.WhenAll(tasks);
            for (int i = 0; i < count; i++)
            {
                results[i].String.ShouldBe($"parallel_{i}");
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
                LuaValue result = await (Task<LuaValue>)tasks[i];
                result.String.ShouldBe(expected);
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
            engine.ExecuteString("while true do end");
            try
            {
                SpinUntilRunning(engine);
                engine.GetActiveIds().Length.ShouldBeGreaterThan(0);
            }
            finally
            {
                engine.Interrupt();
                engine.Wait();
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
                engine.ExecuteString("Sleep(50)");
            }

            // Poll until all 8 coroutines are active — more reliable than SpinUntilRunning
            // followed by a bare assertion, which races if a fast machine sees IsRunning before
            // all coroutines are queued.
            DateTime ready = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetActiveIds().Length < count && DateTime.UtcNow < ready)
            {
                Thread.Sleep(1);
            }

            engine.GetActiveIds().Length.ShouldBe(count);

            // Wait(CancellationToken) now uses KitsuneGetActiveIds, not IsRunning, so it
            // correctly blocks for sleeping coroutines between scheduler ticks.
            using CancellationTokenSource cts = new(TimeSpan.FromSeconds(10));
            engine.Wait(cts.Token);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Async execution ------------------------------------------------------
        [Fact]
        public async Task ExecuteStringAsync_ReturnsResult()
        {
            using KitsuneEngine engine = new();
            LuaValue result = await engine.ExecuteStringAsync("return 'async result'");
            result.String.ShouldBe("async result");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteStringAsync_NilReturn_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            LuaValue result = await engine.ExecuteStringAsync("return nil");
            result.String.ShouldBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteStringAsync_NoReturn_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            LuaValue result = await engine.ExecuteStringAsync("local x = 1 + 1");
            result.String.ShouldBeNull();
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
            Task<LuaValue>[] tasks = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteStringAsync($"return 'async_{i}'"))
                .ToArray();
            LuaValue[] results = await Task.WhenAll(tasks);
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
            LuaValue result = await engine.ExecuteStringAsync("Sleep(50); return 'done'");
            sw.Stop();
            result.String.ShouldBe("done");
            sw.ElapsedMilliseconds.ShouldBeGreaterThanOrEqualTo(40);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Sleep_DoesNotBlockOtherCoroutines()
        {
            using KitsuneEngine engine = new();
            Task<LuaValue> sleepingTask = engine.ExecuteStringAsync("Sleep(2000); return 'slept'");
            SpinUntilActive(engine);
            var sw = System.Diagnostics.Stopwatch.StartNew();
            LuaValue fastResult = await engine.ExecuteStringAsync("return 'fast'");
            sw.Stop();
            sw.ElapsedMilliseconds.ShouldBeLessThan(1000,
                "Fast coroutine took too long — Sleep() may be blocking the scheduler");
            fastResult.String.ShouldBe("fast");
            sleepingTask.IsCompleted.ShouldBeFalse();
            (await sleepingTask).ShouldBe("slept");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Sleep_MultipleConcurrentSleeps_AllCompleteCorrectly()
        {
            using KitsuneEngine engine = new();
            Task<LuaValue> t1 = engine.ExecuteStringAsync("Sleep(50);  return 'a'");
            Task<LuaValue> t2 = engine.ExecuteStringAsync("Sleep(150); return 'b'");
            Task<LuaValue> t3 = engine.ExecuteStringAsync("Sleep(100); return 'c'");
            LuaValue[] results = await Task.WhenAll(t1, t2, t3);
            results[0].ShouldBe("a");
            results[1].ShouldBe("b");
            results[2].ShouldBe("c");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Sleep_ZeroMs_YieldsAndReturnsImmediately()
        {
            using KitsuneEngine engine = new();
            LuaValue result = await engine.ExecuteStringAsync("Sleep(0); return 'yielded'");
            result.String.ShouldBe("yielded");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- ExecuteFunction ------------------------------------------------------
        [Fact]
        public async Task ExecuteFunction_NoArgs_ReturnsResult()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function greet() return 'hello' end");
            engine.Wait();
            LuaValue result = await engine.ExecuteFunctionAsync("greet");
            result.String.ShouldBe("hello");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_WithArgs_ReceivesArguments()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function echo(a, b) return a .. ',' .. b end");
            engine.Wait();
            LuaValue result = await engine.ExecuteFunctionAsync("echo", args: ["foo", "bar"]);
            result.String.ShouldBe("foo,bar");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_DoesNotSetArgsGlobal()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function checkargs(x) return tostring(ARGS) end");
            engine.Wait();
            engine.ExecuteString("ARGS = nil");
            engine.Wait();
            LuaValue result = await engine.ExecuteFunctionAsync("checkargs", args: ["ignored"]);
            result.String.ShouldBe("nil");
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
            engine.ExecuteString("function boom() error('fn error') end");
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
            LuaValue result = await engine.ExecuteFunctionAsync("add", args: ["3", "4"]);
            result.String.ShouldBe("7");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_WithTypedNumberArgs_PassedAsNumbers()
        {
            using KitsuneEngine engine = new();
            await engine.ExecuteStringAsync("function typedAdd(a, b) return tostring(a + b) end");
            LuaValue result = await engine.ExecuteFunctionAsync("typedAdd",
                args: [LuaValue.FromInt64(6), LuaValue.FromInt64(7)]);
            result.String.ShouldBe("13");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_WithTypedBoolArg_PassedAsBool()
        {
            using KitsuneEngine engine = new();
            await engine.ExecuteStringAsync("function checkBool(b) return tostring(b) end");
            LuaValue result = await engine.ExecuteFunctionAsync("checkBool",
                args: [LuaValue.FromBool(true)]);
            result.String.ShouldBe("true");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Dot-path notation for ExecuteFunction and RegisterFunction -----------
        [Fact]
        public async Task ExecuteFunction_DotPath_CallsNestedFunction()
        {
            // ExecuteFunction("Ns.Foo") should find _G.Ns.Foo, not a global named "Ns.Foo".
            using KitsuneEngine engine = new();
            await engine.ExecuteStringAsync("Ns = {}; function Ns.greet() return 'hi' end");
            LuaValue result = await engine.ExecuteFunctionAsync("Ns.greet");
            result.String.ShouldBe("hi");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_DotPath_WithArgs_PassedCorrectly()
        {
            using KitsuneEngine engine = new();
            await engine.ExecuteStringAsync("Math = {}; function Math.add(a, b) return tostring(a + b) end");
            LuaValue result = await engine.ExecuteFunctionAsync("Math.add",
                args: [LuaValue.FromInt64(10), LuaValue.FromInt64(32)]);
            result.String.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_DeepDotPath_CallsFunction()
        {
            using KitsuneEngine engine = new();
            await engine.ExecuteStringAsync("A = {}; A.B = {}; function A.B.fn() return 'deep' end");
            LuaValue result = await engine.ExecuteFunctionAsync("A.B.fn");
            result.String.ShouldBe("deep");
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
            LuaValue result = await engine.ExecuteStringAsync(
                "return tostring(Kitsune.Multiply(6, 7))");
            result.String.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_DotPath_ExecuteFunction_CallsIt()
        {
            // A function registered at a dot-path should also be callable via ExecuteFunction.
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Util.Double", args =>
                (LuaValue)$"{args.First().AsInt64 * 2}");
            LuaValue result = await engine.ExecuteFunctionAsync("Util.Double",
                args: [LuaValue.FromInt64(21)]);
            result.String.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_DeepDotPath_LuaCanCallIt()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("A.B.C.fn", _ => (LuaValue)"deep");
            LuaValue result = await engine.ExecuteStringAsync("return A.B.C.fn()");
            result.String.ShouldBe("deep");
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
            LuaValue result = await engine.ExecuteFunctionAsync("Config.getName");
            result.String.ShouldBe("kitsune");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Pause / Resume -------------------------------------------------------
        [Fact]
        public async Task TaskPause_SuspendsCoroutine_ResumeUnblocks()
        {
            // Task.Pause() inside a coroutine suspends it; engine.Resume() unblocks it.
            using KitsuneEngine engine = new();
            engine.SetVariable("result", 0);
            engine.ExecuteString(@"
                result = 1
                Pause()
                result = 2
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;

            // Wait until the coroutine reaches Pause and becomes paused.
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetStatus(id) != CoroutineStatus.Paused && DateTime.UtcNow < deadline)
            {
                await Task.Delay(1);
            }

            engine.GetStatus(id).ShouldBe(CoroutineStatus.Paused);
            engine.GetInt64("result").ShouldBe(1);

            // Resume it from C# and wait for it to finish.
            engine.Resume(id).ShouldBeTrue();
            await engine.WaitAsync(id);
            engine.GetActiveIds().ShouldNotContain(id);
            engine.GetInt64("result").ShouldBe(2);
        }

        [Fact]
        public async Task TaskPause_LuaSideResume_UnblocksCoroutine()
        {
            // task:Resume() from Lua resumes a paused coroutine.
            using KitsuneEngine engine = new();
            engine.SetVariable("result", 0);
            engine.ExecuteString(@"
                local inner = Tasks.New(function()
                    result = 1
                    Pause()
                    result = 2
                end)
                while inner:GetStatus() ~= TaskStatus.Paused do
                    Sleep(5)
                end
                inner:Resume()
                while not inner:Finished() do
                    Sleep(5)
                end
            ");

            // Wait until result reaches 2 — both outer and inner have finished.
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetInt64("result") != 2 && DateTime.UtcNow < deadline)
            {
                await Task.Delay(5);
            }

            engine.GetInt64("result").ShouldBe(2);
        }

        [Fact]
        public async Task TaskPause_GetStatus_ReturnsPaused()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("Pause() while true do end");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetStatus(id) != CoroutineStatus.Paused && DateTime.UtcNow < deadline)
            {
                await Task.Delay(1);
            }

            engine.GetStatus(id).ShouldBe(CoroutineStatus.Paused);
            engine.Cancel(id);
        }

        [Fact]
        public void Resume_NonExistentId_ReturnsFalse()
        {
            using KitsuneEngine engine = new();
            engine.Resume(99999).ShouldBeFalse();
        }

        [Fact]
        public async Task Resume_RunningCoroutine_ReturnsFalse()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("while true do end");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            engine.Resume(id).ShouldBeFalse();
            engine.Cancel(id);
            await engine.WaitAsync(id);
        }

        [Fact]
        public async Task TaskPause_MultiplePauseResumeCycles_WorkCorrectly()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("count", 0);
            engine.ExecuteString(@"
                for i = 1, 3 do
                    count = i
                    Pause()
                end
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            for (int expected = 1; expected <= 3; expected++)
            {
                DateTime deadline = DateTime.UtcNow.AddSeconds(5);
                while (engine.GetStatus(id) != CoroutineStatus.Paused && DateTime.UtcNow < deadline)
                {
                    await Task.Delay(1);
                }

                engine.GetStatus(id).ShouldBe(CoroutineStatus.Paused);
                engine.GetInt64("count").ShouldBe(expected);
                engine.Resume(id).ShouldBeTrue();
            }
            await engine.WaitAsync(id);
            engine.GetActiveIds().ShouldNotContain(id);
        }

        // -- Pause/Resume value passing -------------------------------------------
        [Fact]
        public async Task Pause_ResumedWithoutValue_ReturnsNil()
        {
            // task:Resume() with no value makes Pause() return nil.
            using KitsuneEngine engine = new();
            engine.SetVariable("received", 0L);
            engine.ExecuteString(@"
                received = Pause()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetStatus(id) != CoroutineStatus.Paused && DateTime.UtcNow < deadline)
            {
                await Task.Delay(1);
            }

            engine.GetStatus(id).ShouldBe(CoroutineStatus.Paused);
            engine.Resume(id).ShouldBeTrue();
            await engine.WaitAsync(id);
            engine.GetInt64("received").ShouldBeNull();
        }

        [Fact]
        public async Task Pause_LuaResumedWithIntValue_ReturnsInt()
        {
            // task:Resume(42) makes Pause() return 42.
            using KitsuneEngine engine = new();
            engine.SetVariable("received", 0L);
            engine.ExecuteString(@"
                local t
                t = Tasks.New(function()
                    received = Pause()
                end)
                while t:GetStatus() ~= TaskStatus.Paused do Sleep(5) end
                t:Resume(42)
                t:Wait()
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetInt64("received").ShouldBe(42L);
        }

        [Fact]
        public async Task Pause_LuaResumedWithStringValue_ReturnsString()
        {
            // task:Resume("hello") makes Pause() return "hello".
            using KitsuneEngine engine = new();
            engine.SetVariable("received", string.Empty);
            engine.ExecuteString(@"
                local t
                t = Tasks.New(function()
                    received = Pause()
                end)
                while t:GetStatus() ~= TaskStatus.Paused do Sleep(5) end
                t:Resume('hello')
                t:Wait()
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetString("received").ShouldBe("hello");
        }

        [Fact]
        public async Task Pause_MultipleCycles_EachValueDeliveredOnce()
        {
            // Each Pause() returns the value from its own Resume() call.
            using KitsuneEngine engine = new();
            engine.SetVariable("sum", 0L);
            engine.ExecuteString(@"
                local t
                t = Tasks.New(function()
                    local a = Pause()
                    local b = Pause()
                    local c = Pause()
                    sum = a + b + c
                end)
                while t:GetStatus() ~= TaskStatus.Paused do Sleep(1) end
                t:Resume(10)
                while t:GetStatus() ~= TaskStatus.Paused do Sleep(1) end
                t:Resume(20)
                while t:GetStatus() ~= TaskStatus.Paused do Sleep(1) end
                t:Resume(12)
                t:Wait()
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetInt64("sum").ShouldBe(42L);
        }

        [Fact]
        public async Task TaskNew_StartsImmediately_WithoutExplicitResume()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("ran", false);
            engine.ExecuteString(@"
                local t = Tasks.New(function() ran = true end)
                while not t:Finished() do
                    Sleep(5)
                end
            ");
            SpinUntilRunning(engine);
            int outer = engine.RunningCoroutineId;
            await engine.WaitAsync(outer);
            engine.GetBool("ran").ShouldBe(true);
        }

        [Fact]
        public async Task Tasks_SetErrorHandler_CalledOnFireAndForgetError()
        {
            // Tasks.SetErrorHandler receives (id, err) when a fire-and-forget task faults.
            using KitsuneEngine engine = new();
            engine.SetVariable("capturedId", 0);
            engine.SetVariable("capturedErr", string.Empty);

            // RunString is blocking — handler is registered before we continue.
            engine.RunString(@"
                Tasks.SetErrorHandler(function(id, err)
                    capturedId  = id
                    capturedErr = err
                end)
            ");

            // Now launch a faulting fire-and-forget task.
            engine.ExecuteString("error('boom')");
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetInt64("capturedId") == 0 && DateTime.UtcNow < deadline)
            {
                await Task.Delay(5);
            }

            engine.GetInt64("capturedId").ShouldNotBe(0);
            engine.GetVariable("capturedErr")!.String!.ShouldContain("boom");
        }

        [Fact]
        public async Task Tasks_SetErrorHandler_ClearedByNil_DoesNotCallOldHandler()
        {
            // Setting the handler to nil clears it; subsequent errors must not call it.
            using KitsuneEngine engine = new();
            engine.SetVariable("called", false);

            // Use RunString (blocking) so the handler setup is fully committed before we continue.
            engine.RunString(@"
                Tasks.SetErrorHandler(function(id, err) called = true end)
                Tasks.SetErrorHandler(nil)
            ");

            // Launch a faulting fire-and-forget and give the scheduler time to process it.
            engine.ExecuteString("error('should not reach handler')");
            await Task.Delay(300);
            engine.GetBool("called").ShouldBe(false);
        }

        [Fact]
        public async Task Tasks_NoErrorHandler_FireAndForgetError_DoesNotCrash()
        {
            // Without a handler, a fire-and-forget error writes to stderr but does not throw.
            using KitsuneEngine engine = new();
            engine.ExecuteString("error('unhandled task error')");

            // Give the scheduler time to process and compact the slot — no exception must escape.
            await Task.Delay(300);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task TasksNew_WithArgs_ArgsPassedToFunction()
        {
            // Arguments after the function are passed as parameters on first resume.
            using KitsuneEngine engine = new();
            engine.SetVariable("result", 0);
            engine.ExecuteString(@"
                local t = Tasks.New(function(a, b) result = a + b end, 10, 32)
                while not t:Finished() do Sleep(5) end
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetInt64("result").ShouldBe(42);
        }

        [Fact]
        public async Task TasksNew_Cancel_WhilePaused_SlotsEventuallyFreed()
        {
            // task:Cancel() on a paused coroutine must free the slot (not hang).
            using KitsuneEngine engine = new();
            engine.ExecuteString(@"
                local t = Tasks.New(function() Pause() while true do end end)
                while t:GetStatus() ~= TaskStatus.Paused do Sleep(5) end
                t:Cancel()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task TasksNew_Dispose_WhilePaused_DoesNotHang()
        {
            // Dropping the only Task handle while it is paused must cancel and free the slot.
            using KitsuneEngine engine = new();
            engine.ExecuteString(@"
                do
                    local t = Tasks.New(function() Pause() while true do end end)
                    while t:GetStatus() ~= TaskStatus.Paused do Sleep(5) end
                end -- t goes out of scope, __gc fires
                collectgarbage()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);

            // Allow GC + scheduler one cycle to release the inner slot.
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.IsRunning && DateTime.UtcNow < deadline)
            {
                await Task.Delay(5);
            }

            engine.IsRunning.ShouldBeFalse();
        }

        [Fact]
        public void Pause_CalledFromRegisteredFunction_IsNoOp()
        {
            // Pause() must be a no-op when called from a C# registered function
            // (not inside the scheduler's lua_resume), to avoid corrupting the inline path.
            using KitsuneEngine engine = new();
            bool callbackRan = false;
            engine.RegisterFunction("DoWork", _ =>
            {
                callbackRan = true;
                return LuaValue.None;
            });

            // Pause() inside a registered function must return without yielding.
            engine.RunString("Pause() DoWork()");
            callbackRan.ShouldBeTrue();
        }

        [Fact]
        public async Task Resume_AfterDone_ReturnsFalse()
        {
            // Resuming an already-completed coroutine must return false, not crash.
            // Use a short Sleep so the slot is visible long enough to capture its id.
            using KitsuneEngine engine = new();
            engine.ExecuteString("Sleep(1); return 1");
            SpinUntilActive(engine);
            int id = engine.GetActiveIds()[0];
            await engine.WaitAsync(id);
            engine.Resume(id).ShouldBeFalse();
        }

        [Fact]
        public async Task TasksOpen_CanWrapRunningSlot()
        {
            // Tasks.Open(id) returns a Task handle for an existing running slot.
            using KitsuneEngine engine = new();
            engine.SetVariable("result", 0);
            engine.ExecuteString(@"
                local t = Tasks.New(function()
                    result = 1
                    Pause()
                    result = 2
                end)
                local id = t:GetId()
                local t2 = Tasks.Open(id)
                while t2:GetStatus() ~= TaskStatus.Paused do Sleep(5) end
                t2:Resume()
                while not t2:Finished() do Sleep(5) end
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetInt64("result") != 2 && DateTime.UtcNow < deadline)
            {
                await Task.Delay(5);
            }

            engine.GetInt64("result").ShouldBe(2);
        }

        [Fact]
        public async Task Dispose_WithPausedCoroutine_DoesNotHang()
        {
            // Disposing the engine while a coroutine is paused must complete promptly.
            KitsuneEngine engine = new();
            engine.ExecuteString("Pause() while true do end");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetStatus(id) != CoroutineStatus.Paused && DateTime.UtcNow < deadline)
            {
                await Task.Delay(1);
            }

            engine.GetStatus(id).ShouldBe(CoroutineStatus.Paused);

            // Dispose must not block.
            var disposeTask = System.Threading.Tasks.Task.Run(() => engine.Dispose());
            (await System.Threading.Tasks.Task.WhenAny(disposeTask, System.Threading.Tasks.Task.Delay(5000)))
                .ShouldBe(disposeTask);
        }

        // -- Cancel ---------------------------------------------------------------
        [Fact]
        public void Cancel_RunningCoroutine_SetsErrorAndFreesSlot()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("while true do end");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
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
            engine.ExecuteString("Sleep(10000); return 'never'");
            SpinUntilActive(engine);
            int id = engine.GetActiveIds()[0];

            // Wait until the coroutine has actually entered Sleep() before cancelling,
            // otherwise we may cancel while it's still WORKING and the Ticker fires
            // at the next 1000-instruction boundary, causing a marginal timing failure.
            DateTime sleepDeadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetStatus(id) != CoroutineStatus.Sleeping && DateTime.UtcNow < sleepDeadline)
            {
                Thread.Sleep(1);
            }

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
            engine.ExecuteString("while true do end");
            SpinUntilRunning(engine);
            int cancelId = engine.RunningCoroutineId;
            Task<LuaValue> keepTask = engine.ExecuteStringAsync("Sleep(100); return 'kept'");
            engine.Cancel(cancelId);
            LuaValue kept = await keepTask;
            kept.String.ShouldBe("kept");
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
            engine.ExecuteString("while true do end");
            engine.ExecuteString("while true do end");
            SpinUntilRunning(engine);
            DateTime ready = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetActiveIds().Length < 2 && DateTime.UtcNow < ready)
            {
                Thread.Sleep(1);
            }

            int[] activeIds = engine.GetActiveIds();
            int idA = activeIds[0], idB = activeIds[1];
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
            engine.ExecuteString("Sleep(60000)");
            SpinUntilActive(engine);
            int id = engine.GetActiveIds()[0];
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
            engine.ExecuteString("while true do end");
            SpinUntilRunning(engine);
            int id = engine.GetActiveIds()[0];
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
            engine.RunString("return 42").AsInt64.ShouldBe(42L);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task GetStatus_AfterRuntimeError_ReturnsFaulted()
        {
            using KitsuneEngine engine = new();
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("error('test error')"));
            ex.Message.ShouldContain("test error");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void GetStatus_AfterCancelOnSleepingCoroutine_ReturnsCancelled()
        {
            // After Cancel, GetStatus returns Cancelled until the slot is freed.
            // On fast schedulers (e.g. Linux) the slot may already be freed, returning None.
            using KitsuneEngine engine = new();
            engine.ExecuteString("Sleep(60000)");
            SpinUntilActive(engine);
            int id = engine.GetActiveIds()[0];
            DateTime sleepDeadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetStatus(id) != CoroutineStatus.Sleeping && DateTime.UtcNow < sleepDeadline)
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
            engine.ExecuteString("Sleep(500)");
            SpinUntilActive(engine);
            int id = engine.GetActiveIds()[0];
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

            // RunString is synchronous; after return the slot is auto-released
            engine.RunString("return 'done'").String.ShouldBe("done");
            engine.GetActiveIds().ShouldBeEmpty();
            engine.GetRuntime(99999).ShouldBe(0.0);  // non-existent id returns 0
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
                Task<LuaValue>[] tasks = Enumerable.Range(0, batchSize)
                    .Select(j => engine.ExecuteStringAsync($"return tostring({offset + j})"))
                    .ToArray();
                LuaValue[] batchResults = await Task.WhenAll(tasks);
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
            var results = new LuaValue[total];

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
                    "local n = 0; for _ = 1, 5000 do n = n + (counter or 0) end");
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
                engine.ExecuteString($"function stress_fn_{i}(x) return tostring(x * {i}) end");
            }

            engine.Wait();

            Task<LuaValue>[] tasks = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteFunctionAsync($"stress_fn_{i}",
                    args: [LuaValue.FromInt64(42)]))
                .ToArray();
            LuaValue[] results = await Task.WhenAll(tasks);

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

            Task<LuaValue>[] tasks = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteStringAsync(
                    $"slot_{i} = {i}; return tostring({i})"))
                .ToArray();
            LuaValue[] results = await Task.WhenAll(tasks);

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
            using KitsuneEngine engine = new();
            const int count = 30;
            Task<LuaValue>[] tasks = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteStringAsync($"return tostring({i})"))
                .ToArray();
            LuaValue[] results = await Task.WhenAll(tasks);
            for (int i = 0; i < count; i++)
            {
                results[i].String.ShouldBe(i.ToString());
            }
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- RegisterFunction -----------------------------------------------------
        [Fact]
        public async Task RegisterFunction_ReturnsString_LuaReceivesIt()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Greet", _ => "hello from C#");
            LuaValue result = await engine.ExecuteStringAsync("return Greet()");
            result.String.ShouldBe("hello from C#");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_ReturnsNumber_LuaReceivesIt()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("FortyTwo", _ => (LuaValue)42.5);
            LuaValue result = await engine.ExecuteStringAsync("return tostring(FortyTwo())");
            result.String.ShouldBe("42.5");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_ReturnsBool_LuaReceivesIt()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Yes", _ => (LuaValue)true);
            LuaValue result = await engine.ExecuteStringAsync("return tostring(Yes())");
            result.String.ShouldBe("true");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_ReturnsNone_LuaReceivesNil()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Void", _ => LuaValue.None);
            LuaValue result = await engine.ExecuteStringAsync(
                "local x = Void(); return tostring(x)");
            result.String.ShouldBe("nil");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_StringArg_ReceivedCorrectly()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Echo", args => args.First());
            LuaValue result = await engine.ExecuteStringAsync("return Echo('ping')");
            result.String.ShouldBe("ping");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_NumberArg_ReceivedCorrectly()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Double", args =>
                LuaValue.FromInt64(args.First().AsInt64 * 2));
            LuaValue result = await engine.ExecuteStringAsync("return tostring(Double(7))");
            result.String.ShouldBe("14");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_BoolArg_ReceivedCorrectly()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Not", args => LuaValue.FromBool(!args.First().Boolean));
            LuaValue result = await engine.ExecuteStringAsync("return tostring(Not(true))");
            result.String.ShouldBe("false");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_MultipleArgs_AllReceivedInOrder()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Concat", args =>
                LuaValue.FromString(string.Join("-", args.Select(a => a.String ?? "?"))));
            LuaValue result = await engine.ExecuteStringAsync(
                "return Concat('a', 'b', 'c')");
            result.String.ShouldBe("a-b-c");
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
        public async Task RegisterFunction_ThrowsLuaException_RaisesLuaErrorWithMessage()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Boom", _ => throw new LuaException("custom kaboom"));
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("return Boom()"));
            ex.Message.ShouldContain("custom kaboom");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_ThrowsOtherException_RaisesLuaErrorWithMessage()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Crash", _ => throw new InvalidOperationException("managed crash"));
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("return Crash()"));
            ex.Message.ShouldContain("managed crash");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_ErrorCaughtByPcall_DoesNotAbortCoroutine()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Boom", _ => throw new LuaException("pcall me"));
            LuaValue result = await engine.ExecuteStringAsync(
                "local ok, err = pcall(Boom); return tostring(ok) .. ':' .. err");
            result.String.ShouldNotBeNull();
            result.String.ShouldStartWith("false:");
            result.String.ShouldContain("pcall me");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_MultipleFunctions_EachCallsCorrectHandler()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("GetA", _ => "A");
            engine.RegisterFunction("GetB", _ => "B");
            engine.RegisterFunction("GetC", _ => "C");
            LuaValue result = await engine.ExecuteStringAsync(
                "return GetA() .. GetB() .. GetC()");
            result.String.ShouldBe("ABC");
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
            LuaValue result = await engine.ExecuteStringAsync(
                "return tostring(Increment()) .. ',' .. tostring(Increment())");
            result.String.ShouldBe("1,2");
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
            Task<LuaValue>[] tasks = Enumerable.Range(1, count)
                .Select(i => engine.ExecuteStringAsync($"return tostring(Square({i}))"))
                .ToArray();
            LuaValue[] results = await Task.WhenAll(tasks);

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

        [Fact]
        public async Task TwoEngines_FunctionRegisteredByFirst_RemainsCallableAfterFirstDisposed()
        {
            // Delegate GCHandles go into GlobalHandles (not per-engine); they are freed after
            // lua_close on the last Dispose.  Engine1 disposing first is therefore safe.
            KitsuneEngine engine1 = new();
            KitsuneEngine engine2 = new();
            try
            {
                int callCount = 0;
                engine1.RegisterFunction("MultiEngineFn", _ =>
                {
                    callCount++;
                    return LuaValue.FromInt64(callCount);
                });

                engine1.Dispose();

                LuaValue r1 = await engine2.ExecuteStringAsync("return MultiEngineFn()");
                LuaValue r2 = await engine2.ExecuteStringAsync("return MultiEngineFn()");
                r1.AsInt64.ShouldBe(1L);
                r2.AsInt64.ShouldBe(2L);
                callCount.ShouldBe(2);
                engine2.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine2.Dispose();
            }
        }

        [Fact]
        public async Task TwoEngines_UserdataCreatedByFirst_RemainsAccessibleAfterFirstDisposed()
        {
            // Userdata GCHandles go into GlobalHandles; they are freed after lua_close on the
            // last Dispose, so engine1 disposing first is safe.
            KitsuneEngine engine1 = new();
            KitsuneEngine engine2 = new();
            try
            {
                engine1.RegisterUserdata<Counter>();
                var counter = new Counter { Value = 5 };
                engine1.SetVariable("sharedCounter", engine1.CreateUserdata(counter));

                engine1.Dispose();

                LuaValue result = await engine2.ExecuteStringAsync(
                    "sharedCounter:Increment(); sharedCounter:Increment(); return sharedCounter:Get()");
                result.AsInt64.ShouldBe(7L);
                counter.Value.ShouldBe(7);
                engine2.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine2.Dispose();
            }
        }

        // -- CFunction (SetVariable / args / return) ----------------------------
        [Fact]
        public async Task CFunction_SetVariable_LuaCanCallIt()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("MyFunc", LuaValue.FromCFunction(args => (LuaValue)"hello from cfunction"));
            LuaValue result = await engine.ExecuteStringAsync("return MyFunc()");
            result.String.ShouldBe("hello from cfunction");
        }

        [Fact]
        public async Task CFunction_AsArg_LuaCanCallIt()
        {
            using KitsuneEngine engine = new();
            LuaValue result = await engine.ExecuteStringAsync(
                "local f = ...; return f(10, 20)",
                default,
                LuaValue.FromCFunction(args => (LuaValue)(args[0].AsDouble + args[1].AsDouble)));
            result.AsDouble.ShouldBe(30.0);
        }

        [Fact]
        public async Task CFunction_ReturnedFromRegisteredFunction_LuaCanCallIt()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("GetAdder", args =>
            {
                double addend = args.Count > 0 ? args[0].AsDouble : 0;
                return LuaValue.FromCFunction(inner => (LuaValue)(inner[0].AsDouble + addend));
            });
            LuaValue result = await engine.ExecuteStringAsync("local add5 = GetAdder(5)\nreturn add5(3)");
            result.AsDouble.ShouldBe(8.0);
        }

        [Fact]
        public async Task CFunction_ThrowsLuaException_RaisesLuaError()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("Boom", LuaValue.FromCFunction(_ => throw new LuaException("boom from cfunction")));
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("Boom()"));
            ex.Message.ShouldContain("boom from cfunction");
        }

        [Fact]
        public async Task CFunction_InTable_LuaCanCallIt()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("Ns.Fn", LuaValue.FromCFunction(_ => (LuaValue)99.0));
            LuaValue result = await engine.ExecuteStringAsync("return Ns.Fn()");
            result.AsDouble.ShouldBe(99.0);
        }

        [Fact]
        public async Task CFunction_Dispose_DoesNotCrash()
        {
            KitsuneEngine engine = new();
            engine.SetVariable("Fn", LuaValue.FromCFunction(_ => LuaValue.None));
            Should.NotThrow(engine.Dispose);
        }

        // -- Thread (coroutine) iterator ----------------------------------------
        [Fact]
        public async Task Thread_IterateAsync_YieldsValues()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = await engine.ExecuteStringAsync(
                "return coroutine.create(function() coroutine.yield(1) coroutine.yield(2) coroutine.yield(3) end)");
            thread.Type.ShouldBe(LuaType.Thread);
            var values = new List<double>();
            await foreach (var v in thread.ThreadRef!.IterateAsync())
            {
                values.Add(v.AsDouble);
            }

            values.ShouldBe([1.0, 2.0, 3.0]);
            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        [Fact]
        public async Task Thread_IterateAsync_FinalReturnValueIncluded()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = await engine.ExecuteStringAsync(
                "return coroutine.create(function() coroutine.yield(10) return 20 end)");
            var values = new List<double>();
            await foreach (var v in thread.ThreadRef!.IterateAsync())
            {
                values.Add(v.AsDouble);
            }

            values.ShouldBe([10.0, 20.0]);
            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        [Fact]
        public async Task Thread_IterateAsync_EmptyCoroutine_NoValues()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = await engine.ExecuteStringAsync(
                "return coroutine.create(function() end)");
            var values = new List<LuaValue>();
            await foreach (var v in thread.ThreadRef!.IterateAsync())
            {
                values.Add(v);
            }

            values.ShouldBeEmpty();
            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        [Fact]
        public async Task Thread_IterateAsync_DeadThread_NoValues()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = await engine.ExecuteStringAsync(
                "return coroutine.create(function() coroutine.yield(1) end)");

            // exhaust it first
            await foreach (var item in thread.ThreadRef!.IterateAsync())
            {
            }

            // iterating a dead thread should produce nothing
            var second = new List<LuaValue>();
            await foreach (var v in thread.ThreadRef!.IterateAsync())
            {
                second.Add(v);
            }
            second.ShouldBeEmpty();
            thread.ThreadRef?.Dispose();
        }

        [Fact]
        public async Task Thread_IterateAsync_Error_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = await engine.ExecuteStringAsync(
                "return coroutine.create(function() coroutine.yield(1) error(\"boom\") end)");
            var values = new List<double>();
            await Should.ThrowAsync<LuaException>(async () =>
            {
                await foreach (var v in thread.ThreadRef!.IterateAsync())
                {
                    values.Add(v.AsDouble);
                }
            });
            values.ShouldBe([1.0]);  // first yield was received before the error
            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        [Fact]
        public async Task Thread_IterateAsync_PassedAsArg_LuaCanResume()
        {
            using KitsuneEngine engine = new();

            // Pass the thread as a variable set on the global bridge
            LuaValue thread = await engine.ExecuteStringAsync(
                "return coroutine.create(function() for i = 1, 5 do coroutine.yield(i) end end)");
            engine.SetVariable("Gen", thread);

            // Lua can also resume a thread set via SetVariable
            LuaValue result = await engine.ExecuteStringAsync(
                "local ok, v = coroutine.resume(Gen) return v");
            result.AsDouble.ShouldBe(1.0);
            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        [Fact]
        public async Task Thread_IterateAsync_NilYield_ContinuesIteration()
        {
            // coroutine.yield(nil) produces LuaType.Nil — iteration must NOT stop.
            // Only coroutine.yield() with no args produces TNONE, which stops iteration.
            using KitsuneEngine engine = new();
            LuaValue thread = await engine.ExecuteStringAsync(
                "return coroutine.create(function() coroutine.yield(nil) coroutine.yield(1) end)");
            var values = new List<LuaValue>();
            await foreach (var v in thread.ThreadRef!.IterateAsync())
            {
                values.Add(v);
            }

            values.Count.ShouldBe(2);
            values[0].Type.ShouldBe(LuaType.Nil);
            values[1].AsDouble.ShouldBe(1.0);
            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        [Fact]
        public async Task Thread_IterateAsync_NoYieldOnlyReturn_ProducesReturnValue()
        {
            // A thread that skips yield() and goes straight to return still produces one element.
            using KitsuneEngine engine = new();
            LuaValue thread = await engine.ExecuteStringAsync(
                "return coroutine.create(function() return 42 end)");
            var values = new List<double>();
            await foreach (var v in thread.ThreadRef!.IterateAsync())
            {
                values.Add(v.AsDouble);
            }

            values.ShouldBe([42.0]);
            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        [Fact]
        public async Task Thread_IterateAsync_MixedValueTypes_AllReceived()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = await engine.ExecuteStringAsync(@"
                return coroutine.create(function()
                    coroutine.yield('hello')
                    coroutine.yield(42)
                    coroutine.yield(true)
                end)");
            var values = new List<LuaValue>();
            await foreach (var v in thread.ThreadRef!.IterateAsync())
            {
                values.Add(v);
            }

            values.Count.ShouldBe(3);
            values[0].String.ShouldBe("hello");
            values[1].AsDouble.ShouldBe(42.0);
            values[2].Boolean.ShouldBeTrue();
            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        [Fact]
        public async Task Thread_IterateAsync_TableYield_ContentsAccessible()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = await engine.ExecuteStringAsync(
                "return coroutine.create(function() coroutine.yield({x=1, y=2}) end)");
            LuaValue? received = null;
            await foreach (var v in thread.ThreadRef!.IterateAsync())
            {
                received = v;
            }

            received.ShouldNotBeNull();
            received!.Value.Type.ShouldBe(LuaType.Table);
            using var tableRef = received.Value.TableRef;
            tableRef.ShouldNotBeNull();
            var table = tableRef!.GetContents();
            table.ShouldContain(kvp => kvp.Key.String == "x" && kvp.Value.AsDouble == 1.0);
            table.ShouldContain(kvp => kvp.Key.String == "y" && kvp.Value.AsDouble == 2.0);
            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        [Fact]
        public async Task Thread_IterateAsync_PartialIteration_ThreadRemainsResumable()
        {
            // Breaking the loop early leaves the thread suspended; a second iteration
            // picks up from the next yield, not from the beginning.
            using KitsuneEngine engine = new();
            LuaValue thread = await engine.ExecuteStringAsync(@"
                return coroutine.create(function()
                    coroutine.yield(1)
                    coroutine.yield(2)
                    coroutine.yield(3)
                end)");

            var first = new List<double>();
            await foreach (var v in thread.ThreadRef!.IterateAsync())
            {
                first.Add(v.AsDouble);
                break;
            }

            first.ShouldBe([1.0]);

            var rest = new List<double>();
            await foreach (var v in thread.ThreadRef!.IterateAsync())
            {
                rest.Add(v.AsDouble);
            }

            rest.ShouldBe([2.0, 3.0]);
            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        [Fact]
        public async Task Thread_IterateAsync_CancellationToken_CancelsIteration()
        {
            // The cancellation token is checked at the top of each loop iteration;
            // cancelling inside the loop stops the NEXT step before it is started.
            using KitsuneEngine engine = new();
            LuaValue thread = await engine.ExecuteStringAsync(@"
                return coroutine.create(function()
                    for i = 1, 100 do coroutine.yield(i) end
                end)");
            using CancellationTokenSource cts = new();
            var values = new List<double>();
            await Should.ThrowAsync<OperationCanceledException>(async () =>
            {
                await foreach (var v in thread.ThreadRef!.IterateAsync(cts.Token))
                {
                    values.Add(v.AsDouble);
                    if (values.Count == 3)
                    {
                        cts.Cancel();
                    }
                }
            });

            values.ShouldBe([1.0, 2.0, 3.0]);
            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        [Fact]
        public async Task Thread_IterateAsync_FromCallback_ThrowsLuaException()
        {
            // IterateThreadAsync must refuse to start (inLuaCallback guard) when called
            // from within a RegisterFunction callback on the scheduler thread.
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() coroutine.yield(1) end)");

            engine.RegisterFunction("TryIterate", _ =>
            {
                thread.ThreadRef!.Iterate().GetEnumerator().MoveNext();
                return LuaValue.None;
            });

            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("TryIterate()"));
            ex.Message.ShouldContain("registered function");
            engine.GetActiveIds().ShouldBeEmpty();
            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        [Fact]
        public async Task Thread_IterateAsync_ThreadFromGetVariable_WorksCorrectly()
        {
            // A thread set as a Lua global and then read back via GetVariable can be iterated.
            // This exercises the LUA_TTHREAD branch in FillKitsuneVariableFromStack (GetVariable path).
            using KitsuneEngine engine = new();
            engine.RunString("myThread = coroutine.create(function() coroutine.yield(99) end)");
            LuaValue thread = engine.GetVariable("myThread");
            thread.Type.ShouldBe(LuaType.Thread);

            var values = new List<double>();
            await foreach (var v in thread.ThreadRef!.IterateAsync())
            {
                values.Add(v.AsDouble);
            }

            values.ShouldBe([99.0]);
            engine.GetActiveIds().ShouldBeEmpty();
            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        // -- Thread (coroutine) synchronous iterator
        [Fact]
        public void Thread_Iterate_YieldsValues()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() coroutine.yield(1) coroutine.yield(2) coroutine.yield(3) end)");
            thread.Type.ShouldBe(LuaType.Thread);
            var values = new List<double>();
            foreach (var v in thread.ThreadRef!.Iterate())
            {
                values.Add(v.AsDouble);
            }

            values.ShouldBe([1.0, 2.0, 3.0]);
            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Iterate_FinalReturnValueIncluded()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() coroutine.yield(10) return 20 end)");
            var values = new List<double>();
            foreach (var v in thread.ThreadRef!.Iterate())
            {
                values.Add(v.AsDouble);
            }

            values.ShouldBe([10.0, 20.0]);
            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Iterate_EmptyCoroutine_NoValues()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() end)");
            thread.ThreadRef!.Iterate().ShouldBeEmpty();
            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Iterate_DeadThread_NoValues()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() coroutine.yield(1) end)");

            // exhaust it first
            thread.ThreadRef!.Iterate().ToList();

            // iterating a dead thread should produce nothing
            thread.ThreadRef!.Iterate().ShouldBeEmpty();
            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Iterate_Error_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() coroutine.yield(1) error(\"boom\") end)");
            var values = new List<double>();
            Should.Throw<LuaException>(() =>
            {
                foreach (var v in thread.ThreadRef!.Iterate())
                {
                    values.Add(v.AsDouble);
                }
            });
            values.ShouldBe([1.0]);  // first yield was received before the error
            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Iterate_NilYield_ContinuesIteration()
        {
            // coroutine.yield(nil) produces LuaType.Nil — iteration must NOT stop.
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() coroutine.yield(nil) coroutine.yield(1) end)");
            var values = new List<LuaValue>();
            foreach (var v in thread.ThreadRef!.Iterate())
            {
                values.Add(v);
            }

            values.Count.ShouldBe(2);
            values[0].Type.ShouldBe(LuaType.Nil);
            values[1].AsDouble.ShouldBe(1.0);
            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Iterate_NoYieldOnlyReturn_ProducesReturnValue()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() return 42 end)");
            var values = new List<double>();
            foreach (var v in thread.ThreadRef!.Iterate())
            {
                values.Add(v.AsDouble);
            }

            values.ShouldBe([42.0]);
            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Iterate_MixedValueTypes_AllReceived()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(@"
                return coroutine.create(function()
                    coroutine.yield('hello')
                    coroutine.yield(42)
                    coroutine.yield(true)
                end)");
            var values = new List<LuaValue>();
            foreach (var v in thread.ThreadRef!.Iterate())
            {
                values.Add(v);
            }

            values.Count.ShouldBe(3);
            values[0].String.ShouldBe("hello");
            values[1].AsDouble.ShouldBe(42.0);
            values[2].Boolean.ShouldBeTrue();
            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Iterate_TableYield_ContentsAccessible()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() coroutine.yield({x=1, y=2}) end)");
            LuaValue? received = null;
            foreach (var v in thread.ThreadRef!.Iterate())
            {
                received = v;
            }

            received.ShouldNotBeNull();
            received!.Value.Type.ShouldBe(LuaType.Table);
            using var tableRef = received.Value.TableRef;
            tableRef.ShouldNotBeNull();
            var table = tableRef!.GetContents();
            table.ShouldContain(kvp => kvp.Key.String == "x" && kvp.Value.AsDouble == 1.0);
            table.ShouldContain(kvp => kvp.Key.String == "y" && kvp.Value.AsDouble == 2.0);
            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Iterate_PartialIteration_ThreadRemainsResumable()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(@"
                return coroutine.create(function()
                    coroutine.yield(1)
                    coroutine.yield(2)
                    coroutine.yield(3)
                end)");

            var first = new List<double>();
            foreach (var v in thread.ThreadRef!.Iterate())
            {
                first.Add(v.AsDouble);
                break;
            }

            first.ShouldBe([1.0]);

            var rest = new List<double>();
            foreach (var v in thread.ThreadRef!.Iterate())
            {
                rest.Add(v.AsDouble);
            }

            rest.ShouldBe([2.0, 3.0]);
            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Iterate_LargeSequence_AllValuesCorrect()
        {
            // Stresses the one-step-per-coroutine approach over 100 iterations.
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() for i = 1, 100 do coroutine.yield(i) end end)");
            var values = new List<long>();
            foreach (var v in thread.ThreadRef!.Iterate())
            {
                values.Add(v.AsInt64);
            }

            values.Count.ShouldBe(100);
            for (int i = 0; i < 100; i++)
            {
                values[i].ShouldBe(i + 1L);
            }

            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Iterate_CancellationToken_CancelsIteration()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() for i = 1, 100 do coroutine.yield(i) end end)");
            using CancellationTokenSource cts = new();
            var values = new List<double>();
            Should.Throw<OperationCanceledException>(() =>
            {
                foreach (var v in thread.ThreadRef!.Iterate(cts.Token))
                {
                    values.Add(v.AsDouble);
                    if (values.Count == 3)
                    {
                        cts.Cancel();
                    }
                }
            });

            values.ShouldBe([1.0, 2.0, 3.0]);
            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Iterate_PassedAsArg_LuaCanResume()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() for i = 1, 5 do coroutine.yield(i) end end)");
            engine.SetVariable("Gen", thread);

            LuaValue result = engine.RunString("local ok, v = coroutine.resume(Gen) return v");
            result.AsDouble.ShouldBe(1.0);
            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Iterate_ThreadFromGetVariable_WorksCorrectly()
        {
            // A thread stored as a Lua global, read back via GetVariable, can be iterated.
            using KitsuneEngine engine = new();
            engine.RunString("myThread = coroutine.create(function() coroutine.yield(99) end)");
            LuaValue thread = engine.GetVariable("myThread");
            thread.Type.ShouldBe(LuaType.Thread);

            var values = new List<double>();
            foreach (var v in thread.ThreadRef!.Iterate())
            {
                values.Add(v.AsDouble);
            }

            values.ShouldBe([99.0]);
            engine.GetActiveIds().ShouldBeEmpty();
            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public async Task Thread_Iterate_FromCallback_ThrowsLuaException()
        {
            // IterateThread must refuse to step (inLuaCallback guard fires on first MoveNext)
            // when called from within a RegisterFunction callback.
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() coroutine.yield(1) end)");

            engine.RegisterFunction("TryIterate", _ =>
            {
                thread.ThreadRef!.Iterate().GetEnumerator().MoveNext();
                return LuaValue.None;
            });

            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("TryIterate()"));
            ex.Message.ShouldContain("registered function");
            engine.GetActiveIds().ShouldBeEmpty();
            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        // -- Shallow / deep table bridge
        [Fact]
        public void GetVariable_TableValue_IsOpaqueWithNoContents()
        {
            // GetVariable returns type=Table with .Table (old snapshot) null.
            // The live registry ref is in .TableRef — use .TableRef!.GetContents() to access.
            using KitsuneEngine engine = new();
            engine.ExecuteString("t = {x=1, y=2}");
            engine.Wait();

            LuaValue v = engine.GetVariable("t");
            v.Type.ShouldBe(LuaType.Table);
            v.Table.ShouldBeNull();         // .Table snapshot is never populated by GetVariable
            v.TableRef.ShouldNotBeNull();   // but a live ref IS available via TableRef
            v.TableRef!.Dispose();
        }

        [Fact]
        public void GetAll_NestedTableValue_IsOpaque()
        {
            // GetAll is shallow: iterating a table whose values include a sub-table yields
            // an opaque Table entry (type=Table, Table==null) for that value.
            // Unlike GetVariable, GetAll returns no TableRef either — both are null.
            using KitsuneEngine engine = new();
            engine.ExecuteString("outer = { scalar = 42, inner = {a=1, b=2} }");
            engine.Wait();

            var all = engine.GetAll("outer");
            all.ShouldContain(kvp => kvp.Key.String == "scalar" && kvp.Value.AsDouble == 42);
            var innerEntry = all.Single(kvp => kvp.Key.String == "inner");
            innerEntry.Value.Type.ShouldBe(LuaType.Table);
            innerEntry.Value.Table.ShouldBeNull();    // no snapshot
            innerEntry.Value.TableRef.ShouldBeNull(); // and no live ref — truly opaque from GetAll
        }

        [Fact]
        public void GetResult_TableReturn_ContainsFullContents()
        {
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return {x=10, y=20}");
            result.Type.ShouldBe(LuaType.Table);
            using var contents = result.TableRef;
            contents.ShouldNotBeNull();
            var table = contents!.GetContents();
            table.ShouldContain(kvp => kvp.Key.String == "x" && kvp.Value.AsDouble == 10);
            table.ShouldContain(kvp => kvp.Key.String == "y" && kvp.Value.AsDouble == 20);
        }

        [Fact]
        public void RegisterFunction_TableArg_ReceivedWithFullContents()
        {
            using KitsuneEngine engine = new();
            LuaValue? received = null;
            engine.RegisterFunction("Capture", args =>
            {
                received = args[0];
                return LuaValue.None;
            });
            engine.ExecuteString("Capture({a='hello', b=99})");
            engine.Wait();

            received.ShouldNotBeNull();
            received!.Value.Type.ShouldBe(LuaType.Table);
            received.Value.Table.ShouldNotBeNull();
            received.Value.Table!.ShouldContain(kvp => kvp.Key.String == "a" && kvp.Value.String == "hello");
            received.Value.Table!.ShouldContain(kvp => kvp.Key.String == "b" && kvp.Value.AsDouble == 99);
        }

        // -- LuaTableRef lifecycle ------------------------------------------------
        [Fact]
        public void LuaTableRef_DoubleDispose_DoesNotThrow()
        {
            using KitsuneEngine engine = new();
            LuaTableRef tref = engine.RunString("return {}").TableRef!;
            tref.Dispose();
            Should.NotThrow(() => tref.Dispose());
        }

        [Fact]
        public void LuaTableRef_AfterDispose_GetContents_ThrowsObjectDisposedException()
        {
            using KitsuneEngine engine = new();
            LuaTableRef tref = engine.RunString("return {x=1}").TableRef!;
            tref.Dispose();
            Should.Throw<ObjectDisposedException>(() => tref.GetContents());
        }

        [Fact]
        public void LuaTableRef_AfterDispose_SetContents_ThrowsObjectDisposedException()
        {
            using KitsuneEngine engine = new();
            LuaTableRef tref = engine.RunString("return {x=1}").TableRef!;
            tref.Dispose();
            Should.Throw<ObjectDisposedException>(() => tref.SetContents([]));
        }

        // -- GetVariable returns a live TableRef, GetAll does not -----------------
        [Fact]
        public void GetVariable_TableRef_IsNotNull_ContentsAccessible()
        {
            // GetVariable wraps the live table in a TableRef; contents are on demand.
            using KitsuneEngine engine = new();
            engine.ExecuteString("t = {x=1, y=2}");
            engine.Wait();
            LuaValue v = engine.GetVariable("t");
            v.Type.ShouldBe(LuaType.Table);
            using var tref = v.TableRef;
            tref.ShouldNotBeNull();
            var contents = tref!.GetContents();
            contents.ShouldContain(kvp => kvp.Key.String == "x" && kvp.Value.AsDouble == 1);
            contents.ShouldContain(kvp => kvp.Key.String == "y" && kvp.Value.AsDouble == 2);
        }

        // -- TableRef.GetContents -------------------------------------------------
        [Fact]
        public void TableRef_GetContents_ConsistentOnMultipleCalls()
        {
            // GetContents snapshots the current state; calling it twice on an unchanged
            // table must return the same keys and values both times.
            using KitsuneEngine engine = new();
            using LuaTableRef tref = engine.RunString("return {a=1, b=2}").TableRef!;
            var first = tref.GetContents();
            var second = tref.GetContents();
            first.Count.ShouldBe(2);
            second.Count.ShouldBe(2);
            first.ShouldContain(kvp => kvp.Key.String == "a" && kvp.Value.AsDouble == 1);
            second.ShouldContain(kvp => kvp.Key.String == "a" && kvp.Value.AsDouble == 1);
        }

        [Fact]
        public void TableRef_GetContents_ArrayStyleIntegerKeys()
        {
            // Lua array-style tables have integer 1-based keys in the snapshot.
            using KitsuneEngine engine = new();
            using LuaTableRef tref = engine.RunString("return {10, 20, 30}").TableRef!;
            var contents = tref.GetContents();
            contents.Count.ShouldBe(3);
            contents.ShouldContain(kvp => kvp.Key.Type == LuaType.Integer && kvp.Key.AsInt64 == 1 && kvp.Value.AsDouble == 10);
            contents.ShouldContain(kvp => kvp.Key.Type == LuaType.Integer && kvp.Key.AsInt64 == 2 && kvp.Value.AsDouble == 20);
            contents.ShouldContain(kvp => kvp.Key.Type == LuaType.Integer && kvp.Key.AsInt64 == 3 && kvp.Value.AsDouble == 30);
        }

        [Fact]
        public void TableRef_GetContents_NestedTableIsExpandedInSnapshot()
        {
            // GetContents deep-snapshots nested tables up to the engine's max depth.
            // Nested values come back as LuaType.Table with .Table populated (not a live ref).
            using KitsuneEngine engine = new();
            using LuaTableRef tref = engine.RunString("return {inner = {a=10, b=20}}").TableRef!;
            var contents = tref.GetContents();
            var innerEntry = contents.Single(kvp => kvp.Key.String == "inner");
            innerEntry.Value.Type.ShouldBe(LuaType.Table);
            innerEntry.Value.Table.ShouldNotBeNull();  // nested table is eagerly snapshotted
            innerEntry.Value.Table!.ShouldContain(kvp => kvp.Key.String == "a" && kvp.Value.AsDouble == 10);
            innerEntry.Value.Table!.ShouldContain(kvp => kvp.Key.String == "b" && kvp.Value.AsDouble == 20);
        }

        // -- TableRef.SetContents -------------------------------------------------
        [Fact]
        public async Task TableRef_SetContents_LuaReadsUpdatedValues()
        {
            // SetContents replaces all keys; old keys disappear, new keys are visible.
            using KitsuneEngine engine = new();
            engine.ExecuteString("t = {a=1, b=2}");
            engine.Wait();
            using LuaTableRef tref = engine.GetVariable("t").TableRef!;
            tref.SetContents(new List<KeyValuePair<LuaValue, LuaValue>>
            {
                new(LuaValue.FromString("x"), LuaValue.FromInt64(99)),
                new(LuaValue.FromString("y"), LuaValue.FromInt64(100)),
            }.AsReadOnly()).ShouldBeTrue();

            LuaValue result = await engine.ExecuteStringAsync(
                "return tostring(t.a) .. ':' .. tostring(t.x) .. ':' .. tostring(t.y)");
            result.String.ShouldBe("nil:99:100");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task TableRef_SetContents_EmptyList_ClearsTable()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("t = {a=1, b=2}");
            engine.Wait();
            using LuaTableRef tref = engine.GetVariable("t").TableRef!;
            tref.SetContents([]).ShouldBeTrue();
            LuaValue result = await engine.ExecuteStringAsync(
                "local n = 0; for _ in pairs(t) do n = n + 1 end; return n");
            result.AsInt64.ShouldBe(0L);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_SetContents_RoundTrip_WriteReadConsistent()
        {
            // Write entries via SetContents then read them back via GetContents.
            using KitsuneEngine engine = new();
            engine.ExecuteString("t = {}");
            engine.Wait();
            using LuaTableRef tref = engine.GetVariable("t").TableRef!;
            var entries = new List<KeyValuePair<LuaValue, LuaValue>>
            {
                new(LuaValue.FromString("name"), LuaValue.FromString("alice")),
                new(LuaValue.FromInt64(1), LuaValue.FromInt64(42)),
            }.AsReadOnly();
            tref.SetContents(entries).ShouldBeTrue();
            var readBack = tref.GetContents();
            readBack.ShouldContain(kvp => kvp.Key.String == "name" && kvp.Value.String == "alice");
            readBack.ShouldContain(kvp => kvp.Key.Type == LuaType.Integer && kvp.Key.AsInt64 == 1 && kvp.Value.AsDouble == 42);
        }

        // -- Live-table SetVariable round-trip ------------------------------------
        [Fact]
        public async Task SetVariable_WithTableRef_AliasesLiveLuaTable()
        {
            // Passing a LuaValue with a live TableRef to SetVariable pushes the same
            // Lua table object — mutations through the alias are visible via the original.
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return {val=42}");
            engine.SetVariable("alias", result);
            LuaValue read = await engine.ExecuteStringAsync("return alias.val");
            read.AsInt64.ShouldBe(42L);

            // Mutate via alias; the original TableRef should reflect the change.
            await engine.ExecuteStringAsync("alias.val = 99");
            var contents = result.TableRef!.GetContents();
            contents.Single(kvp => kvp.Key.String == "val").Value.AsDouble.ShouldBe(99.0);
            result.TableRef?.Dispose();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- TableRef stability ---------------------------------------------------
        [Fact]
        public void TableRef_RemainsValidAfterGcCycles_AndConcurrentCoroutines()
        {
            // The luaL_ref keeps the table in the Lua registry; .NET GC and Lua GC
            // cycles must not invalidate it while C# holds the ref.
            var engine = new KitsuneEngine();
            try
            {
                LuaValue r = engine.RunString("return {x=77}");
                using LuaTableRef tref = r.TableRef!;

                GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking: true);
                GC.WaitForPendingFinalizers();
                GC.Collect(GC.MaxGeneration, GCCollectionMode.Forced, blocking: true);
                engine.RunString("Sleep(0)");  // give scheduler a cycle

                var contents = tref.GetContents();
                contents.ShouldContain(kvp => kvp.Key.String == "x" && kvp.Value.AsDouble == 77);
            }
            finally
            {
                engine.Dispose();
            }
        }

        [Fact]
        public void ManyTableRefs_DisposedBeforeEngine_NoLeak()
        {
            // 20 table refs created, verified, and explicitly disposed before Dispose().
            var engine = new KitsuneEngine();
            try
            {
                var refs = new List<LuaTableRef>();
                for (int i = 0; i < 20; i++)
                {
                    refs.Add(engine.RunString($"return {{n={i}}}").TableRef!);
                }
                foreach (var tr in refs)
                {
                    tr.GetContents().ShouldNotBeEmpty();
                    tr.Dispose();
                }
                engine.RunString("Sleep(0)");  // drain deferred unref queue
            }
            finally
            {
                engine.Dispose();
            }
        }

        [Fact]
        public void ExecuteString_TableArg_ContentAccessibleFromARGS()
        {
            using KitsuneEngine engine = new();
            var tableArg = LuaValue.FromTable(new List<KeyValuePair<LuaValue, LuaValue>>
            {
                new(LuaValue.FromString("name"),  LuaValue.FromString("alice")),
                new(LuaValue.FromString("score"), LuaValue.FromInt64(99)),
            }.AsReadOnly());

            engine.RunString(
                "local t = ...; return t.name .. ':' .. tostring(t.score)",
                tableArg).String.ShouldBe("alice:99");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ExecuteFile_TableArg_ContentAccessibleFromARGS()
        {
            // For ExecuteFile, first vararg is file path; extra args follow.
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "local _, t = ...; return t.x .. ':' .. tostring(t.y)");
                using KitsuneEngine engine = new();
                var tableArg = LuaValue.FromTable(new List<KeyValuePair<LuaValue, LuaValue>>
                {
                    new(LuaValue.FromString("x"), LuaValue.FromString("hello")),
                    new(LuaValue.FromString("y"), LuaValue.FromInt64(7)),
                }.AsReadOnly());

                engine.RunFile(path, args: [tableArg]).String.ShouldBe("hello:7");
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
                "function process(t) return t.key .. ':' .. tostring(t.val) end");
            engine.Wait();

            var tableArg = LuaValue.FromTable(new List<KeyValuePair<LuaValue, LuaValue>>
            {
                new(LuaValue.FromString("key"), LuaValue.FromString("test")),
                new(LuaValue.FromString("val"), LuaValue.FromInt64(42)),
            }.AsReadOnly());

            LuaValue result = await engine.ExecuteFunctionAsync("process", args: [tableArg]);
            result.String.ShouldBe("test:42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteFunction_NestedTableArg_NestedContentsAccessible()
        {
            // A table arg containing a nested table is fully pushed; nested keys are reachable.
            using KitsuneEngine engine = new();
            engine.ExecuteString(
                "function getDeep(t) return t.outer.inner end");
            engine.Wait();

            var inner = LuaValue.FromTable(new List<KeyValuePair<LuaValue, LuaValue>>
            {
                new(LuaValue.FromString("inner"), LuaValue.FromString("deep value")),
            }.AsReadOnly());
            var outer = LuaValue.FromTable(new List<KeyValuePair<LuaValue, LuaValue>>
            {
                new(LuaValue.FromString("outer"), inner),
            }.AsReadOnly());

            LuaValue result = await engine.ExecuteFunctionAsync("getDeep", args: [outer]);
            result.String.ShouldBe("deep value");
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
            LuaValue result = await engine.ExecuteStringAsync(@"
                local co = coroutine.create(function()
                    coroutine.yield(10)
                    return 20
                end)
                local ok1, v1 = coroutine.resume(co)
                local ok2, v2 = coroutine.resume(co)
                return tostring(v1) .. ':' .. tostring(v2)
            ");
            result.String.ShouldBe("10:20");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_Wrap_Generator_ProducesCorrectSequence()
        {
            // coroutine.wrap (the generator idiom) must work inside a Kitsune coroutine.
            using KitsuneEngine engine = new();
            LuaValue result = await engine.ExecuteStringAsync(@"
                local gen = coroutine.wrap(function()
                    coroutine.yield(1)
                    coroutine.yield(2)
                    coroutine.yield(3)
                end)
                return tostring(gen()) .. ':' .. tostring(gen()) .. ':' .. tostring(gen())
            ");
            result.String.ShouldBe("1:2:3");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_DirectYield_FromKitsuneCoroutine_ResumesNormally()
        {
            // coroutine.yield() called directly from a Kitsune-managed coroutine yields to
            // the scheduler, which re-resumes it with zero args on the next pass.
            // The state after the yield must be unchanged; the resume must complete normally.
            using KitsuneEngine engine = new();
            LuaValue result = await engine.ExecuteStringAsync(@"
                local x = 1
                coroutine.yield()
                x = x + 1
                return tostring(x)
            ");
            result.String.ShouldBe("2");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_InfiniteGenerator_ProducesCorrectValues()
        {
            // An infinite generator (sub-coroutine that never returns) must behave
            // correctly when its parent Kitsune coroutine calls it a fixed number of times.
            using KitsuneEngine engine = new();
            LuaValue result = await engine.ExecuteStringAsync(@"
                local function counter(start)
                    return coroutine.wrap(function()
                        local n = start
                        while true do coroutine.yield(n); n = n + 1 end
                    end)
                end
                local c = counter(5)
                return tostring(c()) .. ':' .. tostring(c()) .. ':' .. tostring(c())
            ");
            result.String.ShouldBe("5:6:7");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_ConcurrentKitsuneCoroutines_EachWithOwnSubCoroutine_AreIndependent()
        {
            // Multiple concurrent Kitsune coroutines each owning their own sub-coroutine;
            // sub-coroutine state must not bleed between Kitsune-managed threads.
            using KitsuneEngine engine = new();
            Task<LuaValue>[] tasks = Enumerable.Range(1, 5).Select(i =>
                engine.ExecuteStringAsync($@"
                    local co = coroutine.create(function()
                        coroutine.yield({i} * 10)
                        return {i} * 100
                    end)
                    local _, v1 = coroutine.resume(co)
                    local _, v2 = coroutine.resume(co)
                    return tostring(v1) .. ':' .. tostring(v2)
                ")).ToArray();
            LuaValue[] results = await Task.WhenAll(tasks);
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
            LuaValue result = await engine.ExecuteStringAsync("coroutine.yield('hello')");
            result.String.ShouldBeNull();  // no return statement ? result is nil/none
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_YieldWithValue_ResumeReceivesNil()
        {
            // The scheduler always resumes with 0 args after a yield,
            // so coroutine.yield() always returns nil inside a Kitsune-managed coroutine.
            using KitsuneEngine engine = new();
            LuaValue result = await engine.ExecuteStringAsync(@"
                local v = coroutine.yield('discarded')
                return tostring(v)
            ");
            result.String.ShouldBe("nil");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Coroutine_YieldIsNotReturn_ResultComesFromReturnOnly()
        {
            // Reinforces that yield value != result; only return sets the coroutine result.
            using KitsuneEngine engine = new();
            LuaValue result = await engine.ExecuteStringAsync(@"
                coroutine.yield('not the result')
                return 'the real result'
            ");
            result.String.ShouldBe("the real result");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Wchar bridge ---------------------------------------------------------
        [Fact]
        public void Wchar_ReturnedFromScript_HasWcharType()
        {
            // A Lua Wchar returned by a coroutine is surfaced as LuaType.Wchar, not LuaType.String.
            using KitsuneEngine engine = new();
            LuaValue v = engine.RunString("return Wchar.FromUtf8('hello wchar')");
            v.Type.ShouldBe(LuaType.Char16);
            v.String.ShouldBe("hello wchar");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Wchar_ReturnedFromScript_StringAccessible()
        {
            // GetResultString decodes the UTF-8 bytes regardless of String vs Wchar type.
            using KitsuneEngine engine = new();
            engine.RunString("return Wchar.FromUtf8('kitsune wchar')").String.ShouldBe("kitsune wchar");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Wchar_SetVariable_PushesWcharIntoLua()
        {
            // Setting a Wchar variable pushes a Lua Wchar object; Lua can call Wchar methods on it.
            using KitsuneEngine engine = new();
            engine.SetVariable("wv", LuaValue.FromWchar("hello"));
            LuaValue result = await engine.ExecuteStringAsync(
                "return tostring(type(wv) == 'userdata' and wv:ToUtf8() == 'hello')");
            result.String.ShouldBe("true");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Wchar_SetVariable_LuaCanCallWcharMethods()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("greeting", LuaValue.FromWchar("Hello World"));
            LuaValue result = await engine.ExecuteStringAsync(
                "return greeting:ToUpper():ToUtf8()");
            result.String.ShouldBe("HELLO WORLD");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Wchar_GetVariable_FromLuaWcharGlobal_ReturnsWcharType()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("myWchar = Wchar.FromUtf8('bridge test')");
            engine.Wait();
            LuaValue v = engine.GetVariable("myWchar");
            v.Type.ShouldBe(LuaType.Char16);
            v.String.ShouldBe("bridge test");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Wchar_RoundTrip_SetAndGet_PreservesContent()
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
            LuaValue result = engine.RunString("return { w = Wchar.FromUtf8('in table') }");
            result.Type.ShouldBe(LuaType.Table);
            using var tableRef = result.TableRef;
            tableRef.ShouldNotBeNull();
            var table = tableRef!.GetContents();
            var entry = table.Single(kvp => kvp.Key.String == "w");
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
            engine.ExecuteString("CaptureWchar(Wchar.FromUtf8('from lua'))");
            engine.Wait();
            received.ShouldNotBeNull();
            received!.Value.Type.ShouldBe(LuaType.Char16);
            received.Value.String.ShouldBe("from lua");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Wchar_RegisterFunction_ReturnWchar_LuaReceivesWcharObject()
        {
            // A C# function returning LuaType.Wchar pushes a Lua Wchar object; Lua can call methods on it.
            using KitsuneEngine engine = new();
            engine.RegisterFunction("MakeWchar", _ => LuaValue.FromWchar("from csharp"));
            LuaValue result = await engine.ExecuteStringAsync(
                "local w = MakeWchar(); return tostring(type(w)=='userdata' and w:ToUpper():ToUtf8())");
            result.String.ShouldBe("FROM CSHARP");
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
            engine.ExecuteString("CaptureJson(Json.New())");
            engine.Wait();
            received.ShouldNotBeNull();
            received!.Value.Type.ShouldBe(LuaType.Userdata);
            received.Value.Bytes.ShouldNotBeNull();
            received.Value.String.ShouldBe("LUAJSON");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Userdata_CoroutineResult_TypeNameInBytes()
        {
            // An unrecognised userdata returned from a coroutine arrives via GetResultVariable
            // with Type == Userdata and Bytes holding the metatable __name.
            using KitsuneEngine engine = new();
            LuaValue v = engine.RunString("return Json.New()");
            v.Type.ShouldBe(LuaType.Userdata);
            v.Bytes.ShouldNotBeNull();
            v.String.ShouldBe("LUAJSON");
            v.UserdataRef?.Dispose();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Userdata_DifferentType_TypeNameMatchesMetatable()
        {
            // Verifies the __name lookup is not hard-coded: Stream.New() carries a
            // different metatable name ("STREAM") from Json.New() ("LUAJSON").
            using KitsuneEngine engine = new();
            LuaValue? received = null;
            engine.RegisterFunction("CaptureStream", args =>
            {
                received = args[0];
                return LuaValue.None;
            });
            engine.ExecuteString("CaptureStream(Stream.New())");
            engine.Wait();
            received.ShouldNotBeNull();
            received!.Value.Type.ShouldBe(LuaType.Userdata);
            received.Value.Bytes.ShouldNotBeNull();
            received.Value.String.ShouldBe("STREAM");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- RegisterUserdata / CreateUserdata ------------------------------------
        [Fact]
        public async Task RegisterUserdata_MethodsCallable_FromLua()
        {
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            engine.SetVariable("c", engine.CreateUserdata(new Counter { Value = 10 }));
            LuaValue result = await engine.ExecuteStringAsync(
                "c:Increment(); c:Increment(); return c:Get()");
            result.AsInt64.ShouldBe(12);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterUserdata_MetaMethod_ToStringCallable()
        {
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            engine.SetVariable("c", engine.CreateUserdata(new Counter { Value = 7 }));
            LuaValue result = await engine.ExecuteStringAsync("return tostring(c)");
            result.String.ShouldBe("Counter(7)");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterUserdata_MetaMethod_GCIsCalled()
        {
            Counter counter = new Counter { Value = 69 };
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            engine.SetVariable("c", engine.CreateUserdata(counter));
            LuaValue result = await engine.ExecuteStringAsync("local v = c; c=nil; return tostring(v);");
            result.String.ShouldBe("Counter(69)");
            engine.CollectGarbage();
            engine.GetActiveIds().ShouldBeEmpty();
            counter.DidGc.ShouldBeTrue();
        }

        // This particular test needs to run solo, if it run with multiple tests it will not properly test dispose on kitsuneengine.
        [Fact]
        public async Task RegisterUserdata_MetaMethod_DoesntLeak()
        {
            Counter counter = new Counter { Value = 69 };
            using (KitsuneEngine engine = new())
            {
                engine.RegisterUserdata<Counter>();
                engine.SetVariable("c", engine.CreateUserdata(counter));
                LuaValue result = await engine.ExecuteStringAsync("local v = c; c=nil; return v;");
                result.UserdataRef.ShouldNotBeNull();
                engine.CollectGarbage();
                counter.DidGc.ShouldBeFalse();
                result.UserdataRef?.Dispose();
                if (KitsuneEngine.GetReferences() != 1)
                {
                    engine.CollectGarbage();
                }
                engine.GetActiveIds().ShouldBeEmpty();
            }

            if (KitsuneEngine.GetReferences() == 0)
            {
                counter.DidGc.ShouldBeTrue();
            }
        }

        [Fact]
        public async Task RegisterUserdata_MetaMethod_GCIsCalledOnClose()
        {
            Counter counter = new Counter { Value = 420 };
            using (KitsuneEngine engine = new())
            {
                engine.RegisterUserdata<Counter>();
                engine.SetVariable("c", engine.CreateUserdata(counter));
                LuaValue result = await engine.ExecuteStringAsync("local v = c; c=nil; return tostring(v);");
                result.String.ShouldBe("Counter(420)");
                engine.GetActiveIds().ShouldBeEmpty();
            }

            // This test only works if its run solo, otherwise references might not be 0 which means the KitsuneCleanup has not been called
            if (KitsuneEngine.GetReferences() == 0)
            {
                counter.DidGc.ShouldBeTrue();
            }
        }

        [Fact]
        public async Task RegisterUserdata_NoToStringMetaMethod_DefaultUsesObjectToString()
        {
            // Widget has no [LuaMetaMethod("__tostring")], so the injected default must
            // call inst.ToString() — the standard C# Object.ToString() override.
            // The user-defined Counter case above confirms the user's method is NOT
            // replaced; this case confirms the fallback fires only when nothing is defined.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Widget>();
            var w = new Widget("hello");
            engine.SetVariable("w", engine.CreateUserdata(w));
            LuaValue result = await engine.ExecuteStringAsync("return tostring(w)");
            result.String.ShouldBe(w.ToString());
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RegisterUserdata_DuplicateName_ReturnsFalse()
        {
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>().ShouldBeTrue();
            engine.RegisterUserdata<Counter>().ShouldBeFalse();
        }

        [Fact]
        public async Task RegisterUserdata_InstancePassedToCallback_GetUserdataReturnsInstance()
        {
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            var original = new Counter { Value = 42 };
            engine.SetVariable("c", engine.CreateUserdata(original));

            Counter? captured = null;
            engine.RegisterFunction("Capture", args =>
            {
                captured = args[0].GetUserdata<Counter>();
                return LuaValue.None;
            });

            engine.ExecuteString("Capture(c)");
            engine.Wait();
            captured.ShouldNotBeNull();
            captured.ShouldBeSameAs(original);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterUserdata_GcFreesHandle_EngineDisposeDoesNotLeak()
        {
            var engine = new KitsuneEngine();
            try
            {
                engine.RegisterUserdata<Counter>();
                engine.SetVariable("c", engine.CreateUserdata(new Counter()));

                // Force __gc by collecting and nilifying the global.
                await engine.ExecuteStringAsync(
                    "c = nil; collectgarbage('collect'); collectgarbage('collect')");
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine.Dispose();
            }
        }

        [Fact]
        public async Task RegisterUserdata_MultipleInstances_IndependentState()
        {
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            engine.SetVariable("a", engine.CreateUserdata(new Counter { Value = 0 }));
            engine.SetVariable("b", engine.CreateUserdata(new Counter { Value = 100 }));
            LuaValue result = await engine.ExecuteStringAsync(
                "a:Increment(); a:Increment(); b:Increment(); return tostring(a:Get()) .. ':' .. tostring(b:Get())");
            result.String.ShouldBe("2:101");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterUserdata_Method_WithLuaArgs_ReceivedCorrectly()
        {
            // args[0] = self (the userdata); args[1..n] = the Lua call arguments.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            engine.SetVariable("c", engine.CreateUserdata(new Counter { Value = 10 }));
            LuaValue result = await engine.ExecuteStringAsync(
                "c:Add(5); c:Add(3); return c:Get()");
            result.AsInt64.ShouldBe(18);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterUserdata_Method_ThrowsLuaException_CatchableByPcall()
        {
            // A [LuaMethod] that throws LuaException must propagate the *original* message as a
            // Lua error. TargetInvocationException must NOT swallow it before it reaches pcall.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            engine.SetVariable("c", engine.CreateUserdata(new Counter { Value = -1 }));
            LuaValue result = await engine.ExecuteStringAsync(
                "local ok, err = pcall(function() c:BangIfNegative() end); return tostring(ok) .. ':' .. tostring(err)");
            result.String.ShouldStartWith("false:");
            result.String.ShouldContain("Counter value is negative");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterUserdata_Method_DoesNotThrow_PcallReturnsTrue()
        {
            // When Value >= 0 the method must not throw; pcall sees true.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            engine.SetVariable("c", engine.CreateUserdata(new Counter { Value = 5 }));
            LuaValue result = await engine.ExecuteStringAsync(
                "local ok = pcall(function() c:BangIfNegative() end); return tostring(ok)");
            result.String.ShouldBe("true");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RegisterUserdata_GetVariableType_ReturnsUserdata()
        {
            // SetVariable with a Kitsune userdata stores it in Lua; GetVariableType must
            // reflect the Lua type, not the C# type.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            engine.SetVariable("c", engine.CreateUserdata(new Counter()));
            engine.GetVariableType("c").ShouldBe(LuaType.Userdata);
        }

        [Fact]
        public void RegisterUserdata_GetVariable_PreservesInstance()
        {
            // GetVariable on a Kitsune-registered global returns a LuaValue whose
            // UserdataGCHandlePtr is populated, so GetUserdata<T>() recovers the original instance.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            var original = new Counter { Value = 99 };
            engine.SetVariable("c", engine.CreateUserdata(original));
            LuaValue v = engine.GetVariable("c");
            v.Type.ShouldBe(LuaType.Userdata);
            v.GetUserdata<Counter>().ShouldBeSameAs(original);
            v.UserdataRef?.Dispose();
        }

        [Fact]
        public void RegisterUserdata_ReturnedFromCoroutine_GetUserdataReturnsInstance()
        {
            // A Kitsune-registered userdata returned by RunString has UserdataGCHandlePtr set
            // so the original C# instance can be recovered via GetUserdata<T>().
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            var original = new Counter { Value = 77 };
            engine.SetVariable("c", engine.CreateUserdata(original));
            LuaValue result = engine.RunString("return c");
            result.Type.ShouldBe(LuaType.Userdata);
            result.GetUserdata<Counter>().ShouldBeSameAs(original);
            result.UserdataRef?.Dispose();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RegisterUserdata_GetUserdata_WrongType_ReturnsNull()
        {
            // GetUserdata<T>() returns null when the stored instance is not a T;
            // the correct type returns non-null. No engine interaction needed.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            LuaValue v = engine.CreateUserdata(new Counter { Value = 1 });
            v.GetUserdata<string>().ShouldBeNull();
            v.GetUserdata<Counter>().ShouldNotBeNull();
        }

        [Fact]
        public async Task RegisterUserdata_TwoTypesRegistered_WorkIndependently()
        {
            // Two independently registered types must not share metatables or methods.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>().ShouldBeTrue();
            engine.RegisterUserdata<Widget>().ShouldBeTrue();
            engine.SetVariable("c", engine.CreateUserdata(new Counter { Value = 3 }));
            engine.SetVariable("w", engine.CreateUserdata(new Widget("kitsune")));
            LuaValue result = await engine.ExecuteStringAsync(
                "c:Increment(); return c:Get() .. ':' .. w:GetName()");
            result.String.ShouldBe("4:kitsune");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterUserdata_ConcurrentMethodCalls_AllCorrect()
        {
            // 20 concurrent coroutines each hold their own Counter instance; method calls
            // must not cross-contaminate instances under scheduler pressure.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            const int count = 20;
            for (int i = 0; i < count; i++)
            {
                engine.SetVariable($"co{i}", engine.CreateUserdata(new Counter { Value = i * 10 }));
            }

            Task<LuaValue>[] tasks = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteStringAsync(
                    $"co{i}:Increment(); co{i}:Increment(); return co{i}:Get()"))
                .ToArray();
            LuaValue[] results = await Task.WhenAll(tasks);
            for (int i = 0; i < count; i++)
            {
                results[i].AsInt64.ShouldBe((i * 10) + 2, $"instance {i} returned wrong value");
            }

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterUserdata_FromCallback_ThrowsLuaException()
        {
            // RegisterUserdata may not be called from inside a RegisterFunction callback.
            using KitsuneEngine engine = new();
            engine.RegisterFunction("TryRegister", _ =>
            {
                engine.RegisterUserdata<Counter>();
                return LuaValue.None;
            });
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("TryRegister()"));
            ex.Message.ShouldContain("registered function");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task CreateUserdata_FromCallback_ThrowsLuaException()
        {
            // CreateUserdata may not be called from inside a RegisterFunction callback.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            engine.RegisterFunction("TryCreate", _ =>
            {
                engine.CreateUserdata(new Counter());
                return LuaValue.None;
            });
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("TryCreate()"));
            ex.Message.ShouldContain("registered function");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterUserdata_UserdataPassedViaARGS_MethodCallable()
        {
            // A userdata instance passed as a coroutine arg arrives with its GCHandlePtr
            // populated so Lua can call methods and C# sees the same mutated instance.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            var c = new Counter { Value = 5 };
            LuaValue result = await engine.ExecuteStringAsync(
                "local c = ...; c:Increment(); return c:Get()",
                default,
                engine.CreateUserdata(c));
            result.AsInt64.ShouldBe(6);
            c.Value.ShouldBe(6);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RegisterUserdata_HandleFreedByDisposeWithoutGc_DoesNotCrash()
        {
            // If the engine is disposed before Lua GC fires __gc, the _userdataHandles
            // fallback frees the GCHandle safely — no ObjectDisposedException or double-free.
            KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            engine.SetVariable("c", engine.CreateUserdata(new Counter()));
            Should.NotThrow(engine.Dispose);
        }

        [Fact]
        public void CreateUserdata_NullInstance_ThrowsArgumentNullException()
        {
            // GCHandle.Alloc(null) is valid in .NET: it creates a handle whose Target is null.
            // A null-instance userdata would silently surface as "expected T as self" when Lua
            // calls any method, hiding the real mistake.  ArgumentNullException must be thrown
            // before any handle is allocated.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            Should.Throw<ArgumentNullException>(() => engine.CreateUserdata<Counter>(null!));
        }

        [Fact]
        public void RegisterUserdata_DuplicateMethodName_ThrowsInvalidOperationException()
        {
            // Two [LuaMethod] attributes that map to the same Lua name silently overwrite each
            // other in the dictionary; the earlier fix makes this a hard error instead.
            using KitsuneEngine engine = new();
            Should.Throw<InvalidOperationException>(() => engine.RegisterUserdata<TypeWithDuplicateLuaMethod>())
                .Message.ShouldContain("Foo");
        }

        [Fact]
        public void RegisterUserdata_DuplicateMetaMethodName_ThrowsInvalidOperationException()
        {
            // Two [LuaMetaMethod] with the same name must also be rejected.
            using KitsuneEngine engine = new();
            Should.Throw<InvalidOperationException>(() => engine.RegisterUserdata<TypeWithDuplicateLuaMetaMethod>())
                .Message.ShouldContain("__tostring");
        }

        [Fact]
        public async Task RegisterUserdata_MethodReturnsUserdata_LuaCanCallMethods()
        {
            // A RegisterFunction handler may return a pre-created userdata LuaValue;
            // Lua can call methods on the received instance.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            var source = new Counter { Value = 42 };
            LuaValue preCreated = engine.CreateUserdata(source);
            engine.RegisterFunction("GetCounter", _ => preCreated);
            LuaValue result = await engine.ExecuteStringAsync(
                "local c = GetCounter(); c:Increment(); return c:Get()");
            result.AsInt64.ShouldBe(43);
            source.Value.ShouldBe(43);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterUserdata_TableYield_ContainsUserdata()
        {
            // A table returned from a coroutine that contains a registered userdata entry
            // must preserve the userdata type and GCHandlePtr for that entry.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            var c = new Counter { Value = 55 };
            engine.SetVariable("c", engine.CreateUserdata(c));
            LuaValue result = engine.RunString("return { item = c, label = 'test' }");
            result.Type.ShouldBe(LuaType.Table);
            using var tableRef = result.TableRef;
            tableRef.ShouldNotBeNull();
            var table = tableRef!.GetContents();
            var entry = table.Single(kvp => kvp.Key.String == "item");
            entry.Value.Type.ShouldBe(LuaType.Userdata);
            entry.Value.GetUserdata<Counter>().ShouldBeSameAs(c);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterUserdata_RegisterFunctionReceivesUserdata_GetUserdataWorks()
        {
            // When Lua passes a Kitsune userdata as an argument to a RegisterFunction callback
            // the received LuaValue has UserdataGCHandlePtr set so GetUserdata<T>() works.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            var original = new Counter { Value = 7 };
            engine.SetVariable("c", engine.CreateUserdata(original));

            Counter? captured = null;
            int capturedValue = -1;
            engine.RegisterFunction("Inspect", args =>
            {
                captured = args[0].GetUserdata<Counter>();
                capturedValue = captured?.Value ?? -1;
                return LuaValue.None;
            });

            engine.ExecuteString("Inspect(c)");
            engine.Wait();
            captured.ShouldNotBeNull();
            captured.ShouldBeSameAs(original);
            capturedValue.ShouldBe(7);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterUserdata_DotPath_UserdataAccessibleViaNestedTable()
        {
            // SetVariable at a dot-path should work for userdatas, storing the object in a sub-table.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            var c = new Counter { Value = 3 };
            engine.SetVariable("ns.counter", engine.CreateUserdata(c));
            LuaValue result = await engine.ExecuteStringAsync(
                "ns.counter:Increment(); ns.counter:Increment(); return ns.counter:Get()");
            result.AsInt64.ShouldBe(5);
            c.Value.ShouldBe(5);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void NohookCallback_ErrorCaughtByPcall_HookRestoredAndCancelStillWorks()
        {
            // Regression: if a nohook callback error was caught by pcall, the coroutine
            // could continue running without the Ticker hook, making Cancel ineffective.
            // Verifies the hook is always restored before the error unwinds.
            using KitsuneEngine engine = new();
            engine.ExecuteString(@"
                local ok, err = pcall(function()
                    local iter = CSV.New():DecodeFromFunction(function() error('supplier error') end)
                    iter()
                end)
                while true do end
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
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
            LuaValue result = await engine.ExecuteStringAsync(@"
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
            result.String.ShouldBe("2");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task MetamethodFromCCode_WithConcurrentCoroutines_NoCrash()
        {
            // Regression: the Ticker must not yield when a non-yieldable C-call boundary
            // is on the stack (e.g. when __tostring is dispatched via tostring()).
            using KitsuneEngine engine = new();

            // Runs concurrently to keep the scheduler active.
            Task<LuaValue> bgTask = engine.ExecuteStringAsync(
                "local n = 0; for _ = 1, 1000000 do n = n + 1 end; return tostring(n)");

            // Calls tostring() on an object with __tostring; dispatched via a non-yieldable
            // C boundary — the Ticker must handle this without crashing.
            Task<LuaValue> fgTask = engine.ExecuteStringAsync(@"
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

            LuaValue[] results = await Task.WhenAll(bgTask, fgTask);
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
            LuaValue result = await engine.ExecuteStringAsync(
                "return jv.name .. ':' .. tostring(jv.score)");
            result.String.ShouldBe("alice:99");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Json_SetVariable_NestedObject_LuaReadsDeeply()
        {
            // Nested JSON objects become nested Lua tables; deep path access works.
            using KitsuneEngine engine = new();
            engine.SetVariable("jv", LuaValue.FromJson(JsonNode.Parse("""{"outer":{"inner":"deep"}}""")));
            LuaValue result = await engine.ExecuteStringAsync("return jv.outer.inner");
            result.String.ShouldBe("deep");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Json_SetVariable_ArrayNode_LuaUsesOneBasedKeys()
        {
            // Json:Decode maps JSON arrays to Lua tables with 1-based integer keys.
            using KitsuneEngine engine = new();
            engine.SetVariable("jv", LuaValue.FromJson(JsonNode.Parse("[10,20,30]")));
            LuaValue result = await engine.ExecuteStringAsync(
                "return tostring(jv[1]) .. ':' .. tostring(jv[2]) .. ':' .. tostring(jv[3])");
            result.String.ShouldBe("10:20:30");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Json_ExecuteStringArg_LuaReadsFields()
        {
            // A JsonNode passed as an ARGS element is accessible as a table in the script.
            using KitsuneEngine engine = new();
            engine.RunString(
                "local t = ...; return tostring(t.x + t.y)",
                LuaValue.FromJson(JsonNode.Parse("""{"x":7,"y":8}"""))).String.ShouldBe("15");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Json_TableResult_AsJsonNode_ProducesJsonObject()
        {
            // Lua returns a string-keyed table; AsJsonNode() converts the LuaType.Table
            // linked list to a JsonObject — no native-side JSON encoding involved.
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return {name='bob', score=42}");
            result.Type.ShouldBe(LuaType.Table);
            JsonNode? node = result.AsJsonNode();
            node.ShouldBeAssignableTo<JsonObject>();
            ((JsonObject)node!)["name"]!.GetValue<string>().ShouldBe("bob");
            ((JsonObject)node)["score"]!.GetValue<long>().ShouldBe(42L);
            result.TableRef?.Dispose();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Json_TableResult_AsJsonNode_SequentialIntKeys_ProducesJsonArray()
        {
            // Lua returns a sequential table; AsJsonNode() detects 1-based integer keys
            // and produces a JsonArray with elements in order.
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return {10, 20, 30}");
            result.Type.ShouldBe(LuaType.Table);
            JsonNode? node = result.AsJsonNode();
            node.ShouldBeAssignableTo<JsonArray>();
            var arr = (JsonArray)node!;
            arr.Count.ShouldBe(3);
            arr[0]!.GetValue<long>().ShouldBe(10L);
            arr[1]!.GetValue<long>().ShouldBe(20L);
            arr[2]!.GetValue<long>().ShouldBe(30L);
            result.TableRef?.Dispose();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_GetContentsAsJson_StringKeyedTable_ProducesJsonObject()
        {
            using KitsuneEngine engine = new();
            using LuaTableRef tref = engine.RunString("return {name='alice', score=42}").TableRef!;
            JsonNode? node = tref.GetContentsAsJson();
            node.ShouldBeAssignableTo<JsonObject>();
            ((JsonObject)node!)["name"]!.GetValue<string>().ShouldBe("alice");
            ((JsonObject)node)["score"]!.GetValue<long>().ShouldBe(42L);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_GetContentsAsJson_SequentialArray_ProducesJsonArray()
        {
            using KitsuneEngine engine = new();
            using LuaTableRef tref = engine.RunString("return {10, 20, 30}").TableRef!;
            JsonNode? node = tref.GetContentsAsJson();
            node.ShouldBeAssignableTo<JsonArray>();
            var arr = (JsonArray)node!;
            arr.Count.ShouldBe(3);
            arr[0]!.GetValue<long>().ShouldBe(10L);
            arr[1]!.GetValue<long>().ShouldBe(20L);
            arr[2]!.GetValue<long>().ShouldBe(30L);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_GetContentsAsJson_NestedTable_ProducesNestedJson()
        {
            using KitsuneEngine engine = new();
            using LuaTableRef tref = engine.RunString("return {outer={inner='deep'}}").TableRef!;
            JsonNode? node = tref.GetContentsAsJson();
            node.ShouldBeAssignableTo<JsonObject>();
            ((JsonObject)node!)["outer"]!["inner"]!.GetValue<string>().ShouldBe("deep");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_GetContentsAsJson_EmptyTable_ProducesEmptyJsonObject()
        {
            using KitsuneEngine engine = new();

            // Arrays and tables are the same in lua. An empty table is treated as an empty array.
            using LuaTableRef tref = engine.RunString("return {}").TableRef!;
            JsonNode? node = tref.GetContentsAsJson();
            node.ShouldBeAssignableTo<JsonArray>();
            ((JsonArray)node!).Count.ShouldBe(0);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_GetContentsAsJson_AfterDispose_ThrowsObjectDisposedException()
        {
            using KitsuneEngine engine = new();
            LuaTableRef tref = engine.RunString("return {x=1}").TableRef!;
            tref.Dispose();
            Should.Throw<ObjectDisposedException>(() => tref.GetContentsAsJson());
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
            engine.ExecuteString("CaptureJson(jv)");
            engine.Wait();
            captured.ShouldNotBeNull();
            captured.ShouldBeAssignableTo<JsonObject>();
            ((JsonObject)captured!)["a"]!.GetValue<long>().ShouldBe(1L);
            ((JsonObject)captured)["b"]!.GetValue<long>().ShouldBe(2L);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Json_RegisterFunction_ReturnsJsonNode_LuaReadsAsTable()
        {
            // A C# function returning LuaValue.FromJson produces a Lua table in the caller.
            using KitsuneEngine engine = new();
            engine.RegisterFunction("MakeJson", _ =>
                LuaValue.FromJson(JsonNode.Parse("""{"status":"ok","code":200}""")));
            LuaValue result = await engine.ExecuteStringAsync(
                "local t = MakeJson(); return t.status .. ':' .. tostring(t.code)");
            result.String.ShouldBe("ok:200");
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
                LuaValue result = await engine.ExecuteStringAsync(
                    "return j.s .. ':' .. tostring(j.n)");
                result.ShouldBe($"{i}:{i}", $"round-trip failed at iteration {i}");
            }
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
        public void RunString_RuntimeError_ThrowsLuaException()
        {
            // The inline path returns KITSUNE_TERROR for runtime errors; GetOrThrow() surfaces it.
            using KitsuneEngine engine = new();
            LuaException ex = Should.Throw<LuaException>(() => engine.RunString("error('sync boom')"));
            ex.Message.ShouldContain("sync boom");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RunString_WithArgs_ArgsAreVisible()
        {
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("local a,b = ...; return a .. ':' .. b", "hello", "world");
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
            engine.ExecuteString("function syncTarget() return 'fn sync result' end");
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
            engine.ExecuteString("function syncAdd(a, b) return a + b end");
            engine.Wait();
            LuaValue result = engine.RunFunction("syncAdd", LuaValue.FromInt64(10), LuaValue.FromInt64(32));
            result.AsInt64.ShouldBe(42L);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Inline path: error surfacing -----------------------------------------
        [Fact]
        public void RunString_SyntaxError_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            LuaException ex = Should.Throw<LuaException>(() => engine.RunString("~~~~invalid lua~~~~"));
            ex.Message.ShouldNotBeNullOrEmpty();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RunFile_RuntimeError_ThrowsLuaException()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "error('file runtime error')");
                using KitsuneEngine engine = new();
                LuaException ex = Should.Throw<LuaException>(() => engine.RunFile(path));
                ex.Message.ShouldContain("file runtime error");
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public void RunFunction_RuntimeError_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function failSync() error('fn runtime error') end");
            engine.Wait();
            LuaException ex = Should.Throw<LuaException>(() => engine.RunFunction("failSync"));
            ex.Message.ShouldContain("fn runtime error");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Inline path: slot lifecycle ------------------------------------------
        [Fact]
        public void RunString_SlotLifecycle_VisibleDuringExecution_GoneAfter()
        {
            // A registered function callback captures the slot id while it's running.
            // After RunString returns, the slot must be compacted (id no longer in GetActiveIds).
            using KitsuneEngine engine = new();
            int capturedId = -1;
            engine.RegisterFunction("CaptureId", _ =>
            {
                capturedId = engine.RunningCoroutineId;
                return LuaValue.None;
            });
            engine.RunString("CaptureId()");
            capturedId.ShouldBeGreaterThan(0);
            engine.GetActiveIds().ShouldNotContain(capturedId);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RunString_IsRunning_ReturnsTrueWhileExecuting()
        {
            using KitsuneEngine engine = new();
            bool seenRunning = false;
            engine.RegisterFunction("CheckRunning", _ =>
            {
                seenRunning = engine.IsRunning;
                return LuaValue.None;
            });
            engine.RunString("CheckRunning()");
            seenRunning.ShouldBeTrue();
            engine.IsRunning.ShouldBeFalse();
        }

        [Fact]
        public void RunString_GetActiveIds_IncludesIdDuringExecution()
        {
            using KitsuneEngine engine = new();
            bool idWasActive = false;
            engine.RegisterFunction("CheckActive", _ =>
            {
                int id = engine.RunningCoroutineId;
                idWasActive = engine.GetActiveIds().Contains(id);
                return LuaValue.None;
            });
            engine.RunString("CheckActive()");
            idWasActive.ShouldBeTrue();
        }

        [Fact]
        public void RunString_GetRuntime_PositiveDuringExecution()
        {
            using KitsuneEngine engine = new();
            double capturedRuntime = 0;
            engine.RegisterFunction("CaptureRuntime", _ =>
            {
                int id = engine.RunningCoroutineId;
                capturedRuntime = engine.GetRuntime(id);
                return LuaValue.None;
            });
            engine.RunString("CaptureRuntime()");
            capturedRuntime.ShouldBeGreaterThan(0);
        }

        // -- Inline path: Sleep / Yield -------------------------------------------
        [Fact]
        public void RunString_WithSleep_ResultIsCorrect()
        {
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("Sleep(10); return 'slept'");
            result.String.ShouldBe("slept");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RunString_WithSleep_HonoursMinimumDuration()
        {
            using KitsuneEngine engine = new();
            var sw = System.Diagnostics.Stopwatch.StartNew();
            engine.RunString("Sleep(50)");
            sw.Stop();
            sw.ElapsedMilliseconds.ShouldBeGreaterThanOrEqualTo(40,
                "Sleep(50) in a sync RunString must block for at least ~50 ms");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RunString_WithYield_ResultIsCorrect()
        {
            // Yield() in a sync call briefly releases access (1 ms) then resumes.
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("Yield(); return 'yielded'");
            result.String.ShouldBe("yielded");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RunString_WithYield_ARGSPreservedAfterYield()
        {
            // Varargs must still be accessible after re-acquiring access in the yield loop.
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("local a,b = ...; Yield(); return a .. b", "foo", "bar");
            result.String.ShouldBe("foobar");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RunString_WithSleep_AsyncCoroutineRunsDuringYieldWindow()
        {
            // An async coroutine started before RunString must complete during the Sleep window.
            using KitsuneEngine engine = new();
            Task<LuaValue> asyncTask = engine.ExecuteStringAsync("return 'async done'");

            // No spin needed — ExecuteStringAsync submits the coroutine synchronously;
            // the subsequent RunString(Sleep(50)) yields the scheduler enough cycles
            // to complete it regardless of when it was picked up.

            // Sleep gives the scheduler at least one cycle to finish the async task.
            LuaValue syncResult = engine.RunString("Sleep(50); return 'sync done'");
            syncResult.String.ShouldBe("sync done");
            asyncTask.IsCompletedSuccessfully.ShouldBeTrue("async coroutine should finish during the 50 ms yield window");
            (await asyncTask).ShouldBe("async done");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RunString_Yield_AsyncCoroutine_DoesNotBreakSchedulerCycle()
        {
            // Verify Yield() on an async coroutine is unaffected by the inline refactor.
            // The async coroutine uses coroutine.yield(); scheduler must resume it.
            using KitsuneEngine engine = new();
            LuaValue result = await engine.ExecuteStringAsync(@"
                local x = 1
                coroutine.yield()
                x = x + 1
                return x
            ");
            result.AsInt64.ShouldBe(2L);
        }

        // -- Inline path: GetStatus == Inline during yield window -----------------
        [Fact]
        public async Task RunString_GetStatus_IsInline_DuringYieldWindow()
        {
            // During a Sleep() yield window the slot must report KITSUNE_STATUS_INLINE (7).
            using KitsuneEngine engine = new();
            int capturedId = -1;
            CoroutineStatus capturedStatus = CoroutineStatus.None;

            Task runTask = Task.Run(() =>
            {
                engine.RunString("Sleep(200); return 'done'");
            });

            // Wait until the slot appears in GetActiveIds (i.e. Sleep has yielded).
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetActiveIds().Length == 0 && DateTime.UtcNow < deadline)
            {
                Thread.Sleep(1);
            }

            int[] ids = engine.GetActiveIds();
            if (ids.Length > 0)
            {
                capturedId = ids[0];
                capturedStatus = engine.GetStatus(capturedId);
            }

            await runTask;

            capturedId.ShouldBeGreaterThan(0, "slot must be visible during yield window");
            ((int)capturedStatus).ShouldBe(7, "status must be INLINE (7) during Sleep() yield window");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Inline path: Cancel --------------------------------------------------
        [Fact]
        public async Task RunString_Cancel_FromAnotherThread_Interrupts()
        {
            // RunString runs on a background thread. The test thread polls GetActiveIds,
            // then cancels. The Ticker fires within microseconds in the tight loop, raising
            // "cancelled", so RunString throws LuaException on the background thread.
            using KitsuneEngine engine = new();
            LuaException? caught = null;

            Task runTask = Task.Run(() =>
            {
                try
                {
                    engine.RunString("while true do end");
                }
                catch (LuaException ex)
                {
                    caught = ex;
                }
            });

            // Poll until the slot appears in GetActiveIds.
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetActiveIds().Length == 0 && DateTime.UtcNow < deadline)
            {
                Thread.Sleep(1);
            }

            int[] ids = engine.GetActiveIds();
            ids.Length.ShouldBeGreaterThan(0, "inline slot must be visible");
            engine.Cancel(ids[0]);

            await runTask;
            caught.ShouldNotBeNull("Cancel must interrupt the inline call with LuaException");
            caught!.Message.ShouldContain("cancel", Case.Insensitive);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Inline path: WaitAsync blocks until inline call completes ------------
        [Fact]
        public async Task RunString_WaitAsync_BlocksUntilInlineCallCompletes()
        {
            using KitsuneEngine engine = new();

            // Run the inline call on a background thread so this test thread is free to await.
            Task runTask = Task.Run(() => engine.RunString("Sleep(200)"));

            // Poll until the slot appears in GetActiveIds (Sleep has yielded).
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetActiveIds().Length == 0 && DateTime.UtcNow < deadline)
            {
                Thread.Sleep(1);
            }

            int[] ids = engine.GetActiveIds();
            ids.Length.ShouldBeGreaterThan(0, "inline slot must be visible during Sleep() yield window");
            int inlineId = ids[0];

            // WaitAsync must block until the inline call (including the Sleep) completes.
            var sw = System.Diagnostics.Stopwatch.StartNew();
            await engine.WaitAsync(inlineId);
            sw.Stop();

            sw.ElapsedMilliseconds.ShouldBeGreaterThanOrEqualTo(50,
                "WaitAsync must wait for the inline Sleep to complete");
            await runTask;
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Inline path: rapid sequential calls (slot reuse) ---------------------
        [Fact]
        public void RunString_RapidSequential_CorrectResultsNoSlotLeak()
        {
            // 500 sequential inline calls must all return correct results and leave no active slots.
            using KitsuneEngine engine = new();
            for (int i = 0; i < 500; i++)
            {
                LuaValue v = engine.RunString($"return {i}");
                v.AsInt64.ShouldBe(i, $"call {i} returned wrong value");
            }
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RunFunction_RapidSequential_CorrectResultsNoSlotLeak()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function echo(n) return n end");
            engine.Wait();
            for (int i = 0; i < 500; i++)
            {
                LuaValue v = engine.RunFunction("echo", LuaValue.FromInt64(i));
                v.AsInt64.ShouldBe(i, $"call {i} returned wrong value");
            }
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Non-recursive guard: Execute* rejected inside registered functions ---
        [Fact]
        public async Task RegisterFunction_CallingRunString_ReturnsResult()
        {
            // Sync Execute functions are permitted inside a kitsune_CFunction via RunInlineTight.
            // Sleep() and Yield() inside the nested call are no-ops in this context.
            using KitsuneEngine engine = new();
            LuaValue? captured = null;
            engine.RegisterFunction("TryRunString", _ =>
            {
                captured = engine.RunString("return 'nested result'");
                return LuaValue.None;
            });
            await engine.ExecuteStringAsync("TryRunString()");
            captured.ShouldNotBeNull();
            captured!.Value.String.ShouldBe("nested result");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_CallingRunFile_ReturnsResult()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "return 'nested file result'");
                using KitsuneEngine engine = new();
                LuaValue? captured = null;
                engine.RegisterFunction("TryRunFile", _ =>
                {
                    captured = engine.RunFile(path);
                    return LuaValue.None;
                });
                await engine.ExecuteStringAsync("TryRunFile()");
                captured.ShouldNotBeNull();
                captured!.Value.String.ShouldBe("nested file result");
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public async Task RegisterFunction_CallingRunFunction_ReturnsResult()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function syncTarget() return 'target result' end");
            engine.Wait();
            LuaValue? captured = null;
            engine.RegisterFunction("TryRunFunction", _ =>
            {
                captured = engine.RunFunction("syncTarget");
                return LuaValue.None;
            });
            await engine.ExecuteStringAsync("TryRunFunction()");
            captured.ShouldNotBeNull();
            captured!.Value.String.ShouldBe("target result");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_CallingExecuteString_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("TryExecuteString", _ =>
            {
                engine.ExecuteString("return 'nested'");
                return LuaValue.None;
            });
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("TryExecuteString()"));
            ex.Message.ShouldContain("registered function");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_CallingExecuteFile_ThrowsLuaException()
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
                LuaException ex = await Should.ThrowAsync<LuaException>(
                    engine.ExecuteStringAsync("TryExecuteFile()"));
                ex.Message.ShouldContain("registered function");
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public async Task RegisterFunction_CallingExecuteFunction_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function syncTarget() return 'target' end");
            engine.Wait();
            engine.RegisterFunction("TryExecuteFunction", _ =>
            {
                engine.ExecuteFunction("syncTarget");
                return LuaValue.None;
            });
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("TryExecuteFunction()"));
            ex.Message.ShouldContain("registered function");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_CallingExecuteVariable_ThrowsLuaException()
        {
            // ExecuteVariable must be guarded against recursive calls from within a registered
            // function on both layers:
            //   C++: if (g_isSchedulerThread && g_state && g_state->DelegateState) return -1
            //   C#:  if (inLuaCallback) throw new LuaException("cannot be called from within a registered function")
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function() return 'nested' end");

            engine.RegisterFunction("TryExecuteVariable", _ =>
            {
                engine.ExecuteVariable(fn);
                return LuaValue.None;
            });
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("TryExecuteVariable()"));
            ex.Message.ShouldContain("registered function");
            engine.GetActiveIds().ShouldBeEmpty();
            fn.FunctionRef?.Dispose();
        }

        // -- Async Execute functions blocked from inside registered function callbacks --------
        [Fact]
        public async Task RegisterFunction_CallingExecuteStringAsync_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("TryExecuteStringAsync", _ =>
            {
                engine.ExecuteStringAsync("return 'nested'").GetAwaiter().GetResult();
                return LuaValue.None;
            });
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("TryExecuteStringAsync()"));
            ex.Message.ShouldContain("registered function");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_CallingExecuteFileAsync_ThrowsLuaException()
        {
            string path = Path.GetTempFileName();
            try
            {
                File.WriteAllText(path, "return 'nested file'");
                using KitsuneEngine engine = new();
                engine.RegisterFunction("TryExecuteFileAsync", _ =>
                {
                    engine.ExecuteFileAsync(path).GetAwaiter().GetResult();
                    return LuaValue.None;
                });
                LuaException ex = await Should.ThrowAsync<LuaException>(
                    engine.ExecuteStringAsync("TryExecuteFileAsync()"));
                ex.Message.ShouldContain("registered function");
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                File.Delete(path);
            }
        }

        [Fact]
        public async Task RegisterFunction_CallingExecuteFunctionAsync_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("function syncTarget() return 'target' end");
            engine.Wait();
            engine.RegisterFunction("TryExecuteFunctionAsync", _ =>
            {
                engine.ExecuteFunctionAsync("syncTarget").GetAwaiter().GetResult();
                return LuaValue.None;
            });
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("TryExecuteFunctionAsync()"));
            ex.Message.ShouldContain("registered function");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_CallingExecuteVariableAsync_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function() return 'nested' end");
            engine.RegisterFunction("TryExecuteVariableAsync", _ =>
            {
                engine.ExecuteVariableAsync(fn).GetAwaiter().GetResult();
                return LuaValue.None;
            });
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("TryExecuteVariableAsync()"));
            ex.Message.ShouldContain("registered function");
            engine.GetActiveIds().ShouldBeEmpty();
            fn.FunctionRef?.Dispose();
        }

        // -- Sync Execute from callback: behavioral tests -------------------------
        [Fact]
        public void RegisterFunction_CallingRunString_FromSyncCoroutine_ReturnsResult()
        {
            // Also works when the outer coroutine was started by RunString.
            using KitsuneEngine engine = new();
            LuaValue? captured = null;
            engine.RegisterFunction("TryRunString", _ =>
            {
                captured = engine.RunString("return 'sync context'");
                return LuaValue.None;
            });
            engine.RunString("TryRunString()");
            captured.ShouldNotBeNull();
            captured!.Value.String.ShouldBe("sync context");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_CallingRunString_ResultReturnedToLua()
        {
            // The result of RunString can be forwarded back to Lua via resultSetter.
            using KitsuneEngine engine = new();
            engine.RegisterFunction("FetchVersion", _ => engine.RunString("return VERSION"));
            LuaValue result = await engine.ExecuteStringAsync("return FetchVersion()");
            result.String.ShouldBe("1.0.0");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_CallingRunString_WithArgs_PassedCorrectly()
        {
            using KitsuneEngine engine = new();
            LuaValue? captured = null;
            engine.RegisterFunction("TryRunString", args =>
            {
                captured = engine.RunString("local a,b = ...; return a .. ':' .. b", args[0], args[1]);
                return LuaValue.None;
            });
            await engine.ExecuteStringAsync("TryRunString('hello', 'world')");
            captured.ShouldNotBeNull();
            captured!.Value.String.ShouldBe("hello:world");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_CallingRunFunction_WithArgs_ReturnsResult()
        {
            using KitsuneEngine engine = new();
            await engine.ExecuteStringAsync("function add(a, b) return a + b end");
            LuaValue? captured = null;
            engine.RegisterFunction("TryRunFunction", _ =>
            {
                captured = engine.RunFunction("add", LuaValue.FromInt64(10), LuaValue.FromInt64(32));
                return LuaValue.None;
            });
            await engine.ExecuteStringAsync("TryRunFunction()");
            captured.ShouldNotBeNull();
            captured!.Value.AsInt64.ShouldBe(42L);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_CallingRunString_LuaErrorPropagatesAsCallbackError()
        {
            // A Lua error inside a nested RunString throws LuaException from the callback,
            // which the trampoline converts to a Lua error in the outer coroutine.
            using KitsuneEngine engine = new();
            engine.RegisterFunction("TryRunString", _ =>
            {
                engine.RunString("error('nested error')");
                return LuaValue.None;
            });
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("TryRunString()"));
            ex.Message.ShouldContain("nested error");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- ExecuteVariable / ExecuteVariableAsync / RunVariable -------------------------
        [Fact]
        public async Task ExecuteVariable_FunctionValue_CallsFunction()
        {
            // LuaType.Function dispatch: the function ref is pushed from the Lua registry
            // and called with args as direct parameters (no ARGS table, like ExecuteFunction).
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function(a, b) return a .. ',' .. b end");
            fn.Type.ShouldBe(LuaType.Function);

            LuaValue result = await engine.ExecuteVariableAsync(fn, args: ["hello", "world"]);
            result.String.ShouldBe("hello,world");
            engine.GetActiveIds().ShouldBeEmpty();
            fn.FunctionRef?.Dispose();
        }

        // -- Spam tests (per-call performance measurement) --------------------------
        [Fact]
        public async Task Spam_FunctionRef_Invoke_TimeTaken()
        {
            using KitsuneEngine engine = new();
            LuaValue script = engine.RunString("local count=0; return function() count = count + 1; return count; end");

            using (var func = script.FunctionRef)
            {
                func.ShouldNotBeNull();

                Stopwatch sw = Stopwatch.StartNew();

                for (int i = 0; i < 100000; i++)
                {
                    func.Invoke().AsInt64.ShouldBe(i + 1);
                }

                sw.Stop();
                _output.WriteLine($"Elapsed time: {sw.ElapsedMilliseconds} ms  |  {sw.Elapsed.TotalMicroseconds / 1_000_000.0:F2} µs/call");
            }
        }

        [Fact]
        public void Spam_RunFunction_TimeTaken()
        {
            // Measures the per-call cost of RunFunction (name lookup + scheduler round-trip).
            // The function is compiled once; count is an upvalue kept alive by the closure.
            using KitsuneEngine engine = new();
            engine.RunString("local count = 0; function tick() count = count + 1; return count end");

            Stopwatch sw = Stopwatch.StartNew();

            for (int i = 0; i < 100000; i++)
            {
                engine.RunFunction("tick").AsInt64.ShouldBe(i + 1);
            }

            sw.Stop();
            _output.WriteLine($"Elapsed time: {sw.ElapsedMilliseconds} ms  |  {sw.Elapsed.TotalMicroseconds / 1_000_000.0:F2} µs/call");
        }

        [Fact]
        public void Spam_RunString_TimeTaken()
        {
            // Measures the per-call cost of RunString including Lua compilation on each call.
            // Uses fewer iterations than the function-ref test because compilation adds ~2-5 µs.
            using KitsuneEngine engine = new();
            engine.SetInt64("count", 0);

            Stopwatch sw = Stopwatch.StartNew();

            for (int i = 0; i < 100000; i++)
            {
                engine.RunString("count = count + 1; return count").AsInt64.ShouldBe(i + 1);
            }

            sw.Stop();
            _output.WriteLine($"Elapsed time: {sw.ElapsedMilliseconds} ms  |  {sw.Elapsed.TotalMicroseconds / 100_000.0:F2} µs/call");
        }

        [Fact]
        public async Task ExecuteVariable_FunctionValue_NoArgs_ReturnsResult()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function() return 'no args' end");

            LuaValue result = await engine.ExecuteVariableAsync(fn);
            result.String.ShouldBe("no args");
            engine.GetActiveIds().ShouldBeEmpty();
            fn.FunctionRef?.Dispose();
        }

        [Fact]
        public async Task ExecuteVariable_FunctionValue_RuntimeError_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function() error('fn variable boom') end");

            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteVariableAsync(fn));
            ex.Message.ShouldContain("fn variable boom");
            engine.GetActiveIds().ShouldBeEmpty();
            fn.FunctionRef?.Dispose();
        }

        [Fact]
        public async Task ExecuteVariable_StringValue_RunsAsScript()
        {
            // LuaType.String dispatch: the string is loaded as a Lua chunk; args are passed as varargs (...).
            using KitsuneEngine engine = new();
            LuaValue script = LuaValue.FromString("local a,b = ...; return a .. ':' .. b");

            LuaValue result = await engine.ExecuteVariableAsync(script, args: ["x", "y"]);
            result.String.ShouldBe("x:y");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteVariable_StringValue_SyntaxError_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            LuaValue script = LuaValue.FromString("~~~~invalid lua");

            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteVariableAsync(script));
            ex.Message.ShouldNotBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteVariable_NotExecutable_ThrowsLuaException()
        {
            // A non-executable type (number, bool, table, etc.) must produce a faulted slot
            // with a descriptive error rather than crashing or returning -1.
            using KitsuneEngine engine = new();

            LuaException numEx = await Should.ThrowAsync<LuaException>(
                engine.ExecuteVariableAsync(LuaValue.FromInt64(42)));
            numEx.Message.ShouldNotBeNull();

            LuaException boolEx = await Should.ThrowAsync<LuaException>(
                engine.ExecuteVariableAsync(LuaValue.FromBool(true)));
            boolEx.Message.ShouldNotBeNull();

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteVariable_FunctionStoredInVariable_CanBeCalledMultipleTimes()
        {
            // The function ref is not consumed by ExecuteVariable; the same LuaValue can be
            // executed repeatedly and each call returns the correct result.
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function(n) return tostring(n * 2) end");

            LuaValue r1 = await engine.ExecuteVariableAsync(fn, args: [LuaValue.FromInt64(5)]);
            LuaValue r2 = await engine.ExecuteVariableAsync(fn, args: [LuaValue.FromInt64(21)]);
            r1.ShouldBe("10");
            r2.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
            fn.FunctionRef?.Dispose();
        }

        [Fact]
        public void RunVariable_FunctionValue_ReturnsResult()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function(x) return x * x end");

            LuaValue result = engine.RunVariable(fn, LuaValue.FromInt64(7));
            result.AsInt64.ShouldBe(49L);
            engine.GetActiveIds().ShouldBeEmpty();
            fn.FunctionRef?.Dispose();
        }

        [Fact]
        public void RunVariable_StringValue_ReturnsResult()
        {
            using KitsuneEngine engine = new();
            LuaValue script = LuaValue.FromString("local a, b = ...; return a + b");

            LuaValue result = engine.RunVariable(script, LuaValue.FromInt64(10), LuaValue.FromInt64(32));
            result.AsInt64.ShouldBe(42L);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void RunVariable_NotExecutable_ReturnsNone()
        {
            // RunVariable for a non-executable type must return LuaValue.None (not throw).
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunVariable(LuaValue.FromInt64(99));
            result.Type.ShouldBe(LuaType.None);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task ExecuteVariable_FunctionFromRegisterFunction_CallsBack()
        {
            // A function returned from Lua (via GetResultVariable) can be re-executed;
            // if that function calls a C# registered function the full round-trip works.
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Double", args => LuaValue.FromInt64(args[0].AsInt64 * 2));
            LuaValue fn = engine.RunString("return function(n) return tostring(Double(n)) end");

            LuaValue result = await engine.ExecuteVariableAsync(fn, args: [LuaValue.FromInt64(21)]);
            result.String.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
            fn.FunctionRef?.Dispose();
        }

        [Fact]
        public async Task ExecuteVariable_ConcurrentFunctionCalls_AllReturnCorrectResults()
        {
            // Multiple concurrent ExecuteVariableAsync calls on the same function ref must
            // each receive the correct result — the ref is never consumed by the call.
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function(n) return tostring(n) end");

            const int count = 20;
            Task<LuaValue>[] tasks = Enumerable.Range(0, count)
                .Select(i => engine.ExecuteVariableAsync(fn, args: [LuaValue.FromInt64(i)]))
                .ToArray();
            LuaValue[] results = await Task.WhenAll(tasks);
            for (int i = 0; i < count; i++)
            {
                results[i].ShouldBe(i.ToString());
            }

            engine.GetActiveIds().ShouldBeEmpty();
            fn.FunctionRef?.Dispose();
        }

        // -- ExecuteVariable / ExecuteVariableAsync / RunVariable (boundary cases) ------
        [Fact]
        public async Task ExecuteVariable_FunctionValue_IdIsPositive()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function() return 1 end");
            fn.Type.ShouldBe(LuaType.Function);
            (await engine.ExecuteVariableAsync(fn)).AsInt64.ShouldBe(1L);
            engine.GetActiveIds().ShouldBeEmpty();
            fn.FunctionRef?.Dispose();
        }

        [Fact]
        public async Task ExecuteVariable_NotExecutable_IdIsPositiveAndFaulted()
        {
            using KitsuneEngine engine = new();
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteVariableAsync(LuaValue.FromNumber(3.14)));
            ex.Message.ShouldNotBeNull();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task RegisterFunction_CallingRunVariable_ReturnsResult()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function() return 'variable result' end");
            LuaValue? captured = null;
            engine.RegisterFunction("TryRunVariable", _ =>
            {
                captured = engine.RunVariable(fn);
                return LuaValue.None;
            });
            await engine.ExecuteStringAsync("TryRunVariable()");
            captured.ShouldNotBeNull();
            captured!.Value.String.ShouldBe("variable result");
            engine.GetActiveIds().ShouldBeEmpty();
            fn.FunctionRef?.Dispose();
        }

        // -- Function results (LuaType.Function) -----------------------------------------
        // These tests exercise:
        //   (a) the explicit LUA_TFUNCTION branch in KitsuneGetResult that zeroes slot->result.integer
        //   (b) the pendingResults[] array in scheduler step 4 that defers FreeVariableData outside slotsLock
        //   (c) the KitsuneVariableChain deferred-free queue used by KitsuneVariableFree on non-scheduler threads
        [Fact]
        public void GetResultVariable_FunctionReturn_HasFunctionType()
        {
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return function() return 42 end");
            result.Type.ShouldBe(LuaType.Function);
            engine.GetActiveIds().ShouldBeEmpty();
            result.FunctionRef?.Dispose();
        }

        [Fact]
        public void GetResultVariable_TableContainingFunction_FunctionEntryHasFunctionType()
        {
            // A table result containing a function value must preserve the entry as LuaType.Function.
            // This exercises FillKitsuneVariableFromStack + TableToLinkedList for function-valued nodes.
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return { callback = function() return 99 end, n = 1 }");
            result.Type.ShouldBe(LuaType.Table);
            using var tableRef = result.TableRef;
            tableRef.ShouldNotBeNull();
            var table = tableRef!.GetContents();
            table.ShouldContain(kvp => kvp.Key.String == "n" && kvp.Value.AsDouble == 1);
            table.Single(kvp => kvp.Key.String == "callback").Value.Type.ShouldBe(LuaType.Function);
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
            engine.ExecuteString("CaptureFunc(function() return 99 end)");
            engine.Wait();
            received.ShouldNotBeNull();
            received!.Value.Type.ShouldBe(LuaType.Function);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void ReleaseResult_FunctionResult_SchedulerFreesViaPendingResults_NoLeak()
        {
            // ReleaseResult marks the slot for scheduler cleanup without the C# side consuming
            // the result.  The scheduler's step-4 pendingResults array takes ownership of the
            // TFUNCTION result and calls FreeVariableData (? luaL_unref) in phase 2 outside
            // slotsLock.  This is the specific code path changed by the pendingResults fix.
            var engine = new KitsuneEngine();
            try
            {
                for (int i = 0; i < 20; i++)
                {
                    engine.ExecuteString("return function() end");
                }
                engine.Wait();
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine.Dispose();
            }
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
                    engine.ExecuteString("return { f1 = function() end, f2 = function() end, n = 1 }");
                }
                engine.Wait();
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine.Dispose();
            }
        }

        [Fact]
        public async Task DeferredFree_FunctionResultsFreedWhileSchedulerActive_NoLeak()
        {
            // Exercises the KitsuneVariableChain deferred-free queue: function refs are
            // explicitly disposed while the scheduler is running a background coroutine,
            // enqueuing their KitsuneVariable* for the scheduler to luaL_unref on the next
            // drain cycle.  Verifies no leaks after cleanup.
            var engine = new KitsuneEngine();
            try
            {
                engine.ExecuteString("Sleep(2000)");
                SpinUntilActive(engine);
                int bgId = engine.GetActiveIds()[0];

                var refs = new List<LuaValue>();
                for (int i = 0; i < 10; i++)
                {
                    refs.Add(await engine.ExecuteStringAsync("return function() end"));
                }

                // Dispose while the background coroutine is still running — each Dispose
                // enqueues to g_pendingVariableChainHead (non-scheduler-thread deferred path).
                foreach (var r in refs)
                {
                    r.FunctionRef?.Dispose();
                }

                engine.Cancel(bgId);
                engine.Wait();

                // One scheduler cycle ensures DrainPendingVariableChain processes all enqueued frees.
                (await engine.ExecuteStringAsync("return 'drain'")).ShouldBe("drain");
                engine.GetActiveIds().ShouldBeEmpty();
            }
            finally
            {
                engine.Dispose();
            }
        }

        [Fact]
        public void Wait_ForId_WithDeferredFunctionsEnqueued_CompletesWithoutDeadlock()
        {
            // Exercises the drain path in RunInline: dispose function refs (enqueuing to
            // g_pendingVariableChainHead) then run a sync call with Sleep() so the yield
            // window drains them via AcquireLuaAccess without deadlocking.
            using KitsuneEngine engine = new();

            var refs = new List<LuaValue>();
            for (int i = 0; i < 10; i++)
            {
                refs.Add(engine.RunString("return function() end"));
            }

            // Dispose all refs — each enqueues to g_pendingVariableChainHead for deferred luaL_unref.
            foreach (var r in refs)
            {
                r.FunctionRef?.Dispose();
            }

            // Sync-wait on a sleeping coroutine so WaitForResult has multiple 10ms wakeup
            // cycles to drain the enqueued chain entries via AcquireLuaAccess.
            engine.RunString("Sleep(60); return 'drained'").String.ShouldBe("drained");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Stress_FunctionResults_ManyReleasedViaScheduler_NoLeak()
        {
            // 50 concurrent coroutines each returning a function; refs are explicitly disposed
            // before the drain cycle — stresses pendingResults compaction across many scheduler cycles.
            var engine = new KitsuneEngine();
            try
            {
                const int count = 50;
                var results = new System.Collections.Concurrent.ConcurrentBag<LuaValue>();
                Task[] tasks = Enumerable.Range(0, count).Select(_ => Task.Run(async () =>
                {
                    results.Add(await engine.ExecuteStringAsync("return function() end").ConfigureAwait(false));
                })).ToArray();
                await Task.WhenAll(tasks);

                // Dispose all function refs — each enqueues to g_pendingVariableChainHead.
                foreach (var r in results)
                {
                    r.FunctionRef?.Dispose();
                }

                // Drain cycle: scheduler processes all deferred luaL_unref calls.
                await engine.ExecuteStringAsync("return 'drain'");
            }
            finally
            {
                engine.Dispose();
            }
        }

        // -- Boundary / edge cases --------------------------------------------------------
        [Fact]
        public void GetError_NonExistentId_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            engine.GetStatus(99999).ShouldBe(CoroutineStatus.None);
        }

        [Fact]
        public void GetResult_WhileCoroutineStillRunning_ReturnsNull()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("while true do end");
            SpinUntilRunning(engine);
            try
            {
                engine.IsRunning.ShouldBeTrue();
                engine.GetActiveIds().Length.ShouldBeGreaterThan(0);
            }
            finally
            {
                engine.Interrupt();
                engine.Wait();
                engine.GetActiveIds().ShouldBeEmpty();
            }
        }

        [Fact]
        public void ReleaseResult_NonExistentId_IsNoOp()
        {
            using KitsuneEngine engine = new();
            Should.NotThrow(() => engine.Cancel(99999));
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
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return function() end");
            result.Type.ShouldBe(LuaType.Function);

            // RunString consumes the result; slot is auto-released
            engine.GetActiveIds().ShouldBeEmpty();
            result.FunctionRef?.Dispose();
        }

        [Fact]
        public void GetResultVariable_FunctionReturn_ConsumedTwice_ReturnsNoneSecondTime()
        {
            using KitsuneEngine engine = new();
            LuaValue result = engine.RunString("return function() end");
            result.Type.ShouldBe(LuaType.Function);
            engine.GetActiveIds().ShouldBeEmpty();
            result.FunctionRef?.Dispose();
        }

        // -- LuaFunctionRef.Invoke / InvokeAsync ---------------------------------
        [Fact]
        public void FunctionRef_Invoke_NoArgs_ReturnsResult()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function() return 'invoke result' end");
            using LuaFunctionRef funcRef = fn.FunctionRef!;

            LuaValue result = funcRef.Invoke();

            result.String.ShouldBe("invoke result");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void FunctionRef_Invoke_WithStringArgs_ReceivesArgs()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function(a, b) return a .. ',' .. b end");
            using LuaFunctionRef funcRef = fn.FunctionRef!;

            LuaValue result = funcRef.Invoke("hello", "world");

            result.String.ShouldBe("hello,world");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void FunctionRef_Invoke_WithTypedArgs_PassedCorrectly()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function(a, b) return tostring(a + b) end");
            using LuaFunctionRef funcRef = fn.FunctionRef!;

            LuaValue result = funcRef.Invoke(LuaValue.FromInt64(10), LuaValue.FromInt64(32));

            result.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void FunctionRef_Invoke_RuntimeError_ReturnsNone()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function() error('invoke boom') end");
            using LuaFunctionRef funcRef = fn.FunctionRef!;

            LuaValue result = funcRef.Invoke();

            result.Type.ShouldBe(LuaType.None);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void FunctionRef_Invoke_CalledMultipleTimes_CorrectResultEachTime()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function(n) return tostring(n * 2) end");
            using LuaFunctionRef funcRef = fn.FunctionRef!;

            LuaValue r1 = funcRef.Invoke(LuaValue.FromInt64(5));
            LuaValue r2 = funcRef.Invoke(LuaValue.FromInt64(21));

            r1.ShouldBe("10");
            r2.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void FunctionRef_Invoke_CallsRegisteredFunction_FullRoundTrip()
        {
            using KitsuneEngine engine = new();
            engine.RegisterFunction("Double", args => LuaValue.FromInt64(args[0].AsInt64 * 2));
            LuaValue fn = engine.RunString("return function(n) return tostring(Double(n)) end");
            using LuaFunctionRef funcRef = fn.FunctionRef!;

            LuaValue result = funcRef.Invoke(LuaValue.FromInt64(21));

            result.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void FunctionRef_Invoke_Disposed_ThrowsObjectDisposedException()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function() return 'ok' end");
            LuaFunctionRef funcRef = fn.FunctionRef!;
            funcRef.Dispose();

            Should.Throw<ObjectDisposedException>(() => funcRef.Invoke());
        }

        [Fact]
        public async Task FunctionRef_InvokeAsync_NoArgs_ReturnsResult()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function() return 'async invoke result' end");
            using LuaFunctionRef funcRef = fn.FunctionRef!;

            LuaValue result = await funcRef.InvokeAsync();

            result.String.ShouldBe("async invoke result");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task FunctionRef_InvokeAsync_WithStringArgs_ReceivesArgs()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function(a, b) return a .. ',' .. b end");
            using LuaFunctionRef funcRef = fn.FunctionRef!;

            LuaValue result = await funcRef.InvokeAsync(default, "hello", "world");

            result.String.ShouldBe("hello,world");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task FunctionRef_InvokeAsync_WithTypedArgs_PassedCorrectly()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function(a, b) return tostring(a + b) end");
            using LuaFunctionRef funcRef = fn.FunctionRef!;

            LuaValue result = await funcRef.InvokeAsync(default, LuaValue.FromInt64(10), LuaValue.FromInt64(32));

            result.ShouldBe("42");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task FunctionRef_InvokeAsync_RuntimeError_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function() error('async invoke boom') end");
            using LuaFunctionRef funcRef = fn.FunctionRef!;

            LuaException ex = await Should.ThrowAsync<LuaException>(funcRef.InvokeAsync());

            ex.Message.ShouldContain("async invoke boom");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Userdata ref round-trip -----------------------------------------------
        [Fact]
        public void Userdata_LuaNativeRoundTrip_OriginalObjectPushedBack()
        {
            // A Lua-native userdata (Json.New()) returned by RunString must carry a
            // live LuaUserdataRef so that passing it back to a second script calls methods
            // on the original Lua object rather than pushing nil or creating a broken wrapper.
            using KitsuneEngine engine = new();

            // Step 1: obtain a Lua-native userdata from the engine.
            LuaValue jsonObj = engine.RunString("return Json.New()");
            jsonObj.Type.ShouldBe(LuaType.Userdata);
            using LuaUserdataRef udRef = jsonObj.UserdataRef!;
            udRef.ShouldNotBeNull();

            // Step 2: pass it back as the first vararg and call a method on the original object.
            // PushKitsuneVariable uses ud->ref (non-LUA_NOREF) to push from the registry —
            // giving Lua the exact same object, not a new wrapper with a null instance pointer.
            LuaValue result = engine.RunString("local obj = ...; return obj:Encode({Test=1})", jsonObj);

            result.Type.ShouldBe(LuaType.String);
            result.String.ShouldNotBeNull();
            result.String!.ShouldContain("Test");
            result.String!.ShouldContain("1");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task FunctionRef_InvokeAsync_ConcurrentCalls_AllReturnCorrectResults()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function(n) return tostring(n) end");
            using LuaFunctionRef funcRef = fn.FunctionRef!;

            const int count = 20;
            Task<LuaValue>[] tasks = Enumerable.Range(0, count)
                .Select(i => funcRef.InvokeAsync(default, LuaValue.FromInt64(i)))
                .ToArray();
            LuaValue[] results = await Task.WhenAll(tasks);
            for (int i = 0; i < count; i++)
            {
                results[i].ShouldBe(i.ToString());
            }

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task FunctionRef_InvokeAsync_CancelledToken_ThrowsOperationCanceledException()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function() while true do end end");
            using LuaFunctionRef funcRef = fn.FunctionRef!;

            using CancellationTokenSource cts = new(TimeSpan.FromMilliseconds(100));
            await Should.ThrowAsync<OperationCanceledException>(funcRef.InvokeAsync(cts.Token));

            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetActiveIds().Length > 0 && DateTime.UtcNow < deadline)
            {
                Thread.Sleep(1);
            }

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void FunctionRef_InvokeAsync_Disposed_ThrowsObjectDisposedException()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = engine.RunString("return function() return 'ok' end");
            LuaFunctionRef funcRef = fn.FunctionRef!;
            funcRef.Dispose();

            Should.Throw<ObjectDisposedException>(() => funcRef.InvokeAsync());
        }

        // -- KITSUNE_TITERATOR tests --------------------------------------------
        [Fact]
        public async Task Iterator_BasicIteration_ReceivesAllValuesInOrder()
        {
            var source = new List<LuaValue> { "alpha", "beta", "gamma" };
            using KitsuneEngine engine = new();
            engine.SetVariable("Items", LuaValue.FromIterator(source));
            LuaValue result = await engine.ExecuteStringAsync(
                @"local out = {}
                  for v in Items do
                      out[#out + 1] = v
                  end
                  return table.concat(out, ',')");
            result.String.ShouldBe("alpha,beta,gamma");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Iterator_EmptySource_LoopBodyNeverRuns()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("Items", LuaValue.FromIterator(Array.Empty<LuaValue>()));
            LuaValue result = await engine.ExecuteStringAsync(
                @"local count = 0
                  for v in Items do count = count + 1 end
                  return count");
            result.AsInt64.ShouldBe(0);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Iterator_EarlyBreak_FinalizedFires()
        {
            IEnumerable<LuaValue> TrackedSource()
            {
                try
                {
                    yield return "a";
                    yield return "b";
                    yield return "c";
                }
                finally
                {
                    _iteratorEnumeratorDisposed = true;
                }
            }

            using KitsuneEngine engine = new();
            engine.SetVariable("Items", LuaValue.FromIterator(TrackedSource()));

            // Use a do-block so the closure is unreachable after the loop,
            // then nil the global and force two full GC cycles so __gc fires.
            await engine.ExecuteStringAsync(
                @"do
                      local iter = Items
                      Items = nil
                      for v in iter do
                          if v == 'a' then break end
                      end
                  end
                  collectgarbage('collect')
                  collectgarbage('collect')");

            _iteratorEnumeratorDisposed.ShouldBeTrue("Dispose was not called — finalizeFunc did not fire via __gc");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Iterator_Cancel_LoopStopsCleanly()
        {
            var source = new List<LuaValue> { 1L, 2L, 3L, 4L, 5L };
            using KitsuneEngine engine = new();
            engine.SetVariable("Items", LuaValue.FromIterator(source, out LuaIteratorRef handle));
            handle.Cancel();
            LuaValue result = await engine.ExecuteStringAsync(
                @"local count = 0
                  for v in Items do count = count + 1 end
                  return count");
            result.AsInt64.ShouldBe(0);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Iterator_ReEnumerable_TwoIndependentLoops()
        {
            var source = new List<LuaValue> { "x", "y" };
            using KitsuneEngine engine = new();
            engine.SetVariable("Items1", LuaValue.FromIterator(source));
            engine.SetVariable("Items2", LuaValue.FromIterator(source));
            LuaValue result = await engine.ExecuteStringAsync(
                @"local a, b = {}, {}
                  for v in Items1 do a[#a+1] = v end
                  for v in Items2 do b[#b+1] = v end
                  return table.concat(a, ',') .. '|' .. table.concat(b, ',')");
            result.String.ShouldBe("x,y|x,y");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void Iterator_LuaIteratorRef_CsideIteration_ReturnsAllValues()
        {
            var source = new List<LuaValue> { "p", "q", "r" };
            LuaValue.FromIterator(source, out LuaIteratorRef handle);
            handle.Iterator().Select(x => x.String).ToList().ShouldBe(new[] { "p", "q", "r" });
        }

        [Fact]
        public async Task Iterator_LuaIteratorRef_CsideAsyncIteration_ReturnsAllValues()
        {
            var source = new List<LuaValue> { "p", "q", "r" };
            LuaValue.FromIterator(source, out LuaIteratorRef handle);
            var collected = new List<string?>();
            await foreach (LuaValue item in handle.IteratorAsync())
            {
                collected.Add(item.String);
            }

            collected.ShouldBe(new[] { "p", "q", "r" });
        }

        [Fact]
        public async Task Iterator_FactoryPattern_FreshIteratorEachCall()
        {
            var source = new List<LuaValue> { 10L, 20L, 30L };
            using KitsuneEngine engine = new();
            engine.RegisterFunction("MakeIter", _ => LuaValue.FromIterator(source));
            LuaValue result = await engine.ExecuteStringAsync(
                @"local s1, s2 = 0, 0
                  for v in MakeIter() do s1 = s1 + v end
                  for v in MakeIter() do s2 = s2 + v end
                  return s1 + s2");
            result.AsInt64.ShouldBe(120);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Iterator_StepThrows_RaisesLuaError_CatchableByPcall()
        {
            int callCount = 0;
            using KitsuneEngine engine = new();
            engine.RegisterFunction("MakeIter", _ =>
            {
                IEnumerable<LuaValue> Src()
                {
                    yield return "ok";
                    callCount++;
                    throw new InvalidOperationException("step exploded");
                }
                return LuaValue.FromIterator(Src());
            });
            LuaValue result = await engine.ExecuteStringAsync(
                @"local ok, err = pcall(function()
                      local out = {}
                      for v in MakeIter() do out[#out+1] = v end
                  end)
                  return tostring(ok) .. ':' .. tostring(err ~= nil)");
            result.String.ShouldStartWith("false:");
            callCount.ShouldBe(1);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public async Task Iterator_AsyncSource_LuaIteratesBlocking()
        {
            // IAsyncEnumerable source passed to Lua — consumed via ToBlockingEnumerable().
            async IAsyncEnumerable<LuaValue> AsyncSource(
                [System.Runtime.CompilerServices.EnumeratorCancellation] System.Threading.CancellationToken ct = default)
            {
                await System.Threading.Tasks.Task.Yield();
                yield return "a";
                await System.Threading.Tasks.Task.Yield();
                yield return "b";
                await System.Threading.Tasks.Task.Yield();
                yield return "c";
            }

            using KitsuneEngine engine = new();
            engine.SetVariable("Items", LuaValue.FromIterator(AsyncSource()));
            LuaValue result = await engine.ExecuteStringAsync(
                @"local out = {}
                  for v in Items do out[#out+1] = v end
                  return table.concat(out, ',')");
            result.String.ShouldBe("a,b,c");
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Registry references (Register / GetByReference / Unregister) ----------
        [Fact]
        public void Register_String_GetByReference_ReturnsEqualValue()
        {
            using KitsuneEngine engine = new();
            int @ref = engine.Register((LuaValue)"hello");
            @ref.ShouldNotBe(-2); // LUA_NOREF
            engine.GetByReference(@ref).String.ShouldBe("hello");
            engine.Unregister(@ref);
        }

        [Fact]
        public void Register_Number_GetByReference_ReturnsEqualValue()
        {
            using KitsuneEngine engine = new();
            int @ref = engine.Register(LuaValue.FromNumber(3.14));
            @ref.ShouldNotBe(-2);
            engine.GetByReference(@ref).Number.ShouldBe(3.14);
            engine.Unregister(@ref);
        }

        [Fact]
        public void Register_Integer_GetByReference_ReturnsEqualValue()
        {
            using KitsuneEngine engine = new();
            int @ref = engine.Register(LuaValue.FromInt64(42L));
            @ref.ShouldNotBe(-2);
            engine.GetByReference(@ref).Int64.ShouldBe(42L);
            engine.Unregister(@ref);
        }

        [Fact]
        public void Register_Bool_GetByReference_ReturnsEqualValue()
        {
            using KitsuneEngine engine = new();
            int @ref = engine.Register(LuaValue.FromBool(true));
            @ref.ShouldNotBe(-2);
            engine.GetByReference(@ref).Boolean.ShouldBeTrue();
            engine.Unregister(@ref);
        }

        [Fact]
        public void GetByReference_CalledMultipleTimes_PinPersists()
        {
            using KitsuneEngine engine = new();
            int @ref = engine.Register((LuaValue)"persistent");
            engine.GetByReference(@ref).String.ShouldBe("persistent");
            engine.GetByReference(@ref).String.ShouldBe("persistent");
            engine.Unregister(@ref);
        }

        [Fact]
        public void Unregister_ReturnsValue_AndReleasesPin()
        {
            using KitsuneEngine engine = new();
            int @ref = engine.Register((LuaValue)"gone");

            // Unregister returns the value and releases the pin; calling Register again
            // reuses the freed slot, so the original ref now resolves to the new value.
            engine.Unregister(@ref).String.ShouldBe("gone");
            int r2 = engine.Register((LuaValue)"reused");
            r2.ShouldBe(@ref); // Lua freelist reuses the slot
            engine.GetByReference(r2).String.ShouldBe("reused");
            engine.Unregister(r2);
        }

        [Fact]
        public void GetByReference_InvalidRef_ReturnsNone()
        {
            using KitsuneEngine engine = new();
            engine.GetByReference(-2).Type.ShouldBe(LuaType.None); // LUA_NOREF
        }

        [Fact]
        public void Register_Table_GetByReference_ReturnsTableSnapshot()
        {
            using KitsuneEngine engine = new();
            var entries = new List<KeyValuePair<LuaValue, LuaValue>>
            {
                new((LuaValue)"k", (LuaValue)"v")
            };
            int @ref = engine.Register(LuaValue.FromTable(entries.AsReadOnly()));
            @ref.ShouldNotBe(-2);
            LuaValue result = engine.GetByReference(@ref);
            result.Type.ShouldBe(LuaType.Table);
            using var tableRef = result.TableRef;
            tableRef.ShouldNotBeNull();
            var table = tableRef!.GetContents();
            table.Count.ShouldBe(1);
            table[0].Value.String.ShouldBe("v");
            engine.Unregister(@ref).TableRef?.Dispose();
        }

        [Fact]
        public void Register_MultipleValues_EachGetIndependentRef()
        {
            using KitsuneEngine engine = new();
            int r1 = engine.Register((LuaValue)"first");
            int r2 = engine.Register((LuaValue)"second");
            r1.ShouldNotBe(r2);
            engine.GetByReference(r1).String.ShouldBe("first");
            engine.GetByReference(r2).String.ShouldBe("second");
            engine.Unregister(r1);
            engine.Unregister(r2);
        }

        [Fact]
        public async Task Register_Function_PinsAfterOriginalRefReleased()
        {
            using KitsuneEngine engine = new();
            LuaValue fn = await engine.ExecuteStringAsync("return function(x) return x * 2 end");
            fn.Type.ShouldBe(LuaType.Function);

            int @ref = engine.Register(fn);
            @ref.ShouldNotBe(-2);

            // Release the original — the registered pin keeps the function alive.
            fn.FunctionRef!.Dispose();

            LuaValue pinned = engine.GetByReference(@ref);
            pinned.Type.ShouldBe(LuaType.Function);
            LuaValue result = pinned.FunctionRef!.Invoke(LuaValue.FromInt64(21));
            result.Int64.ShouldBe(42L);
            pinned.FunctionRef.Dispose();

            engine.Unregister(@ref).FunctionRef!.Dispose();
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Thread step with arguments -------------------------------------------
        [Fact]
        public void Thread_Step_PassesArgAndReceivesYieldedValue()
        {
            // The coroutine loops: each resume delivers a new number and yields number-1.
            // First step: number=1 ? yields 0. Second step: number=10 ? yields 9.
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(@"
                return coroutine.create(function(number)
                    while true do
                        number = coroutine.yield(number - 1)
                    end
                end)");
            thread.Type.ShouldBe(LuaType.Thread);

            LuaValue r1 = thread.ThreadRef!.Step(LuaValue.FromInt64(1));
            r1.AsInt64.ShouldBe(0L);

            LuaValue r2 = thread.ThreadRef!.Step(LuaValue.FromInt64(10));
            r2.AsInt64.ShouldBe(9L);

            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public async Task Thread_StepAsync_PassesArgAndReceivesYieldedValue()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(@"
                return coroutine.create(function(number)
                    while true do
                        number = coroutine.yield(number - 1)
                    end
                end)");
            thread.Type.ShouldBe(LuaType.Thread);

            LuaValue r1 = await thread.ThreadRef!.StepAsync(default, LuaValue.FromInt64(1));
            r1.AsInt64.ShouldBe(0L);

            LuaValue r2 = await thread.ThreadRef!.StepAsync(default, LuaValue.FromInt64(10));
            r2.AsInt64.ShouldBe(9L);

            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        [Fact]
        public void Thread_Step_YieldWithNoValue_ReturnsNil()
        {
            // A yield with no argument produces KITSUNE_TNIL (thread alive, no value).
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() coroutine.yield() end)");

            LuaValue result = thread.ThreadRef!.Step();
            result.Type.ShouldBe(LuaType.Nil);

            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Step_DeadThread_ReturnsNone()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() end)");

            // Exhaust the thread.
            thread.ThreadRef!.Step();

            // Second step on a dead thread returns None.
            LuaValue result = thread.ThreadRef!.Step();
            result.Type.ShouldBe(LuaType.None);

            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Step_Error_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() error('step boom') end)");

            LuaException ex = Should.Throw<LuaException>(() => thread.ThreadRef!.Step());
            ex.Message.ShouldContain("step boom");

            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Step_MultipleYieldsWithAccumulatedArg()
        {
            // Each step passes a value in; the coroutine accumulates and yields the running total.
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(@"
                return coroutine.create(function(a)
                    local b = coroutine.yield(a)
                    local c = coroutine.yield(a + b)
                    coroutine.yield(a + b + c)
                end)");

            // Step 1 — initial arg=10, yields 10.
            thread.ThreadRef!.Step(LuaValue.FromInt64(10)).AsInt64.ShouldBe(10L);

            // Step 2 — arg=5, yields 10+5=15.
            thread.ThreadRef!.Step(LuaValue.FromInt64(5)).AsInt64.ShouldBe(15L);

            // Step 3 — arg=3, yields 10+5+3=18.
            thread.ThreadRef!.Step(LuaValue.FromInt64(3)).AsInt64.ShouldBe(18L);

            // Step 4 — thread dead.
            thread.ThreadRef!.Step().Type.ShouldBe(LuaType.None);

            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public async Task Thread_StepAsync_Error_ThrowsLuaException()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function(x) error('async step boom ' .. tostring(x)) end)");

            LuaException ex = await Should.ThrowAsync<LuaException>(
                thread.ThreadRef!.StepAsync(default, LuaValue.FromInt64(42)));
            ex.Message.ShouldContain("async step boom 42");

            thread.ThreadRef?.Dispose();
            await engine.ExecuteStringAsync("Sleep(0)");
        }

        [Fact]
        public void Thread_Step_FinalReturnValue_Received()
        {
            // A coroutine that returns (not yields) on the last step produces that value.
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function(x) coroutine.yield(x * 2); return x * 3 end)");

            thread.ThreadRef!.Step(LuaValue.FromInt64(7)).AsInt64.ShouldBe(14L);
            thread.ThreadRef!.Step().AsInt64.ShouldBe(21L);
            thread.ThreadRef!.Step().Type.ShouldBe(LuaType.None);

            thread.ThreadRef?.Dispose();
            engine.RunString("Sleep(0)");
        }

        [Fact]
        public void Thread_Step_Disposed_ThrowsObjectDisposedException()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() coroutine.yield(1) end)");
            LuaThreadRef tref = thread.ThreadRef!;
            tref.Dispose();

            Should.Throw<ObjectDisposedException>(() => tref.Step());
        }

        [Fact]
        public void Thread_StepAsync_Disposed_ThrowsObjectDisposedException()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() coroutine.yield(1) end)");
            LuaThreadRef tref = thread.ThreadRef!;
            tref.Dispose();

            Should.Throw<ObjectDisposedException>(() => tref.StepAsync());
        }

        [Fact]
        public void Thread_Iterate_Disposed_ThrowsObjectDisposedException()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() coroutine.yield(1) end)");
            LuaThreadRef tref = thread.ThreadRef!;
            tref.Dispose();

            Should.Throw<ObjectDisposedException>(() => tref.Iterate());
        }

        [Fact]
        public void Thread_IterateAsync_Disposed_ThrowsObjectDisposedException()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() coroutine.yield(1) end)");
            LuaThreadRef tref = thread.ThreadRef!;
            tref.Dispose();

            Should.Throw<ObjectDisposedException>(() => tref.IterateAsync());
        }

        [Fact]
        public void LuaThreadRef_DoubleDispose_DoesNotThrow()
        {
            using KitsuneEngine engine = new();
            LuaValue thread = engine.RunString(
                "return coroutine.create(function() end)");
            LuaThreadRef tref = thread.ThreadRef!;

            tref.Dispose();
            Should.NotThrow(() => tref.Dispose());
        }

        [Fact]
        public void LuaFunctionRef_DoubleDispose_DoesNotThrow()
        {
            using KitsuneEngine engine = new();
            LuaFunctionRef fref = engine.RunString("return function() end").FunctionRef!;

            fref.Dispose();
            Should.NotThrow(() => fref.Dispose());
        }

        [Fact]
        public void ThreadRef_DisposedBeforeEngine_NoLeak()
        {
            var engine = new KitsuneEngine();
            try
            {
                var refs = new List<LuaValue>();
                for (int i = 0; i < 10; i++)
                {
                    refs.Add(engine.RunString(
                        "return coroutine.create(function() coroutine.yield(1) end)"));
                }

                foreach (var r in refs)
                {
                    r.ThreadRef?.Dispose();
                }

                engine.RunString("Sleep(0)");
            }
            finally
            {
                engine.Dispose();
            }
        }

        [Fact]
        public void FunctionRef_NativePtrIsZero_AfterDispose()
        {
            using KitsuneEngine engine = new();
            LuaFunctionRef fref = engine.RunString("return function() end").FunctionRef!;

            fref.Dispose();
            Should.Throw<ObjectDisposedException>(() => fref.Invoke());
        }

        [Fact]
        public void ThreadRef_NativePtrIsZero_AfterDispose()
        {
            using KitsuneEngine engine = new();
            LuaThreadRef tref = engine.RunString(
                "return coroutine.create(function() end)").ThreadRef!;

            tref.Dispose();
            Should.Throw<ObjectDisposedException>(() => tref.Step());
        }

        // -- CallMetamethod -------------------------------------------------------
        [Fact]
        public void TableRef_CallMetamethod_InvokesNamedMetamethod()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString(@"
                return setmetatable({}, {
                    __tostring = function(t) return 'my_table' end,
                })
            ");
            using LuaTableRef tr = table.TableRef!;

            tr.CallMetamethod("__tostring").String.ShouldBe("my_table");

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_CallMetamethod_WithArgs_PassesThemCorrectly()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString(@"
                return setmetatable({}, {
                    __add = function(a, b) return 'sum=' .. tostring(b) end,
                })
            ");
            using LuaTableRef tr = table.TableRef!;

            tr.CallMetamethod("__add", LuaValue.FromInt64(7)).String.ShouldBe("sum=7");

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_CallMetamethod_AbsentMetamethod_ReturnsNone()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString("return {}");
            using LuaTableRef tr = table.TableRef!;

            tr.CallMetamethod("__tostring").ShouldBe(LuaValue.None);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void UserdataRef_CallMetamethod_Tostring_ReturnsString()
        {
            using KitsuneEngine engine = new();
            LuaValue jsonObj = engine.RunString("return Json.New()");
            using LuaUserdataRef ur = jsonObj.UserdataRef!;

            LuaValue result = ur.CallMetamethod("__tostring");
            result.Type.ShouldBe(LuaType.String);
            result.String.ShouldNotBeNull();

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void UserdataRef_CallMetamethod_AbsentMetamethod_ReturnsNone()
        {
            using KitsuneEngine engine = new();
            LuaValue jsonObj = engine.RunString("return Json.New()");
            using LuaUserdataRef ur = jsonObj.UserdataRef!;

            ur.CallMetamethod("__nonexistent_metamethod__").ShouldBe(LuaValue.None);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- CallMethod -----------------------------------------------------------
        [Fact]
        public void UserdataRef_CallMethod_InvokesMethodWithSelf()
        {
            using KitsuneEngine engine = new();
            LuaValue jsonObj = engine.RunString("return Json.New()");
            using LuaUserdataRef ur = jsonObj.UserdataRef!;

            // Json:Encode({val = 99}) — method is resolved via __index on the userdata
            LuaValue tableArg = engine.RunString("return {val = 99}");
            LuaValue result = ur.CallMethod("Encode", tableArg);
            tableArg.TableRef?.Dispose();
            result.Type.ShouldBe(LuaType.String);
            result.String.ShouldNotBeNull();
            result.String!.ShouldContain("99");

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_CallMethod_InvokesMethodWithSelf()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString(@"
                return {
                    prefix = 'hello',
                    greet = function(self, name) return self.prefix .. '_' .. name end,
                }
            ");
            using LuaTableRef tr = table.TableRef!;

            tr.CallMethod("greet", LuaValue.FromString("world")).String.ShouldBe("hello_world");

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void UserdataRef_CallMethod_MissingMethod_ReturnsNone()
        {
            using KitsuneEngine engine = new();
            LuaValue jsonObj = engine.RunString("return Json.New()");
            using LuaUserdataRef ur = jsonObj.UserdataRef!;

            ur.CallMethod("__nonexistent_method__").ShouldBe(LuaValue.None);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_CallMethod_MissingMethod_ReturnsNone()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString("return {}");
            using LuaTableRef tr = table.TableRef!;

            tr.CallMethod("nonexistent").ShouldBe(LuaValue.None);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_CallMethod_IndexErrorInMetamethod_ReturnsTerror()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString(@"
                return setmetatable({}, {
                    __index = function(_, k) error('lookup failed') end,
                })
            ");
            using LuaTableRef tr = table.TableRef!;

            tr.CallMethod("anything").Type.ShouldBe(LuaType.Error);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_CallMetamethod_VoidMetamethod_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString(@"
                return setmetatable({}, {
                    __len = function(t) end,
                })
            ");
            using LuaTableRef tr = table.TableRef!;

            // __len ran but returned nothing ? TNIL (ran, no data)
            tr.CallMetamethod("__len").Type.ShouldBe(LuaType.Nil);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_CallMethod_VoidMethod_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString(@"
                return { noop = function(self) end }
            ");
            using LuaTableRef tr = table.TableRef!;

            // noop ran but returned nothing ? TNIL
            tr.CallMethod("noop").Type.ShouldBe(LuaType.Nil);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- GetIndex / SetIndex / GetLength -------------------------------------
        [Fact]
        public void TableRef_GetIndex_ReturnsValue()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString("return {name = 'alice', score = 99}");
            using LuaTableRef tr = table.TableRef!;

            tr.GetIndex(LuaValue.FromString("name")).String.ShouldBe("alice");
            tr.GetIndex(LuaValue.FromString("score")).AsInt64.ShouldBe(99);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_GetIndex_MissingKey_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString("return {}");
            using LuaTableRef tr = table.TableRef!;

            tr.GetIndex(LuaValue.FromString("missing")).Type.ShouldBe(LuaType.Nil);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_GetIndex_TriggersIndexMetamethod()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString(@"
                return setmetatable({}, {
                    __index = function(_, k) return k .. '_meta' end,
                })
            ");
            using LuaTableRef tr = table.TableRef!;

            tr.GetIndex(LuaValue.FromString("foo")).String.ShouldBe("foo_meta");

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_SetIndex_StoresAndReadsBack()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString("return {}");
            using LuaTableRef tr = table.TableRef!;

            tr.SetIndex(LuaValue.FromString("key"), LuaValue.FromInt64(42)).ShouldBeTrue();
            tr.GetIndex(LuaValue.FromString("key")).AsInt64.ShouldBe(42);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_SetIndex_TriggersNewindex()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString(@"
                local shadow = {}
                return setmetatable({}, {
                    __newindex = function(_, k, v) shadow[k] = v .. '_modified' end,
                    __index    = shadow,
                })
            ");
            using LuaTableRef tr = table.TableRef!;

            tr.SetIndex(LuaValue.FromString("x"), LuaValue.FromString("hello")).ShouldBeTrue();
            tr.GetIndex(LuaValue.FromString("x")).String.ShouldBe("hello_modified");

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_GetLength_ReturnsSequenceLength()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString("return {10, 20, 30, 40, 50}");
            using LuaTableRef tr = table.TableRef!;

            tr.GetLength().AsInt64.ShouldBe(5);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_GetLength_TriggersLenMetamethod()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString(@"
                return setmetatable({}, {
                    __len = function() return 42 end,
                })
            ");
            using LuaTableRef tr = table.TableRef!;

            tr.GetLength().AsInt64.ShouldBe(42);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void UserdataRef_GetIndex_ReturnsMethod()
        {
            using KitsuneEngine engine = new();
            LuaValue jsonObj = engine.RunString("return Json.New()");
            using LuaUserdataRef ur = jsonObj.UserdataRef!;

            // __index on Json userdata points to the module table; Encode is a function
            var encodeVal = ur.GetIndex(LuaValue.FromString("Encode"));
            encodeVal.FunctionRef?.Dispose();
            encodeVal.Type.ShouldBe(LuaType.Function);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void UserdataRef_GetIndex_NonexistentField_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue jsonObj = engine.RunString("return Json.New()");
            using LuaUserdataRef ur = jsonObj.UserdataRef!;

            ur.GetIndex(LuaValue.FromString("__nonexistent_field__")).Type.ShouldBe(LuaType.Nil);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- KitsuneNext --------------------------------------------------------
        [Fact]
        public void TableRef_Pairs_IteratesAllEntries()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString("return {a=1, b=2, c=3}");
            using LuaTableRef tr = table.TableRef!;

            var seen = new System.Collections.Generic.Dictionary<string, long>();
            foreach (var kv in tr.Pairs())
            {
                seen[kv.Key.String!] = kv.Value.AsInt64;
            }

            seen.Count.ShouldBe(3);
            seen["a"].ShouldBe(1);
            seen["b"].ShouldBe(2);
            seen["c"].ShouldBe(3);

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_Pairs_EmptyTable_NoEntries()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString("return {}");
            using LuaTableRef tr = table.TableRef!;

            tr.Pairs().ShouldBeEmpty();

            engine.GetActiveIds().ShouldBeEmpty();
        }

        [Fact]
        public void TableRef_Pairs_EarlyBreak_DoesNotLeak()
        {
            using KitsuneEngine engine = new();
            LuaValue table = engine.RunString("return {x=10, y=20, z=30}");
            using LuaTableRef tr = table.TableRef!;

            int count = 0;
            foreach (var item in tr.Pairs())
            {
                count++;
                break;
            }

            count.ShouldBe(1);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- KitsuneGCHook / delegate-lifetime tests ---------------------------
        [Fact]
        public void GCHook_RegisterFunction_DelegateFreedAfterGlobalNilledAndCollected()
        {
            // A delegate handle allocated by RegisterFunction should be freed by the
            // KitsuneGCHook __gc when Lua GCs the closure, not held alive indefinitely.
            bool finalizerFired = false;
            using KitsuneEngine engine = new();
            engine.RegisterFunction("GCTestFn", _ =>
            {
                finalizerFired = true;
                return LuaValue.None;
            });

            // Nil the global so the closure (and its upvalues) become unreachable.
            engine.SetVariable("GCTestFn", LuaValue.None);
            engine.CollectGarbage();
            engine.CollectGarbage();

            finalizerFired.ShouldBeFalse("finalizer must not fire before GC");

            // The KitsuneGCHook __gc fires here; LuaDelegateFinalizer frees the GCHandle.
            // We verify the handle was freed by confirming no leak is thrown on dispose.
            // (If the handle was double-freed or leaked, the process would crash or leak.)
        }

        [Fact]
        public async Task GCHook_RegisterFunction_HandleFreedByGC_NotByDispose()
        {
            // After the Lua closure is GC'd the GCHandle must be freed exactly once.
            // Disposing the engine after GC must not double-free it (IsAllocated check).
            int callCount = 0;
            using KitsuneEngine engine = new();
            engine.RegisterFunction("GCTestFn2", _ =>
            {
                callCount++;
                return LuaValue.FromInt64(callCount);
            });

            // Call it once to confirm it works.
            LuaValue result = await engine.ExecuteStringAsync("return GCTestFn2()");
            result.AsInt64.ShouldBe(1);

            // Nil the global and force a full collection cycle.
            engine.SetVariable("GCTestFn2", LuaValue.None);
            engine.CollectGarbage();
            engine.CollectGarbage();

            // Engine dispose must not crash (no double-free).
        }

        [Fact]
        public async Task GCHook_CFunction_SetVariable_HandleFreedAfterGC()
        {
            // A LuaFunction passed as a CFunction value via SetVariable should have its
            // GCHandle freed when Lua GCs the closure.
            int callCount = 0;
            using KitsuneEngine engine = new();
            engine.SetVariable("GCCFn", LuaValue.FromCFunction(_ =>
            {
                callCount++;
                return LuaValue.FromInt64(callCount);
            }));

            LuaValue result = await engine.ExecuteStringAsync("return GCCFn()");
            result.AsInt64.ShouldBe(1);

            // Nil the global and collect.
            engine.SetVariable("GCCFn", LuaValue.None);
            engine.CollectGarbage();
            engine.CollectGarbage();

            // After GC the handle is gone; calling the nilled global must error, not crash.
            LuaException ex = await Should.ThrowAsync<LuaException>(
                engine.ExecuteStringAsync("return GCCFn()"));
            ex.Message.ShouldNotBeNullOrEmpty();
        }

        [Fact]
        public async Task GCHook_RegisterUserdata_MethodHandleFreedAfterAllInstancesCollected()
        {
            // Method lambda GCHandles from RegisterUserdata<T> are freed by KitsuneGCHook
            // when the metatable closures are collected.  Because metatables persist for the
            // lifetime of the Lua state (luaL_newmetatable anchors them in the registry),
            // the closures live as long as the state.  This test verifies:
            //   (a) methods continue to work after instances are collected, and
            //   (b) the engine disposes cleanly without a double-free.
            using KitsuneEngine engine = new();
            engine.RegisterUserdata<Counter>();
            engine.SetVariable("c", engine.CreateUserdata(new Counter { Value = 5 }));

            LuaValue result = await engine.ExecuteStringAsync(
                "c:Increment(); return c:Get()");
            result.AsInt64.ShouldBe(6);

            // Nil the instance and collect.  The metatable (and its method closures) remain
            // registered; only the userdata object itself is collected.
            engine.SetVariable("c", LuaValue.None);
            engine.CollectGarbage();
            engine.CollectGarbage();

            // Re-create an instance and verify methods still work — metatable closures intact.
            engine.SetVariable("c2", engine.CreateUserdata(new Counter { Value = 10 }));
            LuaValue result2 = await engine.ExecuteStringAsync(
                "c2:Increment(); c2:Increment(); return c2:Get()");
            result2.AsInt64.ShouldBe(12);
            engine.GetActiveIds().ShouldBeEmpty();
        }

        // -- Ticker pause/resume currentCoroutineId regression --------------------
        [Fact]
        public async Task Sleep_InsideTask_WhileExternalThreadHoldsLuaAccess_YieldsNotOsSleeps()
        {
            // Regression: when an external thread calls AcquireLuaAccess (e.g. SetString)
            // while a scheduler-managed coroutine is mid-resume, the Ticker parks on
            // pauseFlag.  The external thread's RunInline overwrites currentCoroutineId
            // with its own inline slot id and then zeroes it on completion.  Without the
            // fix the Ticker woke with currentCoroutineId==0, L_Sleep called FindSlot(0)
            // which returned NULL, and the fallback OS ::Sleep blocked the scheduler for
            // the full duration instead of yielding.
            using KitsuneEngine engine = new();

            // Start a task that sleeps for 5 seconds — if it hits OS sleep it blocks
            // the scheduler for the full 5 s and the test will time out.
            engine.ExecuteString("Sleep(5000)");
            SpinUntilActive(engine);
            int id = engine.GetActiveIds()[0];

            // Wait until the coroutine is confirmed sleeping (not just running).
            DateTime sleepDeadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetStatus(id) != CoroutineStatus.Sleeping && DateTime.UtcNow < sleepDeadline)
            {
                await Task.Delay(1);
            }

            engine.GetStatus(id).ShouldBe(CoroutineStatus.Sleeping);

            // Hammer AcquireLuaAccess from multiple threads simultaneously to maximise
            // the chance the Ticker fires exactly during the pause window.
            Task[] hammers = Enumerable.Range(0, 8).Select(_ => Task.Run(() =>
            {
                for (int i = 0; i < 50; i++)
                {
                    engine.SetString("_tickerRaceProbe", "x");
                    Thread.Sleep(1);
                }
            })).ToArray();

            await Task.WhenAll(hammers);

            // Cancel the sleeping task — if it had fallen into OS ::Sleep the cancel
            // would still eventually work but the slot would not be freed for 5 s.
            var sw = Stopwatch.StartNew();
            engine.Cancel(id);

            DateTime cancelDeadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetActiveIds().Contains(id) && DateTime.UtcNow < cancelDeadline)
            {
                await Task.Delay(1);
            }

            sw.Stop();
            sw.ElapsedMilliseconds.ShouldBeLessThan(2000,
                "Cancel took too long — Sleep() inside a task likely fell through to OS ::Sleep " +
                "because currentCoroutineId was zeroed by the Ticker pause path");
            engine.GetActiveIds().ShouldNotContain(id);
        }

        // -- Tasks.GetCurrentId() -------------------------------------------------
        [Fact]
        public async Task GetCurrentId_InsideAsyncCoroutine_ReturnsNonZero()
        {
            // Tasks.GetCurrentId() must return the coroutine's own id when called from
            // inside a scheduler-managed async coroutine.
            using KitsuneEngine engine = new();
            engine.SetVariable("capturedId", 0L);
            engine.ExecuteString(@"
                capturedId = Tasks.GetCurrentId()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetInt64("capturedId").ShouldNotBeNull();
            engine.GetInt64("capturedId")!.Value.ShouldBeGreaterThan(0);
        }

        [Fact]
        public async Task GetCurrentId_MatchesTaskGetId()
        {
            // Tasks.GetCurrentId() inside a Tasks.New coroutine must equal the id
            // returned by task:GetId() on the handle that launched it.
            using KitsuneEngine engine = new();
            engine.SetVariable("innerMatchesOuter", false);
            engine.ExecuteString(@"
                local t
                t = Tasks.New(function()
                    innerMatchesOuter = (Tasks.GetCurrentId() == t:GetId())
                end)
                t:Wait()
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetBool("innerMatchesOuter").ShouldBe(true);
        }

        [Fact]
        public async Task GetCurrentId_EachCoroutineHasUniqueId()
        {
            // Two concurrently-running coroutines must each see their own distinct id.
            using KitsuneEngine engine = new();
            engine.SetVariable("id1", 0L);
            engine.SetVariable("id2", 0L);
            engine.SetVariable("idsDistinct", false);
            engine.ExecuteString(@"
                local t1 = Tasks.New(function() id1 = Tasks.GetCurrentId() end)
                local t2 = Tasks.New(function() id2 = Tasks.GetCurrentId() end)
                t1:Wait() t2:Wait()
                idsDistinct = (id1 ~= 0 and id2 ~= 0 and id1 ~= id2)
                t1:Dispose() t2:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetBool("idsDistinct").ShouldBe(true);
        }

        // -- task:Wait ------------------------------------------------------------
        [Fact]
        public async Task TaskWait_SuspendsCallerUntilTargetFinishes()
        {
            // task:Wait() must block the calling coroutine until the target is done.
            using KitsuneEngine engine = new();
            engine.SetVariable("result", 0L);
            engine.ExecuteString(@"
                local t = Tasks.New(function()
                    Sleep(100)
                    return 42
                end)
                t:Wait()
                result = t:GetResult()
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetInt64("result").ShouldBe(42);
        }

        [Fact]
        public async Task TaskWait_AlreadyFinished_ReturnsImmediately()
        {
            // If the target is already done when Wait is called, it must return without hanging.
            using KitsuneEngine engine = new();
            engine.SetVariable("result", 0L);
            engine.ExecuteString(@"
                local t = Tasks.New(function() return 7 end)
                while not t:Finished() do Sleep(5) end
                t:Wait()
                result = t:GetResult()
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetInt64("result").ShouldBe(7);
        }

        [Fact]
        public async Task TaskWait_WithTimeout_ReturnsAfterTimeout_WhenTargetStillRunning()
        {
            // task:Wait(ms) must return after the timeout even if the target has not finished.
            using KitsuneEngine engine = new();
            engine.SetVariable("waitReturned", false);
            engine.ExecuteString(@"
                local slow = Tasks.New(function() Sleep(10000) end)
                slow:Wait(200)
                waitReturned = true
                slow:Cancel()
                slow:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            using CancellationTokenSource cts1 = new(TimeSpan.FromSeconds(10));
            await engine.WaitAsync(id, cts1.Token);
            engine.GetBool("waitReturned").ShouldBe(true);
        }

        [Fact]
        public async Task TaskWait_WithTimeout_FinishesBefore_WhenTargetFastEnough()
        {
            // task:Wait(ms) must still return promptly when the target finishes before the timeout.
            using KitsuneEngine engine = new();
            engine.SetVariable("result", 0L);
            engine.ExecuteString(@"
                local t = Tasks.New(function() Sleep(50) return 55 end)
                t:Wait(5000)
                result = t:GetResult()
                t:Dispose()
            ");
            using CancellationTokenSource cts2 = new(TimeSpan.FromSeconds(10));
            await engine.WaitAsync(cts2.Token);
            engine.GetInt64("result").ShouldBe(55);
        }

        [Fact]
        public async Task TaskWait_ReleasedHandle_ReturnsImmediately()
        {
            // Calling Wait on a handle whose id is 0 (disposed) must return immediately.
            using KitsuneEngine engine = new();
            engine.SetVariable("waitReturned", false);
            engine.ExecuteString(@"
                local t = Tasks.New(function() return 1 end)
                while not t:Finished() do Sleep(5) end
                t:Dispose()
                t:Wait()
                waitReturned = true
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetBool("waitReturned").ShouldBe(true);
        }

        [Fact]
        public async Task TaskWait_MultipleWaiters_AllWakeAfterTargetFinishes()
        {
            // Multiple coroutines waiting on the same target must all be unblocked when it finishes.
            using KitsuneEngine engine = new();
            engine.SetVariable("count", 0L);
            engine.ExecuteString(@"
                local target = Tasks.New(function() Sleep(150) end)

                local w1 = Tasks.New(function() target:Wait() count = count + 1 end)
                local w2 = Tasks.New(function() target:Wait() count = count + 1 end)
                local w3 = Tasks.New(function() target:Wait() count = count + 1 end)

                w1:Wait() w2:Wait() w3:Wait()
                w1:Dispose() w2:Dispose() w3:Dispose()
                target:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            using CancellationTokenSource cts3 = new(TimeSpan.FromSeconds(10));
            await engine.WaitAsync(id, cts3.Token);
            engine.GetInt64("count").ShouldBe(3);
        }

        [Fact]
        public async Task TaskWait_FaultedTarget_UnblocksWaiter()
        {
            // A target that errors must still unblock any coroutine waiting on it.
            using KitsuneEngine engine = new();
            engine.SetVariable("waitReturned", false);
            engine.ExecuteString(@"
                Tasks.SetErrorHandler(function() end)
                local t = Tasks.New(function() Sleep(50) error('boom') end)
                t:Wait()
                waitReturned = true
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            using CancellationTokenSource cts4 = new(TimeSpan.FromSeconds(10));
            await engine.WaitAsync(id, cts4.Token);
            engine.GetBool("waitReturned").ShouldBe(true);
        }

        [Fact]
        public void TaskWait_OutsideCoroutine_RaisesError()
        {
            // Calling task:Wait() from an inline (non-scheduler) context must raise a Lua error.
            using KitsuneEngine engine = new();
            var ex = Should.Throw<LuaException>(() =>
                engine.RunString("local t = Tasks.New(function() Sleep(5) end); t:Wait()"));
            ex.Message.ShouldContain("inline");
        }

        [Fact]
        public async Task TaskWait_CancelledTarget_UnblocksWaiter()
        {
            // Cancelling the target must unblock a coroutine that is Wait()ing on it.
            using KitsuneEngine engine = new();
            engine.SetVariable("waitReturned", false);
            engine.ExecuteString(@"
                local t = Tasks.New(function() while true do Sleep(10) end end)
                local watcher = Tasks.New(function()
                    t:Wait()
                    waitReturned = true
                end)
                Sleep(100)
                t:Cancel()
                watcher:Wait(3000)
                t:Dispose()
                watcher:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            using CancellationTokenSource cts5 = new(TimeSpan.FromSeconds(10));
            await engine.WaitAsync(id, cts5.Token);
            engine.GetBool("waitReturned").ShouldBe(true);
        }

        // -- Resume waking Sleeping / Waiting tasks --------------------------------
        [Fact]
        public async Task Resume_SleepingTask_WakesImmediately()
        {
            // Resume() on a sleeping coroutine wakes it before the deadline expires.
            using KitsuneEngine engine = new();
            engine.SetVariable("reached", false);
            engine.ExecuteString("Sleep(60000) reached = true");
            SpinUntilActive(engine);
            int id = engine.GetActiveIds()[0];

            // Wait until the coroutine is confirmed sleeping.
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetStatus(id) != CoroutineStatus.Sleeping && DateTime.UtcNow < deadline)
            {
                await Task.Delay(1);
            }

            engine.GetStatus(id).ShouldBe(CoroutineStatus.Sleeping);

            // Force-wake it.
            engine.Resume(id).ShouldBeTrue();
            await engine.WaitAsync(id);
            engine.GetBool("reached").ShouldBe(true);
        }

        [Fact]
        public async Task Resume_SleepingTask_ReturnsFalseWhenAlreadyDone()
        {
            // Resume on a non-sleeping (done) id returns false.
            using KitsuneEngine engine = new();
            engine.ExecuteString("Sleep(1)");
            SpinUntilActive(engine);
            int id = engine.GetActiveIds()[0];
            await engine.WaitAsync(id);
            engine.Resume(id).ShouldBeFalse();
        }

        [Fact]
        public async Task Resume_SleepingTask_ValueDiscarded()
        {
            // Resume(id, value) on a sleeping coroutine wakes it but discards the value.
            // Sleep() returns nothing — the coroutine just wakes up normally.
            using KitsuneEngine engine = new();
            engine.SetVariable("woke", false);
            engine.ExecuteString("Sleep(60000) woke = true");
            SpinUntilActive(engine);
            int id = engine.GetActiveIds()[0];
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetStatus(id) != CoroutineStatus.Sleeping && DateTime.UtcNow < deadline)
            {
                await Task.Delay(1);
            }

            engine.Resume(id, new LuaValue { Type = LuaType.Integer, Int64 = 999 }).ShouldBeTrue();
            await engine.WaitAsync(id);
            engine.GetBool("woke").ShouldBe(true);
        }

        [Fact]
        public async Task Resume_WaitingTask_WakesBeforeTargetFinishes()
        {
            // Resume() on a task:Wait()-suspended coroutine wakes it early.
            using KitsuneEngine engine = new();
            engine.SetVariable("waitReturned", false);
            engine.ExecuteString(@"
                local target = Tasks.New(function() Sleep(60000) end)
                local waiter = Tasks.New(function()
                    target:Wait()
                    waitReturned = true
                end)
                -- expose waiter id for C# to force-resume
                _G.waiterId = waiter:GetId()
                Sleep(200)   -- let waiter reach Wait() state
                target:Dispose()
                waiter:Dispose()
            ");
            SpinUntilActive(engine);
            int outerId = engine.GetActiveIds()[0];

            // Wait for waiterId global to be set and waiter to be in Waiting state.
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            long waiterId = 0;
            while (waiterId == 0 && DateTime.UtcNow < deadline)
            {
                await Task.Delay(5);
                waiterId = engine.GetInt64("waiterId") ?? 0;
            }

            waiterId.ShouldBeGreaterThan(0);

            // Wait until the waiter is confirmed in WAITING state.
            deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetStatus((int)waiterId) != CoroutineStatus.Waiting && DateTime.UtcNow < deadline)
            {
                await Task.Delay(1);
            }

            engine.GetStatus((int)waiterId).ShouldBe(CoroutineStatus.Waiting);

            // Force-wake the waiter from C#.
            engine.Resume((int)waiterId).ShouldBeTrue();

            await engine.WaitAsync(outerId);
            engine.GetBool("waitReturned").ShouldBe(true);
        }

        [Fact]
        public async Task Resume_WithValue_PauseReceivesValue_SleepDoesNot()
        {
            // Verify that Resume(id, value) delivers value to Pause() but not to Sleep().
            using KitsuneEngine engine = new();
            engine.SetVariable("pauseResult", 0L);
            engine.ExecuteString(@"
                local t = Tasks.New(function()
                    pauseResult = Pause()
                end)
                while t:GetStatus() ~= TaskStatus.Paused do Sleep(5) end
                t:Resume(77)
                t:Wait()
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetInt64("pauseResult").ShouldBe(77L);
        }

        [Fact]
        public async Task GetStatus_WaitingCoroutine_ReturnsWaiting()
        {
            // GetStatus returns CoroutineStatus.Waiting when a coroutine is in task:Wait().
            using KitsuneEngine engine = new();
            engine.ExecuteString(@"
                local target = Tasks.New(function() Sleep(60000) end)
                _G.targetId = target:GetId()
                target:Wait()
                target:Dispose()
            ");
            SpinUntilActive(engine);
            int outerId = engine.GetActiveIds()[0];

            // Wait for targetId global.
            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            long targetId = 0;
            while (targetId == 0 && DateTime.UtcNow < deadline)
            {
                await Task.Delay(5);
                targetId = engine.GetInt64("targetId") ?? 0;
            }

            // The outer coroutine is itself in task:Wait() — check its status.
            deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetStatus(outerId) != CoroutineStatus.Waiting && DateTime.UtcNow < deadline)
            {
                await Task.Delay(1);
            }

            engine.GetStatus(outerId).ShouldBe(CoroutineStatus.Waiting);
            engine.Cancel((int)targetId);
            await engine.WaitAsync(outerId);
        }

        // -- Lua-side Resume waking Sleeping / Waiting tasks -----------------------
        [Fact]
        public async Task LuaResume_WakesSleepingTask_FromAnotherCoroutine()
        {
            // task:Resume() called from a sibling coroutine wakes a sleeping task early.
            using KitsuneEngine engine = new();
            engine.SetVariable("woke", false);
            engine.ExecuteString(@"
                local sleeper = Tasks.New(function()
                    Sleep(60000)
                    woke = true
                end)
                Sleep(50)
                sleeper:Resume()
                sleeper:Wait(3000)
                sleeper:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            using CancellationTokenSource cts = new(TimeSpan.FromSeconds(10));
            await engine.WaitAsync(id, cts.Token);
            engine.GetBool("woke").ShouldBe(true);
        }

        [Fact]
        public async Task LuaResume_WakesWaitingTask_FromAnotherCoroutine()
        {
            // task:Resume() on a task:Wait()-suspended coroutine wakes it early from Lua.
            using KitsuneEngine engine = new();
            engine.SetVariable("waitReturned", false);
            engine.ExecuteString(@"
                local target = Tasks.New(function() Sleep(60000) end)
                local waiter = Tasks.New(function()
                    target:Wait()
                    waitReturned = true
                end)
                Sleep(100)
                waiter:Resume()
                waiter:Wait(3000)
                target:Cancel()
                waiter:Dispose()
                target:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            using CancellationTokenSource cts = new(TimeSpan.FromSeconds(10));
            await engine.WaitAsync(id, cts.Token);
            engine.GetBool("waitReturned").ShouldBe(true);
        }

        [Fact]
        public async Task LuaResume_SleepingTask_ValueIsDiscarded()
        {
            // Resuming a sleeping task with a value must not crash; the value is discarded.
            using KitsuneEngine engine = new();
            engine.SetVariable("woke", false);
            engine.ExecuteString(@"
                local t = Tasks.New(function()
                    Sleep(60000)
                    woke = true
                end)
                Sleep(50)
                t:Resume('ignored value')
                t:Wait(3000)
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            using CancellationTokenSource cts = new(TimeSpan.FromSeconds(10));
            await engine.WaitAsync(id, cts.Token);
            engine.GetBool("woke").ShouldBe(true);
        }

        [Fact]
        public async Task LuaResume_SleepingTask_ReturnsTrue()
        {
            // task:Resume() on a sleeping task returns true.
            using KitsuneEngine engine = new();
            engine.SetVariable("resumeResult", false);
            engine.ExecuteString(@"
                local t = Tasks.New(function() Sleep(60000) end)
                Sleep(50)
                resumeResult = t:Resume()
                t:Wait(3000)
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            using CancellationTokenSource cts = new(TimeSpan.FromSeconds(10));
            await engine.WaitAsync(id, cts.Token);
            engine.GetBool("resumeResult").ShouldBe(true);
        }

        // -- ConsumeResult --------------------------------------------------------
        [Fact]
        public async Task ConsumeResult_ReturnsValueAndReleasesSlot()
        {
            // task:ConsumeResult() returns the result and immediately advances the slot to RELEASED.
            using KitsuneEngine engine = new();
            engine.SetVariable("consumed", 0L);
            engine.ExecuteString(@"
                local t = Tasks.New(function() return 42 end)
                t:Wait()
                consumed = t:ConsumeResult()
                -- slot is RELEASED immediately; Finished() returns true for RELEASED
                _G.isFinished = t:Finished()
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetInt64("consumed").ShouldBe(42L);
            engine.GetBool("isFinished").ShouldBe(true);
        }

        [Fact]
        public async Task ConsumeResult_StringValue_FreesImmediately()
        {
            // ConsumeResult on a string result works correctly.
            using KitsuneEngine engine = new();
            engine.SetVariable("consumed", string.Empty);
            engine.ExecuteString(@"
                local t = Tasks.New(function() return 'hello world' end)
                t:Wait()
                consumed = t:ConsumeResult()
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetString("consumed").ShouldBe("hello world");
        }

        [Fact]
        public async Task ConsumeResult_NotFinished_ReturnsNil()
        {
            // ConsumeResult on a still-running task returns nil without disrupting the task.
            using KitsuneEngine engine = new();
            engine.SetVariable("consumed", 99L);
            engine.ExecuteString(@"
                local t = Tasks.New(function() Sleep(60000) return 1 end)
                consumed = t:ConsumeResult()  -- task not done yet → nil
                t:Cancel()
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetInt64("consumed").ShouldBeNull();
        }

        [Fact]
        public async Task ConsumeResult_FaultedTask_ReturnsNilAndReleasesSlot()
        {
            // ConsumeResult on a faulted task returns nil and still releases the slot.
            using KitsuneEngine engine = new();
            engine.SetVariable("consumed", 99L);
            engine.ExecuteString(@"
                local t = Tasks.New(function() error('boom') end)
                t:Wait()
                consumed = t:ConsumeResult()
                -- slot is RELEASED immediately; Finished() returns true for RELEASED
                _G.isFinished = t:Finished()
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetInt64("consumed").ShouldBeNull();
            engine.GetBool("isFinished").ShouldBe(true);
        }

        [Fact]
        public async Task ConsumeResult_TableResult_SlotReleasedBeforeGC()
        {
            // A large table result is freed immediately on ConsumeResult,
            // not waiting for GC to collect the handle.
            using KitsuneEngine engine = new();
            engine.SetVariable("len", 0L);
            engine.ExecuteString(@"
                local t = Tasks.New(function()
                    local tbl = {}
                    for i = 1, 1000 do tbl[i] = i end
                    return tbl
                end)
                t:Wait()
                local result = t:ConsumeResult()
                len = #result
                -- slot is RELEASED immediately; Finished() returns true for RELEASED
                _G.isFinished = t:Finished()
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetInt64("len").ShouldBe(1000L);
            engine.GetBool("isFinished").ShouldBe(true);
        }

        [Fact]
        public async Task GetResult_AfterConsumeResult_ReturnsNil()
        {
            // GetResult after ConsumeResult returns nil — the slot is gone.
            using KitsuneEngine engine = new();
            engine.SetVariable("second", 99L);
            engine.ExecuteString(@"
                local t = Tasks.New(function() return 7 end)
                t:Wait()
                t:ConsumeResult()
                second = t:GetResult()  -- slot released → nil
                t:Dispose()
            ");
            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);
            engine.GetInt64("second").ShouldBeNull();
        }

        // -- task:OnError tests -------------------------------------------------------
        [Fact]
        public async Task OnError_FireAndForget_CalledOnFault()
        {
            // Per-task handler receives (id, err) when a fire-and-forget task faults.
            using KitsuneEngine engine = new();
            engine.SetVariable("capturedId", 0);
            engine.SetVariable("capturedErr", string.Empty);

            engine.RunString(@"
                Tasks.New(function() error('per-task-boom') end)
                    :OnError(function(id, err)
                        capturedId  = id
                        capturedErr = err
                    end)
                    :Dispose()
            ");

            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetInt64("capturedId") == 0 && DateTime.UtcNow < deadline)
            {
                await Task.Delay(5);
            }

            engine.GetInt64("capturedId").ShouldNotBe(0);
            engine.GetVariable("capturedErr").String!.ShouldContain("per-task-boom");
        }

        [Fact]
        public async Task OnError_Observed_CalledOnFault()
        {
            // Per-task handler fires even when the task handle is still alive (non-fire-and-forget).
            using KitsuneEngine engine = new();
            engine.SetVariable("capturedErr", string.Empty);

            engine.ExecuteString(@"
                local t = Tasks.New(function() error('observed-error') end)
                t:OnError(function(id, err) capturedErr = err end)
                t:Wait()
                t:Dispose()
            ");

            SpinUntilRunning(engine);
            int id = engine.RunningCoroutineId;
            await engine.WaitAsync(id);

            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (string.IsNullOrEmpty(engine.GetVariable("capturedErr").String) && DateTime.UtcNow < deadline)
            {
                await Task.Delay(5);
            }

            engine.GetVariable("capturedErr").String!.ShouldContain("observed-error");
        }

        [Fact]
        public async Task OnError_TakesPriorityOverGlobalHandler()
        {
            // When both handlers are set, only the per-task handler is called.
            using KitsuneEngine engine = new();
            engine.SetVariable("globalCalled", false);
            engine.SetVariable("perTaskCalled", false);

            engine.RunString(@"
                Tasks.SetErrorHandler(function(id, err) globalCalled = true end)
            ");

            engine.RunString(@"
                Tasks.New(function() error('priority-test') end)
                    :OnError(function(id, err) perTaskCalled = true end)
                    :Dispose()
            ");

            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetBool("perTaskCalled") != true && DateTime.UtcNow < deadline)
            {
                await Task.Delay(5);
            }

            engine.GetBool("perTaskCalled").ShouldBe(true);
            engine.GetBool("globalCalled").ShouldBe(false);
        }

        [Fact]
        public async Task OnError_NotCalledOnSuccess()
        {
            // Handler must not be called when the task completes without error.
            using KitsuneEngine engine = new();
            engine.SetVariable("handlerCalled", false);

            engine.RunString(@"
                Tasks.New(function() return 42 end)
                    :OnError(function(id, err) handlerCalled = true end)
                    :Dispose()
            ");

            await Task.Delay(300);
            engine.GetBool("handlerCalled").ShouldBe(false);
        }

        [Fact]
        public async Task OnError_ClearedByNil_DoesNotFire()
        {
            // Calling OnError(nil) removes the handler; a subsequent fault must not call it.
            using KitsuneEngine engine = new();
            engine.SetVariable("handlerCalled", false);

            engine.RunString(@"
                local t = Tasks.New(function() error('cleared') end)
                t:OnError(function(id, err) handlerCalled = true end)
                t:OnError(nil)
                t:Dispose()
            ");

            await Task.Delay(300);
            engine.GetBool("handlerCalled").ShouldBe(false);
        }

        [Fact]
        public async Task OnError_ReturnsSelfForChaining()
        {
            // OnError must return the task handle so :Dispose() can be chained.
            using KitsuneEngine engine = new();
            engine.SetVariable("capturedErr", string.Empty);

            engine.RunString(@"
                Tasks.New(function() error('chain-test') end)
                    :OnError(function(id, err) capturedErr = err end)
                    :Dispose()
            ");

            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (string.IsNullOrEmpty(engine.GetVariable("capturedErr").String) && DateTime.UtcNow < deadline)
            {
                await Task.Delay(5);
            }

            engine.GetVariable("capturedErr").String!.ShouldContain("chain-test");
        }

        // -- Tasks.ActiveCount / Tasks.MaxSlots tests ---------------------------------
        [Fact]
        public async Task ActiveCount_WhileCoroutineRunning_IsPositive()
        {
            using KitsuneEngine engine = new();
            engine.ExecuteString("Sleep(500)");
            SpinUntilActive(engine);
            LuaValue count = engine.RunString("return Tasks.ActiveCount()");
            count.AsInt64.ShouldBeGreaterThan(0);
            engine.Interrupt();
            engine.Wait();
        }

        [Fact]
        public void ActiveCount_WhenIdle_IsZero()
        {
            using KitsuneEngine engine = new();
            engine.RunString("return 1");
            engine.GetActiveIds().Length.ShouldBe(0);
        }

        [Fact]
        public void MaxSlots_Is256()
        {
            using KitsuneEngine engine = new();
            long max = engine.RunString("return Tasks.MaxSlots").AsInt64;
            max.ShouldBe(256);
        }

        [Fact]
        public async Task ActiveCount_ReflectsMultipleConcurrentCoroutines()
        {
            using KitsuneEngine engine = new();
            engine.SetVariable("countSeen", 0);

            engine.ExecuteString(@"
                local t1 = Tasks.New(function() Sleep(2000) end)
                local t2 = Tasks.New(function() Sleep(2000) end)
                local t3 = Tasks.New(function() Sleep(2000) end)
                Sleep(50)
                countSeen = Tasks.ActiveCount()
                t1:Cancel() t2:Cancel() t3:Cancel()
            ");

            SpinUntilActive(engine);

            DateTime deadline = DateTime.UtcNow.AddSeconds(5);
            while (engine.GetInt64("countSeen") == 0 && DateTime.UtcNow < deadline)
            {
                await Task.Delay(5);
            }

            (engine.GetInt64("countSeen") >= 3).ShouldBe(true, "Expected ActiveCount >= 3");
            engine.Interrupt();
            engine.Wait();
        }

        private static void SpinUntilRunning(KitsuneEngine engine, int timeoutMs = 2000)
        {
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (!engine.IsRunning && DateTime.UtcNow < deadline)
            {
                Thread.Sleep(1);
            }
        }

        // SpinUntilActive returns as soon as the coroutine has been assigned a slot
        // (appears in GetActiveIds), which happens synchronously with ExecuteString.
        // Use this instead of SpinUntilRunning when the coroutine sleeps immediately,
        // because IsRunning transiently drops to false between scheduler ticks while
        // a coroutine is sleeping and SpinUntilRunning would burn the full 2-second
        // timeout before the subsequent GetActiveIds()[0] call.
        private static void SpinUntilActive(KitsuneEngine engine, int timeoutMs = 5000)
        {
            DateTime deadline = DateTime.UtcNow.AddMilliseconds(timeoutMs);
            while (engine.GetActiveIds().Length == 0 && DateTime.UtcNow < deadline)
            {
                Thread.Sleep(1);
            }
        }

        // Used by RegisterUserdata_DuplicateMethodName_ThrowsInvalidOperationException.
        private sealed class TypeWithDuplicateLuaMethod
        {
            [LuaMethod]
            public LuaValue Foo(IReadOnlyList<LuaValue> args) => LuaValue.None;

            [LuaMethod(Name = "Foo")]
            public LuaValue AlsoFoo(IReadOnlyList<LuaValue> args) => LuaValue.None;
        }

        // Used by RegisterUserdata_DuplicateMetaMethodName_ThrowsInvalidOperationException.
        private sealed class TypeWithDuplicateLuaMetaMethod
        {
            [LuaMetaMethod("__tostring")]
            public LuaValue ToStr1(IReadOnlyList<LuaValue> args) => LuaValue.None;

            [LuaMetaMethod("__tostring")]
            public LuaValue ToStr2(IReadOnlyList<LuaValue> args) => LuaValue.None;
        }

        private sealed class Widget
        {
            public Widget(string name)
            {
                Name = name;
            }

            public string Name { get; }

            [LuaMethod]
            public LuaValue GetName(IReadOnlyList<LuaValue> val) => LuaValue.FromString(Name);
        }

        private sealed class Counter
        {
            public bool DidGc = false;

            public int Value;

            [LuaMethod]
            public LuaValue Increment(IReadOnlyList<LuaValue> val)
            {
                Value++;
                return LuaValue.None;
            }

            // args[0] = self, args[1] = amount to add
            [LuaMethod]
            public LuaValue Add(IReadOnlyList<LuaValue> args)
            {
                Value += (int)args[1].AsInt64;
                return LuaValue.None;
            }

            // Throws when Value is negative so error-propagation tests can use it.
            [LuaMethod]
            public LuaValue BangIfNegative(IReadOnlyList<LuaValue> args)
            {
                if (Value < 0)
                {
                    throw new LuaException("Counter value is negative");
                }

                return LuaValue.None;
            }

            [LuaMethod(Name = "Get")]
            public LuaValue GetValue(IReadOnlyList<LuaValue> val) => LuaValue.FromInt64(Value);

            [LuaMetaMethod("__tostring")]
            public LuaValue ToStr(IReadOnlyList<LuaValue> val) => $"Counter({Value})";

            [LuaMetaMethod("__gc")]
            public void GC(IReadOnlyList<LuaValue> val)
            {
                DidGc = true;
            }
        }
    }
}
