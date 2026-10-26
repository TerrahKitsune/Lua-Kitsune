using KitsuneNet;
using Shouldly;
using System;
using System.Collections.Generic;
using System.IO;
using System.Net;
using System.Net.Http;
using System.Net.WebSockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Xunit;

namespace KitsuneNet.Tests;

[Collection("KitsuneSequential")]

/// <summary>
/// Tests for the HttpServer module.
/// Spins up a real HttpServer inside a Lua coroutine and sends requests to it with the
/// .NET HttpClient / ClientWebSocket, so only the server side is Kitsune code.
/// No external services required. Each test uses a unique port.
/// </summary>
public sealed class KitsuneHttpServerTests
{
    // A non-seekable read-only stream forces Transfer-Encoding: chunked.
    // Stream.New(string) is seekable, so we use a function-backend stream
    // that only advertises CAP_READ (1) — no CAP_SEEK (4).
    private const string MakeChunkedStream = """
        local function make_chunked_stream(data)
            local OPEN, CLOSE, READ = 0, 1, 2
            local CAP_READ = 1
            local pos = 0
            return Stream.New(function(op, arg)
                if op == OPEN then
                    return CAP_READ
                elseif op == READ then
                    local chunk = data:sub(pos + 1, pos + arg)
                    pos = pos + #chunk
                    return chunk
                end
            end)
        end
        """;

    // C# HttpClient tests
    // Pattern: three separate scripts on the same engine share Lua globals.
    //   Script 1 (awaited):  _server = HttpServer.Listen(port)
    //                        Server is bound before this returns, so the C#
    //                        client is guaranteed to find an open port.
    //   Script 2 (background Task): pumps _server:Accept() until _stop is set.
    //   C# client:           fires the real HTTP request.
    //   Script 3 (awaited):  _stop = true  — unblocks the pump loop.
    private const string PumpScript = """
        local co = _server:Accept()
        while not _stop do
            coroutine.resume(co)
        end
        coroutine.resume(co, true)
        """;

    // Module availability
    [Fact]
    public async Task HttpServer_Module_IsTable()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("return type(HttpServer)");
        r.String.ShouldBe("table");
    }

    // Listen
    [Fact]
    public async Task Listen_ValidAddress_ReturnsServer()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local s, err = HttpServer.Listen('127.0.0.1:19800')
            if not s then return 'fail: ' .. tostring(err) end
            s:Close()
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    [Fact]
    public async Task Listen_BadAddress_ReturnsNilAndError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local s, err = HttpServer.Listen('not-a-valid-address:99999')
            return tostring(s == nil and type(err) == 'string')
        ");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Listen_Tostring_ReturnsNonEmpty()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local s = assert(HttpServer.Listen('127.0.0.1:19801'))
            local str = tostring(s)
            s:Close()
            return tostring(type(str) == 'string' and #str > 0)
        ");
        r.String.ShouldBe("true");
    }

    // Accept idempotency
    [Fact]
    public async Task Accept_CalledTwice_ReturnsSameCoroutine()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local s = assert(HttpServer.Listen('127.0.0.1:19802'))
            local co1 = s:Accept()
            local co2 = s:Accept()
            s:Close()
            return tostring(co1 == co2)
        ");
        r.String.ShouldBe("true");
    }

    // Response body
    [Fact]
    public async Task Server_GET_ResponseBodyIsReturned()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19804", "resp:Send('hello world')");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        string body = await client.GetStringAsync("http://127.0.0.1:19804/body");
        await StopLuaServer(engine, pump);
        body.ShouldBe("hello world");
    }

    // URL and method visible on server side
    [Fact]
    public async Task Server_Request_GetUrlAndMethod()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19805", "resp:Send(req:GetMethod() .. ':' .. req:GetUrl())");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        string body = await client.GetStringAsync("http://127.0.0.1:19805/test/path");
        await StopLuaServer(engine, pump);
        body.ShouldBe("GET:/test/path");
    }

    // POST body echoed back
    [Fact]
    public async Task Server_POST_BodyIsReceived()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19806", "resp:Send(req:GetBody())");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        var response = await client.PostAsync("http://127.0.0.1:19806/echo", new StringContent("ping payload"));
        string body = await response.Content.ReadAsStringAsync();
        await StopLuaServer(engine, pump);
        body.ShouldBe("ping payload");
    }

    // The context is per request: stable within one request, fresh for the next one.
    [Fact]
    public async Task Server_GetContext_IsPerRequestAndStableWithinRequest()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19808", """
            local ctx = req:GetContext()
            ctx.visits = (ctx.visits or 0) + 1
            resp:Send(tostring(req:GetContext().visits) .. ':' .. tostring(req:GetContext() == ctx))
            """);
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        string first = await client.GetStringAsync("http://127.0.0.1:19808/ctx");
        string second = await client.GetStringAsync("http://127.0.0.1:19808/ctx");
        await StopLuaServer(engine, pump);
        first.ShouldBe("1:true");
        second.ShouldBe("1:true");
    }

    // Headers visible on server side
    [Fact]
    public async Task Server_GetHeaders_ContainsCustomHeader()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19809", "resp:Send(tostring(req:GetHeaders()['x-kitsune']))");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        var request = new HttpRequestMessage(HttpMethod.Get, "http://127.0.0.1:19809/headers");
        request.Headers.Add("X-Kitsune", "testvalue");
        var response = await client.SendAsync(request);
        string body = await response.Content.ReadAsStringAsync();
        await StopLuaServer(engine, pump);
        body.ShouldBe("testvalue");
    }

    // Send(Stream)
    [Fact]
    public async Task Server_SendStream_BodyIsReceived()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19810", """
            resp:SetHeader('Content-Type', 'text/plain')
            resp:Send(Stream.New('stream body content'))
            """);
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        string body = await client.GetStringAsync("http://127.0.0.1:19810/stream");
        await StopLuaServer(engine, pump);
        body.ShouldBe("stream body content");
    }

    // Double-send raises error
    [Fact]
    public async Task Response_SendTwice_RaisesError()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19811", """
            resp:Send('first')
            local ok = pcall(function() resp:Send('second') end)
            _doubleSendErrored = not ok
            """);
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        string body = await client.GetStringAsync("http://127.0.0.1:19811/dbl");
        LuaValue r = await engine.ExecuteStringAsync("return tostring(_doubleSendErrored)");
        await StopLuaServer(engine, pump);
        body.ShouldBe("first");
        r.String.ShouldBe("true");
    }

    // SetOnDisconnect called after response
    [Fact]
    public async Task Server_SetOnDisconnect_CalledAfterResponse()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19812", "resp:Send('bye')");
        await engine.ExecuteStringAsync("_server:SetOnDisconnect(function(req) _disconnected = true end)");
        HttpStatusCode status;
        using (var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) })
        {
            var response = await client.GetAsync("http://127.0.0.1:19812/disc");
            status = response.StatusCode;
        }

        // Disposing the client closes its pooled connection, which fires the callback.
        bool disconnected = false;
        var deadline = DateTime.UtcNow.AddSeconds(5);
        while (!disconnected && DateTime.UtcNow < deadline)
        {
            disconnected = (await engine.ExecuteStringAsync("return tostring(_disconnected == true)")).String == "true";
            if (!disconnected)
            {
                await Task.Delay(20);
            }
        }

        await StopLuaServer(engine, pump);
        ((int)status).ShouldBe(200);
        disconnected.ShouldBeTrue();
    }

    // Teardown tests
    [Fact]
    public async Task Server_Close_IsIdempotent()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local s = assert(HttpServer.Listen('127.0.0.1:19820'))
            s:Close()
            s:Close()
            local again, err = HttpServer.Listen('127.0.0.1:19820')
            if not again then return 'port not released: ' .. tostring(err) end
            again:Close()
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    [Fact]
    public async Task Server_StopFlag_ClosesCleanly()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local s = assert(HttpServer.Listen('127.0.0.1:19821'))
            local co = s:Accept()
            coroutine.resume(co)
            coroutine.resume(co, true)
            return tostring(coroutine.status(co) == 'dead')
        ");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Server_GC_AfterClose_DoesNotCrash()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local s = assert(HttpServer.Listen('127.0.0.1:19822'))
            s:Close()
            s = nil
            collectgarbage('collect')
            local again, err = HttpServer.Listen('127.0.0.1:19822')
            if not again then return 'port not released: ' .. tostring(err) end
            again:Close()
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    [Fact]
    public async Task Server_GC_WithoutClose_DoesNotCrash()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            do
                local s = assert(HttpServer.Listen('127.0.0.1:19823'))
            end
            collectgarbage('collect')
            collectgarbage('collect')
            local again, err = HttpServer.Listen('127.0.0.1:19823')
            if not again then return 'port not released by __gc: ' .. tostring(err) end
            again:Close()
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    [Fact]
    public async Task Response_AfterRequestGC_RaisesError()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19824", "_savedResp = resp; resp:Send('ok')");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        string body = await client.GetStringAsync("http://127.0.0.1:19824/stale");
        LuaValue r = await engine.ExecuteStringAsync("""
            collectgarbage('collect')
            collectgarbage('collect')
            local ok = pcall(function() _savedResp:Send('again') end)
            _savedResp = nil
            return tostring(not ok)
            """);
        await StopLuaServer(engine, pump);
        body.ShouldBe("ok");
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Server_QueuedRequestsCleanedUp_OnStopFlag()
    {
        // The handler never answers, so the request is still queued when the server stops;
        // StopLuaServer disposes the engine, whose leak check covers the cleanup.
        var (engine, pump) = await StartLuaServer("127.0.0.1:19825", "_seen = true");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(10) };
        Task<HttpResponseMessage> pending = client.GetAsync("http://127.0.0.1:19825/drop");

        bool seen = false;
        var deadline = DateTime.UtcNow.AddSeconds(5);
        while (!seen && DateTime.UtcNow < deadline)
        {
            seen = (await engine.ExecuteStringAsync("return tostring(_seen == true)")).String == "true";
            if (!seen)
            {
                await Task.Delay(20);
            }
        }

        await StopLuaServer(engine, pump);

        // The teardown must drop the queued connection itself; a client timeout would surface as TaskCanceledException.
        await Should.ThrowAsync<HttpRequestException>(pending);
        seen.ShouldBeTrue();

        using KitsuneEngine rebound = new();
        LuaValue r = await rebound.ExecuteStringAsync(@"
            local s, err = HttpServer.Listen('127.0.0.1:19825')
            if not s then return 'port not released: ' .. tostring(err) end
            s:Close()
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    [Fact]
    public async Task CSharpClient_GET_DefaultContentTypeIsNonEmpty()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19832", "resp:Send('ok')");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        var response = await client.GetAsync("http://127.0.0.1:19832/");
        await StopLuaServer(engine, pump);
        response.Content.Headers.ContentType.ShouldNotBeNull();
        response.Content.Headers.ContentType.MediaType.ShouldNotBeNullOrEmpty();
    }

    [Theory]
    [InlineData(19842, "resp:Send()", 200, "OK")]
    [InlineData(19803, "resp:SetCode(200); resp:Send('hello')", 200, "OK")]
    [InlineData(19835, "resp:SetCode(404); resp:Send('not found')", 404, "Not Found")]
    [InlineData(19843, "resp:SetCode(500); resp:Send('boom')", 500, "Internal Server Error")]
    [InlineData(19844, "resp:SetCode(201); resp:Send(Stream.New())", 201, "Created")]
    [InlineData(19845, "req:GetResponse():Reject(403, 'no')", 403, "Forbidden")]
    [InlineData(19807, "resp:Reject(404, 'Not Found')", 404, "Not Found")]
    [InlineData(19841, "resp:Reject(503, 'Service Unavailable')", 503, "Service Unavailable")]
    public async Task CSharpClient_Handler_ResponseHasExpectedStatusAndReasonPhrase(int port, string handler, int code, string reason)
    {
        var (engine, pump) = await StartLuaServer($"127.0.0.1:{port}", handler);
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        var response = await client.GetAsync($"http://127.0.0.1:{port}/");
        await StopLuaServer(engine, pump);
        ((int)response.StatusCode).ShouldBe(code);
        response.ReasonPhrase.ShouldBe(reason);
    }

    [Fact]
    public async Task CSharpClient_SetHeader_ContentType_IsReceivedByClient()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19836",
            """resp:SetHeader('Content-Type', 'application/json'); resp:Send('{"ok":true}')""");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        var response = await client.GetAsync("http://127.0.0.1:19836/");
        await StopLuaServer(engine, pump);
        response.Content.Headers.ContentType.ShouldNotBeNull();
        response.Content.Headers.ContentType.MediaType.ShouldBe("application/json");
    }

    [Fact]
    public async Task CSharpClient_GetUrl_IncludesPathAndQuery()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19837", "resp:Send(req:GetUrl())");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        string body = await client.GetStringAsync("http://127.0.0.1:19837/some/path?foo=bar");
        await StopLuaServer(engine, pump);
        body.ShouldBe("/some/path?foo=bar");
    }

    [Fact]
    public async Task CSharpClient_GetIp_IsNonEmpty()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19839", "resp:Send(req:GetIp())");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        string body = await client.GetStringAsync("http://127.0.0.1:19839/");
        await StopLuaServer(engine, pump);
        body.ShouldStartWith("127.0.0.1:");
        int.TryParse(body["127.0.0.1:".Length..], out int port).ShouldBeTrue();
        port.ShouldBeGreaterThan(0);
    }

    [Fact]
    public async Task CSharpClient_GetId_IsNonZero()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19840", "resp:Send(tostring(req:GetId()))");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        string body = await client.GetStringAsync("http://127.0.0.1:19840/");
        await StopLuaServer(engine, pump);
        long.TryParse(body, out long id).ShouldBeTrue();
        id.ShouldNotBe(0L);
    }

    // Chunked stream tests
    [Fact]
    public async Task Server_SendChunkedStream_NonSeekableStream_IsChunkedWithIntactBody()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19851",
            MakeChunkedStream + "\nresp:Send(make_chunked_stream('hello chunked'))");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        var response = await client.GetAsync("http://127.0.0.1:19851/chunked");
        string body = await response.Content.ReadAsStringAsync();
        await StopLuaServer(engine, pump);
        response.Headers.TransferEncodingChunked.ShouldBe(true);
        body.ShouldBe("hello chunked");
    }

    [Fact]
    public async Task Server_SendChunkedStream_MultipleChunks_BodyIsReassembled()
    {
        // Large enough that the 65536-byte pump window splits it into several chunks.
        var (engine, pump) = await StartLuaServer("127.0.0.1:19852",
            MakeChunkedStream + "\nresp:Send(make_chunked_stream(string.rep('x', 200000)))");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        string body = await client.GetStringAsync("http://127.0.0.1:19852/big");
        await StopLuaServer(engine, pump);
        body.Length.ShouldBe(200000);
    }

    // AliveToken integration
    [Fact]
    public async Task Server_SetAliveToken_DisposedToken_EndsAcceptCoroutineAndReleasesPort()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local server = assert(HttpServer.Listen('127.0.0.1:19870'))
            local token = AliveToken.New()
            server:SetAliveToken(token)
            local co = server:Accept()
            coroutine.resume(co)
            local aliveBefore = coroutine.status(co) == 'suspended'
            token:Dispose()
            coroutine.resume(co)
            local deadAfter = coroutine.status(co) == 'dead'
            local again, err = HttpServer.Listen('127.0.0.1:19870')
            if not again then return 'port not released: ' .. tostring(err) end
            again:Close()
            return tostring(aliveBefore and deadAfter)
            """);
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Server_SetAliveToken_NilDetaches()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local server = assert(HttpServer.Listen('127.0.0.1:19871'))
            local token = AliveToken.New()
            server:SetAliveToken(token)
            server:SetAliveToken(nil)
            local co = server:Accept()
            coroutine.resume(co)
            token:Dispose()
            for i = 1, 3 do coroutine.resume(co) end
            local aliveAfterDispose = coroutine.status(co) == 'suspended'
            coroutine.resume(co, true)
            return tostring(aliveAfterDispose and coroutine.status(co) == 'dead')
            """);
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Server_AliveToken_LiveToken_PumpContinues()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local server = assert(HttpServer.Listen('127.0.0.1:19873'))
            local token = AliveToken.New()
            server:SetAliveToken(token)
            local co = server:Accept()
            -- resume several times with a live token — coroutine should stay alive
            for i = 1, 5 do coroutine.resume(co) end
            local alive = coroutine.status(co) == 'suspended'
            coroutine.resume(co, true)  -- stop cleanly
            server:Close()
            return tostring(alive)
            """);
        r.String.ShouldBe("true");
    }

    // WebSocket server tests
    // All WS server tests use C# ClientWebSocket against a background Lua server
    // (StartWsServer) so curl and libevent never share a single-threaded pump loop.
    [Fact]
    public async Task Server_WebSocket_Upgrade_Succeeds()
    {
        var (engine, pump) = await StartWsServer("127.0.0.1:19880", string.Empty);
        var ws = new ClientWebSocket();
        await ws.ConnectAsync(new Uri("ws://127.0.0.1:19880/"), CancellationToken.None);

        WebSocketState state = ws.State;
        ws.Dispose();
        await StopWsServer(engine, pump);
        state.ShouldBe(WebSocketState.Open);
    }

    [Fact]
    public async Task Server_WebSocket_Echo_TextFrame_RoundTrips()
    {
        var (engine, pump) = await StartWsServer("127.0.0.1:19881", """
            local msg = _server_ws:Poll()
            if msg then _server_ws:Send(msg:GetData()) end
            """);
        var (data, _) = await WsCsEchoOnce("ws://127.0.0.1:19881/", "hello server");
        await StopWsServer(engine, pump);
        data.ShouldBe("hello server");
    }

    [Fact]
    public async Task Server_WebSocket_Echo_BinaryFrame_RoundTrips()
    {
        var (engine, pump) = await StartWsServer("127.0.0.1:19882", """
            local msg = _server_ws:Poll()
            if msg then _server_ws:Send(msg:GetData(), msg:GetType() == 2) end
            """);
        var (data, type) = await WsCsEchoOnce("ws://127.0.0.1:19882/", "bindata", WebSocketMessageType.Binary);
        await StopWsServer(engine, pump);
        data.ShouldBe("bindata");
        type.ShouldBe(WebSocketMessageType.Binary);
    }

    [Fact]
    public async Task Server_WebSocket_IsConnected_TrueAfterUpgrade()
    {
        var (engine, pump) = await StartWsServer("127.0.0.1:19884", string.Empty);
        var ws = new ClientWebSocket();
        await ws.ConnectAsync(new Uri("ws://127.0.0.1:19884/"), CancellationToken.None);
        await WaitForServerWebSocketAsync(engine);
        LuaValue r = await engine.ExecuteStringAsync(
            "return tostring(_server_ws ~= nil and _server_ws:IsConnected())");
        await StopWsServer(engine, pump);
        ws.Dispose();
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Server_WebSocket_GetId_IsNonZero()
    {
        var (engine, pump) = await StartWsServer("127.0.0.1:19885", string.Empty);
        using var ws = new ClientWebSocket();
        await ws.ConnectAsync(new Uri("ws://127.0.0.1:19885/"), CancellationToken.None);
        await WaitForServerWebSocketAsync(engine);
        LuaValue r = await engine.ExecuteStringAsync("""
            if _server_ws then
                local id = _server_ws:GetId()
                return tostring(type(id) == 'number' and id ~= 0)
            end
            return 'false'
            """);
        await StopWsServer(engine, pump);
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Server_WebSocket_GetContext_ReturnsSameTable()
    {
        var (engine, pump) = await StartWsServer("127.0.0.1:19886", string.Empty);
        var ws = new ClientWebSocket();
        await ws.ConnectAsync(new Uri("ws://127.0.0.1:19886/"), CancellationToken.None);
        await WaitForServerWebSocketAsync(engine);
        LuaValue r = await engine.ExecuteStringAsync("""
            if _server_ws then
                local ctx1 = _server_ws:GetContext()
                ctx1.x = 42
                return tostring(_server_ws:GetContext().x == 42)
            end
            return 'false'
            """);
        await StopWsServer(engine, pump);
        ws.Dispose();
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Server_WebSocket_Dispose_AfterDispose_ReadReturnsNil()
    {
        var (engine, pump) = await StartWsServer("127.0.0.1:19887", string.Empty);
        var ws = new ClientWebSocket();
        await ws.ConnectAsync(new Uri("ws://127.0.0.1:19887/"), CancellationToken.None);
        await WaitForServerWebSocketAsync(engine);
        LuaValue r = await engine.ExecuteStringAsync("""
            if _server_ws then
                _server_ws:Dispose()
                return tostring(_server_ws:Read() == nil)
            end
            return 'false'
            """);

        // Server already disposed the WS; closing from client side would throw.
        await StopWsServer(engine, pump);
        ws.Dispose();
        r.String.ShouldBe("true");
    }

    // SSE tests
    [Fact]
    public async Task Server_SSE_EventsDeliveredInOrder()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19890", """
            resp:SetHeader('Content-Type', 'text/event-stream')

            resp:SetHeader('Cache-Control', 'no-cache')
            local events = { 'one', 'two', 'three' }
            local i = 0
            resp:Send(Stream.New(function(op, size)
                if op == 0 then return 1 end
                if op == 2 then
                    i = i + 1
                    if i > #events then return nil end
                    return 'data: ' .. events[i] .. '\n\n'
                end
            end))
            """);

        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };
        string body = await client.GetStringAsync("http://127.0.0.1:19890/");
        await StopLuaServer(engine, pump);

        body.ShouldContain("data: one\n\n");
        body.ShouldContain("data: two\n\n");
        body.ShouldContain("data: three\n\n");
        body.IndexOf("one").ShouldBeLessThan(body.IndexOf("two"));
        body.IndexOf("two").ShouldBeLessThan(body.IndexOf("three"));
    }

    [Fact]
    public async Task Server_SSE_ContentTypeIsEventStream()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19891", """
            resp:SetHeader('Content-Type', 'text/event-stream')
            resp:SetHeader('Cache-Control', 'no-cache')
            local done = false
            resp:Send(Stream.New(function(op, size)
                if op == 0 then return 1 end
                if op == 2 then
                    if done then return nil end
                    done = true
                    return 'data: ping\n\n'
                end
            end))
            """);

        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(5) };
        var response = await client.GetAsync("http://127.0.0.1:19891/");
        await StopLuaServer(engine, pump);

        response.Content.Headers.ContentType.ShouldNotBeNull();
        response.Content.Headers.ContentType.MediaType.ShouldBe("text/event-stream");
    }

    [Fact]
    public async Task Server_SSE_LuaClient_CanConsumeStreamingEvents()
    {
        // The server runs as a background scheduler coroutine via StartLuaServer.
        // The client stream runs as a second scheduler coroutine so both can yield
        // independently — stream:Read() yields back to the scheduler which resumes
        // the server pump, advancing the chunked sender between reads.
        var (engine, pump) = await StartLuaServer("127.0.0.1:19892", """
            resp:SetHeader('Content-Type', 'text/event-stream')
            local events = { 'alpha', 'beta' }
            local i = 0
            resp:Send(Stream.New(function(op, size)
                if op == 0 then return 1 end
                if op == 2 then
                    i = i + 1
                    if i > #events then return nil end
                    return 'data: ' .. events[i] .. '\n\n'
                end
            end))
            """);

        LuaValue r = await engine.ExecuteStringAsync("""
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            -- c:Stream() returns a coroutine; drive it until it yields the stream.
            local co = c:Stream('GET', 'http://127.0.0.1:19892/sse')
            local ok, stream = coroutine.resume(co)
            while ok and type(stream) ~= 'userdata' do
                ok, stream = coroutine.resume(co)
            end
            if not ok or not stream then error('stream connect failed') end
            local body = ''
            local chunk = stream:Read()
            while chunk do
                body = body .. chunk
                chunk = stream:Read()
            end
            stream:Close()
            return body
            """);

        await StopLuaServer(engine, pump);
        r.String!.ShouldContain("data: alpha");
        r.String!.ShouldContain("data: beta");
    }

    private static async Task<(KitsuneEngine Engine, Task PumpTask)> StartWsServer(
        string port, string perTickLua)
    {
        var engine = new KitsuneEngine();
        await engine.ExecuteStringAsync($$"""
            _stop      = false
            _server_ws = nil
            _server    = assert(HttpServer.Listen('{{port}}'))
            """);

        var pumpTask = Task.Run(() => engine.ExecuteStringAsync($$"""
            local co = _server:Accept()
            while not _stop do
                local ok, req = coroutine.resume(co)
                if req and req:IsFinished() and not _server_ws then
                    _server_ws = req:GetResponse():UpgradeToWebSocket()
                end
                if _server_ws then
                    {{perTickLua}}
                end
            end
            if _server_ws then _server_ws:Dispose() end
            coroutine.resume(co, true)
            """));

        // The listener is already bound, so a client connecting before the pump starts
        // simply waits in the accept backlog.
        return (engine, pumpTask);
    }

    private static async Task StopWsServer(KitsuneEngine engine, Task pumpTask)
    {
        await engine.ExecuteStringAsync("_stop = true");
        await pumpTask;
        engine.Dispose();
    }

    // The client's ConnectAsync can return before the server pump has stored the upgraded socket.
    private static async Task WaitForServerWebSocketAsync(KitsuneEngine engine)
    {
        DateTime deadline = DateTime.UtcNow.AddSeconds(5);
        while ((await engine.ExecuteStringAsync("return tostring(_server_ws ~= nil)")).String != "true")
        {
            if (DateTime.UtcNow > deadline)
            {
                throw new TimeoutException("The server never stored the upgraded WebSocket");
            }

            await Task.Delay(10);
        }
    }

    private static async Task<(string Data, WebSocketMessageType Type)> WsCsEchoOnce(
        string url,
        string message,
        WebSocketMessageType sendType = WebSocketMessageType.Text)
    {
        using var ws = new ClientWebSocket();
        using var cts = new CancellationTokenSource(TimeSpan.FromSeconds(5));
        await ws.ConnectAsync(new Uri(url), cts.Token);
        byte[] send = Encoding.UTF8.GetBytes(message);
        await ws.SendAsync(send, sendType, true, cts.Token);
        byte[] recv = new byte[4096];
        var result = await ws.ReceiveAsync(recv, cts.Token);
        try
        {
            await ws.CloseAsync(WebSocketCloseStatus.NormalClosure, string.Empty, CancellationToken.None);
        }
        catch (WebSocketException)
        { /* libevent drops TCP without echoing a WS CLOSE frame */
        }
        return (Encoding.UTF8.GetString(recv, 0, result.Count), result.MessageType);
    }

    private static async Task<(KitsuneEngine Engine, Task PumpTask)> StartLuaServer(
        string port, string handlerLua)
    {
        var engine = new KitsuneEngine();

        // Script 1: bind the server — completes synchronously once the port is open.
        await engine.ExecuteStringAsync($$"""
            _stop   = false
            _server = assert(HttpServer.Listen('{{port}}'))
            _server:SetOnDisconnect(function(req) end)
            _handler = function(req)
                local resp = req:GetResponse()
                {{handlerLua}}
            end
            """);

        // Script 2: pump in background, calling _handler when a request arrives.
        var pumpTask = Task.Run(() => engine.ExecuteStringAsync("""
            local co = _server:Accept()
            while not _stop do
                local ok, req = coroutine.resume(co)
                if req and req:IsFinished() then
                    _handler(req)
                end
            end
            coroutine.resume(co, true)
            """));

        return (engine, pumpTask);
    }

    private static async Task StopLuaServer(KitsuneEngine engine, Task pumpTask)
    {
        await engine.ExecuteStringAsync("_stop = true");
        await pumpTask;
        engine.Dispose();
    }
}
