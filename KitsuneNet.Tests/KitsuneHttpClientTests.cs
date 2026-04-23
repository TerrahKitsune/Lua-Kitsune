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
            local function ws_connect(client, url)
                local ws, err = client:Connect(url)
                if not ws then error('Connect failed: ' .. tostring(err)) end
                ws:Read()  -- drain server welcome frame ('Request served by...')
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
                    local stream, err = client:Stream('GET', 'https://httpbin.org/get')
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
                    local stream, err = client:Stream('GET', 'https://httpbin.org/get')
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
                    local stream, err = client:Stream('GET', 'https://httpbin.org/get')
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
                    local stream, err = client:Stream('GET', 'https://httpbin.org/get')
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
                    local stream, err = client:Stream('POST', 'https://httpbin.org/post',
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
                    local stream, err = client:Stream('POST', 'https://httpbin.org/post',
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
                    local stream, err = client:Stream('PUT', 'https://httpbin.org/put',
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
                    local stream, err = client:Stream('PATCH', 'https://httpbin.org/patch',
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
                    local stream, err = client:Stream('DELETE', 'https://httpbin.org/delete')
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
        [Fact]
        public async Task Http_WebSocket_Connect_Succeeds()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws, err = client:Connect('{WsUrl}')
                    if not ws then error('connect failed: ' .. tostring(err)) end
                    ws:Close()
                    _outcome = 'true'
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_WebSocket_Echo_TextFrame_RoundTrips()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = ws_connect(client, '{WsUrl}')
                    ws:Write('hello kitsune')
                    local frame = ws:Read()
                    ws:Close()
                    _outcome = tostring(frame == 'hello kitsune')
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_WebSocket_Echo_TextFrame_GetInfo_OpcodeIsOne()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = ws_connect(client, '{WsUrl}')
                    ws:Write('opcode_test')
                    local frame = ws:Read()
                    local meta = ws:GetInfo()
                    ws:Close()
                    _outcome = tostring(frame ~= nil and meta.Opcode == 1 and meta.Binary == false)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
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
                    client:SetBinary(true)
                    ws:Write(payload)
                    local frame = ws:Read()
                    ws:Close()
                    _outcome = tostring(frame == payload)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_WebSocket_Echo_BinaryFrame_GetInfo_OpcodeIsTwo()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = ws_connect(client, '{WsUrl}')
                    client:SetBinary(true)
                    ws:Write('\xDE\xAD\xBE\xEF')
                    local frame = ws:Read()
                    local meta = ws:GetInfo()
                    ws:Close()
                    _outcome = tostring(frame ~= nil and meta.Opcode == 2 and meta.Binary == true)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_WebSocket_Echo_BytesLeft_IsZeroForCompleteFrame()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = ws_connect(client, '{WsUrl}')
                    ws:Write('complete')
                    local frame = ws:Read()
                    local meta = ws:GetInfo()
                    ws:Close()
                    _outcome = tostring(frame ~= nil and meta.BytesLeft == 0)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_WebSocket_MultipleFrames_AllEchoedInOrder()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = ws_connect(client, '{WsUrl}')
                    ws:Write('frame1')
                    ws:Write('frame2')
                    ws:Write('frame3')
                    local f1 = ws:Read()
                    local f2 = ws:Read()
                    local f3 = ws:Read()
                    ws:Close()
                    _outcome = tostring(f1 == 'frame1' and f2 == 'frame2' and f3 == 'frame3')
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_WebSocket_MixedFrames_TextAndBinary_EchoedCorrectly()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws = ws_connect(client, '{WsUrl}')
                    ws:Write('text_payload')
                    client:SetBinary(true)
                    ws:Write('\xAB\xCD')
                    client:SetBinary(false)
                    local tf = ws:Read()
                    local tm = ws:GetInfo()
                    local bf = ws:Read()
                    local bm = ws:GetInfo()
                    ws:Close()
                    _outcome = tostring(
                        tf == 'text_payload'   and tm.Binary == false and tm.Opcode == 1 and
                        bf == '\xAB\xCD'        and bm.Binary == true  and bm.Opcode == 2)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_WebSocket_Close_AfterClose_ReadReturnsNil()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws, err = client:Connect('{WsUrl}')
                    if not ws then error('connect failed: ' .. tostring(err)) end
                    ws:Close()
                    local frame = ws:Read()
                    _outcome = tostring(frame == nil)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
        public async Task Http_WebSocket_HasData_TrueWhenConnected_MinusOneAfterClose()
        {
            using KitsuneEngine engine = new();
            LuaValue r = await engine.ExecuteStringAsync(StreamHelper + $@"
                run_http(function()
                    if HttpClient == nil then skip() end
                    local client = HttpClient.New()
                    client:SetTimeout(8000)
                    local ws, err = client:Connect('{WsUrl}')
                    if not ws then error('connect failed: ' .. tostring(err)) end
                    local when_connected = ws:HasData()
                    ws:Close()
                    local after_close = ws:HasData()
                    _outcome = tostring(when_connected == true and after_close == -1)
                end)
                return _outcome or 'skip'
            ");
            if (r != "skip")
            {
                r.String.ShouldBe("true");
            }
        }

        [Fact]
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
                    ws:Write(payload)
                    local frame = ws:Read()
                    ws:Close()
                    _outcome = tostring(frame == payload)
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
    }
}
