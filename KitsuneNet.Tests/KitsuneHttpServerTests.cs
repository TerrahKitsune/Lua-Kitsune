using KitsuneNet;
using Shouldly;
using System;
using System.Net.Http;
using System.Threading;
using System.Threading.Tasks;
using Xunit;

namespace KitsuneNet.Tests;

[Collection("KitsuneSequential")]

/// <summary>
/// Tests for the HttpServer module.
/// Spins up a real HttpServer inside a Lua coroutine, uses HttpClient.New() to
/// send requests against it, and verifies responses. No external services required.
/// Each test uses a unique port to avoid cross-test conflicts.
/// Tests skip when Http is unavailable (needed to send requests).
/// HttpServer is always compiled in so no separate skip guard is needed.
/// </summary>
public sealed class KitsuneHttpServerTests
{
    // Drives server_co and client_co together until the client produces a result.
    // handler(req) is called when req:IsFinished() is true.
    // The client coroutine is the one returned by c:Request() directly.
    private const string RunHelper = @"
        local function run(server_co, client_co, handler, max_ms)
            max_ms = max_ms or 3000
            local deadline = os.clock() * 1000 + max_ms
            local result = nil
            while result == nil and (os.clock() * 1000) < deadline do
                local ok, req = coroutine.resume(server_co)
                if not ok then error('server: ' .. tostring(req)) end
                if req and req:IsFinished() then
                    handler(req)
                end
                if coroutine.status(client_co) ~= 'dead' then
                    local ok2, v = coroutine.resume(client_co)
                    if not ok2 then error('client: ' .. tostring(v)) end
                    if v ~= nil then result = v end
                end
            end
            coroutine.resume(server_co, true)
            if result == nil then error('timeout waiting for response') end
            return result
        end
    ";

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

    // -- C# HttpClient tests --------------------------------------------------
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

    // -- Module availability ---------------------------------------------------
    [Fact]
    public async Task HttpServer_Module_IsTable()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("return type(HttpServer)");
        r.String.ShouldBe("table");
    }

    // -- Listen ----------------------------------------------------------------
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
            local s = HttpServer.Listen('127.0.0.1:19801')
            if not s then return 'skip' end
            local str = tostring(s)
            s:Close()
            return tostring(type(str) == 'string' and #str > 0)
        ");
        if (r != "skip")
        {
            r.String.ShouldBe("true");
        }
    }

    // -- Accept idempotency ---------------------------------------------------
    [Fact]
    public async Task Accept_CalledTwice_ReturnsSameCoroutine()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local s = HttpServer.Listen('127.0.0.1:19802')
            if not s then return 'skip' end
            local co1 = s:Accept()
            local co2 = s:Accept()
            s:Close()
            return tostring(co1 == co2)
        ");
        if (r != "skip")
        {
            r.String.ShouldBe("true");
        }
    }

    // -- GET returns 200 ------------------------------------------------------
    [Fact]
    public async Task Server_GET_RespondsWith200()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            {{RunHelper}}
            local port = '127.0.0.1:19803'
            local server = assert(HttpServer.Listen(port))
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            local client_co = c:Request('GET', 'http://' .. port .. '/hello')
            local result = run(server_co, client_co, function(req)
                req:GetResponse():SetCode(200)
                req:GetResponse():Send('hello')
            end)
            return tostring(result.Code)
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("200");
    }

    // -- Response body --------------------------------------------------------
    [Fact]
    public async Task Server_GET_ResponseBodyIsReturned()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            {{RunHelper}}
            local port = '127.0.0.1:19804'
            local server = assert(HttpServer.Listen(port))
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            local client_co = c:Request('GET', 'http://' .. port .. '/body')
            local result = run(server_co, client_co, function(req)
                req:GetResponse():Send('hello world')
            end)
            return result.Contents
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("hello world");
    }

    // -- URL and method visible on server side --------------------------------
    [Fact]
    public async Task Server_Request_GetUrlAndMethod()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            {{RunHelper}}
            local port = '127.0.0.1:19805'
            local server = assert(HttpServer.Listen(port))
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            local client_co = c:Request('GET', 'http://' .. port .. '/test/path')
            local result = run(server_co, client_co, function(req)
                req:GetResponse():Send(req:GetMethod() .. ':' .. req:GetUrl())
            end)
            return result.Contents
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("GET:/test/path");
    }

    // -- POST body echoed back ------------------------------------------------
    [Fact]
    public async Task Server_POST_BodyIsReceived()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            {{RunHelper}}
            local port = '127.0.0.1:19806'
            local server = assert(HttpServer.Listen(port))
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            local client_co = c:Request('POST', 'http://' .. port .. '/echo', 'ping payload')
            local result = run(server_co, client_co, function(req)
                req:GetResponse():Send(req:GetBody())
            end)
            return result.Contents
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("ping payload");
    }

    // -- Reject returns error code --------------------------------------------
    [Fact]
    public async Task Server_Reject_Returns404()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            {{RunHelper}}
            local port = '127.0.0.1:19807'
            local server = assert(HttpServer.Listen(port))
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            local client_co = c:Request('GET', 'http://' .. port .. '/gone')
            local result = run(server_co, client_co, function(req)
                req:GetResponse():Reject(404, 'Not Found')
            end)
            return tostring(result.Code)
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("404");
    }

    // -- GetContext persists across resumes -----------------------------------
    [Fact]
    public async Task Server_GetContext_PersistsAcrossResumes()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            {{RunHelper}}
            local port = '127.0.0.1:19808'
            local server = assert(HttpServer.Listen(port))
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            local client_co = c:Request('GET', 'http://' .. port .. '/ctx')
            local result = run(server_co, client_co, function(req)
                req:GetContext().visits = (req:GetContext().visits or 0) + 1
                req:GetResponse():Send(tostring(req:GetContext().visits))
            end)
            return result.Contents
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("1");
    }

    // -- Headers visible on server side ---------------------------------------
    [Fact]
    public async Task Server_GetHeaders_ContainsCustomHeader()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            {{RunHelper}}
            local port = '127.0.0.1:19809'
            local server = assert(HttpServer.Listen(port))
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            c:SetDefaultHeader('X-Kitsune', 'testvalue')
            local client_co = c:Request('GET', 'http://' .. port .. '/headers')
            local result = run(server_co, client_co, function(req)
                req:GetResponse():Send(tostring(req:GetHeaders()['x-kitsune']))
            end)
            return result.Contents
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("testvalue");
    }

    // -- Send(Stream) ---------------------------------------------------------
    [Fact]
    public async Task Server_SendStream_BodyIsReceived()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            {{RunHelper}}
            local port = '127.0.0.1:19810'
            local server = assert(HttpServer.Listen(port))
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            local client_co = c:Request('GET', 'http://' .. port .. '/stream')
            local result = run(server_co, client_co, function(req)
                local stream = Stream.New('stream body content')
                req:GetResponse():SetHeader('Content-Type', 'text/plain')
                req:GetResponse():Send(stream)
            end)
            return result.Contents
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("stream body content");
    }

    // -- Double-send raises error ---------------------------------------------
    [Fact]
    public async Task Response_SendTwice_RaisesError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            {{RunHelper}}
            local port = '127.0.0.1:19811'
            local server = assert(HttpServer.Listen(port))
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            local client_co = c:Request('GET', 'http://' .. port .. '/dbl')
            local double_send_errored = false
            local result = run(server_co, client_co, function(req)
                local resp = req:GetResponse()
                resp:Send('first')
                local ok, err = pcall(function() resp:Send('second') end)
                double_send_errored = not ok
            end)
            return tostring(double_send_errored)
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("true");
    }

    // -- SetOnDisconnect called after response --------------------------------
    [Fact]
    public async Task Server_SetOnDisconnect_CalledAfterResponse()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            {{RunHelper}}
            local port = '127.0.0.1:19812'
            local server = assert(HttpServer.Listen(port))
            local disconnected = false
            server:SetOnDisconnect(function(req)
                disconnected = true
            end)
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            local client_co = c:Request('GET', 'http://' .. port .. '/disc')
            local result = run(server_co, client_co, function(req)
                req:GetResponse():Send('bye')
            end)
            -- The client has received the response and closed its end.
            -- Pump the server until MG_EV_CLOSE fires (client disconnect).
            local deadline = os.clock() * 1000 + 1000
            while not disconnected and (os.clock() * 1000) < deadline do
                coroutine.resume(server_co)
            end
            coroutine.resume(server_co, true)
            return tostring(result.Code == 200 and disconnected)
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("true");
    }

    // -- Teardown tests -------------------------------------------------------
    [Fact]
    public async Task Server_Close_IsIdempotent()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local s = assert(HttpServer.Listen('127.0.0.1:19820'))
            s:Close()
            s:Close()
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
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    [Fact]
    public async Task Response_AfterRequestGC_RaisesError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            {{RunHelper}}
            local port = '127.0.0.1:19824'
            local server = assert(HttpServer.Listen(port))
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            local client_co = c:Request('GET', 'http://' .. port .. '/stale')
            local saved_resp = nil
            local result = run(server_co, client_co, function(req)
                saved_resp = req:GetResponse()
                req:GetResponse():Send('ok')
            end)
            collectgarbage('collect')
            collectgarbage('collect')
            local ok, err = pcall(function() saved_resp:Send('again') end)
            return tostring(not ok)
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Server_QueuedRequestsCleanedUp_OnStopFlag()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            local port = '127.0.0.1:19825'
            local server = assert(HttpServer.Listen(port))
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            local client_co = c:Request('GET', 'http://' .. port .. '/drop')
            local deadline = os.clock() * 1000 + 1000
            while (os.clock() * 1000) < deadline do
                coroutine.resume(server_co)
            end
            coroutine.resume(server_co, true)
            collectgarbage('collect')
            return 'ok'
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("ok");
    }

    [Fact]
    public async Task CSharpClient_GET_DefaultStatusIs200()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19830", "resp:Send('hello')");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        var response = await client.GetAsync("http://127.0.0.1:19830/");
        await StopLuaServer(engine, pump);
        ((int)response.StatusCode).ShouldBe(200);
    }

    [Fact]
    public async Task CSharpClient_GET_DefaultBodyIsReturned()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19831", "resp:Send('kitsune')");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        string body = await client.GetStringAsync("http://127.0.0.1:19831/");
        await StopLuaServer(engine, pump);
        body.ShouldBe("kitsune");
    }

    [Fact]
    public async Task CSharpClient_GET_DefaultContentTypeIsNonEmpty()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19832", "resp:Send('ok')");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        var response = await client.GetAsync("http://127.0.0.1:19832/");
        await StopLuaServer(engine, pump);
        response.Content.Headers.ContentType?.MediaType.ShouldNotBeNullOrEmpty();
    }

    [Fact]
    public async Task CSharpClient_RequestHeader_IsReceivedByServer()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19833",
            "resp:Send(tostring(req:GetHeaders()['x-test-header']))");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        var request = new HttpRequestMessage(HttpMethod.Get, "http://127.0.0.1:19833/");
        request.Headers.Add("X-Test-Header", "csharp-value");
        var response = await client.SendAsync(request);
        string body = await response.Content.ReadAsStringAsync();
        await StopLuaServer(engine, pump);
        body.ShouldBe("csharp-value");
    }

    [Fact]
    public async Task CSharpClient_POST_BodyIsReceivedByServer()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19834", "resp:Send(req:GetBody())");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        var response = await client.PostAsync("http://127.0.0.1:19834/",
            new StringContent("hello from csharp"));
        string body = await response.Content.ReadAsStringAsync();
        await StopLuaServer(engine, pump);
        body.ShouldBe("hello from csharp");
    }

    [Fact]
    public async Task CSharpClient_SetCode_404_IsReceivedByClient()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19835",
            "resp:SetCode(404); resp:Send('not found')");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        var response = await client.GetAsync("http://127.0.0.1:19835/");
        await StopLuaServer(engine, pump);
        ((int)response.StatusCode).ShouldBe(404);
    }

    [Fact]
    public async Task CSharpClient_SetHeader_ContentType_IsReceivedByClient()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19836",
            """resp:SetHeader('Content-Type', 'application/json'); resp:Send('{"ok":true}')""");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        var response = await client.GetAsync("http://127.0.0.1:19836/");
        await StopLuaServer(engine, pump);
        response.Content.Headers.ContentType?.MediaType.ShouldBe("application/json");
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
    public async Task CSharpClient_GetMethod_ReturnsGET()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19838", "resp:Send(req:GetMethod())");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        string body = await client.GetStringAsync("http://127.0.0.1:19838/");
        await StopLuaServer(engine, pump);
        body.ShouldBe("GET");
    }

    [Fact]
    public async Task CSharpClient_GetIp_IsNonEmpty()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19839", "resp:Send(req:GetIp())");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        string body = await client.GetStringAsync("http://127.0.0.1:19839/");
        await StopLuaServer(engine, pump);
        body.ShouldNotBeNullOrEmpty();
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

    [Fact]
    public async Task CSharpClient_Reject_Returns503()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19841",
            "resp:Reject(503, 'Service Unavailable')");
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        var response = await client.GetAsync("http://127.0.0.1:19841/");
        await StopLuaServer(engine, pump);
        ((int)response.StatusCode).ShouldBe(503);
    }

    // -- Chunked stream tests (Lua client) ------------------------------------
    [Fact]
    public async Task Server_SendChunkedStream_BodyIsReceived()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            {{RunHelper}}
            {{MakeChunkedStream}}
            local port = '127.0.0.1:19850'
            local server = assert(HttpServer.Listen(port))
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            local client_co = c:Request('GET', 'http://' .. port .. '/chunked')
            local result = run(server_co, client_co, function(req)
                local stream = make_chunked_stream('chunked body content')
                req:GetResponse():Send(stream)
            end)
            return result.Contents
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("chunked body content");
    }

    [Fact]
    public async Task Server_SendChunkedStream_TransferEncodingIsChunked()
    {
        // The Lua HttpClient (libcurl) transparently decodes chunked transfer
        // encoding and does not expose Transfer-Encoding in result.Headers.
        // Verify chunked delivery via the C# client instead — this test just
        // confirms the body arrives intact when the stream has no CAP_SEEK.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            {{RunHelper}}
            {{MakeChunkedStream}}
            local port = '127.0.0.1:19851'
            local server = assert(HttpServer.Listen(port))
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            local client_co = c:Request('GET', 'http://' .. port .. '/chunked')
            local result = run(server_co, client_co, function(req)
                local stream = make_chunked_stream('hello chunked')
                req:GetResponse():Send(stream)
            end)
            return result.Contents
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("hello chunked");
    }

    [Fact]
    public async Task Server_SendChunkedStream_MultipleChunks_BodyIsReassembled()
    {
        // Build a large body so the 65536-byte pump window definitely splits it
        // into multiple chunks.
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($$"""
            if HttpClient == nil then return 'skip' end
            {{RunHelper}}
            {{MakeChunkedStream}}
            local port = '127.0.0.1:19852'
            local server = assert(HttpServer.Listen(port))
            local server_co = server:Accept()
            local c = HttpClient.New()
            c:SetVerifySSL(false)
            local client_co = c:Request('GET', 'http://' .. port .. '/big')
            local big = string.rep('x', 200000)
            local result = run(server_co, client_co, function(req)
                local stream = make_chunked_stream(big)
                req:GetResponse():Send(stream)
            end)
            return tostring(#result.Contents == 200000)
        """);
        if (r == "skip")
        {
            return;
        }

        r.String.ShouldBe("true");
    }

    // -- Chunked stream tests (C# client) ------------------------------------
    [Fact]
    public async Task CSharpClient_SendChunkedStream_BodyIsReceived()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19853", """
            local function make_chunked_stream(data)
                local pos = 0
                return Stream.New(function(op, arg)
                    if op == 0 then return 1
                    elseif op == 2 then
                        local chunk = data:sub(pos + 1, pos + arg)
                        pos = pos + #chunk
                        return chunk
                    end
                end)
            end
            resp:Send(make_chunked_stream('chunked from csharp'))
            """);
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        string body = await client.GetStringAsync("http://127.0.0.1:19853/");
        await StopLuaServer(engine, pump);
        body.ShouldBe("chunked from csharp");
    }

    [Fact]
    public async Task CSharpClient_SendChunkedStream_TransferEncodingIsChunked()
    {
        var (engine, pump) = await StartLuaServer("127.0.0.1:19854", """
            local function make_chunked_stream(data)
                local pos = 0
                return Stream.New(function(op, arg)
                    if op == 0 then return 1
                    elseif op == 2 then
                        local chunk = data:sub(pos + 1, pos + arg)
                        pos = pos + #chunk
                        return chunk
                    end
                end)
            end
            resp:Send(make_chunked_stream('hello'))
            """);
        using var client = new HttpClient { Timeout = TimeSpan.FromSeconds(4) };
        var response = await client.GetAsync("http://127.0.0.1:19854/");
        await StopLuaServer(engine, pump);

        // System.Net.Http transparently decodes chunked; verify the header was present.
        response.Headers.TransferEncodingChunked.ShouldBe(true);
    }

    // -- AliveToken integration -----------------------------------------------
    [Fact]
    public async Task Server_SetAliveToken_DoesNotRaise()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local server = assert(HttpServer.Listen('0.0.0.0:19870'))
            local token = AliveToken.New()
            server:SetAliveToken(token)
            server:Close()
            return 'ok'
            """);
        r.String.ShouldBe("ok");
    }

    [Fact]
    public async Task Server_SetAliveToken_NilDetaches()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local server = assert(HttpServer.Listen('0.0.0.0:19871'))
            local token = AliveToken.New()
            server:SetAliveToken(token)
            server:SetAliveToken(nil)
            server:Close()
            return 'ok'
            """);
        r.String.ShouldBe("ok");
    }

    [Fact]
    public async Task Server_AliveToken_DisposedToken_StopsAcceptLoop()
    {
        using KitsuneEngine engine = new();

        // Start the server, run one pump iteration to prime it, dispose the token,
        // then resume — the coroutine should die without hanging.
        LuaValue r = await engine.ExecuteStringAsync("""
            local server = assert(HttpServer.Listen('0.0.0.0:19872'))
            local token = AliveToken.New()
            server:SetAliveToken(token)
            local co = server:Accept()
            coroutine.resume(co)        -- prime the pump
            token:Dispose()
            coroutine.resume(co)        -- should trigger teardown
            return tostring(coroutine.status(co) == 'dead')
            """);
        r.String.ShouldBe("true");
    }

    [Fact]
    public async Task Server_AliveToken_LiveToken_PumpContinues()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local server = assert(HttpServer.Listen('0.0.0.0:19873'))
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
