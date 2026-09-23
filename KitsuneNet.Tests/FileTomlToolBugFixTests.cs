using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests
{
    // See KitsuneEngineTests for why test classes share a single collection.
    [Collection("KitsuneSequential")]

    /// <summary>
    /// Regression tests for FileSystem.Copy's overwrite flag, FileInfo timestamps,
    /// Toml encoding of nested / empty tables and pretty mode, and the ToolSuite
    /// permission gate denying on error.
    /// </summary>
    public sealed class FileTomlToolBugFixTests
    {
        // Lua deep-equality helper shared by the Toml tests.
        private const string DeepEq = @"
            local function deq(a, b)
                if type(a) ~= type(b) then return false end
                if type(a) ~= 'table' then return a == b end
                for k, v in pairs(a) do
                    if not deq(v, b[k]) then return false end
                end
                for k in pairs(b) do
                    if a[k] == nil then return false end
                end
                return true
            end
        ";

        // -- FileSystem.Copy overwrite flag ----------------------------------

        [Fact]
        public async Task FileSystem_Copy_WithoutOverwrite_FailsWhenDestinationExists_WithTrueOverwrites()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = FileSystem.GetTempFileName()
                local b = FileSystem.GetTempFileName()
                local f = io.open(a, 'wb'); f:write('A'); f:close()
                local g = io.open(b, 'wb'); g:write('B'); g:close()

                local r1 = FileSystem.Copy(a, b)
                local h = io.open(b, 'rb'); local c1 = h:read('a'); h:close()

                local r2 = FileSystem.Copy(a, b, true)
                h = io.open(b, 'rb'); local c2 = h:read('a'); h:close()

                FileSystem.Delete(a)
                FileSystem.Delete(b)
                return tostring(r1) .. '|' .. c1 .. '|' .. tostring(r2) .. '|' .. c2
            ");
            r.String.ShouldBe("false|B|true|A");
        }

        [Fact]
        public async Task FileSystem_Copy_ToNewDestination_WithoutOverwrite_Succeeds()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local a = FileSystem.GetTempFileName()
                local b = a .. '.copy'
                local f = io.open(a, 'wb'); f:write('data'); f:close()
                local ok = FileSystem.Copy(a, b)
                local h = io.open(b, 'rb'); local c = h:read('a'); h:close()
                FileSystem.Delete(a)
                FileSystem.Delete(b)
                return tostring(ok) .. '|' .. c
            ");
            r.String.ShouldBe("true|data");
        }

        // -- FileInfo timestamps are Unix time --------------------------------

        [Fact]
        public async Task FileSystem_GetFileInfo_WriteTime_IsCloseToOsTime()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local p = FileSystem.GetTempFileName()
                local f = io.open(p, 'wb'); f:write('x'); f:close()
                local now = os.time()
                local info = FileSystem.GetFileInfo(p)
                FileSystem.Delete(p)
                local diff = math.abs(info.Write - now)
                if diff > 5 then return 'off by ' .. diff end
                return 'true'
            ");
            r.String.ShouldBe("true");
        }

        // -- Toml encoder -----------------------------------------------------

        [Fact]
        public async Task Toml_RoundTrip_NestedSections_ArraysOfTables_EmptyTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local toml = Toml.New()
                local src = {
                    app   = { sub = { k = 1, deep = { x = 'y' } } },
                    list  = { { a = 1 }, { a = 2 } },
                    empty = {},
                }
                local s = toml:Encode(src)
                local t, err = toml:Decode(s)
                if not t then return 'decode failed: ' .. tostring(err) .. '\n' .. s end
                return tostring(
                    t.app.sub.k == 1 and
                    t.app.sub.deep.x == 'y' and
                    t.list[2].a == 2 and
                    type(t.empty) == 'table' and next(t.empty) == nil and
                    t['app.sub'] == nil)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_NestedHeader_QuotesEachSegmentSeparately()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local toml = Toml.New()
                local s = toml:Encode({ ['my key'] = { sub = { k = 1 } } })
                local t = toml:Decode(s)
                return tostring(s:find('[""my key"".sub]', 1, true) ~= nil and
                                t['my key'].sub.k == 1)
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_ArrayOfTablesInsideSection_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DeepEq + @"
                local toml = Toml.New()
                local src = {
                    app = {
                        name  = 'kitsune',
                        items = { { id = 1, tags = { 'a', 'b' } }, { id = 2, opts = { on = true } } },
                    },
                }
                local s = toml:Encode(src)
                local t = toml:Decode(s)
                return tostring(s:find('[[app.items]]', 1, true) ~= nil and deq(src, t))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_ScalarArrayNextToSection_StaysAtItsLevel()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DeepEq + @"
                local toml = Toml.New()
                local src = { a = { x = 1 }, b = { 1, 2, 3 }, c = { d = { 'p', 'q' } } }
                local t = toml:Decode(toml:Encode(src))
                return tostring(deq(src, t))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_Pretty_DecodesToSameDataAsCompact()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DeepEq + @"
                local src = {
                    title = 'x',
                    app   = { name = 'kitsune', sub = { k = 1, deep = { x = 'y' } } },
                    list  = { { a = 1 }, { a = 2, inner = { z = 3 } } },
                    empty = {},
                }
                local compact = Toml.New():Encode(src)
                local pretty  = Toml.New(true):Encode(src)
                local tc = Toml.New():Decode(compact)
                local tp, err = Toml.New():Decode(pretty)
                if not tp then return 'pretty decode failed: ' .. tostring(err) .. '\n' .. pretty end
                return tostring(pretty ~= compact and
                                pretty:find('\n  ', 1, true) ~= nil and
                                deq(tc, tp) and deq(src, tp))
            ");
            r.String.ShouldBe("true");
        }

        [Fact]
        public async Task Toml_Compact_HasNoIndentation()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local s = Toml.New():Encode({ app = { sub = { k = 1 } } })
                return s
            ");
            r.String.ShouldBe("[app]\n[app.sub]\nk = 1\n");
        }

        // -- ToolSuite permission gate ----------------------------------------

        [WindowsFact]
        public async Task ToolSuite_Gate_RaisingError_DeniesCall_PromptPath()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ran = false
                local s = Llama.CreateToolSuite()
                s:AddTool('t', 'T', {}, function() ran = true return 'RAN' end)
                s:Callback(function(name, args) error('boom') end)
                local p = Llama.CreatePrompt()
                p:AddMessage({ role = 'assistant', content = '',
                               tool_calls = { { id = 'c1', name = 't', arguments = '{}' } } })
                local n = s:Call(p)
                local reply = p:Last().content
                return tostring(ran) .. '|' .. tostring(n) .. '|' .. reply
            ");
            string s = r.String!;
            s.ShouldStartWith("false|1|");
            s.ShouldContain("error");
            s.ShouldContain("boom");
            s.ShouldNotContain("RAN");
        }

        [WindowsFact]
        public async Task ToolSuite_Gate_RaisingError_DeniesCall_TablePath()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ran = false
                local s = Llama.CreateToolSuite()
                s:AddTool('t', 'T', {}, function() ran = true return 'RAN' end)
                s:Callback(function(name, args) error('boom') end)
                local msgs = {
                    { role = 'assistant', content = '',
                      tool_calls = '[{""name"":""t"",""id"":""c1"",""arguments"":{}}]' }
                }
                s:Call(msgs)
                return tostring(ran) .. '|' .. msgs[2].content
            ");
            string s = r.String!;
            s.ShouldStartWith("false|");
            s.ShouldContain("error");
            s.ShouldNotContain("RAN");
        }

        [WindowsFact]
        public async Task ToolSuite_Gate_RaisingErrorAfterYield_DeniesCall_AndContinues()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                local ran = {}
                local s = Llama.CreateToolSuite()
                s:AddTool('bad', 'Bad', {}, function() ran[#ran + 1] = 'bad' return 'BAD' end)
                s:AddTool('good', 'Good', {}, function() ran[#ran + 1] = 'good' return 'ok' end)
                s:Callback(function(name, args)
                    Sleep(0)
                    if name == 'bad' then error('boom') end
                    return true
                end)
                local msgs = {
                    { role = 'assistant', content = '',
                      tool_calls = '[{""name"":""bad"",""id"":""c1"",""arguments"":{}},{""name"":""good"",""id"":""c2"",""arguments"":{}}]' }
                }
                s:Call(msgs)
                return table.concat(ran, ',') .. '|' .. msgs[2].content .. '|' .. msgs[3].content
            ");
            string s = r.String!;
            s.ShouldStartWith("good|");
            s.ShouldContain("boom");
            s.ShouldEndWith("|ok");
        }
    }
}
