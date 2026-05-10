using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests;

[Collection("KitsuneSequential")]

/// <summary>
/// Tests for the TCP module (TCP.StartListener / TCP.Connect).
/// All tests run a listener and a client inside the same Lua engine so no
/// external services are required. Each test uses a unique port to avoid
/// cross-test conflicts.
/// TCP is guarded by KITSUNE_HTTP (libevent), same as HttpServer/WebSocket.
/// </summary>
public sealed class KitsuneTcpTests
{
    // Cooperative pump loop used in tasks-based tests.
    private const string PumpHelper = @"
        local function pump(max_ms)
            max_ms = max_ms or 3000
            local deadline = os.clock() * 1000 + max_ms
            while not _done and (os.clock() * 1000) < deadline do
                Yield()
            end
            if not _done then error('tcp test timeout') end
        end
    ";

    // -------------------------------------------------------------------------
    // 1. Module is a table
    // -------------------------------------------------------------------------
    [WebSocketFact]
    public async System.Threading.Tasks.Task Tcp_Module_IsTable()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("return type(TCP)");
        r.String.ShouldBe("table");
    }

    // -------------------------------------------------------------------------
    // 2. Listener binds successfully
    // -------------------------------------------------------------------------
    [WebSocketFact]
    public async System.Threading.Tasks.Task StartListener_BindsSuccessfully()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local listener, err = TCP.StartListener(19200)
            if not listener then return 'fail: ' .. tostring(err) end
            local s = tostring(listener)
            listener:Dispose()
            return s:find('TcpListener') and 'ok' or 'bad-tostring'
        ");
        r.String.ShouldBe("ok");
    }

    // -------------------------------------------------------------------------
    // 3. Connect returns a TcpClient
    // -------------------------------------------------------------------------
    [WebSocketFact]
    public async System.Threading.Tasks.Task Connect_ReturnsClient()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local listener = assert(TCP.StartListener(19201))
            local client, err = TCP.Connect('127.0.0.1', 19201)
            if not client then return 'fail: ' .. tostring(err) end
            local s = tostring(client)
            client:Dispose()
            listener:Dispose()
            return s:find('TcpClient') and 'ok' or 'bad-tostring'
        ");
        r.String.ShouldBe("ok");
    }

    // -------------------------------------------------------------------------
    // 4. Accept returns nil, nil when no client has connected yet
    // -------------------------------------------------------------------------
    [WebSocketFact]
    public async System.Threading.Tasks.Task Accept_ReturnsNilNil_WhenNoPendingClient()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local listener = assert(TCP.StartListener(19202))
            local c, err   = listener:Accept()
            listener:Dispose()
            if c ~= nil   then return 'expected nil client' end
            if err ~= nil then return 'expected nil error, got: ' .. tostring(err) end
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    // -------------------------------------------------------------------------
    // 5. Accept returns a client after TCP.Connect + Sleep
    // -------------------------------------------------------------------------
    [WebSocketFact]
    public async System.Threading.Tasks.Task Accept_ReturnsClient_AfterConnect()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local listener = assert(TCP.StartListener(19203))
            TCP.Connect('127.0.0.1', 19203)
            Sleep(80)
            local accepted, err = listener:Accept()
            if not accepted then return 'Accept returned nil: ' .. tostring(err) end
            accepted:Dispose()
            listener:Dispose()
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    // -------------------------------------------------------------------------
    // 6. Echo round-trip
    // -------------------------------------------------------------------------
    [WebSocketFact]
    public async System.Threading.Tasks.Task EchoRoundTrip_WorksCorrectly()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(PumpHelper + @"
            _done = false

            local listener = assert(TCP.StartListener(19204))

            -- Server task: accept one client, echo everything back
            Tasks.New(function()
                local accepted
                while not accepted do
                    accepted = listener:Accept()
                    if not accepted then Sleep(10) end
                end
                while true do
                    local data, err = accepted:Poll()
                    if not data then break end
                    if data ~= '' then accepted:Send(data) end
                    Sleep(5)
                end
                accepted:Dispose()
                listener:Dispose()
            end):Dispose()

            -- Client task: connect, send, receive, verify
            Tasks.New(function()
                Sleep(40)
                local client = assert(TCP.Connect('127.0.0.1', 19204))
                for i = 1, 50 do
                    if client:IsConnected() then break end
                    Sleep(10)
                end
                client:Send('hello')
                local reply = ''
                for i = 1, 200 do
                    local data, err = client:Poll()
                    if not data then break end
                    reply = reply .. data
                    if reply == 'hello' then break end
                    Sleep(5)
                end
                client:Dispose()
                if reply ~= 'hello' then
                    error('expected hello, got: ' .. tostring(reply))
                end
                _done = true
            end):Dispose()

            pump(5000)
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    // -------------------------------------------------------------------------
    // 7. Multiple clients are accepted in order
    // -------------------------------------------------------------------------
    [WebSocketFact]
    public async System.Threading.Tasks.Task MultipleClients_AcceptedInOrder()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local listener = assert(TCP.StartListener(19205))
            local N = 3
            local clients = {}
            for i = 1, N do
                clients[i] = assert(TCP.Connect('127.0.0.1', 19205))
            end
            Sleep(100)
            local accepted = {}
            for i = 1, N do
                local c, err = listener:Accept()
                if not c then return 'Accept ' .. i .. ' returned nil: ' .. tostring(err) end
                accepted[i] = c
            end
            local extra, err = listener:Accept()
            if extra ~= nil then return 'expected nil for extra accept' end
            if err ~= nil   then return 'expected nil error for extra accept' end
            for i = 1, N do
                clients[i]:Dispose()
                accepted[i]:Dispose()
            end
            listener:Dispose()
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    // -------------------------------------------------------------------------
    // 8. Poll returns nil + error when client disconnects
    // -------------------------------------------------------------------------
    [WebSocketFact]
    public async System.Threading.Tasks.Task Poll_ReturnsNilAndError_WhenClientDisconnects()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(PumpHelper + @"
            _done = false
            local listener = assert(TCP.StartListener(19206))

            Tasks.New(function()
                local client = assert(TCP.Connect('127.0.0.1', 19206))
                Sleep(60)
                client:Dispose()
            end):Dispose()

            Tasks.New(function()
                local accepted
                while not accepted do
                    accepted = listener:Accept()
                    if not accepted then Sleep(10) end
                end
                local got_nil = false
                for i = 1, 300 do
                    local data, err = accepted:Poll()
                    if not data then
                        if err == nil then
                            error('Poll returned nil,nil after disconnect')
                        end
                        got_nil = true
                        break
                    end
                    Sleep(5)
                end
                if not got_nil then error('never saw disconnect on Poll') end
                accepted:Dispose()
                listener:Dispose()
                _done = true
            end):Dispose()

            pump(5000)
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    // -------------------------------------------------------------------------
    // 9. GetIP returns loopback; GetPort returns the listener port
    // -------------------------------------------------------------------------
    [WebSocketFact]
    public async System.Threading.Tasks.Task GetIP_GetPort_ReturnExpectedValues()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local listener = assert(TCP.StartListener(19207))
            TCP.Connect('127.0.0.1', 19207)
            Sleep(80)
            local accepted = listener:Accept()
            if not accepted then return 'no accepted client' end
            local ip   = accepted:GetIP()
            local port = accepted:GetPort()
            accepted:Dispose()
            listener:Dispose()
            if not (ip == '127.0.0.1' or ip:find('127%.')) then
                return 'unexpected IP: ' .. tostring(ip)
            end
            if type(port) ~= 'number' or port < 1 then
                return 'unexpected port: ' .. tostring(port)
            end
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    // -------------------------------------------------------------------------
    // 10. Dispose is idempotent
    // -------------------------------------------------------------------------
    [WebSocketFact]
    public async System.Threading.Tasks.Task Dispose_IsIdempotent()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local listener = assert(TCP.StartListener(19208))
            local client   = assert(TCP.Connect('127.0.0.1', 19208))
            listener:Dispose()
            listener:Dispose()
            client:Dispose()
            client:Dispose()
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    // -------------------------------------------------------------------------
    // 11. GetContext returns a persistent per-object table
    // -------------------------------------------------------------------------
    [WebSocketFact]
    public async System.Threading.Tasks.Task GetContext_ReturnsPersistentTable()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(@"
            local listener = assert(TCP.StartListener(19209))
            TCP.Connect('127.0.0.1', 19209)
            Sleep(80)
            local accepted = listener:Accept()
            if not accepted then return 'no accepted client' end

            local ctx1 = accepted:GetContext()
            ctx1.foo = 'bar'
            local ctx2 = accepted:GetContext()
            if ctx2.foo ~= 'bar' then return 'context not persistent' end
            if ctx1 ~= ctx2       then return 'context not same table' end

            local lctx = listener:GetContext()
            lctx.x = 42
            if listener:GetContext().x ~= 42 then return 'listener context not persistent' end

            accepted:Dispose()
            listener:Dispose()
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    // -------------------------------------------------------------------------
    // 12. Large payload round-trip
    // -------------------------------------------------------------------------
    [WebSocketFact]
    public async System.Threading.Tasks.Task LargePayload_EchoesCorrectly()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync(PumpHelper + @"
            _done = false
            local PAYLOAD = string.rep('x', 65536)
            local listener = assert(TCP.StartListener(19210))

            Tasks.New(function()
                local accepted
                while not accepted do
                    accepted = listener:Accept()
                    if not accepted then Sleep(5) end
                end
                local buf = ''
                while #buf < #PAYLOAD do
                    local data, err = accepted:Poll()
                    if not data then break end
                    buf = buf .. data
                    Sleep(1)
                end
                accepted:Send(buf)
                -- Keep pumping the event base so libevent can drain its write
                -- buffer to the client. Stop once the client disconnects.
                while true do
                    local data, err = accepted:Poll()
                    if not data then break end
                    Sleep(1)
                end
                accepted:Dispose()
                listener:Dispose()
            end):Dispose()

            Tasks.New(function()
                Sleep(40)
                local client = assert(TCP.Connect('127.0.0.1', 19210))
                for i = 1, 20 do
                    if client:IsConnected() then break end
                    Sleep(10)
                end
                client:Send(PAYLOAD)
                local reply = ''
                for i = 1, 1000 do
                    local data, err = client:Poll()
                    if not data then break end
                    reply = reply .. data
                    if #reply >= #PAYLOAD then break end
                    Sleep(2)
                end
                client:Dispose()
                if #reply ~= #PAYLOAD then
                    error('size mismatch: ' .. #reply .. ' vs ' .. #PAYLOAD)
                end
                _done = true
            end):Dispose()

            pump(10000)
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }
}
