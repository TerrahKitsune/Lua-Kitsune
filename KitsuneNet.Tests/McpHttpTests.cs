using KitsuneNet;
using Shouldly;
using System.Diagnostics;
using System.Net;
using System.Net.Http.Headers;
using System.Text;
using System.Text.Json.Nodes;
using Xunit;

namespace KitsuneNet.Tests;

/// <summary>
/// Tests for the MCP server's HTTP transport. The server runs inside a KitsuneEngine in
/// this process and the .NET HttpClient plays the MCP client, so only the server side is
/// Kitsune code. Each test uses its own port.
/// </summary>
[Collection("KitsuneSequential")]
public sealed class McpHttpTests
{
    private const string ProtocolVersion = "2025-11-25";

    private const string Tools = @"
        _mcp:AddTool('echo', 'Echoes', {
            { name = 'text', type = 'string', required = true },
            { name = 'n', type = 'integer' },
        }, function(context, request)
            return tostring(request.Arguments.text) .. '|' .. tostring(request.Parameters[1])
                .. '|' .. tostring(request.Parameters[2])
        end)

        _mcp:AddTool('fail', '', {}, function() error('boom') end)

        _mcp:AddTool('slow', '', {}, function(context)
            context.log = (context.log or '') .. 'start;'
            Sleep(300)
            context.log = context.log .. 'end;'
            return 'slow done'
        end)

        _mcp:AddTool('log', '', {}, function(context) return context.log or '' end)

        _mcp:AddTool('printer', '', {}, function()
            print('from tool', 1)
            io.write('w')
            return 'ret'
        end)

        _mcp:AddTool('whoami', '', {}, function(context, request)
            return request.McpSessionId .. '|' .. request.Client.Name .. '|' .. request.Client.Version
                .. '|' .. tostring(request.RequestId)
        end)

        _mcp:AddTool('can_elicit', '', {}, function(context, request)
            return tostring(request.CanElicit)
        end)

        _mcp:AddTool('elicit', '', {}, function(context, request)
            local form = _mcp:CreateElicitation('Fill in', function(c, r, reason) return 'default:' .. reason end)
            form:AddQuestion('n', 'integer', 'N', true)
            local ok, results = form:Elicit(context, request)
            return tostring(ok) .. '|' .. tostring(results)
        end)
    ";

    private static int nextPort = 19910;

    // Create
    [Fact]
    public async Task Create_HttpWithoutAddress_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local ok, err = pcall(MCP.Create, { Transport = 'http' })
            return tostring(ok) .. '|' .. tostring(err)
        ");
        r.String.ShouldBe("false|MCP.Create: the http transport needs an Address (\"127.0.0.1:port\")");
    }

    [Fact]
    public async Task Create_HttpNonLoopbackAddress_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local out = {}
            for _, address in ipairs({ '0.0.0.0:19909', ':19909', '192.168.1.10:19909', '127.0.0.1', '[::1]:19909' }) do
                local ok, err = pcall(MCP.Create, { Transport = 'http', Address = address })
                out[#out + 1] = tostring(ok) .. ':' .. tostring(tostring(err):find('is not a loopback address', 1, true) ~= nil)
            end
            return table.concat(out, ' ')
        ");
        r.String.ShouldBe("false:true false:true false:true false:true false:true");
    }

    [Fact]
    public async Task Create_UnknownTransport_Throws()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local ok, err = pcall(MCP.Create, { Transport = 'tcp' })
            return tostring(ok) .. '|' .. tostring(err)
        ");
        r.String.ShouldBe("false|MCP.Create: Transport must be \"stdio\" or \"http\"");
    }

    [Fact]
    public async Task Create_Http_IsNotTheStdioSingleton()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local stdio = MCP.Create({ Transport = 'stdio' })
            local a = MCP.Create({ Transport = 'http', Address = '127.0.0.1:19909' })
            local b = MCP.Create({ Transport = 'http', Address = '127.0.0.1:19909' })
            return tostring(a ~= stdio) .. '|' .. tostring(a ~= b) .. '|' .. tostring(MCP.Create() == stdio)
        ");
        r.String.ShouldBe("true|true|true");
    }

    // Start / Stop
    [Fact]
    public async Task Start_AddressInUse_ReturnsFalseAndTheError()
    {
        await using var server = await McpHttpServer.StartAsync();
        LuaValue r = await server.Engine.ExecuteStringAsync($@"
            local second = MCP.Create({{ Transport = 'http', Address = '127.0.0.1:{server.Port}' }})
            local ok, err = second:Start()
            return tostring(ok) .. '|' .. type(err) .. '|' .. tostring(second:IsRunning())
        ");
        r.String.ShouldBe("false|string|false");
    }

    [Fact]
    public async Task Stop_ClosesTheListener()
    {
        await using var server = await McpHttpServer.StartAsync();
        await server.StopAsync();

        LuaValue running = await server.Engine.ExecuteStringAsync("return tostring(_mcp:IsRunning())");
        running.String.ShouldBe("false");
        await Should.ThrowAsync<HttpRequestException>(() => server.PostAsync(@"{""jsonrpc"":""2.0"",""id"":1,""method"":""ping""}"));
    }

    // Handshake and sessions
    [Fact]
    public async Task Initialize_ReturnsSessionIdHeaderAndNegotiatedVersion()
    {
        await using var server = await McpHttpServer.StartAsync(initialize: false);
        using HttpResponseMessage response = await server.PostAsync(McpHttpServer.InitializeJson, withSession: false);

        response.StatusCode.ShouldBe(HttpStatusCode.OK);
        response.Content.Headers.ContentType!.MediaType.ShouldBe("application/json");
        response.Headers.GetValues("MCP-Session-Id").Single().Length.ShouldBe(36);

        JsonNode body = JsonNode.Parse(await response.Content.ReadAsStringAsync())!;
        body["id"]!.GetValue<int>().ShouldBe(1);
        body["result"]!["protocolVersion"]!.GetValue<string>().ShouldBe(ProtocolVersion);
        body["result"]!["serverInfo"]!["name"]!.GetValue<string>().ShouldBe("test");
        body["result"]!["capabilities"]!["tools"].ShouldNotBeNull();
    }

    [Fact]
    public async Task Notification_Returns202WithoutBody()
    {
        await using var server = await McpHttpServer.StartAsync();
        using HttpResponseMessage response = await server.PostAsync(@"{""jsonrpc"":""2.0"",""method"":""notifications/initialized""}");

        response.StatusCode.ShouldBe(HttpStatusCode.Accepted);
        (await response.Content.ReadAsStringAsync()).ShouldBeEmpty();
    }

    [Fact]
    public async Task Ping_ReturnsEmptyResult()
    {
        await using var server = await McpHttpServer.StartAsync();
        JsonNode response = await server.RequestAsync("ping", "{}");
        response["result"]!.ToJsonString().ShouldBe("{}");
    }

    [Fact]
    public async Task ToolsList_ReturnsRegisteredTools()
    {
        await using var server = await McpHttpServer.StartAsync(Tools);
        JsonNode response = await server.RequestAsync("tools/list", "{}");

        JsonArray tools = response["result"]!["tools"]!.AsArray();
        tools[0]!["name"]!.GetValue<string>().ShouldBe("echo");
        tools[0]!["inputSchema"]!["properties"]!["n"]!["type"]!.GetValue<string>().ShouldBe("integer");
        tools[0]!["inputSchema"]!["required"]!.ToJsonString().ShouldBe(@"[""text""]");
    }

    [Fact]
    public async Task UnknownMethod_ReturnsMethodNotFound()
    {
        await using var server = await McpHttpServer.StartAsync();
        JsonNode response = await server.RequestAsync("resources/list", "{}");
        response["error"]!["code"]!.GetValue<int>().ShouldBe(-32601);
    }

    [Fact]
    public async Task Post_WithoutSessionId_Returns400()
    {
        await using var server = await McpHttpServer.StartAsync();
        using HttpResponseMessage response = await server.PostAsync(@"{""jsonrpc"":""2.0"",""id"":5,""method"":""tools/list""}", withSession: false);

        response.StatusCode.ShouldBe(HttpStatusCode.BadRequest);
        JsonNode.Parse(await response.Content.ReadAsStringAsync())!["error"]!["message"]!.GetValue<string>()
            .ShouldBe("Missing MCP-Session-Id header");
    }

    [Fact]
    public async Task Post_UnknownSessionId_Returns404()
    {
        await using var server = await McpHttpServer.StartAsync();
        using HttpResponseMessage response = await server.PostAsync(
            @"{""jsonrpc"":""2.0"",""id"":5,""method"":""tools/list""}",
            withSession: false,
            configure: m => m.Headers.TryAddWithoutValidation("MCP-Session-Id", "not-a-session"));
        response.StatusCode.ShouldBe(HttpStatusCode.NotFound);
    }

    [Fact]
    public async Task Post_UnsupportedProtocolVersionHeader_Returns400()
    {
        await using var server = await McpHttpServer.StartAsync();
        using HttpResponseMessage response = await server.PostAsync(
            @"{""jsonrpc"":""2.0"",""id"":5,""method"":""tools/list""}",
            configure: m => m.Headers.TryAddWithoutValidation("MCP-Protocol-Version", "1999-01-01"));
        response.StatusCode.ShouldBe(HttpStatusCode.BadRequest);
    }

    [Fact]
    public async Task Post_ForeignOrigin_Returns403()
    {
        await using var server = await McpHttpServer.StartAsync(initialize: false);
        using HttpResponseMessage response = await server.PostAsync(
            McpHttpServer.InitializeJson,
            withSession: false,
            configure: m => m.Headers.TryAddWithoutValidation("Origin", "https://evil.example"));
        response.StatusCode.ShouldBe(HttpStatusCode.Forbidden);
    }

    [Theory]
    [InlineData("http://localhost:3000")]
    [InlineData("http://127.0.0.1")]
    [InlineData("https://[::1]:8443")]
    public async Task Post_LoopbackOrigin_IsAccepted(string origin)
    {
        await using var server = await McpHttpServer.StartAsync(initialize: false);
        using HttpResponseMessage response = await server.PostAsync(
            McpHttpServer.InitializeJson,
            withSession: false,
            configure: m => m.Headers.TryAddWithoutValidation("Origin", origin));
        response.StatusCode.ShouldBe(HttpStatusCode.OK);
    }

    [Theory]
    [InlineData("{not json", -32700)]
    [InlineData("42", -32600)]
    [InlineData(@"[{""jsonrpc"":""2.0"",""id"":1,""method"":""ping""}]", -32600)]
    [InlineData("", -32700)]
    public async Task Post_InvalidBody_Returns400(string body, int code)
    {
        await using var server = await McpHttpServer.StartAsync();
        using HttpResponseMessage response = await server.PostAsync(body);

        response.StatusCode.ShouldBe(HttpStatusCode.BadRequest);
        JsonNode error = JsonNode.Parse(await response.Content.ReadAsStringAsync())!;
        error["error"]!["code"]!.GetValue<int>().ShouldBe(code);
        error["id"].ShouldBeNull();
    }

    [Fact]
    public async Task Post_ResponseMessage_Returns202()
    {
        await using var server = await McpHttpServer.StartAsync();
        using HttpResponseMessage response = await server.PostAsync(@"{""jsonrpc"":""2.0"",""id"":""kitsune-1"",""result"":{}}");
        response.StatusCode.ShouldBe(HttpStatusCode.Accepted);
    }

    [Fact]
    public async Task Get_Returns405()
    {
        await using var server = await McpHttpServer.StartAsync();
        using HttpResponseMessage response = await server.SendAsync(HttpMethod.Get);

        response.StatusCode.ShouldBe(HttpStatusCode.MethodNotAllowed);
        response.Content.Headers.Allow.ShouldBe(new[] { "POST", "DELETE" });
    }

    [Fact]
    public async Task Delete_EndsTheSession()
    {
        await using var server = await McpHttpServer.StartAsync();
        using (HttpResponseMessage deleted = await server.SendAsync(HttpMethod.Delete))
        {
            deleted.StatusCode.ShouldBe(HttpStatusCode.OK);
        }

        using HttpResponseMessage after = await server.PostAsync(@"{""jsonrpc"":""2.0"",""id"":5,""method"":""tools/list""}");
        after.StatusCode.ShouldBe(HttpStatusCode.NotFound);
    }

    [Fact]
    public async Task OtherPath_Returns404()
    {
        await using var server = await McpHttpServer.StartAsync();
        using HttpResponseMessage response = await server.PostAsync(@"{""jsonrpc"":""2.0"",""id"":5,""method"":""ping""}", path: "/other");
        response.StatusCode.ShouldBe(HttpStatusCode.NotFound);
    }

    // Tool calls
    [Fact]
    public async Task ToolsCall_PassesArgumentsAndParameters()
    {
        await using var server = await McpHttpServer.StartAsync(Tools);
        (await server.CallToolAsync("echo", @"{""text"":""hi"",""n"":7}")).ShouldBe("hi|hi|7");
    }

    [Fact]
    public async Task ToolsCall_UnknownTool_ReturnsInvalidParams()
    {
        await using var server = await McpHttpServer.StartAsync(Tools);
        JsonNode response = await server.RequestAsync("tools/call", @"{""name"":""nope"",""arguments"":{}}");

        response["error"]!["code"]!.GetValue<int>().ShouldBe(-32602);
        response["error"]!["message"]!.GetValue<string>().ShouldBe("Unknown tool");
    }

    [Fact]
    public async Task ToolsCall_WithoutParams_ReturnsInvalidParams()
    {
        await using var server = await McpHttpServer.StartAsync(Tools);
        using HttpResponseMessage response = await server.PostAsync(@"{""jsonrpc"":""2.0"",""id"":5,""method"":""tools/call""}");

        JsonNode body = JsonNode.Parse(await response.Content.ReadAsStringAsync())!;
        body["error"]!["message"]!.GetValue<string>().ShouldBe("Missing tool name");
    }

    [Fact]
    public async Task ToolsCall_ErrorInCallback_ReturnsIsError()
    {
        await using var server = await McpHttpServer.StartAsync(Tools);
        JsonNode response = await server.RequestAsync("tools/call", @"{""name"":""fail"",""arguments"":{}}");

        response["result"]!["isError"]!.GetValue<bool>().ShouldBeTrue();
        response["result"]!["content"]![0]!["text"]!.GetValue<string>().ShouldContain("boom");
    }

    [Fact]
    public async Task ToolsCall_YieldingCallback_ReturnsAfterSleep()
    {
        await using var server = await McpHttpServer.StartAsync(Tools);
        var watch = Stopwatch.StartNew();
        (await server.CallToolAsync("slow")).ShouldBe("slow done");
        watch.ElapsedMilliseconds.ShouldBeGreaterThanOrEqualTo(250);
    }

    [Fact]
    public async Task ToolsCall_TwoCallsAtOnce_RunOneAfterTheOther()
    {
        await using var server = await McpHttpServer.StartAsync(Tools);
        string[] results = await Task.WhenAll(server.CallToolAsync("slow"), server.CallToolAsync("slow"));

        results.ShouldBe(new[] { "slow done", "slow done" });
        (await server.CallToolAsync("log")).ShouldBe("start;end;start;end;");
    }

    [Fact]
    public async Task ToolsCall_ListWhileACallSleeps_IsAnsweredAfterTheCall()
    {
        await using var server = await McpHttpServer.StartAsync(Tools);
        Task<string> slow = server.CallToolAsync("slow");
        await Task.Delay(100);

        var watch = Stopwatch.StartNew();
        await server.RequestAsync("tools/list", "{}");
        watch.ElapsedMilliseconds.ShouldBeGreaterThanOrEqualTo(100);
        (await slow).ShouldBe("slow done");
    }

    [Fact]
    public async Task ToolsCall_NewConnectionPerRequest_KeepsTheSession()
    {
        await using var server = await McpHttpServer.StartAsync(Tools, initialize: false);
        server.CloseConnectionAfterEachRequest = true;
        await server.InitializeAsync();

        (await server.CallToolAsync("echo", @"{""text"":""a""}")).ShouldBe("a|a|nil");
        (await server.CallToolAsync("whoami")).ShouldStartWith(server.SessionId + "|tests|1|");
    }

    [Fact]
    public async Task ToolsCall_PrintInsideCallback_IsCaptured()
    {
        await using var server = await McpHttpServer.StartAsync(Tools);
        await server.Engine.ExecuteStringAsync("print('outside any tool call')");

        JsonNode response = await server.RequestAsync("tools/call", @"{""name"":""printer"",""arguments"":{}}");
        JsonArray content = response["result"]!["content"]!.AsArray();
        content.Count.ShouldBe(2);
        content[0]!["text"]!.GetValue<string>().ShouldBe("from tool\t1\nw");
        content[1]!["text"]!.GetValue<string>().ShouldBe("ret");
    }

    [Fact]
    public async Task ToolsCall_RequestCarriesSessionIdClientAndRequestId()
    {
        await using var server = await McpHttpServer.StartAsync(Tools);
        JsonNode response = await server.RequestAsync("tools/call", @"{""name"":""whoami"",""arguments"":{}}");

        int id = response["id"]!.GetValue<int>();
        response["result"]!["content"]![0]!["text"]!.GetValue<string>().ShouldBe($"{server.SessionId}|tests|1|{id}");
    }

    [Fact]
    public async Task ToolsCall_TwoSessions_SeeTheirOwnSessionId()
    {
        await using var server = await McpHttpServer.StartAsync(Tools);
        string first = server.SessionId!;
        (await server.CallToolAsync("whoami")).ShouldStartWith(first + "|");

        await server.InitializeAsync();
        server.SessionId.ShouldNotBe(first);
        (await server.CallToolAsync("whoami")).ShouldStartWith(server.SessionId + "|");
    }

    [Fact]
    public async Task ToolsCall_CanElicitIsFalseEvenWhenTheClientDeclaresIt()
    {
        await using var server = await McpHttpServer.StartAsync(Tools);
        (await server.CallToolAsync("can_elicit")).ShouldBe("false");
    }

    [Fact]
    public async Task Elicit_OverHttp_RunsTheDefaultCallbackAsUnsupported()
    {
        await using var server = await McpHttpServer.StartAsync(Tools);
        (await server.CallToolAsync("elicit")).ShouldBe("true|default:unsupported");
    }

    [Fact]
    public async Task ToolsCall_ClientDisconnectsEarly_CallFinishesAndServerKeepsServing()
    {
        await using var server = await McpHttpServer.StartAsync(Tools);
        using (var cts = new CancellationTokenSource(TimeSpan.FromMilliseconds(100)))
        {
            await Should.ThrowAsync<OperationCanceledException>(() => server.CallToolAsync("slow", "{}", cts.Token));
        }

        (await server.CallToolAsync("echo", @"{""text"":""still here""}")).ShouldBe("still here|still here|nil");
        (await server.CallToolAsync("log")).ShouldBe("start;end;");
    }

    [Fact]
    public async Task ToolsCall_ToolCallsOsExit_ReturnsACleanResultAndStopsTheServer()
    {
        await using var server = await McpHttpServer.StartAsync(
            "_mcp:AddTool('exit_now', '', {}, function() print('leaving') os.exit(3) return 'unreachable' end)");
        JsonNode response = await server.RequestAsync("tools/call", @"{""name"":""exit_now"",""arguments"":{}}");

        response["result"]!["isError"].ShouldBeNull();
        response["result"]!["content"]!.AsArray().Select(c => c!["text"]!.GetValue<string>())
            .ShouldBe(new[] { "leaving\n", "exit 3" });
        await server.ExpectExitAsync(3);
    }

    // HTTP mode must leave the process's own stdin/stdout to the hosting script.
    [KitsuneExeFact]
    public async Task HttpTransport_LeavesStdinAndStdoutToTheScript()
    {
        int port = TakePort();
        string script = Path.Combine(Path.GetTempPath(), "_kitsune_mcp_http_" + Guid.NewGuid().ToString("N") + ".lua");
        await File.WriteAllTextAsync(script, $@"
            local context = {{}}
            local mcp = MCP.Create({{ Name = 'test', Version = '1', Transport = 'http', Address = '127.0.0.1:{port}' }}, context)
            mcp:AddTool('line', '', {{}}, function(context) return tostring(context.line) end)
            mcp:AddTool('quit', '', {{}}, function() mcp:Stop() return 'bye' end)
            assert(mcp:Start())
            print('ready')
            io.stdout:flush()
            context.line = io.read('l')
            io.write('got ' .. context.line .. '\n')
            io.stdout:flush()
            while mcp:IsRunning() do Sleep(5) end
        ");

        var psi = new ProcessStartInfo(KitsuneExeFactAttribute.ExePath, $"\"{script}\"")
        {
            UseShellExecute = false,
            CreateNoWindow = true,
            RedirectStandardInput = true,
            RedirectStandardOutput = true,
            RedirectStandardError = true,
            WorkingDirectory = Path.GetDirectoryName(KitsuneExeFactAttribute.ExePath)!,
        };
        using Process process = Process.Start(psi)!;
        try
        {
            using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(10));
            (await process.StandardOutput.ReadLineAsync(timeout.Token)).ShouldBe("ready");
            await process.StandardInput.WriteAsync("hello\n");
            await process.StandardInput.FlushAsync();
            (await process.StandardOutput.ReadLineAsync(timeout.Token)).ShouldBe("got hello");

            using var client = new McpHttpClient(port);
            await client.InitializeAsync();
            (await client.CallToolAsync("line")).ShouldBe("hello");
            (await client.CallToolAsync("quit")).ShouldBe("bye");

            await process.WaitForExitAsync(timeout.Token);
        }
        finally
        {
            if (!process.HasExited)
            {
                process.Kill(true);
            }

            File.Delete(script);
        }
    }

    private static int TakePort() => Interlocked.Increment(ref nextPort);

    // Server side: an engine running an HTTP MCP server in the global _mcp, plus a client for it.
    private sealed class McpHttpServer : IAsyncDisposable
    {
        public const string InitializeJson = McpHttpClient.InitializeJson;

        private readonly McpHttpClient client;
        private readonly Task keepAlive;
        private bool stopped;

        private McpHttpServer(KitsuneEngine engine, int port, Task keepAlive)
        {
            Engine = engine;
            Port = port;
            this.keepAlive = keepAlive;
            client = new McpHttpClient(port);
        }

        public KitsuneEngine Engine { get; }

        public int Port { get; }

        public string? SessionId => client.SessionId;

        public bool CloseConnectionAfterEachRequest
        {
            set => client.CloseConnectionAfterEachRequest = value;
        }

        public static async Task<McpHttpServer> StartAsync(string toolsLua = "", bool initialize = true)
        {
            int port = TakePort();
            var engine = new KitsuneEngine();
            await engine.ExecuteStringAsync($@"
                _mcp = MCP.Create({{ Name = 'test', Version = '1', Transport = 'http', Address = '127.0.0.1:{port}' }}, {{}})
                {toolsLua}
                assert(_mcp:Start())
            ");

            // Keeps a script running for as long as the server does, like a real host script.
            Task keepAlive = Task.Run(() => engine.ExecuteStringAsync("while _mcp:IsRunning() do Sleep(5) end"));

            var server = new McpHttpServer(engine, port, keepAlive);
            if (initialize)
            {
                await server.InitializeAsync();
            }

            return server;
        }

        public Task InitializeAsync() => client.InitializeAsync();

        public Task<HttpResponseMessage> PostAsync(
            string json,
            bool withSession = true,
            Action<HttpRequestMessage>? configure = null,
            string path = "/mcp")
            => client.SendAsync(HttpMethod.Post, json, withSession, configure, path);

        public Task<HttpResponseMessage> SendAsync(HttpMethod method) => client.SendAsync(method, null);

        public Task<JsonNode> RequestAsync(string method, string paramsJson) => client.RequestAsync(method, paramsJson);

        public Task<string> CallToolAsync(string name, string argumentsJson = "{}", CancellationToken cancellationToken = default)
            => client.CallToolAsync(name, argumentsJson, cancellationToken);

        // The script that keeps the server alive is stopped by os.exit like every other coroutine.
        public async Task ExpectExitAsync(int exitCode)
        {
            stopped = true;
            LuaExitException exit = await Should.ThrowAsync<LuaExitException>(() => keepAlive.WaitAsync(TimeSpan.FromSeconds(10)));
            exit.ExitCode.ShouldBe(exitCode);
        }

        public async Task StopAsync()
        {
            if (stopped)
            {
                return;
            }

            stopped = true;
            await Engine.ExecuteStringAsync("_mcp:Stop()");
            await keepAlive.WaitAsync(TimeSpan.FromSeconds(10));
        }

        public async ValueTask DisposeAsync()
        {
            try
            {
                await StopAsync();
            }
            finally
            {
                client.Dispose();
                Engine.Dispose();
            }
        }
    }

    // Client side
    private sealed class McpHttpClient : IDisposable
    {
        public const string InitializeJson =
            @"{""jsonrpc"":""2.0"",""id"":1,""method"":""initialize"",""params"":{""protocolVersion"":""" + ProtocolVersion
            + @""",""capabilities"":{""elicitation"":{}},""clientInfo"":{""name"":""tests"",""version"":""1""}}}";

        private readonly HttpClient http = new() { Timeout = TimeSpan.FromSeconds(10) };
        private readonly string baseUrl;
        private int nextId = 1;

        public McpHttpClient(int port)
        {
            baseUrl = $"http://127.0.0.1:{port}";
        }

        public string? SessionId { get; private set; }

        public bool CloseConnectionAfterEachRequest { get; set; }

        public async Task InitializeAsync()
        {
            using HttpResponseMessage response = await SendAsync(HttpMethod.Post, InitializeJson, withSession: false);
            response.StatusCode.ShouldBe(HttpStatusCode.OK);
            SessionId = response.Headers.GetValues("MCP-Session-Id").Single();

            using HttpResponseMessage initialized = await SendAsync(
                HttpMethod.Post,
                @"{""jsonrpc"":""2.0"",""method"":""notifications/initialized""}");
            initialized.StatusCode.ShouldBe(HttpStatusCode.Accepted);
        }

        public async Task<HttpResponseMessage> SendAsync(
            HttpMethod method,
            string? json,
            bool withSession = true,
            Action<HttpRequestMessage>? configure = null,
            string path = "/mcp",
            CancellationToken cancellationToken = default)
        {
            using var message = new HttpRequestMessage(method, baseUrl + path);
            message.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("application/json"));
            message.Headers.Accept.Add(new MediaTypeWithQualityHeaderValue("text/event-stream"));
            message.Headers.ConnectionClose = CloseConnectionAfterEachRequest;
            if (json != null)
            {
                message.Content = new StringContent(json, Encoding.UTF8, "application/json");
            }

            if (withSession && SessionId != null)
            {
                message.Headers.TryAddWithoutValidation("MCP-Session-Id", SessionId);
                message.Headers.TryAddWithoutValidation("MCP-Protocol-Version", ProtocolVersion);
            }

            configure?.Invoke(message);
            return await http.SendAsync(message, cancellationToken);
        }

        public async Task<JsonNode> RequestAsync(string method, string paramsJson, CancellationToken cancellationToken = default)
        {
            int id = Interlocked.Increment(ref nextId);
            using HttpResponseMessage response = await SendAsync(
                HttpMethod.Post,
                $@"{{""jsonrpc"":""2.0"",""id"":{id},""method"":""{method}"",""params"":{paramsJson}}}",
                cancellationToken: cancellationToken);
            response.StatusCode.ShouldBe(HttpStatusCode.OK);

            JsonNode body = JsonNode.Parse(await response.Content.ReadAsStringAsync(cancellationToken))!;
            body["id"]!.GetValue<int>().ShouldBe(id);
            return body;
        }

        public async Task<string> CallToolAsync(string name, string argumentsJson = "{}", CancellationToken cancellationToken = default)
        {
            JsonNode response = await RequestAsync(
                "tools/call",
                $@"{{""name"":""{name}"",""arguments"":{argumentsJson}}}",
                cancellationToken);
            JsonNode result = response["result"].ShouldNotBeNull();
            return result["content"]![0]!["text"]!.GetValue<string>();
        }

        public void Dispose() => http.Dispose();
    }
}
