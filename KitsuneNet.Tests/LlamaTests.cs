using KitsuneNet;
using Shouldly;
using System.Threading.Tasks;
using Xunit;
using Xunit.Abstractions;

namespace KitsuneNet.Tests;

/// <summary>
/// Integration tests for the Llama module (llama.cpp native provider).
///
/// Smoke tests run always.  Model-dependent tests are gated by [LlamaFact],
/// which skips when the GGUF file is absent (run tests/fetch-test-model.ps1).
///
/// Poll() returns (ok, data):
///   ok   = false when generation is complete
///   data = nil when nothing is ready yet, or a table with:
///     data.type = "token" | "reasoning" | "tool_calls" | "error"
///     data.text = string payload
/// </summary>
[Collection("KitsuneSequential")]
public sealed class LlamaTests
{
    // Lua snippet: wait until ctx is ready before starting a test.
    private const string AwaitReady = @"
        do
            local deadline = os.clock() + 10
            while not ctx:IsReady() and os.clock() < deadline do Sleep(50) end
            assert(ctx:IsReady(), 'LlamaCtx not ready at start of test')
        end";

    // Lua epilogue: disposes local 'ctx'.
    private const string DisposeContext = "ctx:Dispose()";

    private const int TimeoutSec = 120;

    private readonly ITestOutputHelper _out;

    public LlamaTests(ITestOutputHelper output)
    {
        _out = output;
    }

    // ── Smoke (no model required) ─────────────────────────────────────────────
    [Fact]
    public void Llama_ModuleExists()
    {
        using KitsuneEngine engine = new();
        engine.ExecuteString("assert(type(Llama) == 'table', 'Llama module missing')");
    }

    [Fact]
    public void Llama_CreateContext_ReturnsContext()
    {
        using KitsuneEngine engine = new();
        engine.ExecuteString(@"
            local ctx = Llama.CreateContext({})
            assert(ctx ~= nil, 'CreateContext returned nil')
            ctx:Dispose()
        ");
    }

    [Fact]
    public void Llama_Context_IsModelLoaded_False_BeforeLoad()
    {
        using KitsuneEngine engine = new();
        engine.ExecuteString(@"
            local ctx = Llama.CreateContext({})
            assert(ctx:IsModelLoaded() == false, 'should be false before load')
            ctx:Dispose()
        ");
    }

    [Fact]
    public void Llama_Dispose_IsIdempotent()
    {
        using KitsuneEngine engine = new();
        engine.ExecuteString(@"
            local ctx = Llama.CreateContext({})
            ctx:Dispose()
            ctx:Dispose()
        ");
    }

    // ── Model ─────────────────────────────────────────────────────────────────
    [LlamaFact]
    public async Task Llama_LoadModel_Succeeds()
    {
        using KitsuneEngine engine = new();
        LuaValue result = await engine.ExecuteStringAsync($@"
            {LoadContext()}
            local loaded = tostring(ctx:IsModelLoaded())
            {DisposeContext}
            return loaded
        ").ConfigureAwait(false);
        _out.WriteLine($"IsModelLoaded: {result}");
        result.ToString().ShouldBe("true");
    }

    // ── Generation ───────────────────────────────────────────────────────────
    [LlamaFact]
    public async Task Llama_Generate_ProducesTokens()
    {
        using KitsuneEngine engine = new();
        LuaValue result = await engine.ExecuteStringAsync($@"
            {LoadContext()}
            {AwaitReady}
            ctx:Generate(
                {{{{ role = 'user', content = 'Reply with the single word hello.' }}}},
                {{ max_tokens = 32 }}
            )
            local content = ''
            local start = os.clock()
            local ok, data = ctx:Poll()
            while ok and os.clock() - start < {TimeoutSec} do
                if data then
                    if data.type == 'error' then error(data.text) end
                    if data.type == 'token' or data.type == 'reasoning' then content = content .. data.text end
                end
                Sleep(10)
                ok, data = ctx:Poll()
            end
            {DisposeContext}
            return content
        ").ConfigureAwait(false);
        _out.WriteLine($"Generated: {result}");
        result.ToString().Length.ShouldBeGreaterThan(0);
    }

    [LlamaFact]
    public async Task Llama_Generate_StopWorks()
    {
        using KitsuneEngine engine = new();
        LuaValue result = await engine.ExecuteStringAsync($@"
            {LoadContext()}
            {AwaitReady}
            ctx:Generate(
                {{{{ role = 'user', content = 'Count from 1 to 1000.' }}}},
                {{ max_tokens = 4096 }}
            )
            local count = 0
            local start = os.clock()
            local ok, data = ctx:Poll()
            while ok and os.clock() - start < 15 do
                if data then
                    if data.type == 'error' then error(data.text) end
                    if data.type == 'token' then
                        count = count + 1
                        if count >= 3 then ctx:Stop(); break end
                    end
                end
                Sleep(10)
                ok, data = ctx:Poll()
            end
            local deadline = os.clock() + 10
            while not ctx:IsReady() and os.clock() < deadline do Sleep(50) end
            local ready = tostring(ctx:IsReady())
            {DisposeContext}
            return ready
        ").ConfigureAwait(false);
        result.ToString().ShouldBe("true");
    }

    // ── Reasoning ────────────────────────────────────────────────────────────
    [LlamaFact]
    public async Task Llama_Generate_ReasoningType_Returned()
    {
        using KitsuneEngine engine = new();
        LuaValue result = await engine.ExecuteStringAsync($@"
            {LoadContext()}
            {AwaitReady}
            ctx:Generate(
                {{{{ role = 'user', content = 'What is 7 * 8? Think step by step.' }}}},
                {{ max_tokens = 512 }}
            )
            local content, reasoning = '', ''
            local start = os.clock()
            local ok, data = ctx:Poll()
            while ok and os.clock() - start < {TimeoutSec} do
                if data then
                    if data.type == 'error' then error(data.text) end
                    if data.type == 'token'     then content   = content   .. data.text end
                    if data.type == 'reasoning' then reasoning = reasoning .. data.text end
                end
                Sleep(10)
                ok, data = ctx:Poll()
            end
            {DisposeContext}
            return tostring(#reasoning > 0) .. '|' .. tostring(#reasoning)
        ").ConfigureAwait(false);
        _out.WriteLine($"Reasoning result: {result}");
        result.ToString().ShouldStartWith("true");
    }

    // ── Tool calling ─────────────────────────────────────────────────────────
    [LlamaFact]
    public async Task Llama_Generate_ToolCall_IsDetected()
    {
        using KitsuneEngine engine = new();
        LuaValue result = await engine.ExecuteStringAsync($@"
            {LoadContext()}
            {AwaitReady}
            local tools = {{{{
                type = 'function',
                ['function'] = {{
                    name = 'get_weather',
                    description = 'Get the current weather for a city',
                    parameters = {{
                        type = 'object',
                        properties = {{
                            city = {{ type = 'string', description = 'The city name' }}
                        }},
                        required = {{'city'}}
                    }}
                }}
            }}}}
            ctx:Generate(
                {{
                    {{ role = 'system', content = 'You are a helpful assistant. Always use tools when available.' }},
                    {{ role = 'user',   content = 'What is the weather in Paris right now?' }}
                }},
                {{ tools = tools, max_tokens = 256 }}
            )
            local full, tool_json, has_tool = '', '', false
            local start = os.clock()
            local ok, data = ctx:Poll()
            while ok and os.clock() - start < {TimeoutSec} do
                if data then
                    if data.type == 'error'      then error(data.text) end
                    if data.type == 'token'      then full      = full      .. data.text end
                    if data.type == 'tool_calls' then tool_json = tool_json .. data.text; has_tool = true end
                end
                Sleep(10)
                ok, data = ctx:Poll()
            end
            {DisposeContext}
            local out = tool_json ~= '' and tool_json or full
            return tostring(has_tool) .. '|' .. out:sub(1, 200)
        ").ConfigureAwait(false);
        _out.WriteLine($"Tool call output: {result}");
        result.ToString().ShouldNotBeEmpty();
    }

    // ── Reset / multi-turn ───────────────────────────────────────────────────
    [LlamaFact]
    public async Task Llama_Reset_AllowsNewGeneration()
    {
        using KitsuneEngine engine = new();
        LuaValue result = await engine.ExecuteStringAsync($@"
            {LoadContext()}
            {AwaitReady}

            ctx:Generate(
                {{{{ role = 'user', content = 'Say A.' }}}},
                {{ max_tokens = 16 }}
            )
            local out1 = ''
            local start = os.clock()
            local ok, data = ctx:Poll()
            while ok and os.clock() - start < {TimeoutSec} do
                if data then
                    if data.type == 'error' then error(data.text) end
                    if data.type == 'token' or data.type == 'reasoning' then out1 = out1 .. data.text end
                end
                Sleep(10)
                ok, data = ctx:Poll()
            end

            ctx:Reset()
            local deadline = os.clock() + 10
            while not ctx:IsReady() and os.clock() < deadline do Sleep(50) end

            ctx:Generate(
                {{{{ role = 'user', content = 'Say B.' }}}},
                {{ max_tokens = 16 }}
            )
            local out2 = ''
            start = os.clock()
            ok, data = ctx:Poll()
            while ok and os.clock() - start < {TimeoutSec} do
                if data then
                    if data.type == 'error' then error(data.text) end
                    if data.type == 'token' or data.type == 'reasoning' then out2 = out2 .. data.text end
                end
                Sleep(10)
                ok, data = ctx:Poll()
            end

            {DisposeContext}
            return tostring(#out1 > 0 and #out2 > 0)
        ").ConfigureAwait(false);
        result.ToString().ShouldBe("true");
    }

    // Lua prologue: creates and loads the model into local 'ctx'.
    private static string LoadContext(int nCtx = 2048) =>
        $@"local ctx = Llama.CreateContext({{n_ctx = {nCtx}}})
            assert(ctx:SetModel(""{LlamaFactAttribute.ModelPath.Replace("\\", "\\\\")}""))
            assert(ctx:LoadModel())";
}
