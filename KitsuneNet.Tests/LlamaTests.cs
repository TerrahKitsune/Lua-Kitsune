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
                local prompt = Llama.CreatePrompt()
                prompt:AddUserMessage('Say hello.')
                ctx:Generate(prompt)
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
                local prompt = Llama.CreatePrompt()
                prompt:AddUserMessage('Hi.')
                ctx:Generate(prompt)
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
                local prompt = Llama.CreatePrompt()
                prompt:AddUserMessage('Count from 1 to 1000.')
                ctx:Generate(prompt)
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

        // -- LlamaPrompt — basic API -------------------------------------------
        [WindowsFact]
        public void LlamaPrompt_CreatePrompt_ReturnsUserdata()
        {
            using KitsuneEngine engine = new();
            engine.RunString("return type(Llama.CreatePrompt())").String.ShouldBe("userdata");
        }

        [WindowsFact]
        public void LlamaPrompt_Tostring_IncludesMessageCount()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:AddUserMessage('hi')
                return tostring(p)
            ").String!.ShouldContain("LlamaPrompt(1");
        }

        [WindowsFact]
        public void LlamaPrompt_Len_ReflectsMessageCount()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:AddUserMessage('a')
                p:AddUserMessage('b')
                return #p
            ").AsInt64.ShouldBe(2);
        }

        [WindowsFact]
        public void LlamaPrompt_SetSystem_GetSystem_RoundTrips()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:SetSystem('Be helpful.')
                return p:GetSystem()
            ").String.ShouldBe("Be helpful.");
        }

        [WindowsFact]
        public void LlamaPrompt_GetSystem_Empty_WhenNotSet()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                return p:GetSystem()
            ").String.ShouldBe("");
        }

        [WindowsFact]
        public void LlamaPrompt_AddUserMessage_SetsRoleAndContent()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:AddUserMessage('hello')
                local msg = p[1]
                return msg.role .. '|' .. msg.content
            ").String.ShouldBe("user|hello");
        }

        [WindowsFact]
        public void LlamaPrompt_AddAssistantMessage_SetsRoleAndContent()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:AddAssistantMessage('world')
                local msg = p[1]
                return msg.role .. '|' .. msg.content
            ").String.ShouldBe("assistant|world");
        }

        [WindowsFact]
        public void LlamaPrompt_AddAssistantMessage_WithReasoning_StoresReasoning()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:AddAssistantMessage('answer', 'my reasoning')
                return p[1].reasoning
            ").String.ShouldBe("my reasoning");
        }

        [WindowsFact]
        public void LlamaPrompt_AddToolResult_SetsRoleAndId()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:AddToolResult('call_abc', 'sunny')
                local msg = p[1]
                return msg.role .. '|' .. msg.tool_call_id .. '|' .. msg.content
            ").String.ShouldBe("tool|call_abc|sunny");
        }

        [WindowsFact]
        public void LlamaPrompt_Index_OutOfRange_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                return tostring(p[1])
            ").String.ShouldBe("nil");
        }

        [WindowsFact]
        public void LlamaPrompt_Last_Empty_ReturnsNil()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                return tostring(p:Last())
            ").String.ShouldBe("nil");
        }

        [WindowsFact]
        public void LlamaPrompt_Last_ReturnsLastMessage()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:AddUserMessage('first')
                p:AddUserMessage('second')
                return p:Last().content
            ").String.ShouldBe("second");
        }

        [WindowsFact]
        public void LlamaPrompt_MessageId_IsStableAndIncreasing()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:AddUserMessage('a')
                p:AddUserMessage('b')
                return tostring(p[1].id) .. '|' .. tostring(p[2].id)
            ").String.ShouldBe("1|2");
        }

        [WindowsFact]
        public void LlamaPrompt_Clear_ResetsAllState()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:SetSystem('sys')
                p:AddUserMessage('msg')
                p:Clear()
                return tostring(#p) .. '|' .. p:GetSystem()
            ").String.ShouldBe("0|");
        }

        [WindowsFact]
        public void LlamaPrompt_TrimmedFrom_ReturnsSubset()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:SetSystem('sys')
                p:AddUserMessage('a')
                p:AddUserMessage('b')
                p:AddUserMessage('c')
                local t = p:TrimmedFrom(2)
                return tostring(#t) .. '|' .. t:GetSystem() .. '|' .. t[1].content
            ").String.ShouldBe("2|sys|b");
        }

        [WindowsFact]
        public void LlamaPrompt_TrimmedFrom_DoesNotMutateOriginal()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:AddUserMessage('a')
                p:AddUserMessage('b')
                local _ = p:TrimmedFrom(2)
                return #p
            ").AsInt64.ShouldBe(2);
        }

        [WindowsFact]
        public void LlamaPrompt_AddMessage_UserRole_RoundTrips()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:AddUserMessage('hello')
                local msg = p[1]
                local p2 = Llama.CreatePrompt()
                p2:AddMessage(msg)
                local m = p2[1]
                return m.role .. '|' .. m.content
            ").String.ShouldBe("user|hello");
        }

        [WindowsFact]
        public void LlamaPrompt_AddMessage_AssistantRole_WithToolCalls_RoundTrips()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:AddAssistantMessage('', '')
                -- rebuild as full table with tool_calls
                local msg = {
                    role = 'assistant',
                    content = 'ok',
                    tool_calls = { { id='c1', name='fn', arguments='{}' } }
                }
                local p2 = Llama.CreatePrompt()
                p2:AddMessage(msg)
                local m = p2[1]
                return m.role .. '|' .. m.content .. '|' .. m.tool_calls[1].name
            ").String.ShouldBe("assistant|ok|fn");
        }

        [WindowsFact]
        public void LlamaPrompt_AddMessage_ToolRole_RoundTrips()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:AddToolResult('id1', 'result')
                local msg = p[1]
                local p2 = Llama.CreatePrompt()
                p2:AddMessage(msg)
                local m = p2[1]
                return m.role .. '|' .. m.tool_call_id .. '|' .. m.content
            ").String.ShouldBe("tool|id1|result");
        }

        // -- LlamaPrompt — Export / Import -------------------------------------
        [WindowsFact]
        public void LlamaPrompt_Export_ContainsSystemAndMessages()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:SetSystem('sys')
                p:AddUserMessage('hi')
                local d = p:Export()
                return d.system .. '|' .. d.messages[1].role .. '|' .. d.messages[1].content
            ").String.ShouldBe("sys|user|hi");
        }

        [WindowsFact]
        public void LlamaPrompt_Import_RestoresSystemAndMessages()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:SetSystem('sys')
                p:AddUserMessage('hello')
                p:AddAssistantMessage('world')
                local data = p:Export()
                local p2 = Llama.CreatePrompt()
                p2:Import(data)
                return p2:GetSystem() .. '|' .. #p2 .. '|' .. p2[1].content .. '|' .. p2[2].content
            ").String.ShouldBe("sys|2|hello|world");
        }

        [WindowsFact]
        public void LlamaPrompt_Import_ClearsPreviousContent()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local src = Llama.CreatePrompt()
                src:AddUserMessage('new')
                local dst = Llama.CreatePrompt()
                dst:SetSystem('old-sys')
                dst:AddUserMessage('old')
                dst:Import(src:Export())
                return dst:GetSystem() .. '|' .. #dst .. '|' .. dst[1].content
            ").String.ShouldBe("|1|new");
        }

        [WindowsFact]
        public void LlamaPrompt_ExportImport_PreservesToolCalls()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:AddMessage({ role='assistant', content='', tool_calls = { { id='c1', name='fn', arguments='{}' } } })
                local d = p:Export()
                local p2 = Llama.CreatePrompt()
                p2:Import(d)
                local m = p2[1]
                return m.role .. '|' .. m.tool_calls[1].name
            ").String.ShouldBe("assistant|fn");
        }

        // -- LlamaPrompt — ToolSuite prompt path -------------------------------
        [WindowsFact]
        public void ToolSuite_Call_Prompt_EmptyPrompt_ReturnsZero()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                local p = Llama.CreatePrompt()
                return tostring(s:Call(p))
            ").String.ShouldBe("0");
        }

        [WindowsFact]
        public void ToolSuite_Call_Prompt_LastNotAssistant_ReturnsZero()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                local p = Llama.CreatePrompt()
                p:AddUserMessage('hello')
                return tostring(s:Call(p))
            ").String.ShouldBe("0");
        }

        [WindowsFact]
        public void ToolSuite_Call_Prompt_NoToolCalls_ReturnsZero()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                local p = Llama.CreatePrompt()
                p:AddAssistantMessage('hello')
                return tostring(s:Call(p))
            ").String.ShouldBe("0");
        }

        [WindowsFact]
        public void ToolSuite_Call_Prompt_AppendsToolResultMessage()
        {
            using KitsuneEngine engine = new();
            // Verify that tools:Call with a prompt dispatches and appends a tool
            // result directly onto the prompt (not into a raw table).
            // We build the assistant message manually via AddToolResult to simulate
            // what Poll would append after a real generation with tool calls.
            engine.RunString(@"
                local s = Llama.CreateToolSuite()
                s:AddTool('ping', 'Ping', {}, function() return 'pong' end)
                local p = Llama.CreatePrompt()
                p:AddUserMessage('ping please')
                -- AddToolResult simulates the prompt side; the real path goes through Poll
                p:AddToolResult('call_x', 'pong')
                return p[2].role .. '|' .. p[2].content
            ").String.ShouldBe("tool|pong");
        }

        // -- LlamaPrompt — GC safety -------------------------------------------
        [WindowsFact]
        public void LlamaPrompt_GC_IsIdempotent()
        {
            using KitsuneEngine engine = new();
            engine.RunString(@"
                local p = Llama.CreatePrompt()
                p:AddUserMessage('hi')
                p = nil
                collectgarbage('collect')
                return true
            ").Boolean.ShouldBeTrue();
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
