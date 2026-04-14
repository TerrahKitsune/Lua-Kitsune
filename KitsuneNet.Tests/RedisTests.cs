using KitsuneNet;
using Shouldly;
using Xunit;
using Xunit.Abstractions;

namespace KitsuneNet.Tests;

[Collection("KitsuneSequential")]
public sealed class RedisTests
{
    // Sample JSON document used by all GetJson tests.
    private const string JsonDoc =
        """{"NullableGuid":{"HasValue":false,"Value":"SomeGuidBlablabla"},"OtherProperties":null,"TestArray":[{"Id":1,"Name":"Cake"},{"Id":2,"Name":"Also Cake"}],"SomeNumber":123}""";

    private readonly ITestOutputHelper _output;

    public RedisTests(ITestOutputHelper output) => _output = output;

    // -- Connection ------------------------------------------------------------
    [RedisFact]
    public async Task Open_ValidHost_ToStringStartsWithRedis()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            assert(redis, 'Open returned nil')
            return tostring(redis):sub(1, 6)
            """);
        r.String.ShouldBe("Redis:");
    }

    [RedisFact]
    public async Task Open_InvalidHost_ThrowsConnectionError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync("""
            local ok, err = pcall(Redis.Open, 'invalid.host.does.not.exist', 6379, false, 1)
            return tostring(not ok)
            """);
        r.String.ShouldBe("true");
    }

    // -- Command ---------------------------------------------------------------
    [RedisFact]
    public async Task Command_PING_ReturnsPong()
    {
        using KitsuneEngine engine = new();
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
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_cmd_sg')
            redis:Command('SET', 'kitsune_test_cmd_sg', 'hello')
            local reply = redis:Command('GET', 'kitsune_test_cmd_sg')
            redis:Command('DEL', 'kitsune_test_cmd_sg')
            return reply.Value
            """);
        r.String.ShouldBe("hello");
    }

    [RedisFact]
    public async Task Command_DEL_RemovesKey()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_cmd_del', 'x')
            local reply = redis:Command('DEL', 'kitsune_test_cmd_del')
            return tostring(reply.Value)
            """);
        r.String.ShouldBe("1");
    }

    // -- GetString -------------------------------------------------------------
    [RedisFact]
    public async Task GetString_Set_Tostring_RoundTrip()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_str')
            local str = redis:GetString('kitsune_test_str')
            str:Set('world')
            local val = tostring(str)
            redis:Command('DEL', 'kitsune_test_str')
            return val
            """);
        r.String.ShouldBe("world");
    }

    [RedisFact]
    public async Task GetString_Delete_ReturnsOldValue()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_del', 'goodbye')
            local str = redis:GetString('kitsune_test_str_del')
            return str:Delete()
            """);
        r.String.ShouldBe("goodbye");
    }

    [RedisFact]
    public async Task GetString_SetTTL_GetTTL_ReturnsPositive()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_ttl', 'temp')
            local str = redis:GetString('kitsune_test_str_ttl')
            str:SetTTL(60000)
            local ttl = str:GetTTL()
            redis:Command('DEL', 'kitsune_test_str_ttl')
            return tostring(ttl > 0)
            """);
        r.String.ShouldBe("true");
    }

    // -- GetKey ----------------------------------------------------------------
    [RedisFact]
    public async Task GetKey_Tostring_ReturnsKeyName()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local key = redis:GetKey('kitsune_test_keyname')
            return tostring(key)
            """);
        r.String.ShouldBe("kitsune_test_keyname");
    }

    [RedisFact]
    public async Task GetKey_Type_ReturnsString()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_keytype', 'v')
            local key = redis:GetKey('kitsune_test_keytype')
            local t = key:Type()
            redis:Command('DEL', 'kitsune_test_keytype')
            return t
            """);
        r.String.ShouldBe("string");
    }

    [RedisFact]
    public async Task GetKey_Delete_ReturnsTrueWhenExists()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_keydel', 'v')
            local key = redis:GetKey('kitsune_test_keydel')
            return tostring(key:Delete())
            """);
        r.String.ShouldBe("true");
    }

    // -- GetHashset ------------------------------------------------------------
    [RedisFact]
    public async Task GetHashset_SetGet_RoundTrip()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_hash')
            local hash = redis:GetHashset('kitsune_test_hash')
            hash['field1'] = 'alpha'
            local v = hash['field1']
            redis:Command('DEL', 'kitsune_test_hash')
            return v
            """);
        r.String.ShouldBe("alpha");
    }

    [RedisFact]
    public async Task GetHashset_DeleteField_ReturnsNil()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('HSET', 'kitsune_test_hash_del', 'f', 'v')
            local hash = redis:GetHashset('kitsune_test_hash_del')
            hash['f'] = nil
            local v = hash['f']
            redis:Command('DEL', 'kitsune_test_hash_del')
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    // -- GetList ---------------------------------------------------------------
    [RedisFact]
    public async Task GetList_RPUSH_ThenIndex_ReturnsValues()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list')
            redis:Command('RPUSH', 'kitsune_test_list', 'first', 'second', 'third')
            local list = redis:GetList('kitsune_test_list')
            local v1 = list[1]
            local v2 = list[2]
            redis:Command('DEL', 'kitsune_test_list')
            return v1 .. ':' .. v2
            """);
        r.String.ShouldBe("first:second");
    }

    // -- GetSet ----------------------------------------------------------------
    [RedisFact]
    public async Task GetSet_SADD_IsMember_ReturnsTrueAndFalse()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set')
            redis:Command('SADD', 'kitsune_test_set', 'apple', 'banana', 'cherry')
            local set = redis:GetSet('kitsune_test_set')
            local has     = set['banana']
            local missing = set['grape']
            redis:Command('DEL', 'kitsune_test_set')
            return tostring(has) .. ':' .. tostring(missing)
            """);
        r.String.ShouldBe("true:false");
    }

    // -- GetSortedSet ----------------------------------------------------------
    [RedisFact]
    public async Task GetSortedSet_ZADD_IndexByRank_ReturnsLowestScoreMember()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_zset')
            redis:Command('ZADD', 'kitsune_test_zset', '10', 'memberA')
            redis:Command('ZADD', 'kitsune_test_zset', '20', 'memberB')
            local zset = redis:GetSortedSet('kitsune_test_zset')
            local first = zset[1]
            redis:Command('DEL', 'kitsune_test_zset')
            return first
            """);
        r.String.ShouldBe("memberA");
    }

    // -- GetStream -------------------------------------------------------------
    [RedisFact]
    public async Task GetStream_Add_Then_Read_ReturnsEntry()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_stream')
            local stream = redis:GetStream('kitsune_test_stream')
            local id = stream:Add({{f1='hello', f2='world'}})
            local entry_id, fields = stream:Read()
            redis:Command('DEL', 'kitsune_test_stream')
            return tostring(id ~= nil) .. ':' .. tostring(entry_id ~= nil) .. ':' .. tostring(fields ~= nil)
        ");
        r.String.ShouldBe("true:true:true");
    }

    // -- Key iterator ----------------------------------------------------------
    [RedisFact]
    public async Task KeyIterator_SCAN_FindsInsertedKeys()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_iter_1', '1')
            redis:Command('SET', 'kitsune_iter_2', '2')
            redis:Command('SET', 'kitsune_iter_3', '3')
            local found = 0
            for key in redis, redis, nil do
                local name = tostring(key)
                if name:find('kitsune_iter_', 1, true) then
                    found = found + 1
                end
            end
            redis:Command('DEL', 'kitsune_iter_1')
            redis:Command('DEL', 'kitsune_iter_2')
            redis:Command('DEL', 'kitsune_iter_3')
            return tostring(found >= 3)
            """);
        r.String.ShouldBe("true");
    }

    // -- Subscribe -------------------------------------------------------------
    [RedisFact]
    public async Task Subscribe_ReturnsThread()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local co = redis:Subscribe('kitsune_sub_type_test')
            local t = type(co)
            coroutine.resume(co, true)
            return t
            """);
        r.String.ShouldBe("thread");
    }

    [RedisFact]
    public async Task PSubscribe_ReturnsThread()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local co = redis:PSubscribe('kitsune_psub_*')
            local t = type(co)
            coroutine.resume(co, true)
            return t
            """);
        r.String.ShouldBe("thread");
    }

    [RedisFact]
    public async Task Subscribe_RoundTrip_ReceivesMessage()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local channel = 'kitsune_pubsub_rt'
            local unique  = 'msg-' .. tostring(Time())

            local sub = {Open()}
            local co  = sub:Subscribe(channel)

            -- Drain subscribe ack frames before publishing.
            for i = 1, 5 do
                coroutine.resume(co, false)
                Sleep(10)
            end

            -- Publish from a separate connection.
            local pub = {Open()}
            pub:Command('PUBLISH', channel, unique)

            -- Poll until the message arrives or a 5-second deadline is reached.
            local deadline = Time() + 5000
            local result   = nil
            while Time() < deadline do
                local ok, ch, msg = coroutine.resume(co, false)
                if not ok then error(tostring(ch)) end
                if ch == channel and msg == unique then
                    result = ch .. ':' .. msg
                    break
                end
                Sleep(1)
            end

            coroutine.resume(co, true)
            return result or 'timeout'
            """);
        r.ShouldNotBe("timeout");
        r.String.ShouldStartWith("kitsune_pubsub_rt:");
    }

    [RedisFact]
    public async Task PSubscribe_RoundTrip_ReceivesMessageWithPattern()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local pattern = 'kitsune_pps_*'
            local channel = 'kitsune_pps_test'
            local unique  = 'msg-' .. tostring(Time())

            local sub = {Open()}
            local co  = sub:PSubscribe(pattern)

            -- Drain psubscribe ack frames before publishing.
            for i = 1, 5 do
                coroutine.resume(co, false)
                Sleep(10)
            end

            local pub = {Open()}
            pub:Command('PUBLISH', channel, unique)

            local deadline = Time() + 5000
            local result   = nil
            while Time() < deadline do
                local ok, pat, ch, msg = coroutine.resume(co, false)
                if not ok then error(tostring(pat)) end
                if ch == channel and msg == unique then
                    result = pat .. ':' .. ch .. ':' .. msg
                    break
                end
                Sleep(1)
            end

            coroutine.resume(co, true)
            return result or 'timeout'
            """);
        r.ShouldNotBe("timeout");
        r.String.ShouldStartWith("kitsune_pps_*:kitsune_pps_test:");
    }

    // -- RedisString extended --------------------------------------------------
    [RedisFact]
    public async Task GetString_Set_ReturnsOldValue()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_prev', 'old')
            local str = redis:GetString('kitsune_test_str_prev')
            local old = str:Set('new')
            redis:Command('DEL', 'kitsune_test_str_prev')
            return old
            """);
        r.String.ShouldBe("old");
    }

    [RedisFact]
    public async Task GetString_Set_ReturnsNilWhenKeyDidNotExist()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_str_nonexist')
            local str = redis:GetString('kitsune_test_str_nonexist')
            local old = str:Set('first')
            redis:Command('DEL', 'kitsune_test_str_nonexist')
            return tostring(old)
            """);
        r.String.ShouldBe("nil");
    }

    [RedisFact]
    public async Task GetString_Len_ReturnsStrlen()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_len', 'hello')
            local str = redis:GetString('kitsune_test_str_len')
            local n = #str
            redis:Command('DEL', 'kitsune_test_str_len')
            return tostring(n)
            """);
        r.String.ShouldBe("5");
    }

    [RedisFact]
    public async Task GetString_At_ReturnsByte()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_at', 'ABC')
            local str = redis:GetString('kitsune_test_str_at')
            local b = str:At(1)
            redis:Command('DEL', 'kitsune_test_str_at')
            return tostring(b)
            """);
        r.String.ShouldBe("65"); // 'A'
    }

    [RedisFact]
    public async Task GetString_IndexRead_ReturnsByte()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_idxr', 'ABC')
            local str = redis:GetString('kitsune_test_str_idxr')
            local b = str[2]
            redis:Command('DEL', 'kitsune_test_str_idxr')
            return tostring(b)
            """);
        r.String.ShouldBe("66"); // 'B'
    }

    [RedisFact]
    public async Task GetString_IndexWrite_SetsByte()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_idxw', 'ABC')
            local str = redis:GetString('kitsune_test_str_idxw')
            str[1] = 88  -- 'X'
            local val = tostring(str)
            redis:Command('DEL', 'kitsune_test_str_idxw')
            return val
            """);
        r.String.ShouldBe("XBC");
    }

    [RedisFact]
    public async Task GetString_Concat_ProducesJoinedString()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_cc', 'Hello')
            local str = redis:GetString('kitsune_test_str_cc')
            local result = str .. ', world'
            redis:Command('DEL', 'kitsune_test_str_cc')
            return result
            """);
        r.String.ShouldBe("Hello, world");
    }

    [RedisFact]
    public async Task GetString_GetOrSet_ReturnsExistingValue()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_gos', 'existing')
            local str = redis:GetString('kitsune_test_str_gos')
            local v = str:GetOrSet('new')
            redis:Command('DEL', 'kitsune_test_str_gos')
            return v
            """);
        r.String.ShouldBe("existing");
    }

    [RedisFact]
    public async Task GetString_GetOrSet_SetsAndReturnsNewWhenMissing()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_str_gos2')
            local str = redis:GetString('kitsune_test_str_gos2')
            local v = str:GetOrSet('default')
            redis:Command('DEL', 'kitsune_test_str_gos2')
            return v
            """);
        r.String.ShouldBe("default");
    }

    [RedisFact]
    public async Task GetString_GetTTL_ReturnsNegativeOneWhenNoPersist()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_nottl', 'v')
            local str = redis:GetString('kitsune_test_str_nottl')
            local ttl = str:GetTTL()
            redis:Command('DEL', 'kitsune_test_str_nottl')
            return tostring(ttl)
            """);
        r.String.ShouldBe("-1");
    }

    [RedisFact]
    public async Task GetString_SetTTL_Zero_RemovesTTL()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_persist', 'v')
            local str = redis:GetString('kitsune_test_str_persist')
            str:SetTTL(60000)
            str:SetTTL(0)
            local ttl = str:GetTTL()
            redis:Command('DEL', 'kitsune_test_str_persist')
            return tostring(ttl)
            """);
        r.String.ShouldBe("-1");
    }

    [RedisFact]
    public async Task GetString_Pairs_IteratesBytes()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_pairs', 'AB')
            local str = redis:GetString('kitsune_test_str_pairs')
            local bytes = {{}}
            for i, b in pairs(str) do
                bytes[i] = b
            end
            redis:Command('DEL', 'kitsune_test_str_pairs')
            return tostring(bytes[1]) .. ':' .. tostring(bytes[2])
        ");
        r.String.ShouldBe("65:66"); // 'A'=65, 'B'=66
    }

    [RedisFact]
    public async Task GetString_Call_ReturnsRedisKey()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_call', 'v')
            local str = redis:GetString('kitsune_test_str_call')
            local key = str()
            redis:Command('DEL', 'kitsune_test_str_call')
            return tostring(key)
            """);
        r.String.ShouldBe("kitsune_test_str_call");
    }

    // -- RedisKey extended -----------------------------------------------------
    [RedisFact]
    public async Task GetKey_SetTTL_GetTTL_RoundTrip()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_key_ttl', 'v')
            local key = redis:GetKey('kitsune_test_key_ttl')
            key:SetTTL(60000)
            local ttl = key:GetTTL()
            redis:Command('DEL', 'kitsune_test_key_ttl')
            return tostring(ttl > 0)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetKey_SetTTL_Zero_RemovesTTL()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_key_persist', 'v')
            local key = redis:GetKey('kitsune_test_key_persist')
            key:SetTTL(60000)
            key:SetTTL(0)
            local ttl = key:GetTTL()
            redis:Command('DEL', 'kitsune_test_key_persist')
            return tostring(ttl)
            """);
        r.String.ShouldBe("-1");
    }

    [RedisFact]
    public async Task GetKey_Type_ReturnsHash()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('HSET', 'kitsune_test_key_hashtype', 'f', 'v')
            local key = redis:GetKey('kitsune_test_key_hashtype')
            local t = key:Type()
            redis:Command('DEL', 'kitsune_test_key_hashtype')
            return t
            """);
        r.String.ShouldBe("hash");
    }

    [RedisFact]
    public async Task GetKey_Type_ReturnsList()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('RPUSH', 'kitsune_test_key_listtype', 'v')
            local key = redis:GetKey('kitsune_test_key_listtype')
            local t = key:Type()
            redis:Command('DEL', 'kitsune_test_key_listtype')
            return t
            """);
        r.String.ShouldBe("list");
    }

    [RedisFact]
    public async Task GetKey_Type_ReturnsNoneWhenMissing()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_key_noexist')
            local key = redis:GetKey('kitsune_test_key_noexist')
            return key:Type()
            """);
        r.String.ShouldBe("none");
    }

    // -- GetHashset extended ---------------------------------------------------
    [RedisFact]
    public async Task GetHashset_MultipleFields_RoundTrip()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_hash_multi')
            local hash = redis:GetHashset('kitsune_test_hash_multi')
            hash['a'] = 'alpha'
            hash['b'] = 'beta'
            hash['c'] = 'gamma'
            local a = hash['a']
            local b = hash['b']
            local c = hash['c']
            redis:Command('DEL', 'kitsune_test_hash_multi')
            return a .. ':' .. b .. ':' .. c
            """);
        r.String.ShouldBe("alpha:beta:gamma");
    }

    [RedisFact]
    public async Task GetHashset_Pairs_IteratesAllFields()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_hash_pairs')
            redis:Command('HSET', 'kitsune_test_hash_pairs', 'x', '1', 'y', '2', 'z', '3')
            local hash = redis:GetHashset('kitsune_test_hash_pairs')
            local count = 0
            for k, v in pairs(hash) do
                count = count + 1
            end
            redis:Command('DEL', 'kitsune_test_hash_pairs')
            return tostring(count)
            """);
        r.String.ShouldBe("3");
    }

    [RedisFact]
    public async Task GetHashset_Tostring_ContainsKeyName()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local hash = redis:GetHashset('kitsune_test_hash_ts')
            local s = tostring(hash)
            return tostring(s:find('kitsune_test_hash_ts', 1, true) ~= nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetHashset_Call_ReturnsKeyAndType()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local hash = redis:GetHashset('kitsune_test_hash_call')
            local key, t = hash()
            return tostring(key) .. ':' .. tostring(t)
            """);
        r.String.ShouldBe("kitsune_test_hash_call:1");
    }

    // -- GetList extended ------------------------------------------------------
    [RedisFact]
    public async Task GetList_Len_ReturnsCount()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_len')
            redis:Command('RPUSH', 'kitsune_test_list_len', 'a', 'b', 'c')
            local list = redis:GetList('kitsune_test_list_len')
            local n = #list
            redis:Command('DEL', 'kitsune_test_list_len')
            return tostring(n)
            """);
        r.String.ShouldBe("3");
    }

    [RedisFact]
    public async Task GetList_IndexZero_LPops()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_lpop')
            redis:Command('RPUSH', 'kitsune_test_list_lpop', 'first', 'second')
            local list = redis:GetList('kitsune_test_list_lpop')
            local v = list[0]
            redis:Command('DEL', 'kitsune_test_list_lpop')
            return v
            """);
        r.String.ShouldBe("first");
    }

    [RedisFact]
    public async Task GetList_NegativeIndex_ReturnsFromEnd()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_neg')
            redis:Command('RPUSH', 'kitsune_test_list_neg', 'a', 'b', 'c')
            local list = redis:GetList('kitsune_test_list_neg')
            local v = list[-1]
            redis:Command('DEL', 'kitsune_test_list_neg')
            return v
            """);
        r.String.ShouldBe("c");
    }

    [RedisFact]
    public async Task GetList_AssignIndexZero_AppendsByRpush()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_push')
            local list = redis:GetList('kitsune_test_list_push')
            list[0] = 'appended'
            local v = list[1]
            redis:Command('DEL', 'kitsune_test_list_push')
            return v
            """);
        r.String.ShouldBe("appended");
    }

    [RedisFact]
    public async Task GetList_AssignExistingIndex_UpdatesViaLset()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_lset')
            redis:Command('RPUSH', 'kitsune_test_list_lset', 'old', 'b')
            local list = redis:GetList('kitsune_test_list_lset')
            list[1] = 'new'
            local v = list[1]
            redis:Command('DEL', 'kitsune_test_list_lset')
            return v
            """);
        r.String.ShouldBe("new");
    }

    [RedisFact]
    public async Task GetList_Pairs_IteratesAll()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_pairs')
            redis:Command('RPUSH', 'kitsune_test_list_pairs', 'x', 'y', 'z')
            local list = redis:GetList('kitsune_test_list_pairs')
            local found = {{}}
            for i, v in pairs(list) do
                found[i] = v
            end
            redis:Command('DEL', 'kitsune_test_list_pairs')
            return found[1] .. ':' .. found[2] .. ':' .. found[3]
        ");
        r.String.ShouldBe("x:y:z");
    }

    [RedisFact]
    public async Task GetList_Tostring_ContainsKeyName()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local list = redis:GetList('kitsune_test_list_ts')
            local s = tostring(list)
            return tostring(s:find('kitsune_test_list_ts', 1, true) ~= nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetList_Call_ReturnsKeyAndType()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local list = redis:GetList('kitsune_test_list_call')
            local key, t = list()
            return tostring(key) .. ':' .. tostring(t)
            """);
        r.String.ShouldBe("kitsune_test_list_call:2");
    }

    // -- GetSet extended -------------------------------------------------------
    [RedisFact]
    public async Task GetSet_Srandmember_ReturnsAMember()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_rand')
            redis:Command('SADD', 'kitsune_test_set_rand', 'apple', 'banana')
            local set = redis:GetSet('kitsune_test_set_rand')
            local v = set[0]
            redis:Command('DEL', 'kitsune_test_set_rand')
            return tostring(v == 'apple' or v == 'banana')
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSet_Spop_RemovesAndReturnsMember()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_pop')
            redis:Command('SADD', 'kitsune_test_set_pop', 'only')
            local set = redis:GetSet('kitsune_test_set_pop')
            local v = set[-1]
            local remaining = redis:Command('SCARD', 'kitsune_test_set_pop')
            redis:Command('DEL', 'kitsune_test_set_pop')
            return v .. ':' .. tostring(remaining.Value)
            """);
        r.String.ShouldBe("only:0");
    }

    [RedisFact]
    public async Task GetSet_AssignTrue_AddsMember()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_add')
            local set = redis:GetSet('kitsune_test_set_add')
            set['mango'] = true
            local has = set['mango']
            redis:Command('DEL', 'kitsune_test_set_add')
            return tostring(has)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSet_AssignFalse_RemovesMember()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SADD', 'kitsune_test_set_remf', 'peach')
            local set = redis:GetSet('kitsune_test_set_remf')
            set['peach'] = false
            local has = set['peach']
            redis:Command('DEL', 'kitsune_test_set_remf')
            return tostring(has)
            """);
        r.String.ShouldBe("false");
    }

    [RedisFact]
    public async Task GetSet_AssignNil_RemovesMember()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SADD', 'kitsune_test_set_remn', 'plum')
            local set = redis:GetSet('kitsune_test_set_remn')
            set['plum'] = nil
            local has = set['plum']
            redis:Command('DEL', 'kitsune_test_set_remn')
            return tostring(has)
            """);
        r.String.ShouldBe("false");
    }

    [RedisFact]
    public async Task GetSet_Len_ReturnsScard()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_len')
            redis:Command('SADD', 'kitsune_test_set_len', 'a', 'b', 'c', 'd')
            local set = redis:GetSet('kitsune_test_set_len')
            local n = #set
            redis:Command('DEL', 'kitsune_test_set_len')
            return tostring(n)
            """);
        r.String.ShouldBe("4");
    }

    [RedisFact]
    public async Task GetSet_IndexedAccess_ReturnsMember()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_idx')
            redis:Command('SADD', 'kitsune_test_set_idx', 'solo')
            local set = redis:GetSet('kitsune_test_set_idx')
            local v = set[1]
            redis:Command('DEL', 'kitsune_test_set_idx')
            return v
            """);
        r.String.ShouldBe("solo");
    }

    [RedisFact]
    public async Task GetSet_Pairs_IteratesAll()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_pairs')
            redis:Command('SADD', 'kitsune_test_set_pairs', 'x', 'y', 'z')
            local set = redis:GetSet('kitsune_test_set_pairs')
            local count = 0
            for i, v in pairs(set) do
                count = count + 1
            end
            redis:Command('DEL', 'kitsune_test_set_pairs')
            return tostring(count)
            """);
        r.String.ShouldBe("3");
    }

    [RedisFact]
    public async Task GetSet_Tostring_ContainsKeyName()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local set = redis:GetSet('kitsune_test_set_ts')
            local s = tostring(set)
            return tostring(s:find('kitsune_test_set_ts', 1, true) ~= nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSet_Call_ReturnsKeyAndType()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local set = redis:GetSet('kitsune_test_set_call')
            local key, t = set()
            return tostring(key) .. ':' .. tostring(t)
            """);
        r.String.ShouldBe("kitsune_test_set_call:3");
    }

    // -- GetSortedSet extended -------------------------------------------------
    [RedisFact]
    public async Task GetSortedSet_GetScore_ByMemberName()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_zset_score')
            redis:Command('ZADD', 'kitsune_test_zset_score', '42', 'alpha')
            local zset = redis:GetSortedSet('kitsune_test_zset_score')
            local score = zset['alpha']
            redis:Command('DEL', 'kitsune_test_zset_score')
            return tostring(score)
            """);
        r.String.ShouldBe("42");
    }

    [RedisFact]
    public async Task GetSortedSet_AddViaAssignment_AddsToSet()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_zset_add')
            local zset = redis:GetSortedSet('kitsune_test_zset_add')
            zset['alpha'] = 99
            local score = zset['alpha']
            redis:Command('DEL', 'kitsune_test_zset_add')
            return tostring(score)
            """);
        r.String.ShouldBe("99");
    }

    [RedisFact]
    public async Task GetSortedSet_RemoveViaNil_DeletesMember()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_zset_rem')
            redis:Command('ZADD', 'kitsune_test_zset_rem', '5', 'beta')
            local zset = redis:GetSortedSet('kitsune_test_zset_rem')
            zset['beta'] = nil
            local score = zset['beta']
            redis:Command('DEL', 'kitsune_test_zset_rem')
            return tostring(score == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSortedSet_Pairs_YieldsMembersWithScores()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_zset_pairs')
            redis:Command('ZADD', 'kitsune_test_zset_pairs', '10', 'a')
            redis:Command('ZADD', 'kitsune_test_zset_pairs', '20', 'b')
            local zset = redis:GetSortedSet('kitsune_test_zset_pairs')
            local members = {{}}
            for member, score in pairs(zset) do
                members[member] = score
            end
            redis:Command('DEL', 'kitsune_test_zset_pairs')
            return tostring(members['a']) .. ':' .. tostring(members['b'])
        ");
        r.String.ShouldBe("10:20");
    }

    [RedisFact]
    public async Task GetSortedSet_Tostring_ContainsKeyName()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local zset = redis:GetSortedSet('kitsune_test_zset_ts')
            local s = tostring(zset)
            return tostring(s:find('kitsune_test_zset_ts', 1, true) ~= nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSortedSet_Call_ReturnsKeyAndType()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local zset = redis:GetSortedSet('kitsune_test_zset_call')
            local key, t = zset()
            return tostring(key) .. ':' .. tostring(t)
            """);
        r.String.ShouldBe("kitsune_test_zset_call:4");
    }

    // -- GetStream extended ----------------------------------------------------
    [RedisFact]
    public async Task GetStream_Add_IdIsNonEmptyString()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_stream_id')
            local stream = redis:GetStream('kitsune_test_stream_id')
            local id = stream:Add({{k='v'}})
            redis:Command('DEL', 'kitsune_test_stream_id')
            return tostring(type(id) == 'string' and #id > 0)
        ");
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetStream_MultipleAdd_ReadSequential()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_stream_seq')
            local stream = redis:GetStream('kitsune_test_stream_seq')
            local id1 = stream:Add({{msg='first'}})
            stream:Add({{msg='second'}})
            local eid, fields = stream:Read()
            local eid2, fields2 = stream:Read(eid)
            redis:Command('DEL', 'kitsune_test_stream_seq')
            return fields.msg .. ':' .. fields2.msg
        ");
        r.String.ShouldBe("first:second");
    }

    [RedisFact]
    public async Task GetStream_Trim_ReducesLength()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_stream_trim')
            local stream = redis:GetStream('kitsune_test_stream_trim')
            stream:Add({{n='1'}})
            stream:Add({{n='2'}})
            stream:Add({{n='3'}})
            local trimmed = stream:Trim(1)
            local len = redis:Command('XLEN', 'kitsune_test_stream_trim')
            redis:Command('DEL', 'kitsune_test_stream_trim')
            return tostring(trimmed >= 0) .. ':' .. tostring(len.Value == 1)
        ");
        r.String.ShouldBe("true:true");
    }

    [RedisFact]
    public async Task GetStream_Tostring_ContainsKeyName()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local stream = redis:GetStream('kitsune_test_stream_ts')
            local s = tostring(stream)
            return tostring(s:find('kitsune_test_stream_ts', 1, true) ~= nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetStream_Call_ReturnsRedisKey()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local stream = redis:GetStream('kitsune_test_stream_call')
            local key = stream()
            return tostring(key)
            """);
        r.String.ShouldBe("kitsune_test_stream_call");
    }

    // -- Command reply ---------------------------------------------------------
    [RedisFact]
    public async Task Command_GET_NonExistentKey_NilValue()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_reply_nil')
            local reply = redis:Command('GET', 'kitsune_test_reply_nil')
            return tostring(reply.Value == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task Command_SMEMBERS_ValueIsTable()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_reply_arr')
            redis:Command('SADD', 'kitsune_test_reply_arr', 'x', 'y')
            local reply = redis:Command('SMEMBERS', 'kitsune_test_reply_arr')
            redis:Command('DEL', 'kitsune_test_reply_arr')
            return tostring(type(reply.Value) == 'table' and #reply.Value == 2)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task Command_INCR_ReturnsInteger()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_reply_int')
            redis:Command('SET', 'kitsune_test_reply_int', '10')
            local reply = redis:Command('INCR', 'kitsune_test_reply_int')
            redis:Command('DEL', 'kitsune_test_reply_int')
            return tostring(reply.Value)
            """);
        r.String.ShouldBe("11");
    }

    [RedisFact]
    public async Task GetStream_Read_EmptyStream_ReturnsNilPair()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_stream_empty')
            local stream = redis:GetStream('kitsune_test_stream_empty')
            local eid, fields = stream:Read()
            return tostring(eid == nil and fields == nil)
            """);
        r.String.ShouldBe("true");
    }

    // -- Command validation ----------------------------------------------------
    [RedisFact]
    public async Task Command_NilArgument_Raises()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local ok = pcall(function()
                redis:Command('SET', nil, 'value')
            end)
            return tostring(not ok)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task Command_BooleanArgument_Raises()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local ok = pcall(function()
                redis:Command('SET', 'key', true)
            end)
            return tostring(not ok)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task Command_WRONGTYPE_Raises()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_wt_list')
            redis:Command('RPUSH', 'kitsune_test_wt_list', 'item')
            local ok = pcall(function()
                redis:Command('GET', 'kitsune_test_wt_list')
            end)
            redis:Command('DEL', 'kitsune_test_wt_list')
            return tostring(not ok)
            """);
        r.String.ShouldBe("true");
    }

    // -- GetString extended (new paths) ----------------------------------------
    [RedisFact]
    public async Task GetString_GetSet_IsAliasOfGetOrSet()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_getset_alias', 'original')
            local str = redis:GetString('kitsune_test_str_getset_alias')
            local old = str:GetSet('replaced')
            redis:Command('DEL', 'kitsune_test_str_getset_alias')
            return old
            """);
        r.String.ShouldBe("original");
    }

    [RedisFact]
    public async Task GetString_Concat_TwoRedisStrings()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_cat_a', 'foo')
            redis:Command('SET', 'kitsune_test_str_cat_b', 'bar')
            local a = redis:GetString('kitsune_test_str_cat_a')
            local b = redis:GetString('kitsune_test_str_cat_b')
            local result = a .. b
            redis:Command('DEL', 'kitsune_test_str_cat_a', 'kitsune_test_str_cat_b')
            return result
            """);
        r.String.ShouldBe("foobar");
    }

    [RedisFact]
    public async Task GetString_At_OutOfRange_ReturnsNil()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('SET', 'kitsune_test_str_at_oor', 'Hi')
            local str = redis:GetString('kitsune_test_str_at_oor')
            local b = str:At(100)
            redis:Command('DEL', 'kitsune_test_str_at_oor')
            return tostring(b == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetString_Delete_NonExistentKey_ReturnsNil()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_str_del_nokey')
            local str = redis:GetString('kitsune_test_str_del_nokey')
            local v = str:Delete()
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetString_GetTTL_NonExistentKey_ReturnsNil()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_str_ttl_nokey')
            local str = redis:GetString('kitsune_test_str_ttl_nokey')
            return tostring(str:GetTTL() == nil)
            """);
        r.String.ShouldBe("true");
    }

    // -- GetKey extended (new paths) -------------------------------------------
    [RedisFact]
    public async Task GetKey_GetTTL_NonExistentKey_ReturnsNil()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_key_ttl_noexist')
            local key = redis:GetKey('kitsune_test_key_ttl_noexist')
            return tostring(key:GetTTL() == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetKey_SetTTL_NonExistentKey_ReturnsFalse()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_key_pexpire_nokey')
            local key = redis:GetKey('kitsune_test_key_pexpire_nokey')
            return tostring(key:SetTTL(60000))
            """);
        r.String.ShouldBe("false");
    }

    // -- GetHashset extended (new paths) --------------------------------------
    [RedisFact]
    public async Task GetHashset_StoreAndRetrieveNumber()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_hash_num')
            local hash = redis:GetHashset('kitsune_test_hash_num')
            hash['count'] = 42
            local v = hash['count']
            redis:Command('DEL', 'kitsune_test_hash_num')
            return v
            """);
        r.String.ShouldBe("42");
    }

    [RedisFact]
    public async Task GetHashset_StoreTable_IsJsonEncoded()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_hash_json')
            local hash = redis:GetHashset('kitsune_test_hash_json')
            hash['data'] = {{x=1, y=2}}
            local v = hash['data']
            redis:Command('DEL', 'kitsune_test_hash_json')
            return tostring(type(v) == 'string' and v:find('1', 1, true) ~= nil)
        ");
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetHashset_Len_ReturnsZero()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('HSET', 'kitsune_test_hash_lenz', 'f', 'v')
            local hash = redis:GetHashset('kitsune_test_hash_lenz')
            local n = #hash
            redis:Command('DEL', 'kitsune_test_hash_lenz')
            return tostring(n)
            """);
        r.String.ShouldBe("0");
    }

    // -- GetList extended (new paths) ------------------------------------------
    [RedisFact]
    public async Task GetList_Len_EmptyList_ReturnsZero()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_empty')
            local list = redis:GetList('kitsune_test_list_empty')
            return tostring(#list)
            """);
        r.String.ShouldBe("0");
    }

    [RedisFact]
    public async Task GetList_IndexZero_EmptyList_ReturnsNil()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_lpop_empty')
            local list = redis:GetList('kitsune_test_list_lpop_empty')
            local v = list[0]
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetList_AssignNegativeIndex_PrependsByLpush()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_list_neg_push')
            redis:Command('RPUSH', 'kitsune_test_list_neg_push', 'a', 'b', 'c')
            local list = redis:GetList('kitsune_test_list_neg_push')
            list[-5] = 'prepended'
            local first = list[1]
            redis:Command('DEL', 'kitsune_test_list_neg_push')
            return first
            """);
        r.String.ShouldBe("prepended");
    }

    // -- GetSet extended (new paths) -------------------------------------------
    [RedisFact]
    public async Task GetSet_IndexedAccess_BeyondSize_ReturnsNil()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_oob')
            redis:Command('SADD', 'kitsune_test_set_oob', 'a', 'b')
            local set = redis:GetSet('kitsune_test_set_oob')
            local v = set[10]
            redis:Command('DEL', 'kitsune_test_set_oob')
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSet_MutationInvalidatesScanCache()
    {
        using KitsuneEngine engine = new();

        // set[1] fills the SSCAN cache; set[2] keeps it alive (2 of 3).
        // After SREM the cache must be cleared so set[3] re-scans the
        // now-2-member set and returns nil instead of a stale third element.
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_set_stale')
            redis:Command('SADD', 'kitsune_test_set_stale', 'alpha', 'beta', 'gamma')
            local set = redis:GetSet('kitsune_test_set_stale')
            local m1 = set[1]
            local m2 = set[2]
            set['beta'] = nil
            local third = set[3]
            redis:Command('DEL', 'kitsune_test_set_stale')
            return tostring(third == nil)
            """);
        r.String.ShouldBe("true");
    }

    // -- GetSortedSet extended (new paths) -------------------------------------
    [RedisFact]
    public async Task GetSortedSet_IndexByRank_OutOfRange_ReturnsNil()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_zset_oor')
            redis:Command('ZADD', 'kitsune_test_zset_oor', '1', 'a')
            local zset = redis:GetSortedSet('kitsune_test_zset_oor')
            local v = zset[100]
            redis:Command('DEL', 'kitsune_test_zset_oor')
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSortedSet_Score_NonExistentMember_ReturnsNil()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_test_zset_noscore')
            redis:Command('ZADD', 'kitsune_test_zset_noscore', '5', 'member')
            local zset = redis:GetSortedSet('kitsune_test_zset_noscore')
            local score = zset['nonexistent']
            redis:Command('DEL', 'kitsune_test_zset_noscore')
            return tostring(score == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetSortedSet_Len_ReturnsZero()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('ZADD', 'kitsune_test_zset_lenz', '1', 'a')
            local zset = redis:GetSortedSet('kitsune_test_zset_lenz')
            local n = #zset
            redis:Command('DEL', 'kitsune_test_zset_lenz')
            return tostring(n)
            """);
        r.String.ShouldBe("0");
    }

    [RedisFact]
    public async Task GetSortedSet_Pairs_FloatScoreRoundTrips()
    {
        using KitsuneEngine engine = new();

        // Verifies that lua_stringtonumber is used (not atoll) so fractional scores survive.
        LuaValue r = await engine.ExecuteStringAsync($$"""
            local redis = {{Open()}}
            redis:Command('DEL', 'kitsune_test_zset_float')
            redis:Command('ZADD', 'kitsune_test_zset_float', '1.5', 'a')
            redis:Command('ZADD', 'kitsune_test_zset_float', '2', 'b')
            local zset = redis:GetSortedSet('kitsune_test_zset_float')
            local scores = {}
            for member, score in pairs(zset) do
                scores[member] = score
            end
            redis:Command('DEL', 'kitsune_test_zset_float')
            return tostring(scores['a']) .. ':' .. tostring(scores['b'])
            """);
        r.String.ShouldBe("1.5:2");
    }

    [RedisFact]
    public async Task GetJson_Tostring_ShowsKeyAndPath()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local json = redis:GetJson('kitsune_test_json')
            local s = tostring(json)
            return tostring(s:find('kitsune_test_json', 1, true) ~= nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_GetScalar_ReturnsNumber()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local n = json.SomeNumber()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(n)
            """);
        r.String.ShouldBe("123");
    }

    [RedisFact]
    public async Task GetJson_GetNestedBool_ReturnsFalse()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local v = json.NullableGuid.HasValue()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(v)
            """);
        r.String.ShouldBe("false");
    }

    [RedisFact]
    public async Task GetJson_GetNestedString_ReturnsValue()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local v = json.NullableGuid.Value()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return v
            """);
        r.String.ShouldBe("SomeGuidBlablabla");
    }

    [RedisFact]
    public async Task GetJson_GetArrayElement_ByLuaIndex_ReturnsSecondEntry()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local name = json.TestArray[2].Name()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return name
            """);
        r.String.ShouldBe("Also Cake");
    }

    [RedisFact]
    public async Task GetJson_GetArrayElement_FirstEntry()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local name = json.TestArray[1].Name()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return name
            """);
        r.String.ShouldBe("Cake");
    }

    [RedisFact]
    public async Task GetJson_GetMissingPath_ReturnsNil()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local v = json.nonexistent()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_Get_Root_ReturnsTable()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local doc = json()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(type(doc) == 'table' and doc.SomeNumber == 123)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_SetViaMethod_UpdatesScalar()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            json.SomeNumber:Set(456)
            local v = json.SomeNumber()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(v)
            """);
        r.String.ShouldBe("456");
    }

    [RedisFact]
    public async Task GetJson_SetViaNewindex_UpdatesScalar()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            json.SomeNumber = 789
            local v = json.SomeNumber()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(v)
            """);
        r.String.ShouldBe("789");
    }

    [RedisFact]
    public async Task GetJson_SetNestedViaNewindex_UpdatesField()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            json.NullableGuid.HasValue = true
            local v = json.NullableGuid.HasValue()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(v)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_Delete_DeletesPath()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            json.NullableGuid:Delete()
            local v = json.NullableGuid()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_Type_ReturnsNonNilForRootAndArray()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local t_obj = json:Type()
            local t_arr = json.TestArray:Type()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(t_obj ~= nil) .. ':' .. tostring(t_arr ~= nil)
            """);
        r.String.ShouldBe("true:true");
    }

    [RedisFact]
    public async Task GetJson_Length_ReturnsArrayLength()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local len = json.TestArray:Length()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(len)
            """);
        r.String.ShouldBe("2");
    }

    [RedisFact]
    public async Task GetJson_LenMetamethod_ReturnsArrayLength()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local len = #json.TestArray
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(len)
            """);
        r.String.ShouldBe("2");
    }

    [RedisFact]
    public async Task GetJson_PathChaining_ToStringShowsAccumulatedPath()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local json = redis:GetJson('kitsune_test_json')
            local s = tostring(json.TestArray[2].Name)
            return tostring(s:find('TestArray', 1, true) ~= nil)
            """);
        r.String.ShouldBe("true");
    }

    // -- GetJson extended ------------------------------------------------------
    [RedisFact]
    public async Task GetJson_GetNull_ReturnsJsonNullSentinel()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local v = json.OtherProperties()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(v == Json.Null)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_GetNestedObject_ReturnsTable()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local val = json.NullableGuid()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(type(val) == 'table' and val.HasValue == false and val.Value == 'SomeGuidBlablabla')
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_GetArray_ReturnsTableWithTwoElements()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local val = json.TestArray()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(type(val) == 'table' and #val == 2)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_GetArrayElementId_ReturnsInteger()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local id1 = json.TestArray[1].Id()
            local id2 = json.TestArray[2].Id()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(id1) .. ':' .. tostring(id2)
            """);
        r.String.ShouldBe("1:2");
    }

    [RedisFact]
    public async Task GetJson_GetNonExistentKey_ReturnsNil()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            redis:Command('DEL', 'kitsune_json_noexist')
            local json = redis:GetJson('kitsune_json_noexist')
            local v = json()
            return tostring(v == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_SetString_UpdatesField()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            json.NullableGuid.Value = 'UpdatedGuid'
            local v = json.NullableGuid.Value()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return v
            """);
        r.String.ShouldBe("UpdatedGuid");
    }

    [RedisFact]
    public async Task GetJson_SetNil_SetsJsonNull()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            json.SomeNumber:Set(nil)
            local v = json.SomeNumber()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(v == Json.Null)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_SetJsonNullSentinel_RoundTrips()
    {
        using KitsuneEngine engine = new();

        // Read a null field ? get Json.Null sentinel ? assign it to another field ? reads back as Json.Null.
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local null_val = json.OtherProperties()
            json.SomeNumber:Set(null_val)
            local v = json.SomeNumber()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(v == Json.Null)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_SetTable_ReplacesNestedObject()
    {
        using KitsuneEngine engine = new();

        // Sets each field individually (the table-encoding path); verifies both fields update.
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            json.NullableGuid.HasValue = true
            json.NullableGuid.Value = 'NewGuid'
            local has = json.NullableGuid.HasValue()
            local val = json.NullableGuid.Value()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(has) .. ':' .. val
            """);
        r.String.ShouldBe("true:NewGuid");
    }

    [RedisFact]
    public async Task GetJson_Set_Table_EncodesAndUpdates()
    {
        using KitsuneEngine engine = new();

        // Calls Set({...}) with a Lua table and verifies the raw JSON.GET sees the change.
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            json.NullableGuid:Set({{Value='TableSet'}})
            local raw = redis:Command('JSON.GET', 'kitsune_test_json', '$.NullableGuid.Value')
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(raw.Value ~= nil and raw.Value:find('TableSet', 1, true) ~= nil)
        ");
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_SetViaArrayIndexNewindex_UpdatesElement()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            json.TestArray[1].Name = 'Updated Cake'
            local name = json.TestArray[1].Name()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return name
            """);
        r.String.ShouldBe("Updated Cake");
    }

    [RedisFact]
    public async Task GetJson_Delete_Root_ReturnsOneAndKeyGone()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local count = json:Delete()
            local exists = redis:Command('EXISTS', 'kitsune_test_json')
            return tostring(count) .. ':' .. tostring(exists.Value == 0)
            """);
        r.String.ShouldBe("1:true");
    }

    [RedisFact]
    public async Task GetJson_Delete_ReturnsDeleteCount()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local count = json.NullableGuid:Delete()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(count)
            """);
        r.String.ShouldBe("1");
    }

    [RedisFact]
    public async Task GetJson_Type_NumberField_ReturnsTypeString()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local t = json.SomeNumber:Type()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(t ~= nil and type(t) == 'string')
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_Type_ObjectField_ReturnsObject()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local t = json.NullableGuid:Type()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return t
            """);
        r.String.ShouldBe("object");
    }

    [RedisFact]
    public async Task GetJson_Type_ArrayField_ReturnsArray()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local t = json.TestArray:Type()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return t
            """);
        r.String.ShouldBe("array");
    }

    [RedisFact]
    public async Task GetJson_Length_NonArray_ReturnsNilOrRaises()
    {
        using KitsuneEngine engine = new();

        // JSON.ARRLEN on a non-array type: Valkey returns a wrapped null element ? nil,
        // or raises an error. Either outcome is correct.
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local ok, result = pcall(function()
                return json.SomeNumber:Length()
            end)
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(not ok or result == nil)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_ArrayIndexZero_RaisesError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local json = redis:GetJson('kitsune_test_json')
            local ok = pcall(function()
                local _ = json.TestArray[0]
            end)
            return tostring(not ok)
            """);
        r.String.ShouldBe("true");
    }

    [RedisFact]
    public async Task GetJson_ReusePathObject_IndependentAccess()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local guid = json.NullableGuid
            local has = guid.HasValue()
            local val = guid.Value()
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(has) .. ':' .. val
            """);
        r.String.ShouldBe("false:SomeGuidBlablabla");
    }

    [RedisFact]
    public async Task GetJson_Tostring_ArrayIndexPath_ShowsBrackets()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            local json = redis:GetJson('kitsune_test_json')
            local s = tostring(json.TestArray[1])
            return tostring(s:find('[0]', 1, true) ~= nil)
            """);
        r.String.ShouldBe("true");
    }

    // -- GetJson pairs ---------------------------------------------------------
    [RedisFact]
    public async Task GetJson_Pairs_OnObject_IteratesKeyValuePairs()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local keys = {{}}
            for k, v in pairs(json.NullableGuid) do
                keys[#keys+1] = k
            end
            table.sort(keys)
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return table.concat(keys, ',')
        ");
        r.String.ShouldBe("HasValue,Value");
    }

    [RedisFact]
    public async Task GetJson_Pairs_OnObject_ValuesAreDecodedCorrectly()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local result = {{}}
            for k, v in pairs(json.NullableGuid) do
                result[k] = tostring(v)
            end
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return result.Value .. ':' .. result.HasValue
        ");
        r.String.ShouldBe("SomeGuidBlablabla:false");
    }

    [RedisFact]
    public async Task GetJson_Pairs_OnArray_CountsAllElements()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local count = 0
            for i, v in pairs(json.TestArray) do
                count = count + 1
            end
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(count)
            """);
        r.String.ShouldBe("2");
    }

    [RedisFact]
    public async Task GetJson_Pairs_OnRoot_IteratesTopLevelKeys()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local keys = {{}}
            for k, v in pairs(json) do
                keys[#keys+1] = k
            end
            table.sort(keys)
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return table.concat(keys, ',')
        ");
        r.String.ShouldBe("NullableGuid,OtherProperties,SomeNumber,TestArray");
    }

    [RedisFact]
    public async Task GetJson_Pairs_OnMissingPath_IteratesNothing()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local count = 0
            for k, v in pairs(json.nonexistent) do
                count = count + 1
            end
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(count)
            """);
        r.String.ShouldBe("0");
    }

    [RedisFact]
    public async Task GetJson_Pairs_OnScalar_IteratesNothing()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local count = 0
            for k, v in pairs(json.SomeNumber) do
                count = count + 1
            end
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(count)
            """);
        r.String.ShouldBe("0");
    }

    [RedisFact]
    public async Task GetJson_Pairs_ViaMethod_WorksLikePairs()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($"""
            local redis = {Open()}
            {JsonSetup()}
            local json = redis:GetJson('kitsune_test_json')
            local count = 0
            for k, v in json.NullableGuid:Pairs() do
                count = count + 1
            end
            redis:Command('JSON.DEL', 'kitsune_test_json')
            return tostring(count)
            """);
        r.String.ShouldBe("2");
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

    // -- GetJson ---------------------------------------------------------------
    private string JsonSetup(string key = "kitsune_test_json") =>
        $"redis:Command('JSON.SET', '{key}', '$', '{JsonDoc}')";
}
