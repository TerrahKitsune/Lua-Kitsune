using KitsuneNet;
using Shouldly;
using System.Diagnostics;
using System.Text.Json.Nodes;
using Xunit;

namespace KitsuneNet.Tests;

/// <summary>
/// Tests for the MCP server's elicitation support. The server speaks JSON-RPC over
/// the process's own stdin/stdout, so the protocol tests run the script in a
/// Kitsune.exe child process and play the client over its pipes.
/// </summary>
[Collection("KitsuneSequential")]
public sealed class McpTests
{
#if DEBUG
    private const string Configuration = "Debug";
#else
    private const string Configuration = "Release";
#endif

    // Every tool returns a string; "ERR:" marks an Elicit that returned false.
    private const string ServerScript = @"
        local mcp = MCP.Create({ Name = 'test', Version = '1' }, {})
        local function default(context, request, reason) return 'default:' .. reason end

        local form = mcp:CreateElicitation('Fill in', default)
        form:AddQuestion('note', 'string', 'Note', false)
        form:AddQuestion('n', 'integer', 'N', true)

        local optionalOnly = mcp:CreateElicitation('Optional', default)
        optionalOnly:AddQuestion('note', 'string', 'Note', false)

        local _, loadTimeErr = pcall(form.Elicit, form, {}, {})

        local function ask(e, context, request)
            local ok, results = e:Elicit(context, request)
            if not ok then return 'ERR:' .. tostring(results) end
            return results
        end

        mcp:AddTool('form', '', {}, function(context, request)
            local r = ask(form, context, request)
            if type(r) == 'string' then return r end
            return tostring(r.note) .. '|' .. tostring(r.n)
        end)

        mcp:AddTool('optional_only', '', {}, function(context, request)
            local r = ask(optionalOnly, context, request)
            if type(r) == 'string' then return r end
            return next(r) == nil and 'empty' or 'not empty'
        end)

        mcp:AddTool('can_elicit', '', {}, function(context, request)
            return tostring(request.CanElicit)
        end)

        mcp:AddTool('load_time', '', {}, function() return tostring(loadTimeErr) end)

        mcp:AddTool('in_task', '', {}, function(context, request)
            local err
            Tasks.New(function()
                local ok, e = pcall(form.Elicit, form, context, request)
                err = ok and 'no error' or tostring(e)
            end)
            for _ = 1, 500 do
                if err then return err end
                Sleep(10)
            end
            return 'task never finished'
        end)

        mcp:AddTool('no_yield', '', {}, function(context, request)
            local ok, err = pcall(table.sort, { 2, 1 }, function(a, b)
                form:Elicit(context, request)
                return a < b
            end)
            return ok and 'no error' or tostring(err)
        end)

        assert(mcp:Start())
        while mcp:IsRunning() do Sleep(5) end
    ";

    private const string ElicitingCapabilities = @"{""elicitation"":{}}";

    // bin\{Configuration}\net10.0\ -> repo root
    private static readonly string KitsuneExe = Path.GetFullPath(Path.Combine(
        AppContext.BaseDirectory, "..", "..", "..", "..", "x64", Configuration, "Kitsune.exe"));

    // -- Where Elicit may be called ---------------------------------------------
    [Fact]
    public async Task Elicit_AtLoadTime_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local mcp = MCP.Create({ Name = 'test', Version = '1' }, {})
            local e = mcp:CreateElicitation('Fill in', function() return 'default' end)
            e:AddQuestion('n', 'integer', 'N', true)
            local ok, err = pcall(e.Elicit, e, {}, {})
            return tostring(ok) .. '|' .. tostring(err)
        ");
        r.String.ShouldBe("false|Elicit: must be called from inside a tool call");
    }

    [WindowsFact]
    public async Task Elicit_AtLoadTime_ThrowsAndSendsNothing()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        (await client.CallToolAsync("load_time")).ShouldBe("Elicit: must be called from inside a tool call");
    }

    [WindowsFact]
    public async Task Elicit_FromTaskStartedByTool_ThrowsAndSendsNothing()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        (await client.CallToolAsync("in_task")).ShouldContain("Elicit: must be called from inside a tool call");
    }

    [WindowsFact]
    public async Task Elicit_FromNonYieldableCallback_ThrowsAndSendsNothing()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        (await client.CallToolAsync("no_yield")).ShouldContain("Elicit: can't wait here");
    }

    // -- JSON null in the client's messages --------------------------------------
    [WindowsFact]
    public async Task Elicit_OptionalAnswerNull_IsAbsent()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        string text = await client.CallToolAnsweringAsync("form",
            @"{""action"":""accept"",""content"":{""note"":null,""n"":5}}");
        text.ShouldBe("nil|5");
    }

    [WindowsFact]
    public async Task Elicit_RequiredAnswerNull_IsMissing()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        string text = await client.CallToolAnsweringAsync("form",
            @"{""action"":""accept"",""content"":{""note"":""hi"",""n"":null}}");
        text.ShouldBe("ERR:invalid response from client: missing required answer 'n'");
    }

    [WindowsFact]
    public async Task Elicit_AcceptWithNullContent_GivesEmptyResults()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        string text = await client.CallToolAnsweringAsync("optional_only",
            @"{""action"":""accept"",""content"":null}");
        text.ShouldBe("empty");
    }

    [WindowsFact]
    public async Task Initialize_ElicitationCapabilityNull_CannotElicit()
    {
        await using var client = await McpClient.StartAsync(@"{""elicitation"":null}");
        (await client.CallToolAsync("can_elicit")).ShouldBe("false");
    }

    [WindowsFact]
    public async Task Initialize_ElicitationCapabilityEmptyObject_CanElicit()
    {
        await using var client = await McpClient.StartAsync(ElicitingCapabilities);
        (await client.CallToolAsync("can_elicit")).ShouldBe("true");
    }

    // -- Client side ------------------------------------------------------------
    private sealed class McpClient : IAsyncDisposable
    {
        private static readonly TimeSpan ReadTimeout = TimeSpan.FromSeconds(10);

        private readonly Process process;
        private readonly string scriptPath;
        private int nextId = 1;

        private McpClient(Process process, string scriptPath)
        {
            this.process = process;
            this.scriptPath = scriptPath;
        }

        public static async Task<McpClient> StartAsync(string capabilitiesJson)
        {
            File.Exists(KitsuneExe).ShouldBeTrue($"Kitsune.exe not built: {KitsuneExe}");
            string script = Path.Combine(Path.GetTempPath(), "_kitsune_mcp_" + Guid.NewGuid().ToString("N") + ".lua");
            await File.WriteAllTextAsync(script, ServerScript);

            var psi = new ProcessStartInfo(KitsuneExe, $"\"{script}\"")
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardInput = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                StandardInputEncoding = new System.Text.UTF8Encoding(false), // no BOM before the first message
                WorkingDirectory = Path.GetDirectoryName(KitsuneExe)!,
            };
            var client = new McpClient(Process.Start(psi)!, script);

            JsonNode init = await client.RequestAsync("initialize",
                $@"{{""protocolVersion"":""2025-11-25"",""capabilities"":{capabilitiesJson},""clientInfo"":{{""name"":""tests"",""version"":""1""}}}}");
            init["result"]!["protocolVersion"]!.GetValue<string>().ShouldBe("2025-11-25");
            await client.SendAsync(@"{""jsonrpc"":""2.0"",""method"":""notifications/initialized""}");
            return client;
        }

        // Calls a tool that doesn't elicit: the very next message must be its result.
        public async Task<string> CallToolAsync(string name)
        {
            JsonNode response = await RequestAsync("tools/call", $@"{{""name"":""{name}"",""arguments"":{{}}}}");
            return ToolText(response);
        }

        // Calls a tool that elicits once, answering the elicitation/create with `result`.
        public async Task<string> CallToolAnsweringAsync(string name, string resultJson)
        {
            int id = nextId++;
            await SendAsync($@"{{""jsonrpc"":""2.0"",""id"":{id},""method"":""tools/call"",""params"":{{""name"":""{name}"",""arguments"":{{}}}}}}");

            JsonNode request = await ReadAsync();
            request["method"]?.GetValue<string>().ShouldBe("elicitation/create");
            string elicitId = request["id"]!.GetValue<string>();
            await SendAsync($@"{{""jsonrpc"":""2.0"",""id"":""{elicitId}"",""result"":{resultJson}}}");

            JsonNode response = await ReadAsync();
            response["id"]!.GetValue<int>().ShouldBe(id);
            return ToolText(response);
        }

        public async ValueTask DisposeAsync()
        {
            try
            {
                process.StandardInput.Close(); // EOF ends the server's poll loop and the script
                using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(10));
                await process.WaitForExitAsync(cts.Token);
            }
            catch (OperationCanceledException)
            {
                process.Kill(true);
            }
            finally
            {
                process.Dispose();
                File.Delete(scriptPath);
            }
        }

        private static string ToolText(JsonNode response)
        {
            JsonNode result = response["result"].ShouldNotBeNull();
            return result["content"]![0]!["text"]!.GetValue<string>();
        }

        private async Task<JsonNode> RequestAsync(string method, string paramsJson)
        {
            int id = nextId++;
            await SendAsync($@"{{""jsonrpc"":""2.0"",""id"":{id},""method"":""{method}"",""params"":{paramsJson}}}");
            JsonNode response = await ReadAsync();
            response["method"].ShouldBeNull($"expected the response to {method}, got a request: {response.ToJsonString()}");
            response["id"]!.GetValue<int>().ShouldBe(id);
            return response;
        }

        private async Task SendAsync(string json)
        {
            await process.StandardInput.WriteAsync(json + "\n"); // MCP stdio frames are '\n'-terminated
            await process.StandardInput.FlushAsync();
        }

        private async Task<JsonNode> ReadAsync()
        {
            using var cts = new CancellationTokenSource(ReadTimeout);
            string? line;
            try
            {
                line = await process.StandardOutput.ReadLineAsync(cts.Token);
            }
            catch (OperationCanceledException)
            {
                throw new TimeoutException("no message from the MCP server within " + ReadTimeout);
            }
            if (line == null)
            {
                throw new InvalidOperationException("MCP server exited: " + await process.StandardError.ReadToEndAsync());
            }

            return JsonNode.Parse(line)!;
        }
    }
}
