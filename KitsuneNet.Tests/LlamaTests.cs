using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests
{
    [Collection("KitsuneSequential")]
    public sealed class LlamaTests
    {
        private static string ModelPath =>
            Environment.GetEnvironmentVariable("KITSUNE_LLAMA_MODEL")!;

        [WindowsFact]
        public void Llama_GlobalExists()
        {
            using KitsuneEngine engine = new();
            engine.RunString("return type(Llama)").String.ShouldBe("table");
        }

        [WindowsFact]
        public void Llama_CreateContext_ReturnsUserdata()
        {
            using KitsuneEngine engine = new();
            engine.RunString("local ctx = Llama.CreateContext(); local t = type(ctx); ctx:Dispose(); return t").String.ShouldBe("userdata");
        }

        [WindowsFact]
        public void Llama_GetLogs_ReturnsTable()
        {
            using KitsuneEngine engine = new();
            engine.RunString("return type(Llama.GetLogs())").String.ShouldBe("table");
        }

        [WindowsFact]
        public void Llama_Context_Dispose_IsIdempotent()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local ctx = Llama.CreateContext()
                ctx:Dispose()
                ctx:Dispose()
                return true
            ").Boolean.ShouldBeTrue();
        }

        [WindowsFact]
        public void Llama_Context_Info_ReturnsTable()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local ctx = Llama.CreateContext()
                local info = ctx:Info()
                ctx:Dispose()
                return type(info)
            ").String.ShouldBe("table");
        }

        [WindowsFact]
        public void Llama_Context_StatusIdle_WhenNew()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local ctx = Llama.CreateContext()
                local info = ctx:Info()
                ctx:Dispose()
                return info.context.status
            ").String.ShouldBe("idle");
        }

        [LlamaFact]
        public void Llama_Generate_ProducesNonEmptyResponse()
        {
            using KitsuneEngine engine = new();
            var result = engine.RunString($@"
                local ctx = Llama.CreateContext()
                ctx:SetModel([[{ModelPath}]])
                ctx:LoadModel()
                local deadline = os.clock() + 30
                while ctx:Info().context.status == 'loading' and os.clock() < deadline do end
                ctx:Generate({{ {{ role = 'user', content = 'Say hello.' }} }})
                local result = ''
                local deadline2 = os.clock() + 60
                local ok, data = ctx:Poll()
                while ok do
                    if data then
                        if data.type == 'error' then error(data.text) end
                        if data.type == 'token' then result = result .. data.text end
                    end
                    Sleep(10)
                    ok, data = ctx:Poll()
                end
                ctx:Dispose()
                return result
            ");

            result.String.ShouldNotBeNullOrEmpty();
        }

        [LlamaFact]
        public void Llama_Generate_StatusBecomesIdle_AfterGeneration()
        {
            using KitsuneEngine engine = new();
            var result = engine.RunString($@"
                local ctx = Llama.CreateContext()
                ctx:SetModel([[{ModelPath}]])
                ctx:LoadModel()
                local deadline = os.clock() + 30
                while ctx:Info().context.status == 'loading' and os.clock() < deadline do end
                ctx:Generate({{ {{ role = 'user', content = 'Hi.' }} }})
                local deadline2 = os.clock() + 60
                local ok, data = ctx:Poll()
                while ok do
                    if data and data.type == 'error' then error(data.text) end
                    Sleep(10)
                    ok, data = ctx:Poll()
                end
                local status = ctx:Info().context.status
                ctx:Dispose()
                return status
            ");

            result.String.ShouldBe("idle");
        }

        [LlamaFact]
        public void Llama_Generate_Stop_Cancels()
        {
            using KitsuneEngine engine = new();
            var result = engine.RunString($@"
                local ctx = Llama.CreateContext()
                ctx:SetModel([[{ModelPath}]])
                ctx:LoadModel()
                local deadline = os.clock() + 30
                while ctx:Info().context.status == 'loading' and os.clock() < deadline do end
                ctx:Generate({{ {{ role = 'user', content = 'Count from 1 to 1000.' }} }})
                local count = 0
                local ok, data = ctx:Poll()
                while ok do
                    if data then
                        if data.type == 'error' then error(data.text) end
                        if data.type == 'token' then
                            count = count + 1
                            if count >= 5 then
                                ctx:Stop()
                                break
                            end
                        end
                    end
                    Sleep(10)
                    ok, data = ctx:Poll()
                end
                local deadline3 = os.clock() + 10
                while ctx:Info().context.status ~= 'idle' and os.clock() < deadline3 do end
                local status = ctx:Info().context.status
                ctx:Dispose()
                return status
            ");

            result.String.ShouldBe("idle");
        }

        // -- ToolSuite — basic API ---------------------------------------------
        [WindowsFact]
        public void ToolSuite_CreateToolSuite_ReturnsUserdata()
        {
            using KitsuneEngine engine = new();
            engine.RunString("return type(Llama.CreateToolSuite())").String
                .ShouldBe("userdata");
        }

        [WindowsFact]
        public void ToolSuite_Tostring_IncludesToolCount()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                return tostring(s)
            ").String!.ShouldContain("ToolSuite(");
        }

        [WindowsFact]
        public void ToolSuite_AddTool_ReturnsTrue()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                return s:AddTool('greet', 'Say hi', {}, function() return 'hi' end)
            ").Boolean.ShouldBeTrue();
        }

        // -- ToolSuite — GetJson -----------------------------------------------
        [WindowsFact]
        public void ToolSuite_GetJson_EmptySuite_ReturnsEmptyArray()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                return s:GetJson()
            ").String.ShouldBe("[]");
        }

        [WindowsFact]
        public void ToolSuite_GetJson_ContainsToolName()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                s:AddTool('get_weather', 'Weather tool',
                    { {name='city', type='string', description='City name', required=true} },
                    function(city) return 'sunny in '..city end)
                return s:GetJson()
            ").String!.ShouldContain("get_weather");
        }

        [WindowsFact]
        public void ToolSuite_GetJson_ContainsRequiredParam()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                s:AddTool('ping', 'Ping',
                    { {name='host', type='string', description='host', required=true} },
                    function(h) return h end)
                return s:GetJson()
            ").String!.ShouldContain(@"""required"":[""host""]");
        }

        // -- ToolSuite — Call early-return paths -------------------------------
        [WindowsFact]
        public void ToolSuite_Call_EmptyMessages_ReturnsZero()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                return tostring(s:Call({}))
            ").String.ShouldBe("0");
        }

        [WindowsFact]
        public void ToolSuite_Call_LastMessageNotAssistant_ReturnsZero()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                return tostring(s:Call({ {role='user', content='hello'} }))
            ").String.ShouldBe("0");
        }

        [WindowsFact]
        public void ToolSuite_Call_AssistantMessage_NoToolCalls_ReturnsZero()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                return tostring(s:Call({ {role='assistant', content='hi'} }))
            ").String.ShouldBe("0");
        }

        [WindowsFact]
        public void ToolSuite_Call_LastMessage_NotATable_ReturnsZero()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                local msgs = { {role='user', content='x'} }
                msgs[2] = 'not a table'
                return tostring(s:Call(msgs))
            ").String.ShouldBe("0");
        }

        // -- ToolSuite — Call dispatch -----------------------------------------
        [WindowsFact]
        public void ToolSuite_Call_AppendsToolReply_WithCorrectRole()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                s:AddTool('ping', 'Ping', {}, function() return 'pong' end)
                local msgs = {
                    {role='assistant', content='',
                     tool_calls='[{""name"":""ping"",""id"":""id1"",""arguments"":{}}]'}
                }
                s:Call(msgs)
                return msgs[2].role
            ").String.ShouldBe("tool");
        }

        [WindowsFact]
        public void ToolSuite_Call_ContentMatchesCallbackReturn()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                s:AddTool('temp', 'Temp', { {name='unit', type='string', description='unit'} },
                    function(unit) return '42'..unit end)
                local msgs = {
                    {role='assistant', content='',
                     tool_calls='[{""name"":""temp"",""id"":""t1"",""arguments"":{""unit"":""C""}}]'}
                }
                s:Call(msgs)
                return msgs[2].content
            ").String.ShouldBe("42C");
        }

        [WindowsFact]
        public void ToolSuite_Call_ToolCallId_MatchesMessageId()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                s:AddTool('t', 'T', {}, function() return 'ok' end)
                local msgs = {
                    {role='assistant', content='',
                     tool_calls='[{""name"":""t"",""id"":""myid"",""arguments"":{}}]'}
                }
                s:Call(msgs)
                return msgs[2].tool_call_id
            ").String.ShouldBe("myid");
        }

        [WindowsFact]
        public void ToolSuite_Call_UnknownTool_AppendsNotFoundReply()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                local msgs = {
                    {role='assistant', content='',
                     tool_calls='[{""name"":""nonexistent"",""id"":""x1"",""arguments"":{}}]'}
                }
                s:Call(msgs)
                return msgs[2].content
            ").String!.ShouldContain("Tool not found");
        }

        [WindowsFact]
        public void ToolSuite_Call_MultipleTools_AllDispatched()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                s:AddTool('a', 'A', {}, function() return 'ra' end)
                s:AddTool('b', 'B', {}, function() return 'rb' end)
                local msgs = {
                    {role='assistant', content='',
                     tool_calls='[{""name"":""a"",""id"":""i1"",""arguments"":{}},{""name"":""b"",""id"":""i2"",""arguments"":{}}]'}
                }
                local n = s:Call(msgs)
                return tostring(n)..' '..msgs[2].content..' '..msgs[3].content
            ").String.ShouldBe("2 ra rb");
        }

        // -- ToolSuite — Callback (permission gate) ----------------------------
        [WindowsFact]
        public void ToolSuite_Callback_AllowingGate_ToolRuns()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                s:AddTool('work', 'Work', {}, function() return 'done' end)
                s:Callback(function(name, args) return true end)
                local msgs = {
                    {role='assistant', content='',
                     tool_calls='[{""name"":""work"",""id"":""w1"",""arguments"":{}}]'}
                }
                s:Call(msgs)
                return msgs[2].content
            ").String.ShouldBe("done");
        }

        [WindowsFact]
        public void ToolSuite_Callback_DenyingGate_AppendsDeniedReply()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                s:AddTool('danger', 'Danger', {}, function() return 'SHOULD NOT RUN' end)
                s:Callback(function(name, args) return false end)
                local msgs = {
                    {role='assistant', content='',
                     tool_calls='[{""name"":""danger"",""id"":""d1"",""arguments"":{}}]'}
                }
                s:Call(msgs)
                return msgs[2].content
            ").String!.ShouldContain("permission denied");
        }

        [WindowsFact]
        public void ToolSuite_Callback_ReceivesToolNameAndArgs()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local gotName, gotCity = nil, nil
                local s = Llama.CreateToolSuite()
                s:AddTool('weather', 'Weather',
                    { {name='city', type='string', description='city'} },
                    function() return 'ok' end)
                s:Callback(function(name, args)
                    gotName = name
                    gotCity = args and args.city or 'nil'
                    return true
                end)
                local msgs = {
                    {role='assistant', content='',
                     tool_calls='[{""name"":""weather"",""id"":""w1"",""arguments"":{""city"":""Paris""}}]'}
                }
                s:Call(msgs)
                return gotName..' '..gotCity
            ").String.ShouldBe("weather Paris");
        }

        [WindowsFact]
        public void ToolSuite_Callback_Nil_RemovesGate()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                s:AddTool('t', 'T', {}, function() return 'ok' end)
                s:Callback(function() return false end)
                s:Callback(nil)
                local msgs = {
                    {role='assistant', content='',
                     tool_calls='[{""name"":""t"",""id"":""q1"",""arguments"":{}}]'}
                }
                s:Call(msgs)
                return msgs[2].content
            ").String.ShouldBe("ok");
        }

        // -- ToolSuite — yield survival ----------------------------------------
        // Sleep(0) triggers an immediate yield+resume, exercising the same
        // lua_pcallk continuation path that the engine's 1000-instruction
        // ticker forces mid-callback.
        [WindowsFact]
        public void ToolSuite_Call_ToolCallback_SurvivesYield()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                s:AddTool('slow', 'Slow', { {name='v', type='string', description='v'} },
                    function(v)
                        Sleep(0)
                        return 'after-yield:'..tostring(v)
                    end)
                local msgs = {
                    {role='assistant', content='',
                     tool_calls='[{""name"":""slow"",""id"":""s1"",""arguments"":{""v"":""x""}}]'}
                }
                s:Call(msgs)
                return msgs[2].content
            ").String!.ShouldBe("after-yield:x");
        }

        [WindowsFact]
        public void ToolSuite_Callback_Gate_SurvivesYield()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                s:AddTool('work', 'Work', {}, function() return 'done' end)
                s:Callback(function(name, args)
                    Sleep(0)
                    return true
                end)
                local msgs = {
                    {role='assistant', content='',
                     tool_calls='[{""name"":""work"",""id"":""g1"",""arguments"":{}}]'}
                }
                s:Call(msgs)
                return msgs[2].content
            ").String.ShouldBe("done");
        }

        [WindowsFact]
        public void ToolSuite_Call_MultipleTools_SurviveYieldsInCallbacks()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                s:AddTool('a', 'A', {}, function() Sleep(0); return 'ra' end)
                s:AddTool('b', 'B', {}, function() Sleep(0); return 'rb' end)
                local msgs = {
                    {role='assistant', content='',
                     tool_calls='[{""name"":""a"",""id"":""y1"",""arguments"":{}},{""name"":""b"",""id"":""y2"",""arguments"":{}}]'}
                }
                local n = s:Call(msgs)
                return tostring(n)..' '..msgs[2].content..' '..msgs[3].content
            ").String.ShouldBe("2 ra rb");
        }

        [WindowsFact]
        public void ToolSuite_Callback_GateAndToolBothYield_CorrectResults()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local count = 0
                local s = Llama.CreateToolSuite()
                s:AddTool('op', 'Op', {}, function() Sleep(0); return 'ran' end)
                s:Callback(function(name, args)
                    count = count + 1
                    Sleep(0)
                    return count == 1
                end)
                local msgs = {
                    {role='assistant', content='',
                     tool_calls='[{""name"":""op"",""id"":""m1"",""arguments"":{}},{""name"":""op"",""id"":""m2"",""arguments"":{}}]'}
                }
                local n = s:Call(msgs)
                return tostring(n)..' '..msgs[2].content..' '..msgs[3].content
            ").String.ShouldBe("2 ran error: permission denied");
        }

        // -- ToolSuite — stack balance -----------------------------------------
        [WindowsFact]
        public void ToolSuite_Call_Repeated_DoesNotLeakLuaStack()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                s:AddTool('t', 'T', {}, function() return 'ok' end)
                for i = 1, 200 do
                    s:Call({ {role='user', content='hi'} })
                    s:Call({ {role='assistant', content='hi'} })
                    local msgs = {
                        {role='assistant', content='',
                         tool_calls='[{""name"":""t"",""id"":""r'..i..'}"",""arguments"":{}}]'}
                    }
                    s:Call(msgs)
                end
                return true
            ").Boolean.ShouldBeTrue();
        }
    }
}
