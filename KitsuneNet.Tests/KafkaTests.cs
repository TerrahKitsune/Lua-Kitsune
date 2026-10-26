using KitsuneNet;
using Shouldly;
using Xunit;
using Xunit.Abstractions;

namespace KitsuneNet.Tests;

[Collection("KitsuneSequential")]
public sealed class KafkaTests
{
    // Lua locals bootstrap, topic, groupId, part and createMainTopic are declared before this.
    private const string LuaPrelude = @"
        local function res(s) return 'kitsune_' .. RUN_ID .. '_' .. s end

        local function newProducer()
            return Kafka.NewProducer({['bootstrap.servers'] = bootstrap})
        end

        local function newConsumer(offsetReset)
            local conf = {['bootstrap.servers'] = bootstrap, ['group.id'] = groupId}
            if offsetReset then conf['auto.offset.reset'] = offsetReset end
            return Kafka.NewConsumer(conf)
        end

        -- Creates the topic and waits until its metadata is listed and every partition answers GetOffsets
        local function ensureTopic(name, count)
            local p = newProducer()
            local created, createErr = p:CreateTopic(name, count)
            if not created then p:Close(); error('CreateTopic failed: ' .. tostring(createErr)) end
            local deadline = Time() + 30000
            while Time() < deadline do
                local ok, meta = p:GetMetadata()
                local ready = false
                if ok then
                    for _, t in ipairs(meta.Topics) do
                        if t.Name == name and t.ErrorCode == 0 then ready = true; break end
                    end
                end
                for i = 0, count - 1 do
                    if ready then ready = p:GetOffsets(name, i) end
                end
                if ready then p:Close(); return end
                Sleep(100)
            end
            p:Close()
            error('topic ' .. name .. ' not usable before the deadline')
        end

        -- Sends messages ({key, value, headers}) to the partition, waits until the broker
        -- reports them, and returns the offset of the first one.
        local function produce(messages, partition)
            partition = partition or part
            local p = newProducer()
            local okLow, low, firstOffset = p:GetOffsets(topic, partition)
            assert(okLow, tostring(low))
            for _, m in ipairs(messages) do
                local ok, err = p:Send(topic, m.key, m.value, m.headers, partition)
                assert(ok, tostring(err))
            end
            p:Close()

            local reader = newProducer()
            local wanted = firstOffset + #messages
            local high = firstOffset
            local deadline = Time() + 20000
            while high < wanted and Time() < deadline do
                local ok, _, h = reader:GetOffsets(topic, partition)
                if ok then high = h end
                if high < wanted then Sleep(100) end
            end
            reader:Close()
            assert(high >= wanted, 'produced messages were not reported by the broker')
            return firstOffset
        end

        -- Polls until need distinct messages (by partition and offset) satisfy accept, or the deadline passes
        local function pollUntil(consumer, accept, need, timeoutMs)
            local got, seen = {}, {}
            local deadline = Time() + (timeoutMs or 20000)
            while #got < need and Time() < deadline do
                local ok, d = consumer:Poll()
                if not ok then error(tostring(d)) end
                if d and d.ErrorCode == 0 and accept(d) then
                    local id = d.Partition .. ':' .. d.Offset
                    if not seen[id] then
                        seen[id] = true
                        got[#got + 1] = d
                    end
                end
                if not d then Sleep(50) end
            end
            return got
        end

        -- Drives polls until the group coordinator shows the consumer holding a partition
        local function waitGroupAssigned(consumer, timeoutMs)
            local deadline = Time() + (timeoutMs or 30000)
            while Time() < deadline do
                local ok, msg = consumer:Poll()
                if not ok then error(tostring(msg)) end
                local okDesc, descs = consumer:DescribeGroups({groupId})
                if okDesc and descs[1] and #descs[1].Members > 0 and #descs[1].Members[1].Partitions > 0 then
                    return true
                end
                Sleep(200)
            end
            return false
        end

        -- The offset the next message appended to the topic partition will get
        local function endOffset()
            local p = newProducer()
            local ok, low, high = p:GetOffsets(topic, part)
            p:Close()
            assert(ok, tostring(low))
            return high
        end

        -- Reads from the offset until a message with the value arrives; returns it, or nil on timeout
        local function receive(offset, value)
            local consumer = newConsumer()
            assert(consumer:Assign({topic .. ':' .. part .. ':' .. offset}))
            local got = pollUntil(consumer, function(d) return d.Value == value end, 1)
            consumer:Close()
            return got[1]
        end

        if createMainTopic then ensureTopic(topic, 1) end
    ";

    private readonly ITestOutputHelper _output;

    public KafkaTests(ITestOutputHelper output) => _output = output;

    // creation
    [KafkaFact]
    public async Task NewProducer_ValidConfig_ReturnsUserdata()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local p = Kafka.NewProducer({{['bootstrap.servers']='{Bootstrap()}'}})
            assert(p, 'NewProducer returned nil')
            return tostring(p):sub(1, 13)
        ");
        r.String.ShouldBe("KafkaProducer");
    }

    [KafkaFact]
    public async Task NewConsumer_ValidConfig_ReturnsUserdata()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local c = Kafka.NewConsumer({{['bootstrap.servers']='{Bootstrap()}'}})  
            assert(c, 'NewConsumer returned nil')
            return tostring(c):sub(1, 13)
        ");
        r.String.ShouldBe("KafkaConsumer");
    }

    [KafkaFact]
    public async Task Consumer_Subscribe_ReturnsTrue()
    {
        LuaValue r = await Run("subscribe", @"
            local c = newConsumer()
            local ok, err = c:Subscribe({topic})
            c:Close()
            return tostring(ok) .. ':' .. tostring(err == nil)
        ");
        r.String.ShouldBe("true:true");
    }

    [KafkaFact]
    public async Task Consumer_Assign_ReturnsTrue()
    {
        LuaValue r = await Run("assign_ok", @"
            local c = newConsumer()
            local ok, err = c:Assign({topic .. ':' .. part})
            c:Close()
            return tostring(ok) .. ':' .. tostring(err == nil)
        ");
        r.String.ShouldBe("true:true");
    }

    [KafkaFact]
    public async Task Consumer_Poll_IdleReturnsTrue()
    {
        LuaValue r = await Run("poll_idle", @"
            local c = newConsumer('latest')
            assert(c:Subscribe({topic}))
            assert(waitGroupAssigned(c), 'group assignment not confirmed')

            -- idle: ok=true, msg=nil; events queued by the rebalance are drained first
            local ok, msg
            local deadline = Time() + 10000
            repeat
                ok, msg = c:Poll()
                if not ok then error(tostring(msg)) end
                if msg then Sleep(50) end
            until msg == nil or Time() > deadline
            c:Close()
            return tostring(ok) .. ':' .. tostring(msg == nil)
        ");
        r.String.ShouldBe("true:true");
    }

    [KafkaFact]
    public async Task Producer_Close_IsIdempotent()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local p = Kafka.NewProducer({{['bootstrap.servers']='{Bootstrap()}'}})
            p:Close()
            p:Close()  -- second close must not crash or error
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    [KafkaFact]
    public async Task Consumer_Close_IsIdempotent()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local c = Kafka.NewConsumer({{['bootstrap.servers']='{Bootstrap()}'}})
            c:Close()
            c:Close()  -- second close must not crash or error
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    [KafkaFact]
    public async Task Logs_ReturnsAccumulatedLogsThenClears()
    {
        using KitsuneEngine engine = TestRun.NewEngine();

        // Trigger at least one librdkafka log event by connecting and disconnecting
        LuaValue r = await engine.ExecuteStringAsync($@"
            -- Produce to ensure the library emits at least some internal log lines
            local p = Kafka.NewProducer({{['bootstrap.servers']='{Bootstrap()}', ['debug']='generic'}})
            p:Close()
            local logs1 = Kafka.Logs()
            local logs2 = Kafka.Logs()  -- second call must be empty after the clear
            local lineShape = '%[[^%]]*%] %[[^%]]*%] %[%d+%] %[[^%]]*%]: '
            return type(logs1) .. ':' .. tostring(logs1:match(lineShape) ~= nil) .. ':' .. tostring(logs2 == '')
        ");

        // The debug setting makes librdkafka log while the client starts, so logs1 has log lines
        r.String.ShouldBe("string:true:true");
    }

    // producer
    [KafkaFact]
    public async Task Producer_Send_ReturnsTrue()
    {
        LuaValue r = await Run("send", @"
            local value = 'send-' .. RUN_ID
            local start = endOffset()
            local p = newProducer()
            assert(p, 'NewProducer failed')
            local ok, err = p:Send(topic, 'test-key', value)
            p:Close()
            local msg = receive(start, value)
            return tostring(ok) .. ':' .. tostring(err == nil) .. ':' .. tostring(msg and msg.Key) .. ':' .. tostring(msg and msg.Value == value)
        ");
        r.String.ShouldBe("true:true:test-key:true");
    }

    [KafkaFact]
    public async Task Producer_Send_NilKey_ReturnsTrue()
    {
        LuaValue r = await Run("send_nilkey", @"
            local value = 'keyless-' .. RUN_ID
            local start = endOffset()
            local p = newProducer()
            assert(p, 'NewProducer failed')
            local ok, err = p:Send(topic, nil, value)
            p:Close()
            local msg = receive(start, value)
            return tostring(ok) .. ':' .. tostring(err == nil) .. ':' .. tostring(msg ~= nil and msg.Key == nil) .. ':' .. tostring(msg and msg.Value == value)
        ");
        r.String.ShouldBe("true:true:true:true");
    }

    [KafkaFact]
    public async Task Producer_Send_WithHeaders_ReturnsTrue()
    {
        LuaValue r = await Run("send_headers", @"
            local value = 'hdr-' .. RUN_ID
            local start = endOffset()
            local p = newProducer()
            assert(p, 'NewProducer failed')
            local ok, err = p:Send(topic, 'hdr-key', value, {source='kitsune-test', version='1'})
            p:Close()
            local msg = receive(start, value)
            return tostring(ok) .. ':' .. tostring(err == nil) .. ':' .. tostring(msg and msg.Key) .. ':' .. tostring(msg and msg.Value == value) ..
                   ':' .. tostring(msg and msg.Headers.source) .. ':' .. tostring(msg and msg.Headers.version)
        ");
        r.String.ShouldBe("true:true:hdr-key:true:kitsune-test:1");
    }

    [KafkaFact]
    public async Task Producer_Send_WithHeadersAndPartition_ReturnsTrue()
    {
        LuaValue r = await Run("send_hdrpart", @"
            local value = 'hdrpart-' .. RUN_ID
            local start = endOffset()
            local p = newProducer()
            assert(p, 'NewProducer failed')
            -- Exercise the branch that requires BOTH explicit partition AND headers
            local ok, err = p:Send(topic, 'hdrpart-key', value,
                {src='kitsune-test'}, part)
            p:Close()
            local msg = receive(start, value)
            return tostring(ok) .. ':' .. tostring(err == nil) .. ':' .. tostring(msg and msg.Key) .. ':' .. tostring(msg and msg.Value == value) ..
                   ':' .. tostring(msg and msg.Partition) .. ':' .. tostring(msg and msg.Headers.src)
        ");
        r.String.ShouldBe("true:true:hdrpart-key:true:0:kitsune-test");
    }

    [KafkaFact]
    public async Task Producer_Send_Headers_AreReceivedByConsumer()
    {
        LuaValue r = await Run("hdr_recv", @"
            local uniqueVal = 'hdr-recv-' .. RUN_ID
            local hi0 = produce({{key='hkey', value=uniqueVal, headers={x='42', y='hello'}}})

            local consumer = newConsumer()
            assert(consumer:Assign({topic .. ':' .. part .. ':' .. hi0}))

            local got = pollUntil(consumer, function(d) return d.Value == uniqueVal end, 1)
            consumer:Close()

            if #got == 0 then return 'no-message' end
            return tostring(got[1].Headers.x == '42' and got[1].Headers.y == 'hello')
        ");
        r.String.ShouldBe("true");
    }

    // consumer
    [KafkaFact]
    public async Task Consumer_Subscribe_ToMultipleTopics_Succeeds()
    {
        LuaValue r = await Run("multi_subscribe", new[] { "multi" }, @"
            local topic2 = res('multi')

            local p = newProducer()
            local ok, err = p:CreateTopic(topic2, 1)
            assert(ok, tostring(err))
            p:Close()

            local c = newConsumer()
            local ok2, err2 = c:Subscribe({topic, topic2})
            c:Close()

            return tostring(ok2)
        ");
        r.String.ShouldBe("true");
    }

    // round-trip
    [KafkaFact]
    public async Task Consumer_RoundTrip_MessageFieldsArePopulated()
    {
        LuaValue r = await Run("fields", @"
            local uniqueVal = 'kitsune-fields-' .. RUN_ID
            local hi0 = produce({{key='fields-key', value=uniqueVal}})

            local consumer = newConsumer()
            assert(consumer:Assign({topic .. ':' .. part .. ':' .. hi0}))

            local got = pollUntil(consumer, function(d) return d.Value == uniqueVal end, 1)
            consumer:Close()

            if #got == 0 then return 'no-message' end
            local data = got[1]
            -- Verify the required fields are present and have the right types
            local ok =
                type(data.Value)     == 'string'  and
                type(data.Topic)     == 'string'  and
                type(data.Partition) == 'number'  and
                type(data.Offset)    == 'number'  and
                type(data.Timestamp) == 'number'  and
                type(data.ErrorCode) == 'number'  and
                type(data.Error)     == 'string'  and
                type(data.Headers)   == 'table'   and
                data.Key             == 'fields-key'
            return tostring(ok)
        ");
        r.String.ShouldBe("true");
    }

    // implicit commit via next Poll
    [KafkaFact]
    public async Task Consumer_Poll_CommitsImplicitlyOnNextCall()
    {
        LuaValue r = await Run("commit", @"
            local uniqueVal = 'kitsune-commit-' .. RUN_ID
            local hi0 = produce({{key='commit-key', value=uniqueVal}})

            local consumer = newConsumer()
            assert(consumer:Assign({topic .. ':' .. part .. ':' .. hi0}))

            local got = pollUntil(consumer, function(d) return d.Value == uniqueVal end, 1)
            local found = #got > 0

            -- One more poll commits the previously received message implicitly
            if found then consumer:Poll() end
            consumer:Close()
            if not found then return 'no-message' end

            -- The commit is asynchronous; Kafka stores the offset of the next message to read
            local key = topic .. ':' .. part
            local p = newProducer()
            local committed
            local deadline = Time() + 15000
            repeat
                local ok, offsets = p:GetGroupOffsets(groupId, {key})
                if ok then committed = offsets[key] end
                if committed ~= got[1].Offset + 1 then Sleep(200) end
            until committed == got[1].Offset + 1 or Time() > deadline
            p:Close()
            return tostring(found) .. ':' .. tostring(got[1].Offset == hi0) .. ':' .. tostring(committed == hi0 + 1)
        ");
        r.String.ShouldBe("true:true:true");
    }

    // Group operations
    [KafkaFact]
    public async Task Producer_ListGroups_ReturnsArray()
    {
        // A committed offset makes the group exist, so it must be listed
        LuaValue r = await Run("listgroups", @"
            local p = newProducer()
            local key = topic .. ':' .. part
            local okLow, low, high = p:GetOffsets(topic, part)
            assert(okLow, tostring(low))
            local okSet, errSet = p:SetGroupOffsets(groupId, {[key] = high})
            assert(okSet, tostring(errSet))

            local found, ok, groups = false, false, nil
            local deadline = Time() + 15000
            while not found and Time() < deadline do
                ok, groups = p:ListGroups()
                if not ok then p:Close(); return 'err:' .. tostring(groups) end
                for _, g in ipairs(groups) do
                    if g.GroupId == groupId then found = true; break end
                end
                if not found then Sleep(200) end
            end
            p:Close()
            return tostring(type(groups) == 'table') .. ':' .. tostring(found)
        ");
        r.String.ShouldBe("true:true");
    }

    [KafkaFact]
    public async Task Producer_DescribeGroups_ReturnsDescription()
    {
        // Subscribe and wait for the assignment so the group exists, then describe it
        LuaValue r = await Run("describe", @"
            local consumer = newConsumer('latest')
            assert(consumer:Subscribe({topic}))
            assert(waitGroupAssigned(consumer), 'group assignment not confirmed')
            consumer:Close()

            local p = newProducer()
            local ok, descs = p:DescribeGroups({groupId})
            p:Close()

            if not ok then return 'err:' .. tostring(descs) end
            if #descs == 0 then return 'empty' end
            local d = descs[1]
            local valid = type(d.GroupId)  == 'string' and
                          type(d.State)    == 'string' and
                          type(d.Members)  == 'table'
            return tostring(valid)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task Producer_GetGroupOffsets_ReturnsTable()
    {
        // Create a group with committed offsets, then query them
        LuaValue r = await Run("getoff", @"
            local uniqueVal = 'getoff-' .. RUN_ID
            local hi0 = produce({{value=uniqueVal}})

            local consumer = newConsumer()
            assert(consumer:Assign({topic .. ':' .. part .. ':' .. hi0}))

            local got = pollUntil(consumer, function(d) return d.Value == uniqueVal end, 1)
            -- Poll once more to commit the pending message implicitly
            if #got > 0 then consumer:Poll() end
            consumer:Close()

            if #got == 0 then return 'no-message' end

            -- Query the committed offsets; the commit is asynchronous
            local p = newProducer()
            local ok, offsets
            local deadline = Time() + 15000
            repeat
                ok, offsets = p:GetGroupOffsets(groupId)
                if ok and next(offsets) ~= nil then break end
                Sleep(200)
            until Time() > deadline
            p:Close()
            if not ok then return 'err:' .. tostring(offsets) end
            return tostring(type(offsets) == 'table' and next(offsets) ~= nil)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task Producer_SetGroupOffsets_ResetsPosition()
    {
        LuaValue r = await Run("setoff", @"
            -- Messages make the high watermark non-zero, so the stored offset is not a default
            produce({{value='setoff-1'}, {value='setoff-2'}, {value='setoff-3'}})

            local p = newProducer()
            local ok0, lo0, hi0 = p:GetOffsets(topic, part)
            assert(ok0, tostring(lo0))

            -- Reset the group's offset for this partition to hi0 (current end)
            local key = topic .. ':' .. part
            local ok, err = p:SetGroupOffsets(groupId, {[key] = hi0})
            p:Close()

            if not ok then return 'set-failed:' .. tostring(err) end

            -- Read back to confirm
            local p2 = newProducer()
            local ok2, offsets = p2:GetGroupOffsets(groupId, {key})
            p2:Close()
            if not ok2 then return 'get-failed:' .. tostring(offsets) end

            local storedOffset = offsets[key]
            return tostring(storedOffset == hi0) .. ' stored=' .. tostring(storedOffset) .. ' hi0=' .. hi0
        ");
        r.String.ShouldStartWith("true");
    }

    [KafkaFact]
    public async Task Producer_DeleteGroup_DeletesInactiveGroup()
    {
        LuaValue r = await Run("delgrp_topic", new[] { "delgrp" }, @"
            local deleteGroupId = res('delgrp')

            -- Create a committed offset so the group exists in the broker
            local p = newProducer()
            local key = topic .. ':0'
            local ok0, lo0, hi0 = p:GetOffsets(topic, 0)
            assert(ok0, tostring(lo0))
            local ok, err = p:SetGroupOffsets(deleteGroupId, {[key] = hi0})
            assert(ok, tostring(err))

            -- Verify it appears in list
            local found = false
            local deadline = Time() + 15000
            while not found and Time() < deadline do
                local ok2, groups = p:ListGroups()
                assert(ok2, tostring(groups))
                for _, g in ipairs(groups) do
                    if g.GroupId == deleteGroupId then found = true; break end
                end
                if not found then Sleep(200) end
            end

            -- Delete it
            local ok3, err3 = p:DeleteGroup(deleteGroupId)

            p:Close()
            return tostring(found) .. ':' .. tostring(ok3)
        ");
        r.String.ShouldBe("true:true");
    }

    [KafkaFact]
    public async Task Producer_DeleteGroupOffsets_RemovesOffsets()
    {
        LuaValue r = await Run("deloff_topic", new[] { "deloff" }, @"
            local offsetsGroupId = res('deloff')

            local p = newProducer()
            local key = topic .. ':' .. part
            local ok0, lo0, hi0 = p:GetOffsets(topic, part)
            assert(ok0, tostring(lo0))

            -- Create committed offset
            local ok, err = p:SetGroupOffsets(offsetsGroupId, {[key] = hi0})
            assert(ok, tostring(err))

            -- Confirm it exists
            local ok2, before = p:GetGroupOffsets(offsetsGroupId, {key})
            assert(ok2, tostring(before))
            local hadOffset = before[key] ~= nil

            -- Delete the committed offset
            local ok3, err3 = p:DeleteGroupOffsets(offsetsGroupId, {key})

            -- After deletion the offset should be INVALID (-1001) or absent
            local ok4, after = p:GetGroupOffsets(offsetsGroupId, {key})
            assert(ok4, tostring(after))
            local goneOrInvalid = after[key] == nil or after[key] == -1001

            p:Close()
            return tostring(hadOffset) .. ':' .. tostring(ok3) .. ':' .. tostring(goneOrInvalid)
        ");
        r.String.ShouldBe("true:true:true");
    }

    // GetTopicConfig / SetTopicConfig
    [KafkaFact]
    public async Task Producer_GetTopicConfig_ReturnsTable()
    {
        LuaValue r = await Run("cfg_get", @"
            local p = newProducer()
            local ok, cfg = p:GetTopicConfig(topic)
            p:Close()
            if not ok then return 'err:' .. tostring(cfg) end
            -- retention.ms and cleanup.policy are always present on any topic
            local valid = type(cfg) == 'table'
                         and cfg['retention.ms']    ~= nil
                         and cfg['cleanup.policy']   ~= nil
            return tostring(valid)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task Producer_SetTopicConfig_ChangesRetention()
    {
        LuaValue r = await RunWithoutTopic("cfg_set", new[] { "cfg_set_topic" }, $@"
            local topicName = res('cfg_set_topic')

            local p = newProducer()

            local function waitForTopic(name, want)
                local dl = Time() + 15000
                while Time() < dl do
                    local ok, meta = p:GetMetadata()
                    if not ok then error(tostring(meta)) end
                    local found = false
                    for _, t in ipairs(meta.Topics) do
                        if t.Name == name then found = true; break end
                    end
                    if found == want then return true end
                    Sleep(100)
                end
                return false
            end

            local function waitForConfigValue(name, key, wantVal)
                local dl = Time() + 15000
                while Time() < dl do
                    local ok, cfg = p:GetTopicConfig(name)
                    if ok and cfg[key] == wantVal then return true end
                    Sleep(100)
                end
                return false
            end

            local ok, err = p:CreateTopic(topicName, 1)
            assert(ok, tostring(err))
            assert(waitForTopic(topicName, true), 'topic did not appear')

            -- Read the default config
            local ok2, cfg = p:GetTopicConfig(topicName)
            assert(ok2, tostring(cfg))
            local defaultRetention = cfg['retention.ms']

            -- Set a new retention value and wait for the change to propagate
            local newRetention = '7200000'
            local ok3, err3 = p:SetTopicConfig(topicName, {{['retention.ms'] = newRetention}})
            assert(ok3, tostring(err3))

            local changed = waitForConfigValue(topicName, 'retention.ms', newRetention)

            -- Cleanup
            p:DestroyTopic(topicName)
            p:Close()

            return tostring(changed) ..
                   ' default=' .. tostring(defaultRetention) ..
                   ' updated=' .. newRetention
        ");
        r.String.ShouldStartWith("true");
    }

    [KafkaFact]
    public async Task Consumer_GetTopicConfig_ReturnsTable()
    {
        LuaValue r = await Run("cfg_get_consumer", @"
            local c = newConsumer()
            local ok, cfg = c:GetTopicConfig(topic)
            c:Close()
            if not ok then return 'err:' .. tostring(cfg) end
            return tostring(type(cfg) == 'table' and cfg['retention.ms'] ~= nil)
        ");
        r.String.ShouldBe("true");
    }

    // CreateTopic / DestroyTopic
    [KafkaFact]
    public async Task Producer_CreateTopic_And_DestroyTopic()
    {
        LuaValue r = await RunWithoutTopic("create", new[] { "create_topic" }, @"
            local topicName = res('create_topic')

            local p = newProducer()

            -- Polls GetMetadata until the topic reaches the expected presence, up to 15 s
            local function waitForTopic(name, wantExists)
                local deadline = Time() + 15000
                while Time() < deadline do
                    local ok, meta = p:GetMetadata()
                    if not ok then error('GetMetadata failed: ' .. tostring(meta)) end
                    local found = false
                    for _, t in ipairs(meta.Topics) do
                        if t.Name == name then found = true; break end
                    end
                    if found == wantExists then return true end
                    Sleep(100)
                end
                return false
            end

            local existsBefore = waitForTopic(topicName, false)  -- should not exist yet

            local ok, err = p:CreateTopic(topicName, 1)
            if not ok then p:Close(); return 'create-failed:' .. tostring(err) end

            local existsAfter  = waitForTopic(topicName, true)   -- must appear

            local ok2, err2 = p:CreateTopic(topicName, 1)        -- duplicate must fail

            local ok3, err3 = p:DestroyTopic(topicName)
            if not ok3 then p:Close(); return 'destroy-failed:' .. tostring(err3) end

            local existsGone = waitForTopic(topicName, false)     -- must disappear

            p:Close()
            return tostring(existsBefore) .. ':' .. tostring(existsAfter) ..
                   ':' .. tostring(not ok2) .. ':' .. tostring(ok3) ..
                   ':' .. tostring(existsGone)
        ");
        r.String.ShouldBe("true:true:true:true:true");
    }

    [KafkaFact]
    public async Task Producer_CreateTopic_WithRetention_Succeeds()
    {
        LuaValue r = await RunWithoutTopic("retention", new[] { "retention_topic" }, @"
            local topicName = res('retention_topic')

            local p = newProducer()

            local function waitForTopic(name, wantExists)
                local deadline = Time() + 15000
                while Time() < deadline do
                    local ok, meta = p:GetMetadata()
                    if not ok then error('GetMetadata: ' .. tostring(meta)) end
                    local found = false
                    for _, t in ipairs(meta.Topics) do
                        if t.Name == name then found = true; break end
                    end
                    if found == wantExists then return true end
                    Sleep(100)
                end
                return false
            end

            -- 2 partitions, 1-hour retention (ms), 100 MB retention (bytes)
            local ok, err = p:CreateTopic(topicName, 2, 3600000, 104857600)
            if not ok then p:Close(); return 'create-failed:' .. tostring(err) end

            local existsAfter = waitForTopic(topicName, true)

            local ok2, err2 = p:DestroyTopic(topicName)

            local existsGone = waitForTopic(topicName, false)

            p:Close()
            return tostring(ok) .. ':' .. tostring(existsAfter) ..
                   ':' .. tostring(ok2) .. ':' .. tostring(existsGone)
        ");
        r.String.ShouldBe("true:true:true:true");
    }

    [KafkaFact]
    public async Task Producer_DestroyTopic_NonExistent_ReturnsError()
    {
        LuaValue r = await RunWithoutTopic("destroy_missing", Array.Empty<string>(), @"
            local p = newProducer()
            local ok, err = p:DestroyTopic(res('never_created'))
            p:Close()
            return tostring(not ok) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("true:true");
    }

    // Assign with offset / Seek
    [KafkaFact]
    public async Task Consumer_Assign_WithExplicitOffset_StartsFromThatOffset()
    {
        LuaValue r = await Run("assign_offset", @"
            local count  = 4
            local prefix = 'seek-offset-' .. RUN_ID .. '-'

            -- Earlier messages that the explicit offset must skip
            produce({{value='before-1'}, {value='before-2'}})

            local messages = {}
            for i = 1, count do
                messages[i] = {value = prefix .. i}
            end
            local hi0 = produce(messages)

            -- Assign at the offset where our messages begin so nothing before them arrives
            local consumer = newConsumer('latest')
            assert(consumer:Assign({topic .. ':' .. part .. ':' .. hi0}))

            local got = pollUntil(consumer, function(d) return true end, count)
            consumer:Close()

            local ours = 0
            for _, d in ipairs(got) do
                if string.sub(d.Value, 1, #prefix) == prefix then ours = ours + 1 end
            end
            return tostring(ours) .. '/' .. tostring(count)
        ");
        r.String.ShouldBe("4/4");
    }

    [KafkaFact]
    public async Task Consumer_Assign_WithEarliestKeyword_ReceivesMessages()
    {
        LuaValue r = await Run("assign_earliest", @"
            local prefix = 'assign-earliest-' .. RUN_ID .. '-'
            produce({{value=prefix .. 1}, {value=prefix .. 2}})

            -- 'earliest' in the Assign string sets OFFSET_BEGINNING directly,
            -- independent of auto.offset.reset config
            local consumer = newConsumer('latest')   -- config says latest...
            -- ...but the ':earliest' keyword in the Assign string overrides it
            assert(consumer:Assign({topic .. ':' .. part .. ':earliest'}))

            local got = pollUntil(consumer, function(d)
                return string.sub(d.Value, 1, #prefix) == prefix
            end, 1)

            consumer:Close()
            return tostring(#got >= 1)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task Consumer_Seek_RepositionsToSpecificOffset()
    {
        LuaValue r = await Run("seek_offset", @"
            local count      = 3
            local prefix     = 'seek-specific-' .. RUN_ID .. '-'
            local skipPrefix = 'seek-skip-' .. RUN_ID .. '-'

            produce({{value=skipPrefix .. 1}, {value=skipPrefix .. 2}})

            -- Our messages land at targetStart onwards
            local targets = {}
            for i = 1, count do
                targets[i] = {value = prefix .. i}
            end
            local targetStart = produce(targets)

            -- Assign from earliest so the partition is active from the start
            local consumer = newConsumer('earliest')
            assert(consumer:Assign({topic .. ':' .. part}))

            -- Receiving a skipped message shows the assignment is active, so Seek applies
            local first = pollUntil(consumer, function(d)
                return string.sub(d.Value, 1, #skipPrefix) == skipPrefix
            end, 1)
            assert(#first == 1, 'assignment never delivered a message')

            -- Seek to targetStart: this skips all older messages and positions us
            -- exactly at the first message we produced
            local ok, err = consumer:Seek(topic, part, targetStart)
            assert(ok, err or 'Seek failed')

            local got = pollUntil(consumer, function(d)
                return string.sub(d.Value, 1, #prefix) == prefix
            end, count)

            consumer:Close()
            return tostring(#got) .. '/' .. tostring(count)
        ");
        r.String.ShouldBe("3/3");
    }

    [KafkaFact]
    public async Task Producer_DescribeGroups_MultipleGroups()
    {
        LuaValue r = await Run("describe_multi", new[] { "describe_multi_second" }, @"
            local g1 = groupId
            local g2 = res('describe_multi_second')

            local p = newProducer()
            local ok0, lo0, hi0 = p:GetOffsets(topic, part)
            assert(ok0, tostring(lo0))
            local key = topic .. ':' .. part

            -- Create both groups via committed offsets
            local ok1, e1 = p:SetGroupOffsets(g1, {[key] = hi0})
            local ok2, e2 = p:SetGroupOffsets(g2, {[key] = hi0})
            assert(ok1, tostring(e1))
            assert(ok2, tostring(e2))

            local ok, descs = p:DescribeGroups({g1, g2})
            p:Close()
            if not ok then return 'err:' .. tostring(descs) end
            return tostring(#descs == 2)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task CreateTopic_WithReplicationFactor_Succeeds()
    {
        LuaValue r = await RunWithoutTopic("rf", new[] { "rf_topic" }, @"
            local topicName = res('rf_topic')

            local p = newProducer()
            -- replication_factor = 1 (arg 5), timeout = default
            local ok, err = p:CreateTopic(topicName, 2, nil, nil, 1)
            if not ok then p:Close(); return 'failed:' .. tostring(err) end

            local partitions
            local deadline = Time() + 15000
            while partitions == nil and Time() < deadline do
                local okMeta, meta = p:GetMetadata()
                if not okMeta then p:Close(); error('GetMetadata: ' .. tostring(meta)) end
                for _, t in ipairs(meta.Topics) do
                    if t.Name == topicName and t.ErrorCode == 0 then partitions = #t.Partitions; break end
                end
                if partitions == nil then Sleep(100) end
            end

            p:DestroyTopic(topicName)
            p:Close()
            return tostring(ok) .. ':' .. tostring(partitions)
        ");
        r.String.ShouldBe("true:2");
    }

    [KafkaFact]
    public async Task GetTopicConfig_NonExistentTopic_ReturnsError()
    {
        LuaValue r = await RunWithoutTopic("cfg_missing", Array.Empty<string>(), @"
            local p = newProducer()
            local ok, cfg = p:GetTopicConfig(res('never_created'))
            p:Close()
            return tostring(not ok) .. ':' .. tostring(type(cfg) == 'string')
        ");
        r.String.ShouldBe("true:true");
    }

    [KafkaFact]
    public async Task Consumer_Seek_WithEarliestKeyword_ReceivesMessages()
    {
        LuaValue r = await Run("seek_earliest", @"
            local count  = 3
            local prefix = 'seek-earliest-' .. RUN_ID .. '-'
            local function isOurs(d) return string.sub(d.Value, 1, #prefix) == prefix end

            local messages = {}
            for i = 1, count do
                messages[i] = {value = prefix .. i}
            end
            produce(messages)

            local consumer = newConsumer('latest')
            assert(consumer:Assign({topic .. ':' .. part .. ':earliest'}))

            -- Reading everything once shows the assignment is active, so Seek applies
            local first = pollUntil(consumer, isOurs, count)
            assert(#first == count, 'initial read did not return all messages')

            -- Seek to earliest using the string keyword
            local ok, err = consumer:Seek(topic, part, 'earliest')
            assert(ok, err or 'Seek failed')

            -- Should now receive the messages from the beginning of the partition again
            local again = pollUntil(consumer, isOurs, count)

            consumer:Close()
            return tostring(#again >= 1)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task Consumer_Assign_WithLatestKeyword_NoOldMessages()
    {
        LuaValue r = await Run("assign_latest", @"
            local oldValue = 'latest-old-' .. RUN_ID
            local newPrefix = 'latest-new-' .. RUN_ID .. '-'
            produce({{value=oldValue}})

            -- 'latest' in the Assign string should mean no old messages come through
            local consumer = newConsumer('earliest')   -- config says earliest...
            -- ...but ':latest' in the string overrides it
            assert(consumer:Assign({topic .. ':' .. part .. ':latest'}))

            -- Assigning at the end resolves lazily, so keep producing new messages
            -- until one arrives: that proves the position was set past the old message
            local p = newProducer()
            local sawOld, sawNew = false, false
            local sent = 0
            local deadline = Time() + 30000
            while Time() < deadline and not sawNew do
                sent = sent + 1
                assert(p:Send(topic, nil, newPrefix .. sent, nil, part))
                local pollEnd = Time() + 300
                while Time() < pollEnd do
                    local ok, data = consumer:Poll()
                    if not ok then error(tostring(data)) end
                    if data and data.ErrorCode == 0 then
                        if data.Value == oldValue then
                            sawOld = true
                        elseif string.sub(data.Value, 1, #newPrefix) == newPrefix then
                            sawNew = true
                        end
                    end
                    if not data then Sleep(50) end
                end
            end

            p:Close()
            consumer:Close()
            return tostring(sawNew) .. ':' .. tostring(sawOld)
        ");
        r.String.ShouldBe("true:false");
    }

    [KafkaFact]
    public async Task Producer_GetOffsets_ReturnsLowHigh()
    {
        LuaValue r = await Run("offsets_producer", @"
            local p = newProducer()
            local ok, low, high = p:GetOffsets(topic, part)
            p:Close()

            if not ok then return 'err:' .. tostring(low) end
            local valid = type(low) == 'number' and type(high) == 'number' and high >= low
            return tostring(valid) .. ' low=' .. low .. ' high=' .. high
        ");
        r.String.ShouldStartWith("true");
    }

    [KafkaFact]
    public async Task Consumer_GetOffsets_ReturnsLowHigh()
    {
        LuaValue r = await Run("offsets_consumer", @"
            local c = newConsumer()
            local ok, low, high = c:GetOffsets(topic, part)
            c:Close()

            if not ok then return 'err:' .. tostring(low) end
            local valid = type(low) == 'number' and type(high) == 'number' and high >= low
            return tostring(valid) .. ' low=' .. low .. ' high=' .. high
        ");
        r.String.ShouldStartWith("true");
    }

    [KafkaFact]
    public async Task Producer_GetOffsets_AfterSend_HighIncreases()
    {
        LuaValue r = await Run("offsets_send", @"
            local p = newProducer()

            local ok1, lo1, hi1 = p:GetOffsets(topic, part)
            assert(ok1, tostring(lo1))

            local ok, err = p:Send(topic, 'offs-key', 'offs-val', nil, part)
            assert(ok, err or 'Send failed')
            p:Close()

            -- Re-open a new producer to query (ensures fresh metadata); delivery is asynchronous
            local p2 = newProducer()
            local ok2, lo2, hi2
            local deadline = Time() + 15000
            repeat
                ok2, lo2, hi2 = p2:GetOffsets(topic, part)
                assert(ok2, tostring(lo2))
                if hi2 > hi1 then break end
                Sleep(100)
            until Time() > deadline
            p2:Close()

            return tostring(hi2 > hi1) .. ' before=' .. hi1 .. ' after=' .. hi2
        ");
        r.String.ShouldStartWith("true");
    }

    [KafkaFact]
    public async Task StressTest_OneProducerTwoConsumers_AssignedPartitions_ReceiveAllMessages()
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        string guid = Guid.NewGuid().ToString("N");
        const int countPerPartition = 50;
        const int totalCount = countPerPartition * 2;
        string stressTopic = Resource($"stress2_{guid}");
        string groupId = stressTopic;

        // Create a 2-partition topic and wait for it to appear in metadata so
        // each consumer can be assigned one partition.
        string createLua = $@"
            local p = Kafka.NewProducer({{['bootstrap.servers']='{Bootstrap()}'}})
            local ok, err = p:CreateTopic('{stressTopic}', 2)
            assert(ok, tostring(err))
            local dl = Time() + 15000
            while Time() < dl do
                local ok2, meta = p:GetMetadata()
                if ok2 then
                    for _, t in ipairs(meta.Topics) do
                        if t.Name == '{stressTopic}' then p:Close(); return 'ok' end
                    end
                end
                Sleep(100)
            end
            p:Close()
            error('topic did not appear in metadata')
        ";

        // Producer sends countPerPartition messages to each partition.
        // No need to wait for group stability since consumers use Assign, not Subscribe.
        string producerLua = $@"
            local p = Kafka.NewProducer({{['bootstrap.servers']='{Bootstrap()}'}})
            for i = 1, {countPerPartition} do
                local ok0, e0 = p:Send('{stressTopic}', '{guid}-0-' .. i, '{guid}-0-' .. i, nil, 0)
                assert(ok0, e0 or 'Send p0 ' .. i .. ' failed')
                local ok1, e1 = p:Send('{stressTopic}', '{guid}-1-' .. i, '{guid}-1-' .. i, nil, 1)
                assert(ok1, e1 or 'Send p1 ' .. i .. ' failed')
            end
            p:Close()
            return 'ok'
        ";

        // Each consumer uses Assign to a specific partition to bypass rebalance delay/skew.
        string MakeConsumerLua(int partition) => $@"
            local prefix = '{guid}-{partition}-'
            local need   = {countPerPartition}

            local consumer = Kafka.NewConsumer({{
                ['bootstrap.servers'] = '{Bootstrap()}',
                ['group.id']          = '{groupId}',
            }})
            assert(consumer:Assign({{'{stressTopic}:{partition}:earliest'}}))

            local seen     = {{}}
            local found    = 0
            local deadline = Time() + 90000

            while Time() < deadline do
                local ok, d = consumer:Poll()
                if not ok then error(tostring(d)) end
                if d and d.ErrorCode == 0 and d.Key
                   and string.sub(d.Key, 1, #prefix) == prefix
                   and not seen[d.Key] then
                    seen[d.Key] = true
                    found = found + 1
                    if found >= need then break end
                end
                if not d then Sleep(10) end
            end

            consumer:Close()
            return tostring(found)
        ";

        try
        {
            await engine.ExecuteStringAsync(createLua);

            // Start consumers and producer concurrently.  Consumers use Assign so they
            // start reading immediately without waiting for group rebalance.
            var consumer1Task = Task.Run(async () =>
            {
                using KitsuneEngine e = TestRun.NewEngine();
                return await e.ExecuteStringAsync(MakeConsumerLua(0)).ConfigureAwait(false);
            });
            var consumer2Task = Task.Run(async () =>
            {
                using KitsuneEngine e = TestRun.NewEngine();
                return await e.ExecuteStringAsync(MakeConsumerLua(1)).ConfigureAwait(false);
            });
            var producerTask = Task.Run(async () =>
            {
                using KitsuneEngine e = TestRun.NewEngine();
                return await e.ExecuteStringAsync(producerLua).ConfigureAwait(false);
            });
            await Task.WhenAll(producerTask, consumer1Task, consumer2Task);

            producerTask.Result.String.ShouldBe("ok");
            int c1 = int.Parse(consumer1Task.Result.String!);
            int c2 = int.Parse(consumer2Task.Result.String!);
            (c1 + c2).ShouldBe(totalCount);
        }
        finally
        {
            await CleanupAsync(engine, new[] { stressTopic });
        }
    }

    // Parses KITSUNE_KAFKA_TEST=host:port:topic:partition; only the broker address is used
    private static string Bootstrap()
    {
        var parts = Environment.GetEnvironmentVariable("KITSUNE_KAFKA_TEST")!.Split(':');
        return $"{parts[0]}:{parts[1]}";
    }

    private static string Resource(string suffix) => $"kitsune_{TestRun.Id}_{suffix}";

    // Runs the body against a fresh run-unique topic whose name doubles as the consumer group.
    private static Task<LuaValue> Run(string suffix, string body) =>
        Execute(suffix, Array.Empty<string>(), true, body);

    private static Task<LuaValue> Run(string suffix, string[] extraSuffixes, string body) =>
        Execute(suffix, extraSuffixes, true, body);

    private static Task<LuaValue> RunWithoutTopic(string suffix, string[] extraSuffixes, string body) =>
        Execute(suffix, extraSuffixes, false, body);

    private static async Task<LuaValue> Execute(string suffix, string[] extraSuffixes, bool createTopic, string body)
    {
        using KitsuneEngine engine = TestRun.NewEngine();
        try
        {
            string script =
                $"local bootstrap = '{Bootstrap()}'\n" +
                $"local topic = '{Resource(suffix)}'\n" +
                "local groupId = topic\n" +
                "local part = 0\n" +
                $"local createMainTopic = {(createTopic ? "true" : "false")}\n" +
                LuaPrelude + "\n" +
                body;
            return await engine.ExecuteStringAsync(script);
        }
        finally
        {
            await CleanupAsync(engine, extraSuffixes.Append(suffix).Select(Resource));
        }
    }

    // Deletes leftovers; groups still held by lingering closed consumers cannot be deleted
    // yet and are left for the broker to expire.
    private static async Task CleanupAsync(KitsuneEngine engine, IEnumerable<string> names)
    {
        try
        {
            string luaNames = string.Join(", ", names.Select(n => $"'{n}'"));
            await engine.ExecuteStringAsync($@"
                local p = Kafka.NewProducer({{['bootstrap.servers'] = '{Bootstrap()}'}})
                for _, name in ipairs({{{luaNames}}}) do
                    p:DestroyTopic(name)
                    p:DeleteGroup(name)
                end
                p:Close()
            ");
        }
        catch (Exception)
        {
        }
    }

    private static string LuaValueToString(LuaValue v) => v.Type switch
    {
        LuaType.String => v.String ?? string.Empty,
        LuaType.Integer => v.Int64.ToString(),
        LuaType.Number => v.Number.ToString(),
        LuaType.Boolean => v.Boolean ? "true" : "false",
        LuaType.Nil => "nil",
        LuaType.None => "nil",
        _ => v.String ?? v.Type.ToString(),
    };
}
