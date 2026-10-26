using KitsuneNet;
using Shouldly;
using Xunit;
using Xunit.Abstractions;

namespace KitsuneNet.Tests;

[Collection("KitsuneSequential")]
public sealed class RedisTests : IAsyncLifetime
{
    // Sample JSON document used by all GetJson tests.
    private const string JsonDoc =
        """{"NullableGuid":{"HasValue":false,"Value":"SomeGuidBlablabla"},"OtherProperties":null,"TestArray":[{"Id":1,"Name":"Cake"},{"Id":2,"Name":"Also Cake"}],"SomeNumber":123}""";

    private readonly ITestOutputHelper _output;

    public RedisTests(ITestOutputHelper output) => _output = output;

    public Task InitializeAsync() => Task.CompletedTask;

    // Every key a test creates ends in the run id, so this removes leftovers even when the test failed midway.
    public async Task DisposeAsync()
    {
        if (string.IsNullOrEmpty(Environment.GetEnvironmentVariable("KITSUNE_REDIS_TEST")))
        {
            return;
        }

        using KitsuneEngine engine = TestRun.NewEngine();
        await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local reply = redis:Command('KEYS', 'kitsune_*_' .. RUN_ID)
            if type(reply.Value) == 'table' then
                for _, key in ipairs(reply.Value) do
                    redis:Command('DEL', key.Value)
                end
            end
            """);
    }

    // Connection
    [RedisFact]
    public async Task Open_ValidHost_ToStringStartsWithRedis()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            assert(redis, 'Open returned nil')
            local pong = redis:Command('PING').Value
            local text = tostring(redis)
            -- %p prints a pointer as bare hex on Windows and with a 0x prefix on Linux.
            return pong .. ':' .. tostring(text:match('^Redis: 0?x?%x+$') ~= nil)
            """);
        r.String.ShouldBe("PONG:true");
    }

    [RedisFact]
    public async Task Open_InvalidHost_ThrowsConnectionError()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync("""
            local ok, err = pcall(Redis.Open, 'invalid.host.does.not.exist', 6379, false, 1)
            return tostring(ok) .. ':' .. tostring(tostring(err):find('Connection error:', 1, true) ~= nil)
            """);
        r.String.ShouldBe("false:true");
    }

    // Command
    [RedisFact]
    public async Task Command_PING_ReturnsPong()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local reply = redis:Command('PING')
            return reply.Value
            """);
        r.String.ShouldBe("PONG");
    }

    [RedisFact]
    public async Task Command_SET_GET_RoundTrip()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_cmd_sg_' .. RUN_ID)
            redis:Command('SET', 'kitsune_test_cmd_sg_' .. RUN_ID, 'hello')
            local reply = redis:Command('GET', 'kitsune_test_cmd_sg_' .. RUN_ID)
            redis:Command('DEL', 'kitsune_test_cmd_sg_' .. RUN_ID)
            return reply.Value
            """);
        r.String.ShouldBe("hello");
    }

    [RedisFact]
    public async Task Command_DEL_RemovesKey()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_cmd_del_' .. RUN_ID, 'x')
            local reply = redis:Command('DEL', 'kitsune_test_cmd_del_' .. RUN_ID)
            return tostring(reply.Value)
            """);
        r.String.ShouldBe("1");
    }

    // GetString
    [RedisFact]
    public async Task GetString_Set_Tostring_RoundTrip()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_str_' .. RUN_ID)
            local str = redis:GetString('kitsune_test_str_' .. RUN_ID)
            str:Set('world')
            local val = tostring(str)
            redis:Command('DEL', 'kitsune_test_str_' .. RUN_ID)
            return val
            """);
        r.String.ShouldBe("world");
    }

    [RedisFact]
    public async Task GetString_Delete_ReturnsOldValue()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_del_' .. RUN_ID, 'goodbye')
            local str = redis:GetString('kitsune_test_str_del_' .. RUN_ID)
            return str:Delete()
            """);
        r.String.ShouldBe("goodbye");
    }

    [RedisFact]
    public async Task GetString_SetTTL_GetTTL_ReturnsPositive()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_ttl_' .. RUN_ID, 'temp')
            local str = redis:GetString('kitsune_test_str_ttl_' .. RUN_ID)
            str:SetTTL(60000)
            local ttl = str:GetTTL()
            redis:Command('DEL', 'kitsune_test_str_ttl_' .. RUN_ID)
            return tostring(ttl > 0)
            """);
        r.String.ShouldBe("true");
    }

    // GetKey
    [RedisFact]
    public async Task GetKey_Tostring_ReturnsKeyName()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local key = redis:GetKey('kitsune_test_keyname_' .. RUN_ID)
            return tostring(key)
            """);
        r.String.ShouldBe(TestRun.Name("kitsune_test_keyname"));
    }

    [RedisFact]
    public async Task GetKey_Type_ReturnsString()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_keytype_' .. RUN_ID, 'v')
            local key = redis:GetKey('kitsune_test_keytype_' .. RUN_ID)
            local t = key:Type()
            redis:Command('DEL', 'kitsune_test_keytype_' .. RUN_ID)
            return t
            """);
        r.String.ShouldBe("string");
    }

    [RedisFact]
    public async Task GetKey_Delete_ReturnsTrueWhenExists()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_keydel_' .. RUN_ID, 'v')
            local key = redis:GetKey('kitsune_test_keydel_' .. RUN_ID)
            return tostring(key:Delete())
            """);
        r.String.ShouldBe("true");
    }

    // GetHashset
    [RedisFact]
    public async Task GetHashset_SetGet_RoundTrip()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_hash_' .. RUN_ID)
            local hash = redis:GetHashset('kitsune_test_hash_' .. RUN_ID)
            hash['field1'] = 'alpha'
            local v = hash['field1']
            redis:Command('DEL', 'kitsune_test_hash_' .. RUN_ID)
            return v
            """);
        r.String.ShouldBe("alpha");
    }

    [RedisFact]
    public async Task GetHashset_DeleteField_ReturnsNil()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('HSET', 'kitsune_test_hash_del_' .. RUN_ID, 'f', 'v')
            local hash = redis:GetHashset('kitsune_test_hash_del_' .. RUN_ID)
            hash['f'] = nil
            local v = hash['f']
            redis:Command('DEL', 'kitsune_test_hash_del_' .. RUN_ID)
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    // GetList
    [RedisFact]
    public async Task GetList_RPUSH_ThenIndex_ReturnsValues()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_' .. RUN_ID)
            redis:Command('RPUSH', 'kitsune_test_list_' .. RUN_ID, 'first', 'second', 'third')
            local list = redis:GetList('kitsune_test_list_' .. RUN_ID)
            local v1 = list[1]
            local v2 = list[2]
            redis:Command('DEL', 'kitsune_test_list_' .. RUN_ID)
            return v1 .. ':' .. v2
            """);
        r.String.ShouldBe("first:second");
    }

    // GetSet
    [RedisFact]
    public async Task GetSet_SADD_IsMember_ReturnsTrueAndFalse()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_' .. RUN_ID)
            redis:Command('SADD', 'kitsune_test_set_' .. RUN_ID, 'apple', 'banana', 'cherry')
            local set = redis:GetSet('kitsune_test_set_' .. RUN_ID)
            local has     = set['banana']
            local missing = set['grape']
            redis:Command('DEL', 'kitsune_test_set_' .. RUN_ID)
            return tostring(has) .. ':' .. tostring(missing)
            """);
        r.String.ShouldBe("true:false");
    }

    // GetSortedSet
    [RedisFact]
    public async Task GetSortedSet_ZADD_IndexByRank_ReturnsLowestScoreMember()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_zset_' .. RUN_ID)
            redis:Command('ZADD', 'kitsune_test_zset_' .. RUN_ID, '10', 'memberA')
            redis:Command('ZADD', 'kitsune_test_zset_' .. RUN_ID, '20', 'memberB')
            local zset = redis:GetSortedSet('kitsune_test_zset_' .. RUN_ID)
            local first = zset[1]
            redis:Command('DEL', 'kitsune_test_zset_' .. RUN_ID)
            return first
            """);
        r.String.ShouldBe("memberA");
    }

    // GetStream
    [RedisFact]
    public async Task GetStream_Add_Then_Read_ReturnsEntry()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_stream_' .. RUN_ID)
            local stream = redis:GetStream('kitsune_test_stream_' .. RUN_ID)
            local id = stream:Add({{f1='hello', f2='world'}})
            local entry_id, fields = stream:Read()
            redis:Command('DEL', 'kitsune_test_stream_' .. RUN_ID)
            return tostring(id == entry_id) .. ':' .. fields.f1 .. ':' .. fields.f2
        ");
        r.String.ShouldBe("true:hello:world");
    }

    // Key iterator
    [RedisFact]
    public async Task KeyIterator_SCAN_FindsInsertedKeys()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_iter_1_' .. RUN_ID, '1')
            redis:Command('SET', 'kitsune_iter_2_' .. RUN_ID, '2')
            redis:Command('SET', 'kitsune_iter_3_' .. RUN_ID, '3')
            local found = 0
            for key in redis, redis, nil do
                local name = tostring(key)
                if name:find('kitsune_iter_', 1, true) then
                    found = found + 1
                end
            end
            redis:Command('DEL', 'kitsune_iter_1_' .. RUN_ID)
            redis:Command('DEL', 'kitsune_iter_2_' .. RUN_ID)
            redis:Command('DEL', 'kitsune_iter_3_' .. RUN_ID)
            return tostring(found >= 3)
            """);
        r.String.ShouldBe("true");
    }

    // Subscribe
    [RedisFact]
    public async Task Subscribe_ReturnsThread()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local co, errmsg = redis:Subscribe('kitsune_sub_type_test_' .. RUN_ID)
            local t = type(co)
            local before = coroutine.status(co)
            local ok = coroutine.resume(co, true)
            return t .. ':' .. tostring(errmsg) .. ':' .. before .. ':' .. tostring(ok) .. ':' .. coroutine.status(co)
            """);
        r.String.ShouldBe("thread:nil:suspended:true:dead");
    }

    [RedisFact]
    public async Task PSubscribe_ReturnsThread()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local co, errmsg = redis:PSubscribe('kitsune_psub_' .. RUN_ID .. '_*')
            local t = type(co)
            local before = coroutine.status(co)
            local ok = coroutine.resume(co, true)
            return t .. ':' .. tostring(errmsg) .. ':' .. before .. ':' .. tostring(ok) .. ':' .. coroutine.status(co)
            """);
        r.String.ShouldBe("thread:nil:suspended:true:dead");
    }

    [RedisFact]
    public async Task Subscribe_RoundTrip_ReceivesMessage()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local channel = 'kitsune_pubsub_rt_' .. RUN_ID
            local unique  = 'msg-' .. tostring(Time())

            local sub = {Open()}
            local co  = sub:Subscribe(channel)
            local pub = {Open()}

            -- The subscription ack is not observable, so republish until the message arrives.
            local deadline    = Time() + 5000
            local nextPublish = 0
            local result      = nil
            while Time() < deadline do
                local ok, ch, msg = coroutine.resume(co, false)
                if not ok then error(tostring(ch)) end
                if ch == channel and msg == unique then
                    result = ch .. ':' .. msg
                    break
                end
                if Time() >= nextPublish then
                    pub:Command('PUBLISH', channel, unique)
                    nextPublish = Time() + 100
                end
                Sleep(1)
            end

            coroutine.resume(co, true)
            return result or 'timeout'
            """);
        r.ShouldNotBe("timeout");
        r.String.ShouldStartWith(TestRun.Name("kitsune_pubsub_rt") + ":");
    }

    [RedisFact]
    public async Task PSubscribe_RoundTrip_ReceivesMessageWithPattern()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local pattern = 'kitsune_pps_' .. RUN_ID .. '_*'
            local channel = 'kitsune_pps_' .. RUN_ID .. '_test'
            local unique  = 'msg-' .. tostring(Time())

            local sub = {Open()}
            local co  = sub:PSubscribe(pattern)
            local pub = {Open()}

            -- The subscription ack is not observable, so republish until the message arrives.
            local deadline    = Time() + 5000
            local nextPublish = 0
            local result      = nil
            while Time() < deadline do
                local ok, pat, ch, msg = coroutine.resume(co, false)
                if not ok then error(tostring(pat)) end
                if ch == channel and msg == unique then
                    result = pat .. ':' .. ch .. ':' .. msg
                    break
                end
                if Time() >= nextPublish then
                    pub:Command('PUBLISH', channel, unique)
                    nextPublish = Time() + 100
                end
                Sleep(1)
            end

            coroutine.resume(co, true)
            return result or 'timeout'
            """);
        r.ShouldNotBe("timeout");
        r.String.ShouldStartWith($"kitsune_pps_{TestRun.Id}_*:kitsune_pps_{TestRun.Id}_test:");
    }

    // RedisString extended
    [RedisFact]
    public async Task GetString_Set_ReturnsOldValue()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_prev_' .. RUN_ID, 'old')
            local str = redis:GetString('kitsune_test_str_prev_' .. RUN_ID)
            local old = str:Set('new')
            redis:Command('DEL', 'kitsune_test_str_prev_' .. RUN_ID)
            return old
            """);
        r.String.ShouldBe("old");
    }

    [RedisFact]
    public async Task GetString_Set_ReturnsNilWhenKeyDidNotExist()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_str_nonexist_' .. RUN_ID)
            local str = redis:GetString('kitsune_test_str_nonexist_' .. RUN_ID)
            local old = str:Set('first')
            redis:Command('DEL', 'kitsune_test_str_nonexist_' .. RUN_ID)
            return tostring(old)
            """);
        r.String.ShouldBe("nil");
    }

    [RedisFact]
    public async Task GetString_Len_ReturnsStrlen()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_len_' .. RUN_ID, 'hello')
            local str = redis:GetString('kitsune_test_str_len_' .. RUN_ID)
            local n = #str
            redis:Command('DEL', 'kitsune_test_str_len_' .. RUN_ID)
            return tostring(n)
            """);
        r.String.ShouldBe("5");
    }

    [RedisFact]
    public async Task GetString_At_ReturnsByte()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_at_' .. RUN_ID, 'ABC')
            local str = redis:GetString('kitsune_test_str_at_' .. RUN_ID)
            local b = str:At(1)
            redis:Command('DEL', 'kitsune_test_str_at_' .. RUN_ID)
            return tostring(b)
            """);
        r.String.ShouldBe("65"); // 'A'
    }

    [RedisFact]
    public async Task GetString_IndexRead_ReturnsByte()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_idxr_' .. RUN_ID, 'ABC')
            local str = redis:GetString('kitsune_test_str_idxr_' .. RUN_ID)
            local b = str[2]
            redis:Command('DEL', 'kitsune_test_str_idxr_' .. RUN_ID)
            return tostring(b)
            """);
        r.String.ShouldBe("66"); // 'B'
    }

    [RedisFact]
    public async Task GetString_IndexWrite_SetsByte()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_idxw_' .. RUN_ID, 'ABC')
            local str = redis:GetString('kitsune_test_str_idxw_' .. RUN_ID)
            str[1] = 88  -- 'X'
            local val = tostring(str)
            redis:Command('DEL', 'kitsune_test_str_idxw_' .. RUN_ID)
            return val
            """);
        r.String.ShouldBe("XBC");
    }

    [RedisFact]
    public async Task GetString_Concat_ProducesJoinedString()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_cc_' .. RUN_ID, 'Hello')
            local str = redis:GetString('kitsune_test_str_cc_' .. RUN_ID)
            local result = str .. ', world'
            redis:Command('DEL', 'kitsune_test_str_cc_' .. RUN_ID)
            return result
            """);
        r.String.ShouldBe("Hello, world");
    }

    [RedisFact]
    public async Task GetString_GetOrSet_ReturnsExistingValue()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_gos_' .. RUN_ID, 'existing')
            local str = redis:GetString('kitsune_test_str_gos_' .. RUN_ID)
            local v = str:GetOrSet('new')
            redis:Command('DEL', 'kitsune_test_str_gos_' .. RUN_ID)
            return v
            """);
        r.String.ShouldBe("existing");
    }

    [RedisFact]
    public async Task GetString_GetOrSet_SetsAndReturnsNewWhenMissing()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_str_gos2_' .. RUN_ID)
            local str = redis:GetString('kitsune_test_str_gos2_' .. RUN_ID)
            local v = str:GetOrSet('default')
            redis:Command('DEL', 'kitsune_test_str_gos2_' .. RUN_ID)
            return v
            """);
        r.String.ShouldBe("default");
    }

    [RedisFact]
    public async Task GetString_GetTTL_ReturnsNegativeOneWhenNoPersist()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_nottl_' .. RUN_ID, 'v')
            local str = redis:GetString('kitsune_test_str_nottl_' .. RUN_ID)
            local ttl = str:GetTTL()
            redis:Command('DEL', 'kitsune_test_str_nottl_' .. RUN_ID)
            return tostring(ttl)
            """);
        r.String.ShouldBe("-1");
    }

    [RedisFact]
    public async Task GetString_SetTTL_Zero_RemovesTTL()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_persist_' .. RUN_ID, 'v')
            local str = redis:GetString('kitsune_test_str_persist_' .. RUN_ID)
            str:SetTTL(60000)
            str:SetTTL(0)
            local ttl = str:GetTTL()
            redis:Command('DEL', 'kitsune_test_str_persist_' .. RUN_ID)
            return tostring(ttl)
            """);
        r.String.ShouldBe("-1");
    }

    [RedisFact]
    public async Task GetString_Pairs_IteratesBytes()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_pairs_' .. RUN_ID, 'AB')
            local str = redis:GetString('kitsune_test_str_pairs_' .. RUN_ID)
            local bytes = {{}}
            for i, b in pairs(str) do
                bytes[i] = b
            end
            redis:Command('DEL', 'kitsune_test_str_pairs_' .. RUN_ID)
            return tostring(bytes[1]) .. ':' .. tostring(bytes[2])
        ");
        r.String.ShouldBe("65:66"); // 'A'=65, 'B'=66
    }

    [RedisFact]
    public async Task GetString_Call_ReturnsRedisKey()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_call_' .. RUN_ID, 'v')
            local str = redis:GetString('kitsune_test_str_call_' .. RUN_ID)
            local key = str()
            redis:Command('DEL', 'kitsune_test_str_call_' .. RUN_ID)
            return tostring(key)
            """);
        r.String.ShouldBe(TestRun.Name("kitsune_test_str_call"));
    }

    // RedisKey extended
    [RedisFact]
    public async Task GetKey_SetTTL_GetTTL_RoundTrip()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_key_ttl_' .. RUN_ID, 'v')
            local key = redis:GetKey('kitsune_test_key_ttl_' .. RUN_ID)
            key:SetTTL(60000)
            local ttl = key:GetTTL()
            redis:Command('DEL', 'kitsune_test_key_ttl_' .. RUN_ID)
            return tostring(ttl > 0)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetKey_SetTTL_Zero_RemovesTTL()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_key_persist_' .. RUN_ID, 'v')
            local key = redis:GetKey('kitsune_test_key_persist_' .. RUN_ID)
            key:SetTTL(60000)
            key:SetTTL(0)
            local ttl = key:GetTTL()
            redis:Command('DEL', 'kitsune_test_key_persist_' .. RUN_ID)
            return tostring(ttl)
            """);
        r.String.ShouldBe("-1");
    }

    [RedisFact]
    public async Task GetKey_Type_ReturnsHash()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('HSET', 'kitsune_test_key_hashtype_' .. RUN_ID, 'f', 'v')
            local key = redis:GetKey('kitsune_test_key_hashtype_' .. RUN_ID)
            local t = key:Type()
            redis:Command('DEL', 'kitsune_test_key_hashtype_' .. RUN_ID)
            return t
            """);
        r.String.ShouldBe("hash");
    }

    [RedisFact]
    public async Task GetKey_Type_ReturnsList()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('RPUSH', 'kitsune_test_key_listtype_' .. RUN_ID, 'v')
            local key = redis:GetKey('kitsune_test_key_listtype_' .. RUN_ID)
            local t = key:Type()
            redis:Command('DEL', 'kitsune_test_key_listtype_' .. RUN_ID)
            return t
            """);
        r.String.ShouldBe("list");
    }

    [RedisFact]
    public async Task GetKey_Type_ReturnsNoneWhenMissing()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_key_noexist_' .. RUN_ID)
            local key = redis:GetKey('kitsune_test_key_noexist_' .. RUN_ID)
            return key:Type()
            """);
        r.String.ShouldBe("none");
    }

    // GetHashset extended
    [RedisFact]
    public async Task GetHashset_MultipleFields_RoundTrip()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_hash_multi_' .. RUN_ID)
            local hash = redis:GetHashset('kitsune_test_hash_multi_' .. RUN_ID)
            hash['a'] = 'alpha'
            hash['b'] = 'beta'
            hash['c'] = 'gamma'
            local a = hash['a']
            local b = hash['b']
            local c = hash['c']
            redis:Command('DEL', 'kitsune_test_hash_multi_' .. RUN_ID)
            return a .. ':' .. b .. ':' .. c
            """);
        r.String.ShouldBe("alpha:beta:gamma");
    }

    [RedisFact]
    public async Task GetHashset_Pairs_IteratesAllFields()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_hash_pairs_' .. RUN_ID)
            redis:Command('HSET', 'kitsune_test_hash_pairs_' .. RUN_ID, 'x', '1', 'y', '2', 'z', '3')
            local hash = redis:GetHashset('kitsune_test_hash_pairs_' .. RUN_ID)
            local count = 0
            for k, v in pairs(hash) do
                count = count + 1
            end
            redis:Command('DEL', 'kitsune_test_hash_pairs_' .. RUN_ID)
            return tostring(count)
            """);
        r.String.ShouldBe("3");
    }

    [RedisFact]
    public async Task GetHashset_Tostring_ContainsKeyName()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local hash = redis:GetHashset('kitsune_test_hash_ts_' .. RUN_ID)
            local s = tostring(hash)
            return tostring(s:find('kitsune_test_hash_ts_' .. RUN_ID, 1, true) ~= nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetHashset_Call_ReturnsKeyAndType()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local hash = redis:GetHashset('kitsune_test_hash_call_' .. RUN_ID)
            local key, t = hash()
            return tostring(key) .. ':' .. tostring(t)
            """);
        r.String.ShouldBe(TestRun.Name("kitsune_test_hash_call") + ":1");
    }

    // GetList extended
    [RedisFact]
    public async Task GetList_Len_ReturnsCount()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_len_' .. RUN_ID)
            redis:Command('RPUSH', 'kitsune_test_list_len_' .. RUN_ID, 'a', 'b', 'c')
            local list = redis:GetList('kitsune_test_list_len_' .. RUN_ID)
            local n = #list
            redis:Command('DEL', 'kitsune_test_list_len_' .. RUN_ID)
            return tostring(n)
            """);
        r.String.ShouldBe("3");
    }

    [RedisFact]
    public async Task GetList_IndexZero_LPops()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_lpop_' .. RUN_ID)
            redis:Command('RPUSH', 'kitsune_test_list_lpop_' .. RUN_ID, 'first', 'second')
            local list = redis:GetList('kitsune_test_list_lpop_' .. RUN_ID)
            local v = list[0]
            redis:Command('DEL', 'kitsune_test_list_lpop_' .. RUN_ID)
            return v
            """);
        r.String.ShouldBe("first");
    }

    [RedisFact]
    public async Task GetList_NegativeIndex_ReturnsFromEnd()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_neg_' .. RUN_ID)
            redis:Command('RPUSH', 'kitsune_test_list_neg_' .. RUN_ID, 'a', 'b', 'c')
            local list = redis:GetList('kitsune_test_list_neg_' .. RUN_ID)
            local v = list[-1]
            redis:Command('DEL', 'kitsune_test_list_neg_' .. RUN_ID)
            return v
            """);
        r.String.ShouldBe("c");
    }

    [RedisFact]
    public async Task GetList_AssignIndexZero_AppendsByRpush()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_push_' .. RUN_ID)
            local list = redis:GetList('kitsune_test_list_push_' .. RUN_ID)
            list[0] = 'appended'
            local v = list[1]
            redis:Command('DEL', 'kitsune_test_list_push_' .. RUN_ID)
            return v
            """);
        r.String.ShouldBe("appended");
    }

    [RedisFact]
    public async Task GetList_AssignExistingIndex_UpdatesViaLset()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_lset_' .. RUN_ID)
            redis:Command('RPUSH', 'kitsune_test_list_lset_' .. RUN_ID, 'old', 'b')
            local list = redis:GetList('kitsune_test_list_lset_' .. RUN_ID)
            list[1] = 'new'
            local v = list[1]
            redis:Command('DEL', 'kitsune_test_list_lset_' .. RUN_ID)
            return v
            """);
        r.String.ShouldBe("new");
    }

    [RedisFact]
    public async Task GetList_Pairs_IteratesAll()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_pairs_' .. RUN_ID)
            redis:Command('RPUSH', 'kitsune_test_list_pairs_' .. RUN_ID, 'x', 'y', 'z')
            local list = redis:GetList('kitsune_test_list_pairs_' .. RUN_ID)
            local found = {{}}
            for i, v in pairs(list) do
                found[i] = v
            end
            redis:Command('DEL', 'kitsune_test_list_pairs_' .. RUN_ID)
            return found[1] .. ':' .. found[2] .. ':' .. found[3]
        ");
        r.String.ShouldBe("x:y:z");
    }

    [RedisFact]
    public async Task GetList_Tostring_ContainsKeyName()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local list = redis:GetList('kitsune_test_list_ts_' .. RUN_ID)
            local s = tostring(list)
            return tostring(s:find('kitsune_test_list_ts_' .. RUN_ID, 1, true) ~= nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetList_Call_ReturnsKeyAndType()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local list = redis:GetList('kitsune_test_list_call_' .. RUN_ID)
            local key, t = list()
            return tostring(key) .. ':' .. tostring(t)
            """);
        r.String.ShouldBe(TestRun.Name("kitsune_test_list_call") + ":2");
    }

    // GetSet extended
    [RedisFact]
    public async Task GetSet_Srandmember_ReturnsAMember()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_rand_' .. RUN_ID)
            redis:Command('SADD', 'kitsune_test_set_rand_' .. RUN_ID, 'apple', 'banana')
            local set = redis:GetSet('kitsune_test_set_rand_' .. RUN_ID)
            local v = set[0]
            redis:Command('DEL', 'kitsune_test_set_rand_' .. RUN_ID)
            return tostring(v == 'apple' or v == 'banana')
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSet_Spop_RemovesAndReturnsMember()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_pop_' .. RUN_ID)
            redis:Command('SADD', 'kitsune_test_set_pop_' .. RUN_ID, 'only')
            local set = redis:GetSet('kitsune_test_set_pop_' .. RUN_ID)
            local v = set[-1]
            local remaining = redis:Command('SCARD', 'kitsune_test_set_pop_' .. RUN_ID)
            redis:Command('DEL', 'kitsune_test_set_pop_' .. RUN_ID)
            return v .. ':' .. tostring(remaining.Value)
            """);
        r.String.ShouldBe("only:0");
    }

    [RedisFact]
    public async Task GetSet_AssignTrue_AddsMember()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_add_' .. RUN_ID)
            local set = redis:GetSet('kitsune_test_set_add_' .. RUN_ID)
            set['mango'] = true
            local has = set['mango']
            redis:Command('DEL', 'kitsune_test_set_add_' .. RUN_ID)
            return tostring(has)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSet_AssignFalse_RemovesMember()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SADD', 'kitsune_test_set_remf_' .. RUN_ID, 'peach')
            local set = redis:GetSet('kitsune_test_set_remf_' .. RUN_ID)
            set['peach'] = false
            local has = set['peach']
            redis:Command('DEL', 'kitsune_test_set_remf_' .. RUN_ID)
            return tostring(has)
            """);
        r.String.ShouldBe("false");
    }

    [RedisFact]
    public async Task GetSet_AssignNil_RemovesMember()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SADD', 'kitsune_test_set_remn_' .. RUN_ID, 'plum')
            local set = redis:GetSet('kitsune_test_set_remn_' .. RUN_ID)
            set['plum'] = nil
            local has = set['plum']
            redis:Command('DEL', 'kitsune_test_set_remn_' .. RUN_ID)
            return tostring(has)
            """);
        r.String.ShouldBe("false");
    }

    [RedisFact]
    public async Task GetSet_Len_ReturnsScard()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_len_' .. RUN_ID)
            redis:Command('SADD', 'kitsune_test_set_len_' .. RUN_ID, 'a', 'b', 'c', 'd')
            local set = redis:GetSet('kitsune_test_set_len_' .. RUN_ID)
            local n = #set
            redis:Command('DEL', 'kitsune_test_set_len_' .. RUN_ID)
            return tostring(n)
            """);
        r.String.ShouldBe("4");
    }

    [RedisFact]
    public async Task GetSet_IndexedAccess_ReturnsMember()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_idx_' .. RUN_ID)
            redis:Command('SADD', 'kitsune_test_set_idx_' .. RUN_ID, 'solo')
            local set = redis:GetSet('kitsune_test_set_idx_' .. RUN_ID)
            local v = set[1]
            redis:Command('DEL', 'kitsune_test_set_idx_' .. RUN_ID)
            return v
            """);
        r.String.ShouldBe("solo");
    }

    [RedisFact]
    public async Task GetSet_Pairs_IteratesAll()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_pairs_' .. RUN_ID)
            redis:Command('SADD', 'kitsune_test_set_pairs_' .. RUN_ID, 'x', 'y', 'z')
            local set = redis:GetSet('kitsune_test_set_pairs_' .. RUN_ID)
            local count = 0
            for i, v in pairs(set) do
                count = count + 1
            end
            redis:Command('DEL', 'kitsune_test_set_pairs_' .. RUN_ID)
            return tostring(count)
            """);
        r.String.ShouldBe("3");
    }

    [RedisFact]
    public async Task GetSet_Tostring_ContainsKeyName()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local set = redis:GetSet('kitsune_test_set_ts_' .. RUN_ID)
            local s = tostring(set)
            return tostring(s:find('kitsune_test_set_ts_' .. RUN_ID, 1, true) ~= nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSet_Call_ReturnsKeyAndType()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local set = redis:GetSet('kitsune_test_set_call_' .. RUN_ID)
            local key, t = set()
            return tostring(key) .. ':' .. tostring(t)
            """);
        r.String.ShouldBe(TestRun.Name("kitsune_test_set_call") + ":3");
    }

    // GetSortedSet extended
    [RedisFact]
    public async Task GetSortedSet_GetScore_ByMemberName()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_zset_score_' .. RUN_ID)
            redis:Command('ZADD', 'kitsune_test_zset_score_' .. RUN_ID, '42', 'alpha')
            local zset = redis:GetSortedSet('kitsune_test_zset_score_' .. RUN_ID)
            local score = zset['alpha']
            redis:Command('DEL', 'kitsune_test_zset_score_' .. RUN_ID)
            return tostring(score)
            """);
        r.String.ShouldBe("42");
    }

    [RedisFact]
    public async Task GetSortedSet_AddViaAssignment_AddsToSet()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_zset_add_' .. RUN_ID)
            local zset = redis:GetSortedSet('kitsune_test_zset_add_' .. RUN_ID)
            zset['alpha'] = 99
            local score = zset['alpha']
            redis:Command('DEL', 'kitsune_test_zset_add_' .. RUN_ID)
            return tostring(score)
            """);
        r.String.ShouldBe("99");
    }

    [RedisFact]
    public async Task GetSortedSet_RemoveViaNil_DeletesMember()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_zset_rem_' .. RUN_ID)
            redis:Command('ZADD', 'kitsune_test_zset_rem_' .. RUN_ID, '5', 'beta')
            local zset = redis:GetSortedSet('kitsune_test_zset_rem_' .. RUN_ID)
            zset['beta'] = nil
            local score = zset['beta']
            redis:Command('DEL', 'kitsune_test_zset_rem_' .. RUN_ID)
            return tostring(score == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSortedSet_Pairs_YieldsMembersWithScores()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_zset_pairs_' .. RUN_ID)
            redis:Command('ZADD', 'kitsune_test_zset_pairs_' .. RUN_ID, '10', 'a')
            redis:Command('ZADD', 'kitsune_test_zset_pairs_' .. RUN_ID, '20', 'b')
            local zset = redis:GetSortedSet('kitsune_test_zset_pairs_' .. RUN_ID)
            local members = {{}}
            for member, score in pairs(zset) do
                members[member] = score
            end
            redis:Command('DEL', 'kitsune_test_zset_pairs_' .. RUN_ID)
            return tostring(members['a']) .. ':' .. tostring(members['b'])
        ");
        r.String.ShouldBe("10:20");
    }

    [RedisFact]
    public async Task GetSortedSet_Tostring_ContainsKeyName()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local zset = redis:GetSortedSet('kitsune_test_zset_ts_' .. RUN_ID)
            local s = tostring(zset)
            return tostring(s:find('kitsune_test_zset_ts_' .. RUN_ID, 1, true) ~= nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSortedSet_Call_ReturnsKeyAndType()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local zset = redis:GetSortedSet('kitsune_test_zset_call_' .. RUN_ID)
            local key, t = zset()
            return tostring(key) .. ':' .. tostring(t)
            """);
        r.String.ShouldBe(TestRun.Name("kitsune_test_zset_call") + ":4");
    }

    // GetStream extended
    [RedisFact]
    public async Task GetStream_Add_IdIsNonEmptyString()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_stream_id_' .. RUN_ID)
            local stream = redis:GetStream('kitsune_test_stream_id_' .. RUN_ID)
            local id = stream:Add({{k='v'}})
            redis:Command('DEL', 'kitsune_test_stream_id_' .. RUN_ID)
            return tostring(type(id) == 'string' and #id > 0)
        ");
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetStream_MultipleAdd_ReadSequential()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_stream_seq_' .. RUN_ID)
            local stream = redis:GetStream('kitsune_test_stream_seq_' .. RUN_ID)
            local id1 = stream:Add({{msg='first'}})
            stream:Add({{msg='second'}})
            local eid, fields = stream:Read()
            local eid2, fields2 = stream:Read(eid)
            redis:Command('DEL', 'kitsune_test_stream_seq_' .. RUN_ID)
            return fields.msg .. ':' .. fields2.msg
        ");
        r.String.ShouldBe("first:second");
    }

    [RedisFact]
    public async Task GetStream_Trim_ReducesLength()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_stream_trim_' .. RUN_ID)
            local stream = redis:GetStream('kitsune_test_stream_trim_' .. RUN_ID)
            stream:Add({{n='1'}})
            stream:Add({{n='2'}})
            stream:Add({{n='3'}})
            local trimmed = stream:Trim(1)
            local len = redis:Command('XLEN', 'kitsune_test_stream_trim_' .. RUN_ID)
            redis:Command('DEL', 'kitsune_test_stream_trim_' .. RUN_ID)
            return tostring(trimmed >= 0) .. ':' .. tostring(len.Value == 1)
        ");
        r.String.ShouldBe("true:true");
    }

    [RedisFact]
    public async Task GetStream_Tostring_ContainsKeyName()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local stream = redis:GetStream('kitsune_test_stream_ts_' .. RUN_ID)
            local s = tostring(stream)
            return tostring(s:find('kitsune_test_stream_ts_' .. RUN_ID, 1, true) ~= nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetStream_Call_ReturnsRedisKey()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local stream = redis:GetStream('kitsune_test_stream_call_' .. RUN_ID)
            local key = stream()
            return tostring(key)
            """);
        r.String.ShouldBe(TestRun.Name("kitsune_test_stream_call"));
    }

    // Command reply
    [RedisFact]
    public async Task Command_GET_NonExistentKey_NilValue()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_reply_nil_' .. RUN_ID)
            local reply = redis:Command('GET', 'kitsune_test_reply_nil_' .. RUN_ID)
            return tostring(reply.Value == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task Command_SMEMBERS_ValueIsTable()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_reply_arr_' .. RUN_ID)
            redis:Command('SADD', 'kitsune_test_reply_arr_' .. RUN_ID, 'x', 'y')
            local reply = redis:Command('SMEMBERS', 'kitsune_test_reply_arr_' .. RUN_ID)
            redis:Command('DEL', 'kitsune_test_reply_arr_' .. RUN_ID)
            return tostring(type(reply.Value) == 'table' and #reply.Value == 2)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task Command_INCR_ReturnsInteger()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_reply_int_' .. RUN_ID)
            redis:Command('SET', 'kitsune_test_reply_int_' .. RUN_ID, '10')
            local reply = redis:Command('INCR', 'kitsune_test_reply_int_' .. RUN_ID)
            redis:Command('DEL', 'kitsune_test_reply_int_' .. RUN_ID)
            return tostring(reply.Value)
            """);
        r.String.ShouldBe("11");
    }

    [RedisFact]
    public async Task GetStream_Read_EmptyStream_ReturnsNilPair()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_stream_empty_' .. RUN_ID)
            local stream = redis:GetStream('kitsune_test_stream_empty_' .. RUN_ID)
            local eid, fields = stream:Read()
            return tostring(eid == nil and fields == nil)
            """);
        r.String.ShouldBe("true");
    }

    // Command validation
    [RedisFact]
    public async Task Command_NilArgument_Raises()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local ok, err = pcall(function()
                redis:Command('SET', nil, 'value')
            end)
            return tostring(ok) .. ':' .. tostring(tostring(err):find('command argument 2 must be a string or number (got nil)', 1, true) ~= nil)
            """);
        r.String.ShouldBe("false:true");
    }

    [RedisFact]
    public async Task Command_BooleanArgument_Raises()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local ok, err = pcall(function()
                redis:Command('SET', 'key', true)
            end)
            return tostring(ok) .. ':' .. tostring(tostring(err):find('command argument 3 must be a string or number (got boolean)', 1, true) ~= nil)
            """);
        r.String.ShouldBe("false:true");
    }

    [RedisFact]
    public async Task Command_WRONGTYPE_Raises()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_wt_list_' .. RUN_ID)
            redis:Command('RPUSH', 'kitsune_test_wt_list_' .. RUN_ID, 'item')
            local ok, err = pcall(function()
                redis:Command('GET', 'kitsune_test_wt_list_' .. RUN_ID)
            end)
            redis:Command('DEL', 'kitsune_test_wt_list_' .. RUN_ID)
            return tostring(ok) .. ':' .. tostring(tostring(err):find('Redis error: WRONGTYPE', 1, true) ~= nil)
            """);
        r.String.ShouldBe("false:true");
    }

    // GetString extended (more paths)
    [RedisFact]
    public async Task GetString_GetSet_IsAliasOfGetOrSet()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_getset_alias_' .. RUN_ID, 'original')
            local str = redis:GetString('kitsune_test_str_getset_alias_' .. RUN_ID)
            local old = str:GetSet('replaced')
            redis:Command('DEL', 'kitsune_test_str_getset_alias_' .. RUN_ID)
            return old
            """);
        r.String.ShouldBe("original");
    }

    [RedisFact]
    public async Task GetString_Concat_TwoRedisStrings()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_cat_a_' .. RUN_ID, 'foo')
            redis:Command('SET', 'kitsune_test_str_cat_b_' .. RUN_ID, 'bar')
            local a = redis:GetString('kitsune_test_str_cat_a_' .. RUN_ID)
            local b = redis:GetString('kitsune_test_str_cat_b_' .. RUN_ID)
            local result = a .. b
            redis:Command('DEL', 'kitsune_test_str_cat_a_' .. RUN_ID, 'kitsune_test_str_cat_b_' .. RUN_ID)
            return result
            """);
        r.String.ShouldBe("foobar");
    }

    [RedisFact]
    public async Task GetString_At_OutOfRange_ReturnsNil()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_at_oor_' .. RUN_ID, 'Hi')
            local str = redis:GetString('kitsune_test_str_at_oor_' .. RUN_ID)
            local b = str:At(100)
            redis:Command('DEL', 'kitsune_test_str_at_oor_' .. RUN_ID)
            return tostring(b == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetString_Delete_NonExistentKey_ReturnsNil()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_str_del_nokey_' .. RUN_ID)
            local str = redis:GetString('kitsune_test_str_del_nokey_' .. RUN_ID)
            local v = str:Delete()
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetString_GetTTL_NonExistentKey_ReturnsNil()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_str_ttl_nokey_' .. RUN_ID)
            local str = redis:GetString('kitsune_test_str_ttl_nokey_' .. RUN_ID)
            return tostring(str:GetTTL() == nil)
            """);
        r.String.ShouldBe("true");
    }

    // GetKey extended (more paths)
    [RedisFact]
    public async Task GetKey_GetTTL_NonExistentKey_ReturnsNil()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_key_ttl_noexist_' .. RUN_ID)
            local key = redis:GetKey('kitsune_test_key_ttl_noexist_' .. RUN_ID)
            return tostring(key:GetTTL() == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetKey_SetTTL_NonExistentKey_ReturnsFalse()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_key_pexpire_nokey_' .. RUN_ID)
            local key = redis:GetKey('kitsune_test_key_pexpire_nokey_' .. RUN_ID)
            return tostring(key:SetTTL(60000))
            """);
        r.String.ShouldBe("false");
    }

    // GetHashset extended (more paths)
    [RedisFact]
    public async Task GetHashset_StoreAndRetrieveNumber()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_hash_num_' .. RUN_ID)
            local hash = redis:GetHashset('kitsune_test_hash_num_' .. RUN_ID)
            hash['count'] = 42
            local v = hash['count']
            redis:Command('DEL', 'kitsune_test_hash_num_' .. RUN_ID)
            return v
            """);
        r.String.ShouldBe("42");
    }

    [RedisFact]
    public async Task GetHashset_StoreTable_IsJsonEncoded()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_hash_json_' .. RUN_ID)
            local hash = redis:GetHashset('kitsune_test_hash_json_' .. RUN_ID)
            hash['data'] = {{x=1, y=2}}
            local v = hash['data']
            redis:Command('DEL', 'kitsune_test_hash_json_' .. RUN_ID)
            local decoded = Json.New():Decode(v)
            return type(v) .. ':' .. v:sub(1, 1) .. ':' .. tostring(decoded.x) .. ':' .. tostring(decoded.y)
        ");
        r.String.ShouldBe("string:{:1:2");
    }

    [RedisFact]
    public async Task GetHashset_Len_IsAlwaysZero()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('HSET', 'kitsune_test_hash_lenz_' .. RUN_ID, 'f', 'v')
            local hash = redis:GetHashset('kitsune_test_hash_lenz_' .. RUN_ID)
            local n = #hash
            redis:Command('DEL', 'kitsune_test_hash_lenz_' .. RUN_ID)
            return tostring(n)
            """);
        r.String.ShouldBe("0");
    }

    // GetList extended (more paths)
    [RedisFact]
    public async Task GetList_Len_EmptyList_ReturnsZero()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_empty_' .. RUN_ID)
            local list = redis:GetList('kitsune_test_list_empty_' .. RUN_ID)
            return tostring(#list)
            """);
        r.String.ShouldBe("0");
    }

    [RedisFact]
    public async Task GetList_IndexZero_EmptyList_ReturnsNil()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_lpop_empty_' .. RUN_ID)
            local list = redis:GetList('kitsune_test_list_lpop_empty_' .. RUN_ID)
            local v = list[0]
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetList_AssignNegativeIndex_PrependsByLpush()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_neg_push_' .. RUN_ID)
            redis:Command('RPUSH', 'kitsune_test_list_neg_push_' .. RUN_ID, 'a', 'b', 'c')
            local list = redis:GetList('kitsune_test_list_neg_push_' .. RUN_ID)
            list[-5] = 'prepended'
            local first = list[1]
            redis:Command('DEL', 'kitsune_test_list_neg_push_' .. RUN_ID)
            return first
            """);
        r.String.ShouldBe("prepended");
    }

    // GetSet extended (more paths)
    [RedisFact]
    public async Task GetSet_IndexedAccess_BeyondSize_ReturnsNil()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_oob_' .. RUN_ID)
            redis:Command('SADD', 'kitsune_test_set_oob_' .. RUN_ID, 'a', 'b')
            local set = redis:GetSet('kitsune_test_set_oob_' .. RUN_ID)
            local v = set[10]
            redis:Command('DEL', 'kitsune_test_set_oob_' .. RUN_ID)
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSet_MutationInvalidatesScanCache()
    {
        using KitsuneEngine engine = TestRun.NewEngine();

        // set[1] fills the SSCAN cache; set[2] keeps it alive (2 of 3).
        // After SREM the cache must be cleared so set[3] re-scans the
        // now-2-member set and returns nil instead of a stale third element.
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_stale_' .. RUN_ID)
            redis:Command('SADD', 'kitsune_test_set_stale_' .. RUN_ID, 'alpha', 'beta', 'gamma')
            local set = redis:GetSet('kitsune_test_set_stale_' .. RUN_ID)
            local m1 = set[1]
            local m2 = set[2]
            set['beta'] = nil
            local third = set[3]
            redis:Command('DEL', 'kitsune_test_set_stale_' .. RUN_ID)
            return tostring(third == nil)
            """);
        r.String.ShouldBe("true");
    }

    // GetSortedSet extended (more paths)
    [RedisFact]
    public async Task GetSortedSet_IndexByRank_OutOfRange_ReturnsNil()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_zset_oor_' .. RUN_ID)
            redis:Command('ZADD', 'kitsune_test_zset_oor_' .. RUN_ID, '1', 'a')
            local zset = redis:GetSortedSet('kitsune_test_zset_oor_' .. RUN_ID)
            local v = zset[100]
            redis:Command('DEL', 'kitsune_test_zset_oor_' .. RUN_ID)
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSortedSet_Score_NonExistentMember_ReturnsNil()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_zset_noscore_' .. RUN_ID)
            redis:Command('ZADD', 'kitsune_test_zset_noscore_' .. RUN_ID, '5', 'member')
            local zset = redis:GetSortedSet('kitsune_test_zset_noscore_' .. RUN_ID)
            local score = zset['nonexistent']
            redis:Command('DEL', 'kitsune_test_zset_noscore_' .. RUN_ID)
            return tostring(score == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSortedSet_Len_IsAlwaysZero()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('ZADD', 'kitsune_test_zset_lenz_' .. RUN_ID, '1', 'a')
            local zset = redis:GetSortedSet('kitsune_test_zset_lenz_' .. RUN_ID)
            local n = #zset
            redis:Command('DEL', 'kitsune_test_zset_lenz_' .. RUN_ID)
            return tostring(n)
            """);
        r.String.ShouldBe("0");
    }

    [RedisFact]
    public async Task GetSortedSet_Pairs_FloatScoreRoundTrips()
    {
        using KitsuneEngine engine = TestRun.NewEngine();

        // Verifies that lua_stringtonumber is used (not atoll) so fractional scores survive.
        LuaValue r = await engine.ExecuteStringAsync($$"""
            local redis = {{Open()}}
            redis:Command('DEL', 'kitsune_test_zset_float_' .. RUN_ID)
            redis:Command('ZADD', 'kitsune_test_zset_float_' .. RUN_ID, '1.5', 'a')
            redis:Command('ZADD', 'kitsune_test_zset_float_' .. RUN_ID, '2', 'b')
            local zset = redis:GetSortedSet('kitsune_test_zset_float_' .. RUN_ID)
            local scores = {}
            for member, score in pairs(zset) do
                scores[member] = score
            end
            redis:Command('DEL', 'kitsune_test_zset_float_' .. RUN_ID)
            return tostring(scores['a']) .. ':' .. tostring(scores['b'])
            """);
        r.String.ShouldBe("1.5:2");
    }

    [RedisFact]
    public async Task GetJson_Tostring_ShowsKeyAndPath()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            return tostring(json)
            """);
        r.String.ShouldBe($"RedisJson: {TestRun.Name("kitsune_test_json")} $");
    }

    [RedisFact]
    public async Task GetJson_GetScalar_ReturnsNumber()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local n = json.SomeNumber()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(n)
            """);
        r.String.ShouldBe("123");
    }

    [RedisFact]
    public async Task GetJson_GetNestedBool_ReturnsFalse()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local v = json.NullableGuid.HasValue()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(v)
            """);
        r.String.ShouldBe("false");
    }

    [RedisFact]
    public async Task GetJson_GetNestedString_ReturnsValue()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local v = json.NullableGuid.Value()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return v
            """);
        r.String.ShouldBe("SomeGuidBlablabla");
    }

    [RedisFact]
    public async Task GetJson_GetArrayElement_ByLuaIndex_ReturnsSecondEntry()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local name = json.TestArray[2].Name()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return name
            """);
        r.String.ShouldBe("Also Cake");
    }

    [RedisFact]
    public async Task GetJson_GetArrayElement_FirstEntry()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local name = json.TestArray[1].Name()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return name
            """);
        r.String.ShouldBe("Cake");
    }

    [RedisFact]
    public async Task GetJson_GetMissingPath_ReturnsNil()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local v = json.nonexistent()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_Get_Root_ReturnsTable()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local doc = json()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(type(doc) == 'table' and doc.SomeNumber == 123)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_SetViaMethod_UpdatesScalar()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            json.SomeNumber:Set(456)
            local v = json.SomeNumber()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(v)
            """);
        r.String.ShouldBe("456");
    }

    [RedisFact]
    public async Task GetJson_SetViaNewindex_UpdatesScalar()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            json.SomeNumber = 789
            local v = json.SomeNumber()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(v)
            """);
        r.String.ShouldBe("789");
    }

    [RedisFact]
    public async Task GetJson_SetNestedViaNewindex_UpdatesField()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            json.NullableGuid.HasValue = true
            local v = json.NullableGuid.HasValue()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(v)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_Delete_DeletesPath()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            json.NullableGuid:Delete()
            local v = json.NullableGuid()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_Type_ReturnsNonNilForRootAndArray()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local t_obj = json:Type()
            local t_arr = json.TestArray:Type()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(t_obj) .. ':' .. tostring(t_arr)
            """);
        r.String.ShouldBe("object:array");
    }

    [RedisFact]
    public async Task GetJson_Length_ReturnsArrayLength()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local len = json.TestArray:Length()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(len)
            """);
        r.String.ShouldBe("2");
    }

    [RedisFact]
    public async Task GetJson_LenMetamethod_ReturnsArrayLength()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local len = #json.TestArray
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(len)
            """);
        r.String.ShouldBe("2");
    }

    [RedisFact]
    public async Task GetJson_PathChaining_ToStringShowsAccumulatedPath()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            return tostring(json.TestArray[2].Name)
            """);
        r.String.ShouldBe($"RedisJson: {TestRun.Name("kitsune_test_json")} $.TestArray[1].Name");
    }

    // GetJson extended
    [RedisFact]
    public async Task GetJson_GetNull_ReturnsJsonNullSentinel()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local v = json.OtherProperties()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(v == Json.Null)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_GetNestedObject_ReturnsTable()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local val = json.NullableGuid()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(type(val) == 'table' and val.HasValue == false and val.Value == 'SomeGuidBlablabla')
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_GetArray_ReturnsTableWithTwoElements()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local val = json.TestArray()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(type(val) == 'table' and #val == 2)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_GetArrayElementId_ReturnsInteger()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local id1 = json.TestArray[1].Id()
            local id2 = json.TestArray[2].Id()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(id1) .. ':' .. tostring(id2)
            """);
        r.String.ShouldBe("1:2");
    }

    [RedisFact]
    public async Task GetJson_GetNonExistentKey_ReturnsNil()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_json_noexist_' .. RUN_ID)
            local json = redis:GetJson('kitsune_json_noexist_' .. RUN_ID)
            local v = json()
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_SetString_UpdatesField()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            json.NullableGuid.Value = 'UpdatedGuid'
            local v = json.NullableGuid.Value()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return v
            """);
        r.String.ShouldBe("UpdatedGuid");
    }

    [RedisFact]
    public async Task GetJson_SetNil_SetsJsonNull()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            json.SomeNumber:Set(nil)
            local v = json.SomeNumber()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(v == Json.Null)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_SetJsonNullSentinel_RoundTrips()
    {
        using KitsuneEngine engine = TestRun.NewEngine();

        // Read a null field ? get Json.Null sentinel ? assign it to another field ? reads back as Json.Null.
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local null_val = json.OtherProperties()
            json.SomeNumber:Set(null_val)
            local v = json.SomeNumber()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(v == Json.Null)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_SetTable_ReplacesNestedObject()
    {
        using KitsuneEngine engine = TestRun.NewEngine();

        // Sets each field individually (the table-encoding path); verifies both fields update.
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            json.NullableGuid.HasValue = true
            json.NullableGuid.Value = 'NewGuid'
            local has = json.NullableGuid.HasValue()
            local val = json.NullableGuid.Value()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(has) .. ':' .. val
            """);
        r.String.ShouldBe("true:NewGuid");
    }

    [RedisFact]
    public async Task GetJson_Set_Table_EncodesAndUpdates()
    {
        using KitsuneEngine engine = TestRun.NewEngine();

        // Calls Set({...}) with a Lua table and verifies the raw JSON.GET sees the change.
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            json.NullableGuid:Set({{Value='TableSet'}})
            local raw = redis:Command('JSON.GET', 'kitsune_test_json_' .. RUN_ID, '$.NullableGuid.Value')
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(raw.Value ~= nil and raw.Value:find('TableSet', 1, true) ~= nil)
        ");
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_SetViaArrayIndexNewindex_UpdatesElement()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            json.TestArray[1].Name = 'Updated Cake'
            local name = json.TestArray[1].Name()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return name
            """);
        r.String.ShouldBe("Updated Cake");
    }

    [RedisFact]
    public async Task GetJson_Delete_Root_ReturnsOneAndKeyGone()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local count = json:Delete()
            local exists = redis:Command('EXISTS', 'kitsune_test_json_' .. RUN_ID)
            return tostring(count) .. ':' .. tostring(exists.Value == 0)
            """);
        r.String.ShouldBe("1:true");
    }

    [RedisFact]
    public async Task GetJson_Delete_ReturnsDeleteCount()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local count = json.NullableGuid:Delete()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(count)
            """);
        r.String.ShouldBe("1");
    }

    [RedisFact]
    public async Task GetJson_Type_NumberField_ReturnsTypeString()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local t = json.SomeNumber:Type()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(t)
            """);
        r.String.ShouldBe("integer");
    }

    [RedisFact]
    public async Task GetJson_Type_ObjectField_ReturnsObject()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local t = json.NullableGuid:Type()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return t
            """);
        r.String.ShouldBe("object");
    }

    [RedisFact]
    public async Task GetJson_Type_ArrayField_ReturnsArray()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local t = json.TestArray:Type()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return t
            """);
        r.String.ShouldBe("array");
    }

    [RedisFact]
    public async Task GetJson_Length_NonArray_ReturnsNilOrRaises()
    {
        using KitsuneEngine engine = TestRun.NewEngine();

        // JSON.ARRLEN on a non-array path replies [null], which Length maps to nil.
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local ok, result = pcall(function()
                return json.SomeNumber:Length()
            end)
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(ok) .. ':' .. tostring(result)
            """);
        r.String.ShouldBe("true:nil");
    }

    [RedisFact]
    public async Task GetJson_ArrayIndexZero_RaisesError()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local ok, err = pcall(function()
                local _ = json.TestArray[0]
            end)
            return tostring(ok) .. ':' .. tostring(tostring(err):find('JSON array index must be >= 1 (got 0)', 1, true) ~= nil)
            """);
        r.String.ShouldBe("false:true");
    }

    [RedisFact]
    public async Task GetJson_ReusePathObject_IndependentAccess()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local guid = json.NullableGuid
            local has = guid.HasValue()
            local val = guid.Value()
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(has) .. ':' .. val
            """);
        r.String.ShouldBe("false:SomeGuidBlablabla");
    }

    [RedisFact]
    public async Task GetJson_Tostring_ArrayIndexPath_ShowsBrackets()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local s = tostring(json.TestArray[1])
            return tostring(s:find('[0]', 1, true) ~= nil)
            """);
        r.String.ShouldBe("true");
    }

    // GetJson pairs
    [RedisFact]
    public async Task GetJson_Pairs_OnObject_IteratesKeyValuePairs()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local keys = {{}}
            for k, v in pairs(json.NullableGuid) do
                keys[#keys+1] = k
            end
            table.sort(keys)
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return table.concat(keys, ',')
        ");
        r.String.ShouldBe("HasValue,Value");
    }

    [RedisFact]
    public async Task GetJson_Pairs_OnObject_ValuesAreDecodedCorrectly()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local result = {{}}
            for k, v in pairs(json.NullableGuid) do
                result[k] = tostring(v)
            end
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return result.Value .. ':' .. result.HasValue
        ");
        r.String.ShouldBe("SomeGuidBlablabla:false");
    }

    [RedisFact]
    public async Task GetJson_Pairs_OnArray_CountsAllElements()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local count = 0
            for i, v in pairs(json.TestArray) do
                count = count + 1
            end
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(count)
            """);
        r.String.ShouldBe("2");
    }

    [RedisFact]
    public async Task GetJson_Pairs_OnRoot_IteratesTopLevelKeys()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local keys = {{}}
            for k, v in pairs(json) do
                keys[#keys+1] = k
            end
            table.sort(keys)
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return table.concat(keys, ',')
        ");
        r.String.ShouldBe("NullableGuid,OtherProperties,SomeNumber,TestArray");
    }

    [RedisFact]
    public async Task GetJson_Pairs_OnMissingPath_IteratesNothing()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local count = 0
            for k, v in pairs(json.nonexistent) do
                count = count + 1
            end
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(count)
            """);
        r.String.ShouldBe("0");
    }

    [RedisFact]
    public async Task GetJson_Pairs_OnScalar_IteratesNothing()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local count = 0
            for k, v in pairs(json.SomeNumber) do
                count = count + 1
            end
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(count)
            """);
        r.String.ShouldBe("0");
    }

    [RedisFact]
    public async Task GetJson_Pairs_ViaMethod_WorksLikePairs()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json_' .. RUN_ID)
            local count = 0
            for k, v in json.NullableGuid:Pairs() do
                count = count + 1
            end
            redis:Command('JSON.DEL', 'kitsune_test_json_' .. RUN_ID)
            return tostring(count)
            """);
        r.String.ShouldBe("2");
    }

    // AliveToken integration
    [RedisFact]
    public async Task Redis_Subscribe_SetAliveToken_DoesNotRaise()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local co = assert(redis:Subscribe('kitsune_test_alive_' .. RUN_ID))
            local attached = pcall(co.SetAliveToken, co, AliveToken.New())
            local polled = coroutine.resume(co)
            local alive = coroutine.status(co)
            coroutine.resume(co, true)
            return tostring(attached) .. ':' .. tostring(polled) .. ':' .. alive .. ':' .. coroutine.status(co)
            """);
        r.String.ShouldBe("true:true:suspended:dead");
    }

    [RedisFact]
    public async Task Redis_Subscribe_AliveToken_DisposedToken_StopsCoroutine()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local co = assert(redis:Subscribe('kitsune_test_alive_' .. RUN_ID))
            local token = AliveToken.New()
            co:SetAliveToken(token)
            coroutine.resume(co)        -- first poll
            token:Dispose()
            coroutine.resume(co)        -- should stop
            return tostring(coroutine.status(co) == 'dead')
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task Redis_Subscribe_AliveToken_LiveToken_PumpContinues()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local co = assert(redis:Subscribe('kitsune_test_alive_' .. RUN_ID))
            local token = AliveToken.New()
            co:SetAliveToken(token)
            for i = 1, 5 do coroutine.resume(co) end
            local alive = coroutine.status(co) == 'suspended'
            coroutine.resume(co, true)
            return tostring(alive)
            """);
        r.String.ShouldBe("true");
    }

    // Parses KITSUNE_REDIS_TEST=host:port[:password]
    private static string Host()
    {
        return Environment.GetEnvironmentVariable("KITSUNE_REDIS_TEST")!.Split(':')[0];
    }

    private static int Port()
    {
        var parts = Environment.GetEnvironmentVariable("KITSUNE_REDIS_TEST")!.Split(':');
        return parts.Length > 1 && int.TryParse(parts[1], out int p) ? p : 6379;
    }

    private static string? Password()
    {
        var parts = Environment.GetEnvironmentVariable("KITSUNE_REDIS_TEST")!.Split(':');
        return parts.Length > 2 && parts[2].Length > 0 ? parts[2] : null;
    }

    // Returns a Lua snippet that opens a Redis connection with the test settings.
    private static string Open()
    {
        var pass = Password();
        var passLua = pass is not null ? $"'{pass}'" : "nil";
        return $"Redis.Open('{Host()}', {Port()}, false, 10, nil, {passLua})";
    }

    // GetJson
    private string JsonSetup(string key = "kitsune_test_json") =>
        $"redis:Command('JSON.SET', '{key}_' .. RUN_ID, '$', '{JsonDoc}')";
}
