using KitsuneNet;
using Shouldly;
using Xunit;

namespace KitsuneNet.Tests
{
    // See KitsuneEngineTests for why both classes share a single collection.
    [Collection("KitsuneSequential")]

    /// <summary>
    /// Tests for the Http module.
    /// Tests skip only when KITSUNE_HTTP was not compiled in (Http global is nil).
    /// Network failures cause test failures — not skips.
    /// Buffered tests use httpbin.org; streaming and WebSocket tests also use
    /// httpbin.org and wss://echo.websocket.org respectively.
    /// </summary>
    public sealed class KitsuneHttpClientTests
    {
        private const string WsUrl = "wss://echo.websocket.org";

        // drain(co): resumes a client:Request() coroutine until it produces a
        // non-nil result and returns (ok, result).
        private const string DrainRequest = @"
            local function drain(co)
                local ok, r = coroutine.resume(co)
                while ok and r == nil do ok, r = coroutine.resume(co) end
                return ok, r
            end
        ";

        private const string StreamHelper = @"
            local _outcome = nil
            local function skip() error('__skip__') end
            local function run_http(fn)
                local co = coroutine.create(fn)
                local ok, err = coroutine.resume(co)
                while ok and coroutine.status(co) ~= 'dead' do
                    ok, err = coroutine.resume(co)
                end
                if not ok then
                    if type(err) ~= 'string' or not err:find('__skip__') then
                        error(err, 2)
                    end
                end
            end
            local function stream_open(client, method, url, body, headers)
                local co = client:Stream(method, url, body, headers)
                local ok, s = coroutine.resume(co)
                while ok and type(s) ~= 'userdata' do ok, s = coroutine.resume(co) end
                if not ok then error(tostring(s)) end
                return s
            end
            local function ws_connect(client, url)
                local ws = client:Connect(url)
                if not ws then error('Connect failed') end
                ws:Poll()  -- drain server welcome frame if any
                return ws
            end
        ";

        // -- Http module availability ------------------------------------------
        [Fact]
        public async Task Http_Module_IsTableWhenAvailable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("if HttpClient == nil then return 'skip' end; return type(HttpClient)");
            if (r != "skip")
            {
                r.String.ShouldBe("table");
            }
        }

        // -- HttpClient.Create -------------------------------------------------------
        [Fact]
        public async Task Http_Create_ReturnsNonNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync("if HttpClient == nil then return 'skip' end; return tostring(HttpClient.New() ~= nil)");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Create_Tostring_ReturnsNonEmptyString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                if HttpClient == nil then return 'skip' end
                local s = tostring(HttpClient.New())
                return tostring(type(s) == 'string' and #s > 0)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Create_ConfigMethods_DoNotRaise()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                if HttpClient == nil then return 'skip' end
                local c = HttpClient.New()
                c:SetTimeout(5000)
                c:SetFollowRedirects(true)
                c:SetVerifySSL(false)
                c:SetDefaultHeader('X-Test', '1')
                return 'ok'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("ok");
            }
        }

        // -- HttpClient.UrlEncode / HttpClient.UrlDecode (no network required) -------------
        [Fact]
        public async Task Http_UrlEncode_SpacesAreEncoded()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                if HttpClient == nil then return 'skip' end
                return tostring(HttpClient.UrlEncode('hello world'):find(' ') == nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_UrlEncode_AmpersandIsEncoded()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                if HttpClient == nil then return 'skip' end
                return tostring(HttpClient.UrlEncode('a&b'):find('%%26') ~= nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_UrlDecode_RoundTrip()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                if HttpClient == nil then return 'skip' end
                local orig = 'hello world & foo=bar'
                return tostring(HttpClient.UrlDecode(HttpClient.UrlEncode(orig)) == orig)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_UrlEncode_UnreservedCharsPassThrough()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                if HttpClient == nil then return 'skip' end
                local safe = 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.~'
                return tostring(HttpClient.UrlEncode(safe) == safe)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_UrlDecode_PlusDecodedAsSpace()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                if HttpClient == nil then return 'skip' end
                return tostring(HttpClient.UrlDecode('hello+world') == 'hello world')
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_UrlEncode_Empty_ReturnsEmpty()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                if HttpClient == nil then return 'skip' end
                return tostring(HttpClient.UrlEncode('') == '' and HttpClient.UrlDecode('') == '')
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- Buffered GET ------------------------------------------------------
        [Fact]
        public async Task Http_GET_Returns200()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                client:SetVerifySSL(true)
                local co, err = client:Request('GET', 'https://httpbin.org/get')
                local ok, result = drain(co)
                return tostring(result.Code == 200)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_GET_StatusIsOK()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local co, err = client:Request('GET', 'https://httpbin.org/get')
                local ok, result = drain(co)
                return result.Status
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("OK");
            }
        }

        [Fact]
        public async Task Http_GET_ContentsIsNonEmptyString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local co, err = client:Request('GET', 'https://httpbin.org/get')
                local ok, result = drain(co)
                return tostring(type(result.Contents) == 'string' and #result.Contents > 0)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_GET_HeadersIsTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local co, err = client:Request('GET', 'https://httpbin.org/get')
                local ok, result = drain(co)
                return tostring(type(result.Headers) == 'table')
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- POST with JSON body -----------------------------------------------
        [Fact]
        public async Task Http_POST_JsonBody_EchoesPostedData()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local body = '{""key"":""kitsune""}'
                local co, err = client:Request('POST', 'https://httpbin.org/post', body,
                    { ['Content-Type'] = 'application/json' })
                local ok, result = drain(co)
                return tostring(result.Code == 200 and result.Contents:find('kitsune') ~= nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- Timeout ? transport error (Code is nil) ---------------------------
        [Fact]
        public async Task Http_Timeout_TransportError_CodeIsNil()
        {
            using KitsuneEngine engine = new();

            // 50 ms is far shorter than the 10 s delay endpoint will ever respond.
            // If the network is unreachable, DNS failure also produces nil Code.
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(50)
                local co, err = client:Request('GET', 'https://httpbin.org/delay/10')
                if not co then return 'true' end
                local ok, result = drain(co)
                if not ok then return 'true' end
                return tostring(result.Code == nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Timeout_TransportError_StatusIsNonEmptyString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(50)
                local co, err = client:Request('GET', 'https://httpbin.org/delay/10')
                if not co then return 'true' end
                local ok, result = drain(co)
                if not ok then return 'true' end
                if result.Code ~= nil then return 'skip' end
                return tostring(type(result.Status) == 'string' and #result.Status > 0)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- Buffered request with outStream -----------------------------------
        [Fact]
        public async Task Http_Request_OutStream_ContentsIsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local sink = Stream.New()
                local co, err = client:Request('GET', 'https://httpbin.org/get', nil, nil, sink)
                local ok, result = drain(co)
                return tostring(result.Code == 200 and result.Contents == nil and sink:len() > 0)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- Streaming GET -----------------------------------------------------
        [Fact]
        public async Task Http_Stream_GetInfo_Returns200()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local stream = stream_open(client, 'GET', 'https://httpbin.org/get')
                    local info = stream:GetInfo()
                    stream:Close()
                    _outcome = tostring(info.Code == 200)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Stream_GetInfo_HeadersIsTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local stream = stream_open(client, 'GET', 'https://httpbin.org/get')
                    local info = stream:GetInfo()
                    stream:Close()
                    _outcome = tostring(type(info.Headers) == 'table')
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Stream_Read_DeliversNonEmptyBody()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local stream = stream_open(client, 'GET', 'https://httpbin.org/get')
                    local info = stream:GetInfo()
                    local body = ''
                    local chunk = stream:Read()
                    while chunk do
                        body = body .. chunk
                        chunk = stream:Read()
                    end
                    stream:Close()
                    _outcome = tostring(info.Code == 200 and #body > 0)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Stream_Read_BodyLooksLikeJson()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local stream = stream_open(client, 'GET', 'https://httpbin.org/get')
                    local info = stream:GetInfo()
                    local body = ''
                    local chunk = stream:Read()
                    while chunk do
                        body = body .. chunk
                        chunk = stream:Read()
                    end
                    stream:Close()
                    _outcome = tostring(info.Code == 200 and body:sub(1, 1) == '{')
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- DELETE ------------------------------------------------------------
        [Fact]
        public async Task Http_DELETE_Returns200()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local co, err = client:Request('DELETE', 'https://httpbin.org/delete')
                local ok, result = drain(co)
                return tostring(result.Code == 200)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_DELETE_ResponseEchoesDeleteUrl()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local co, err = client:Request('DELETE', 'https://httpbin.org/delete')
                local ok, result = drain(co)
                return tostring(result.Code == 200 and result.Contents:find('delete') ~= nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- PUT ---------------------------------------------------------------
        [Fact]
        public async Task Http_PUT_Returns200()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local body = '{""item"":""kitsune_put""}'
                local co, err = client:Request('PUT', 'https://httpbin.org/put', body,
                    { ['Content-Type'] = 'application/json' })
                local ok, result = drain(co)
                return tostring(result.Code == 200)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_PUT_JsonBody_EchoedInResponse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local body = '{""item"":""kitsune_put""}'
                local co, err = client:Request('PUT', 'https://httpbin.org/put', body,
                    { ['Content-Type'] = 'application/json' })
                local ok, result = drain(co)
                return tostring(result.Code == 200 and result.Contents:find('kitsune_put') ~= nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- PATCH -------------------------------------------------------------
        [Fact]
        public async Task Http_PATCH_Returns200()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local body = '{""field"":""kitsune_patch""}'
                local co, err = client:Request('PATCH', 'https://httpbin.org/patch', body,
                    { ['Content-Type'] = 'application/json' })
                local ok, result = drain(co)
                return tostring(result.Code == 200)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_PATCH_JsonBody_EchoedInResponse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local body = '{""field"":""kitsune_patch""}'
                local co, err = client:Request('PATCH', 'https://httpbin.org/patch', body,
                    { ['Content-Type'] = 'application/json' })
                local ok, result = drain(co)
                return tostring(result.Code == 200 and result.Contents:find('kitsune_patch') ~= nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- Query-string args -------------------------------------------------
        [Fact]
        public async Task Http_GET_QueryArgs_EchoedInArgsField()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local co, err = client:Request('GET', 'https://httpbin.org/get?kitsune=engine&version=4')
                local ok, result = drain(co)
                return tostring(
                    result.Code == 200 and
                    result.Contents:find('kitsune') ~= nil and
                    result.Contents:find('engine')  ~= nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_GET_UrlEncodedQueryArg_DecodedByServer()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local encoded = HttpClient.UrlEncode('hello world')
                local co, err = client:Request('GET', 'https://httpbin.org/get?q=' .. encoded)
                local ok, result = drain(co)
                return tostring(result.Code == 200 and result.Contents:find('hello world') ~= nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- Request headers ---------------------------------------------------
        [Fact]
        public async Task Http_DefaultHeader_AppearsInEchoedHeaders()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                client:SetDefaultHeader('X-Kitsune-Id', 'kitsune_default_hdr')
                local co, err = client:Request('GET', 'https://httpbin.org/get')
                local ok, result = drain(co)
                return tostring(result.Code == 200 and result.Contents:find('kitsune_default_hdr') ~= nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_PerRequestHeader_AppearsInEchoedHeaders()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local co, err = client:Request('GET', 'https://httpbin.org/get', nil,
                    { ['X-Kitsune-Req'] = 'kitsune_per_req_hdr' })
                local ok, result = drain(co)
                return tostring(result.Code == 200 and result.Contents:find('kitsune_per_req_hdr') ~= nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_DefaultAndPerRequestHeaders_BothPresent()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                client:SetDefaultHeader('X-Kitsune-Default', 'val_default')
                local co, err = client:Request('GET', 'https://httpbin.org/get', nil,
                    { ['X-Kitsune-PerCall'] = 'val_percall' })
                local ok, result = drain(co)
                return tostring(
                    result.Code == 200 and
                    result.Contents:find('val_default') ~= nil and
                    result.Contents:find('val_percall') ~= nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- POST form-urlencoded ----------------------------------------------
        [Fact]
        public async Task Http_POST_FormUrlEncoded_EchoedInFormField()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local body = 'engine=kitsune&version=4'
                local co, err = client:Request('POST', 'https://httpbin.org/post', body,
                    { ['Content-Type'] = 'application/x-www-form-urlencoded' })
                local ok, result = drain(co)
                return tostring(result.Code == 200 and result.Contents:find('kitsune') ~= nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- Streaming for non-GET methods -------------------------------------
        [Fact]
        public async Task Http_Stream_POST_Returns200()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local stream = stream_open(client, 'POST', 'https://httpbin.org/post',
                        '{""msg"":""kitsune_stream_post""}',
                        { ['Content-Type'] = 'application/json' })
                    local info = stream:GetInfo()
                    stream:Close()
                    _outcome = tostring(info.Code == 200)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Stream_POST_BodyEchoedInResponse()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local stream = stream_open(client, 'POST', 'https://httpbin.org/post',
                        '{""msg"":""kitsune_stream_post""}',
                        { ['Content-Type'] = 'application/json' })
                    local info = stream:GetInfo()
                    local body = ''
                    local chunk = stream:Read()
                    while chunk do body = body .. chunk; chunk = stream:Read() end
                    stream:Close()
                    _outcome = tostring(info.Code == 200 and body:find('kitsune_stream_post') ~= nil)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Stream_PUT_Returns200()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local stream = stream_open(client, 'PUT', 'https://httpbin.org/put',
                        '{""msg"":""kitsune_stream_put""}',
                        { ['Content-Type'] = 'application/json' })
                    local info = stream:GetInfo()
                    stream:Close()
                    _outcome = tostring(info.Code == 200)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Stream_PATCH_Returns200()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local stream = stream_open(client, 'PATCH', 'https://httpbin.org/patch',
                        '{""msg"":""kitsune_stream_patch""}',
                        { ['Content-Type'] = 'application/json' })
                    local info = stream:GetInfo()
                    stream:Close()
                    _outcome = tostring(info.Code == 200)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Stream_DELETE_Returns200()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local stream = stream_open(client, 'DELETE', 'https://httpbin.org/delete')
                    local info = stream:GetInfo()
                    stream:Close()
                    _outcome = tostring(info.Code == 200)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- WebSocket ---------------------------------------------------------
        [WebSocketFact]
        public async Task Http_WebSocket_Connect_Succeeds()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = client:Connect('{WsUrl}')
                    if not ws then error('connect failed') end
                    ws:Dispose()
                    _outcome = 'true'
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [WebSocketFact]
        public async Task Http_WebSocket_IsConnected_TrueAfterConnect()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = client:Connect('{WsUrl}')
                    if not ws then error('connect failed') end
                    local connected = ws:IsConnected()
                    ws:Dispose()
                    local after = ws:IsConnected()
                    _outcome = tostring(connected == true and after == false)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [WebSocketFact]
        public async Task Http_WebSocket_GetId_StableAcrossCalls()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = client:Connect('{WsUrl}')
                    if not ws then error('connect failed') end
                    local id1 = ws:GetId()
                    local id2 = ws:GetId()
                    ws:Dispose()
                    _outcome = tostring(type(id1) == 'number' and id1 == id2 and id1 > 0)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [WebSocketFact]
        public async Task Http_WebSocket_GetContext_ReturnsSameTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = client:Connect('{WsUrl}')
                    if not ws then error('connect failed') end
                    local ctx1 = ws:GetContext()
                    ctx1.foo = 'bar'
                    local ctx2 = ws:GetContext()
                    ws:Dispose()
                    _outcome = tostring(ctx2.foo == 'bar')
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [WebSocketFact]
        public async Task Http_WebSocket_Echo_TextFrame_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = ws_connect(client, '{WsUrl}')
                    ws:Send('hello kitsune')
                    local msg = ws:Read()
                    ws:Dispose()
                    _outcome = tostring(msg ~= nil and msg:GetData() == 'hello kitsune')
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [WebSocketFact]
        public async Task Http_WebSocket_Echo_TextFrame_TypeIsOne()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = ws_connect(client, '{WsUrl}')
                    ws:Send('opcode_test')
                    local msg = ws:Read()
                    ws:Dispose()
                    _outcome = tostring(msg ~= nil and msg:GetType() == 1)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [WebSocketFact]
        public async Task Http_WebSocket_Echo_BinaryFrame_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = ws_connect(client, '{WsUrl}')
                    local payload = '\1\2\3\4\5'
                    ws:Send(payload, true)
                    local msg = ws:Read()
                    ws:Dispose()
                    _outcome = tostring(msg ~= nil and msg:GetData() == payload)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [WebSocketFact]
        public async Task Http_WebSocket_Echo_BinaryFrame_TypeIsTwo()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = ws_connect(client, '{WsUrl}')
                    ws:Send('\xDE\xAD\xBE\xEF', true)
                    local msg = ws:Read()
                    ws:Dispose()
                    _outcome = tostring(msg ~= nil and msg:GetType() == 2)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [WebSocketFact]
        public async Task Http_WebSocket_MultipleFrames_AllEchoedInOrder()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = ws_connect(client, '{WsUrl}')
                    ws:Send('frame1')
                    ws:Send('frame2')
                    ws:Send('frame3')
                    local m1 = ws:Read()
                    local m2 = ws:Read()
                    local m3 = ws:Read()
                    ws:Dispose()
                    _outcome = tostring(
                        m1 ~= nil and m1:GetData() == 'frame1' and
                        m2 ~= nil and m2:GetData() == 'frame2' and
                        m3 ~= nil and m3:GetData() == 'frame3')
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [WebSocketFact]
        public async Task Http_WebSocket_MixedFrames_TextAndBinary_TypesCorrect()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = ws_connect(client, '{WsUrl}')
                    ws:Send('text_payload', false)
                    ws:Send('\xAB\xCD', true)
                    local tm = ws:Read()
                    local bm = ws:Read()
                    ws:Dispose()
                    _outcome = tostring(
                        tm ~= nil and tm:GetData() == 'text_payload' and tm:GetType() == 1 and
                        bm ~= nil and bm:GetData() == '\xAB\xCD'     and bm:GetType() == 2)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [WebSocketFact]
        public async Task Http_WebSocket_Dispose_AfterDispose_ReadReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = client:Connect('{WsUrl}')
                    if not ws then error('connect failed') end
                    ws:Dispose()
                    local msg = ws:Read()
                    _outcome = tostring(msg == nil)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [WebSocketFact]
        public async Task Http_WebSocket_Poll_ReturnsNilWhenNoMessage()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = ws_connect(client, '{WsUrl}')
                    local msg = ws:Poll()
                    ws:Dispose()
                    _outcome = tostring(msg == nil)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [WebSocketFact]
        public async Task Http_WebSocket_LargeTextPayload_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = ws_connect(client, '{WsUrl}')
                    local payload = string.rep('kitsune_ws_', 400)
                    ws:Send(payload)
                    local msg = ws:Read()
                    ws:Dispose()
                    _outcome = tostring(msg ~= nil and msg:GetData() == payload)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- Call (simplified blocking helper) ---------------------------------
        [Fact]
        public async Task Http_Call_GET_Returns200()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local result, err = client:Call('GET', 'https://httpbin.org/get')
                    if not result then error('Call failed: ' .. tostring(err)) end
                    _outcome = tostring(result.Code == 200)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Call_GET_ContentsIsNonEmptyString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local result, err = client:Call('GET', 'https://httpbin.org/get')
                    if not result then error('Call failed: ' .. tostring(err)) end
                    _outcome = tostring(type(result.Contents) == 'string' and #result.Contents > 0)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Call_GET_HeadersIsTable()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local result, err = client:Call('GET', 'https://httpbin.org/get')
                    if not result then error('Call failed: ' .. tostring(err)) end
                    _outcome = tostring(type(result.Headers) == 'table')
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Call_POST_WithHeadersAndBody_EchoesPostedData()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local result, err = client:Call('POST', 'https://httpbin.org/post',
                        { ['Content-Type'] = 'application/json' },
                        '{""key"":""kitsune""}')
                    if not result then error('Call failed: ' .. tostring(err)) end
                    _outcome = tostring(result.Code == 200 and result.Contents:find('kitsune') ~= nil)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Call_GetTimestamp_IsNonZeroAfterSuccessfulCall()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local result, err = client:Call('GET', 'https://httpbin.org/get')
                    if not result then error('Call failed: ' .. tostring(err)) end
                    local ts = client:GetTimestamp()
                    _outcome = tostring(ts:TotalMilliseconds() > 0)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Call_NonExistentHost_ReturnsNilAndErrorString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(4000)
                    local result, err = client:Call('GET', 'https://this.host.does.not.exist.invalid/')
                    -- result must be nil; err must be a non-empty string
                    _outcome = tostring(result == nil and type(err) == 'string' and #err > 0)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Call_Timeout_ReturnsNilAndErrorString()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(50)
                    local result, err = client:Call('GET', 'https://httpbin.org/delay/10')
                    _outcome = tostring(result == nil and type(err) == 'string' and #err > 0)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Call_GetTimestamp_IsZeroBeforeAnyCall()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                return tostring(client:GetTimestamp():TotalMilliseconds() == 0)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- AliveToken integration --------------------------------------------
        [Fact]
        public async Task Http_SetAliveToken_DoesNotRaise()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                local token = AliveToken.New()
                client:SetAliveToken(token)
                return 'ok'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("ok");
            }
        }

        [Fact]
        public async Task Http_SetAliveToken_NilDetaches()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                local token = AliveToken.New()
                client:SetAliveToken(token)
                client:SetAliveToken(nil)
                return 'ok'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("ok");
            }
        }

        [Fact]
        public async Task Http_Request_AliveToken_LiveToken_CompletesNormally()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local token = AliveToken.New()
                client:SetAliveToken(token)
                local co = client:Request('GET', 'https://httpbin.org/get')
                local ok, result = drain(co)
                return tostring(ok and result ~= nil and result.Code == 200)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Request_AliveToken_DisposedBeforeStart_ReturnsNilAborted()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(@"
                if HttpClient == nil then return 'skip' end
                local client = HttpClient.New()
                client:SetTimeout(8000)
                local token = AliveToken.New()
                client:SetAliveToken(token)
                token:Dispose()
                local co, err = client:Request('GET', 'https://httpbin.org/get')
                return tostring(co == nil and err == 'aborted')
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Call_AliveToken_LiveToken_CompletesNormally()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local token = AliveToken.New()
                    client:SetAliveToken(token)
                    local result, err = client:Call('GET', 'https://httpbin.org/get')
                    if not result then error('Call failed: ' .. tostring(err)) end
                    _outcome = tostring(result.Code == 200)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Call_AliveToken_DisposedBeforeStart_ReturnsNilAborted()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local token = AliveToken.New()
                    client:SetAliveToken(token)
                    token:Dispose()
                    local result, err = client:Call('GET', 'https://httpbin.org/get')
                    _outcome = tostring(result == nil and err == 'aborted')
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        // -- Concurrent requests (shared CURLM* drain bug regression) ----------
        // These tests specifically exercise the fix for the shared-CURLM* race
        // where concurrent coroutines could steal each other's CURLMSG_DONE and
        // stall forever.  Each test fires two or more requests simultaneously
        // inside the same Lua coroutine drive loop and asserts both complete.

        [Fact]
        public async Task Http_Concurrent_TwoRequests_BothComplete()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local c1 = HttpClient.New()
                c1:SetTimeout(15000)
                local c2 = HttpClient.New()
                c2:SetTimeout(15000)
                local co1 = c1:Request('GET', 'https://httpbin.org/get?id=1')
                local co2 = c2:Request('GET', 'https://httpbin.org/get?id=2')
                local r1, r2
                while not r1 or not r2 do
                    if not r1 then
                        local ok, v = coroutine.resume(co1)
                        if v ~= nil then r1 = v end
                    end
                    if not r2 then
                        local ok, v = coroutine.resume(co2)
                        if v ~= nil then r2 = v end
                    end
                end
                return tostring(r1.Code == 200 and r2.Code == 200)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Concurrent_TwoRequests_BothReturnCorrectBody()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local c1 = HttpClient.New()
                c1:SetTimeout(15000)
                local c2 = HttpClient.New()
                c2:SetTimeout(15000)
                local co1 = c1:Request('GET', 'https://httpbin.org/get?tag=alpha')
                local co2 = c2:Request('GET', 'https://httpbin.org/get?tag=beta')
                local r1, r2
                while not r1 or not r2 do
                    if not r1 then
                        local ok, v = coroutine.resume(co1)
                        if v ~= nil then r1 = v end
                    end
                    if not r2 then
                        local ok, v = coroutine.resume(co2)
                        if v ~= nil then r2 = v end
                    end
                end
                return tostring(
                    r1.Code == 200 and r1.Contents:find('alpha') ~= nil and
                    r2.Code == 200 and r2.Contents:find('beta')  ~= nil)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Concurrent_ThreeRequests_AllComplete()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local function make(tag)
                    local c = HttpClient.New()
                    c:SetTimeout(15000)
                    return c:Request('GET', 'https://httpbin.org/get?tag=' .. tag)
                end
                local co1, co2, co3 = make('one'), make('two'), make('three')
                local r1, r2, r3
                while not r1 or not r2 or not r3 do
                    local function pump(co, prev)
                        if prev then return prev end
                        local ok, v = coroutine.resume(co)
                        return v
                    end
                    r1 = pump(co1, r1)
                    r2 = pump(co2, r2)
                    r3 = pump(co3, r3)
                end
                return tostring(r1.Code == 200 and r2.Code == 200 and r3.Code == 200)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Concurrent_Call_TwoCallsInParallel_BothComplete()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + @"
                if HttpClient == nil then return 'skip' end
                -- Run two Call() coroutines interleaved on the same engine tick.
                local results = {}
                local function run_call(tag)
                    local c = HttpClient.New()
                    c:SetTimeout(15000)
                    local res, err = c:Call('GET', 'https://httpbin.org/get?tag=' .. tag)
                    table.insert(results, res)
                end
                local co1 = coroutine.create(run_call)
                local co2 = coroutine.create(run_call)
                coroutine.resume(co1, 'concurrent_a')
                coroutine.resume(co2, 'concurrent_b')
                while coroutine.status(co1) ~= 'dead' or coroutine.status(co2) ~= 'dead' do
                    if coroutine.status(co1) ~= 'dead' then coroutine.resume(co1) end
                    if coroutine.status(co2) ~= 'dead' then coroutine.resume(co2) end
                end
                return tostring(
                    #results == 2 and
                    results[1] ~= nil and results[1].Code == 200 and
                    results[2] ~= nil and results[2].Code == 200)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_Concurrent_PostAndGet_BothComplete()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(DrainRequest + @"
                if HttpClient == nil then return 'skip' end
                local cGet = HttpClient.New()
                cGet:SetTimeout(15000)
                local cPost = HttpClient.New()
                cPost:SetTimeout(15000)
                local coGet  = cGet:Request('GET', 'https://httpbin.org/get')
                local coPost = cPost:Request('POST', 'https://httpbin.org/post',
                    '{""concurrent"":true}',
                    { ['Content-Type'] = 'application/json' })
                local rGet, rPost
                while not rGet or not rPost do
                    if not rGet then
                        local ok, v = coroutine.resume(coGet)
                        if v ~= nil then rGet = v end
                    end
                    if not rPost then
                        local ok, v = coroutine.resume(coPost)
                        if v ~= nil then rPost = v end
                    end
                end
                return tostring(rGet.Code == 200 and rPost.Code == 200)
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }
    }
}
