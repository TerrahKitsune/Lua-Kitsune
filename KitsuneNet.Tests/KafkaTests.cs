using KitsuneNet;
using Shouldly;
using Xunit;
using Xunit.Abstractions;

namespace KitsuneNet.Tests;

[Collection("KitsuneSequential")]
public sealed class KafkaTests
{
    private readonly ITestOutputHelper _output;

    public KafkaTests(ITestOutputHelper output) => _output = output;

    // -- creation -------------------------------------------------------------
    [KafkaFact]
    public async Task NewProducer_ValidConfig_ReturnsUserdata()
    {
        using KitsuneEngine engine = new();
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
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local c = Kafka.NewConsumer({{['bootstrap.servers']='{Bootstrap()}'}})
            assert(c, 'NewConsumer returned nil')
            return tostring(c):sub(1, 13)
        ");
        r.String.ShouldBe("KafkaConsumer");
    }

    [KafkaFact]
    public async Task Producer_Close_IsIdempotent()
    {
        using KitsuneEngine engine = new();
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
        using KitsuneEngine engine = new();
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
        using KitsuneEngine engine = new();

        // Trigger at least one librdkafka log event by connecting and disconnecting
        LuaValue r = await engine.ExecuteStringAsync($@"
            -- Produce to ensure the library emits at least some internal log lines
            local p = Kafka.NewProducer({{['bootstrap.servers']='{Bootstrap()}'}})
            p:Close()
            local logs1 = Kafka.Logs()
            local logs2 = Kafka.Logs()  -- second call must be empty after the clear
            return tostring(type(logs1) == 'string') .. ':' .. tostring(logs2 == '')
        ");

        // logs1 may or may not have content (depends on broker log level) but must be a string;
        // logs2 must always be empty after the first call clears the buffer
        r.String.ShouldStartWith("true:");
        r.String.ShouldEndWith(":true");
    }

    // -- producer -------------------------------------------------------------
    [KafkaFact]
    public async Task Producer_Send_ReturnsTrue()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local p = Kafka.NewProducer({{['bootstrap.servers']='{Bootstrap()}'}})
            assert(p, 'NewProducer failed')
            local ok, err = p:Send('{Topic()}', 'test-key', 'test-value')
            p:Close()
            return tostring(ok) .. ':' .. tostring(err == nil)
        ");
        r.String.ShouldBe("true:true");
    }

    [KafkaFact]
    public async Task Producer_Send_NilKey_ReturnsTrue()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local p = Kafka.NewProducer({{['bootstrap.servers']='{Bootstrap()}'}})
            assert(p, 'NewProducer failed')
            local ok, err = p:Send('{Topic()}', nil, 'keyless-value')
            p:Close()
            return tostring(ok) .. ':' .. tostring(err == nil)
        ");
        r.String.ShouldBe("true:true");
    }

    [KafkaFact]
    public async Task Producer_Send_WithHeaders_ReturnsTrue()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local p = Kafka.NewProducer({{['bootstrap.servers']='{Bootstrap()}'}})
            assert(p, 'NewProducer failed')
            local ok, err = p:Send('{Topic()}', 'hdr-key', 'hdr-value', {{source='kitsune-test', version='1'}})
            p:Close()
            return tostring(ok) .. ':' .. tostring(err == nil)
        ");
        r.String.ShouldBe("true:true");
    }

    [KafkaFact]
    public async Task Producer_Send_WithHeadersAndPartition_ReturnsTrue()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local p = Kafka.NewProducer({{['bootstrap.servers']='{Bootstrap()}'}})
            assert(p, 'NewProducer failed')
            -- Exercise the branch that requires BOTH explicit partition AND headers
            local ok, err = p:Send('{Topic()}', 'hdrpart-key', 'hdrpart-value',
                {{src='kitsune-test'}}, {Partition()})
            p:Close()
            return tostring(ok) .. ':' .. tostring(err == nil)
        ");
        r.String.ShouldBe("true:true");
    }

    [KafkaFact]
    public async Task Producer_Send_Headers_AreReceivedByConsumer()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}

            local producer = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            -- Snapshot the high-water mark so the consumer starts exactly here,
            -- avoiding any backlog on untracked partitions.
            local ok0, lo0, hi0 = producer:GetOffsets(topic, part)
            assert(ok0, tostring(lo0))

            local uniqueVal = 'hdr-recv-' .. tostring(Time())
            local ok, err = producer:Send(topic, 'hkey', uniqueVal,
                {{x='42', y='hello'}}, part)
            assert(ok, err or 'Send failed')
            producer:Close()

            local consumer = Kafka.NewConsumer({{
                ['bootstrap.servers'] = bootstrap,
                ['group.id']          = 'test_kitsune',
            }})
            local co = consumer:Assign({{topic .. ':' .. part .. ':' .. hi0}})

            local deadline = Time() + 15000
            local data = nil
            while Time() < deadline do
                local ok2, d = coroutine.resume(co, false)
                if not ok2 then error(tostring(d)) end
                if d and d.Value == uniqueVal then data = d; break end
                if not d then Sleep(50) end
            end

            coroutine.resume(co, true)
            consumer:Close()

            if not data then return 'no-message' end
            return tostring(data.Headers.x == '42' and data.Headers.y == 'hello')
        ");
        r.String.ShouldBe("true");
    }

    // -- consumer -------------------------------------------------------------
    [KafkaFact]
    public async Task Consumer_Subscribe_ReturnsThread()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local c = Kafka.NewConsumer({{
                ['bootstrap.servers'] = '{Bootstrap()}',
                ['group.id']          = 'test_kitsune'
            }})
            assert(c, 'NewConsumer failed')
            local co = c:Subscribe({{'{Topic()}'}})
            assert(co, 'Subscribe returned nil')
            c:Close()
            return type(co)
        ");
        r.String.ShouldBe("thread");
    }

    [KafkaFact]
    public async Task Consumer_Subscribe_ToMultipleTopics_ReturnsThread()
    {
        using KitsuneEngine engine = new();

        // Create a temporary second topic to subscribe to alongside the default one
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic1    = '{Topic()}'
            local topic2    = 'kitsune-multi-' .. tostring(Time())

            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            local ok, err = p:CreateTopic(topic2, 1)
            assert(ok, tostring(err))
            p:Close()

            local c = Kafka.NewConsumer({{
                ['bootstrap.servers'] = bootstrap,
                ['group.id']          = 'test_kitsune'
            }})
            local co = c:Subscribe({{topic1, topic2}})
            local ok2 = co ~= nil
            if co then coroutine.resume(co, true) end
            c:Close()

            local p2 = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            p2:DestroyTopic(topic2)
            p2:Close()

            return tostring(ok2) .. ':' .. tostring(type(co) == 'thread')
        ");
        r.String.ShouldBe("true:true");
    }

    [KafkaFact]
    public async Task Consumer_Assign_ReturnsThread()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local c = Kafka.NewConsumer({{
                ['bootstrap.servers'] = '{Bootstrap()}',
                ['group.id']          = 'test_kitsune'
            }})
            assert(c, 'NewConsumer failed')
            local co = c:Assign({{'{Topic()}:0'}})
            assert(co, 'Assign returned nil')
            c:Close()
            return type(co)
        ");
        r.String.ShouldBe("thread");
    }

    // -- round-trip -----------------------------------------------------------
    [KafkaFact]
    public async Task Consumer_RoundTrip_ReceivesProducedMessage()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}
            local uniqueVal = 'kitsune-rt-' .. tostring(Time())

            local producer = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            -- Snapshot before producing so the consumer starts exactly at this message.
            local ok0, lo0, hi0 = producer:GetOffsets(topic, part)
            assert(ok0, tostring(lo0))
            local ok, err = producer:Send(topic, 'rt-key', uniqueVal, nil, part)
            assert(ok, err or 'Send failed')
            producer:Close()

            local consumer = Kafka.NewConsumer({{
                ['bootstrap.servers'] = bootstrap,
                ['group.id']          = 'test_kitsune',
            }})
            local co = consumer:Assign({{topic .. ':' .. part .. ':' .. hi0}})

            local deadline = Time() + 15000
            local received = nil
            while Time() < deadline do
                local ok2, data = coroutine.resume(co, false)
                if not ok2 then error(tostring(data)) end
                if data and data.Value == uniqueVal then
                    received = data.Value
                    break
                end
                if not data then Sleep(50) end
            end

            coroutine.resume(co, true)
            consumer:Close()
            return tostring(received ~= nil)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task Consumer_RoundTrip_MessageFieldsArePopulated()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}
            local uniqueVal = 'kitsune-fields-' .. tostring(Time())

            local producer = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            -- Snapshot before producing so the consumer starts exactly at this message.
            local ok0, lo0, hi0 = producer:GetOffsets(topic, part)
            assert(ok0, tostring(lo0))
            local ok, err = producer:Send(topic, 'fields-key', uniqueVal, nil, part)
            assert(ok, err or 'Send failed')
            producer:Close()

            local consumer = Kafka.NewConsumer({{
                ['bootstrap.servers'] = bootstrap,
                ['group.id']          = 'test_kitsune',
            }})
            local co = consumer:Assign({{topic .. ':' .. part .. ':' .. hi0}})

            local deadline = Time() + 15000
            local data = nil
            while Time() < deadline do
                local ok2, d = coroutine.resume(co, false)
                if not ok2 then error(tostring(d)) end
                if d and d.Value == uniqueVal then
                    data = d
                    break
                end
                if not d then Sleep(50) end
            end

            coroutine.resume(co, true)
            consumer:Close()

            if not data then return 'no-message' end
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

    // -- manual commit ---------------------------------------------------------
    [KafkaFact]
    public async Task Consumer_ManualCommit_CommitsAndPreventsDoubleCommit()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}
            local uniqueVal = 'kitsune-commit-' .. tostring(Time())

            local producer = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            -- Snapshot before producing so the consumer starts exactly at this message.
            local ok0, lo0, hi0 = producer:GetOffsets(topic, part)
            assert(ok0, tostring(lo0))
            local ok, err = producer:Send(topic, 'commit-key', uniqueVal, nil, part)
            assert(ok, err or 'Send failed')
            producer:Close()

            local consumer = Kafka.NewConsumer({{
                ['bootstrap.servers'] = bootstrap,
                ['group.id']          = 'test_kitsune',
            }})
            local co = consumer:Assign({{topic .. ':' .. part .. ':' .. hi0}})
            co:AutoCommit(false)

            local deadline = Time() + 15000
            local found = false
            while Time() < deadline do
                local ok2, d = coroutine.resume(co, false)
                if not ok2 then error(tostring(d)) end
                if d and d.Value == uniqueVal then found = true; break end
                if not d then Sleep(50) end
            end

            coroutine.resume(co, true)

            if not found then
                consumer:Close()
                return 'no-message'
            end

            -- First commit must succeed (consumer holds pending after the coroutine stop)
            local ok1, e1 = consumer:Commit()
            -- Second commit must fail (pending was cleared by the first)
            local ok2, e2 = consumer:Commit()

            consumer:Close()
            return tostring(ok1) .. ':' .. tostring(ok2) .. ':' .. tostring(type(e2) == 'string')
        ");
        r.String.ShouldBe("true:false:true");
    }

    // -- AutoCommit toggle -----------------------------------------------------
    [KafkaFact]
    public async Task Consumer_Assign_Partition_RoundTrip()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}
            local runId     = tostring(Time())
            local count     = 5
            local prefix    = 'assign-p' .. part .. '-' .. runId .. '-'

            -- Send all messages to the configured partition explicitly
            local producer = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            -- Snapshot the high-water mark before producing so the consumer starts
            -- exactly at our messages rather than scanning from the beginning.
            local ok_hw, lo_hw, hi_hw = producer:GetOffsets(topic, part)
            assert(ok_hw, tostring(lo_hw))
            for i = 1, count do
                local ok, err = producer:Send(topic, 'pk-' .. i, prefix .. i, nil, part)
                assert(ok, err or 'Send ' .. i .. ' failed on partition ' .. part)
            end
            producer:Close()

            -- Assign starting from exactly where our messages begin
            local consumer = Kafka.NewConsumer({{
                ['bootstrap.servers'] = bootstrap,
                ['group.id']          = 'test_kitsune',
            }})
            local co = consumer:Assign({{topic .. ':' .. part .. ':' .. hi_hw}})

            -- Receive exactly the messages we just produced; no historical backlog
            local deadline = Time() + 15000
            local found = 0
            while Time() < deadline do
                local ok2, data = coroutine.resume(co, false)
                if not ok2 then error(tostring(data)) end
                if data and data.ErrorCode == 0 and
                   string.sub(data.Value, 1, #prefix) == prefix then
                    found = found + 1
                    if found >= count then break end
                end
                if not data then Sleep(50) end
            end

            coroutine.resume(co, true)
            consumer:Close()
            return tostring(found) .. '/' .. tostring(count)
        ");
        r.String.ShouldBe("5/5");
    }

    // -- Group operations ------------------------------------------------------
    [KafkaFact]
    public async Task Producer_ListGroups_ReturnsArray()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local p = Kafka.NewProducer({{['bootstrap.servers']='{Bootstrap()}'}})
            local ok, groups = p:ListGroups()
            p:Close()
            if not ok then return 'err:' .. tostring(groups) end
            return tostring(type(groups) == 'table')
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task Producer_DescribeGroups_ReturnsDescription()
    {
        using KitsuneEngine engine = new();

        // First produce a message so the test group is created, then describe it
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local groupId   = 'test_kitsune'

            -- Spin up a real consumer to ensure the group exists
            local consumer = Kafka.NewConsumer({{
                ['bootstrap.servers'] = bootstrap,
                ['group.id']          = groupId,
                ['auto.offset.reset'] = 'latest'
            }})
            local co = consumer:Subscribe({{topic}})
            coroutine.resume(co, false)  -- one poll to register
            coroutine.resume(co, true)
            consumer:Close()

            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            local ok, descs = p:DescribeGroups({{groupId}})
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
        using KitsuneEngine engine = new();

        // Create a group with committed offsets, then query them
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}
            local groupId   = 'test_kitsune'
            local uniqueVal = 'getoff-' .. tostring(Time())

            -- Produce a message and consume + commit it to create committed offsets.
            -- Snapshot before producing so the consumer starts exactly at this message.
            local producer = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            local ok0, lo0, hi0 = producer:GetOffsets(topic, part)
            assert(ok0, tostring(lo0))
            producer:Send(topic, nil, uniqueVal, nil, part)
            producer:Close()

            local consumer = Kafka.NewConsumer({{
                ['bootstrap.servers'] = bootstrap,
                ['group.id']          = groupId,
            }})
            local co = consumer:Assign({{topic .. ':' .. part .. ':' .. hi0}})
            co:AutoCommit(false)

            local deadline = Time() + 10000
            local data = nil
            while Time() < deadline do
                local ok2, d = coroutine.resume(co, false)
                if not ok2 then error(tostring(d)) end
                if d and d.Value == uniqueVal then data = d; break end
                if not d then Sleep(50) end
            end
            coroutine.resume(co, true)

            if data then consumer:Commit() end
            consumer:Close()

            if not data then return 'no-message' end

            -- Query the committed offsets
            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            local ok, offsets = p:GetGroupOffsets(groupId)
            p:Close()
            if not ok then return 'err:' .. tostring(offsets) end
            return tostring(type(offsets) == 'table' and next(offsets) ~= nil)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task Producer_SetGroupOffsets_ResetsPosition()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}
            local groupId   = 'test_kitsune'

            -- Snapshot the current high watermark before any messages
            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            local ok0, lo0, hi0 = p:GetOffsets(topic, part)
            assert(ok0, tostring(lo0))

            -- Reset the group's offset for this partition to hi0 (current end)
            local key = topic .. ':' .. part
            local ok, err = p:SetGroupOffsets(groupId, {{[key] = hi0}})
            p:Close()

            if not ok then return 'set-failed:' .. tostring(err) end

            -- Read back to confirm
            local p2 = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            local ok2, offsets = p2:GetGroupOffsets(groupId, {{key}})
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
        using KitsuneEngine engine = new();
        const string groupId = "test_kitsune_delgrp";
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local groupId   = '{groupId}'

            -- Create a committed offset so the group exists in the broker
            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            local key = topic .. ':0'
            local ok0, lo0, hi0 = p:GetOffsets(topic, 0)
            assert(ok0, tostring(lo0))
            local ok, err = p:SetGroupOffsets(groupId, {{[key] = hi0}})
            assert(ok, tostring(err))

            -- Verify it appears in list
            local ok2, groups = p:ListGroups()
            assert(ok2, tostring(groups))
            local found = false
            for _, g in ipairs(groups) do
                if g.GroupId == groupId then found = true; break end
            end

            -- Delete it
            local ok3, err3 = p:DeleteGroup(groupId)

            p:Close()
            return tostring(found) .. ':' .. tostring(ok3)
        ");
        r.String.ShouldBe("true:true");
    }

    [KafkaFact]
    public async Task Producer_DeleteGroupOffsets_RemovesOffsets()
    {
        using KitsuneEngine engine = new();
        const string groupId = "test_kitsune_deloff";
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}
            local groupId   = '{groupId}'

            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            local key = topic .. ':' .. part
            local ok0, lo0, hi0 = p:GetOffsets(topic, part)
            assert(ok0, tostring(lo0))

            -- Create committed offset
            local ok, err = p:SetGroupOffsets(groupId, {{[key] = hi0}})
            assert(ok, tostring(err))

            -- Confirm it exists
            local ok2, before = p:GetGroupOffsets(groupId, {{key}})
            assert(ok2, tostring(before))
            local hadOffset = before[key] ~= nil

            -- Delete the committed offset
            local ok3, err3 = p:DeleteGroupOffsets(groupId, {{key}})

            -- After deletion the offset should be INVALID (-1001) or absent
            local ok4, after = p:GetGroupOffsets(groupId, {{key}})
            assert(ok4, tostring(after))
            local goneOrInvalid = after[key] == nil or after[key] == -1001

            p:Close()
            return tostring(hadOffset) .. ':' .. tostring(ok3) .. ':' .. tostring(goneOrInvalid)
        ");
        r.String.ShouldBe("true:true:true");
    }

    // -- GetTopicConfig / SetTopicConfig --------------------------------------
    [KafkaFact]
    public async Task Producer_GetTopicConfig_ReturnsTable()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
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
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topicName = 'kitsune-cfg-' .. tostring(Time())

            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})

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
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local c = Kafka.NewConsumer({{['bootstrap.servers'] = bootstrap}})
            local ok, cfg = c:GetTopicConfig(topic)
            c:Close()
            if not ok then return 'err:' .. tostring(cfg) end
            return tostring(type(cfg) == 'table' and cfg['retention.ms'] ~= nil)
        ");
        r.String.ShouldBe("true");
    }

    // -- CreateTopic / DestroyTopic --------------------------------------------
    [KafkaFact]
    public async Task Producer_CreateTopic_And_DestroyTopic()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topicName = 'kitsune-test-' .. tostring(Time())

            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})

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
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topicName = 'kitsune-ret-' .. tostring(Time())

            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})

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
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            local ok, err = p:DestroyTopic('kitsune-nonexistent-' .. tostring(Time()))
            p:Close()
            return tostring(not ok) .. ':' .. tostring(type(err) == 'string')
        ");
        r.String.ShouldBe("true:true");
    }

    // -- Assign with offset / Seek ---------------------------------------------
    [KafkaFact]
    public async Task Consumer_Assign_WithExplicitOffset_StartsFromThatOffset()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}
            local runId     = tostring(Time())
            local count     = 4
            local prefix    = 'seek-offset-' .. runId .. '-'

            local producer = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})

            -- Snapshot the high-water mark before producing
            local ok0, lo0, hi0 = producer:GetOffsets(topic, part)
            assert(ok0, tostring(lo0))

            -- Produce count messages to the configured partition
            for i = 1, count do
                local ok, err = producer:Send(topic, nil, prefix .. i, nil, part)
                assert(ok, err or 'Send ' .. i .. ' failed')
            end
            producer:Close()

            -- Assign to the partition starting at the pre-produce high-water mark
            -- so we receive exactly our count messages and nothing before them
            local consumer = Kafka.NewConsumer({{
                ['bootstrap.servers'] = bootstrap,
                ['group.id']          = 'test_kitsune',
                ['auto.offset.reset'] = 'latest'
            }})
            local co = consumer:Assign({{topic .. ':' .. part .. ':' .. hi0}})

            local deadline = Time() + 15000
            local found = 0
            while Time() < deadline do
                local ok2, data = coroutine.resume(co, false)
                if not ok2 then error(tostring(data)) end
                if data and data.ErrorCode == 0 and
                   string.sub(data.Value, 1, #prefix) == prefix then
                    found = found + 1
                    if found >= count then break end
                end
                if not data then Sleep(50) end
            end

            coroutine.resume(co, true)
            consumer:Close()
            return tostring(found) .. '/' .. tostring(count)
        ");
        r.String.ShouldBe("4/4");
    }

    [KafkaFact]
    public async Task Consumer_Assign_WithEarliestKeyword_ReceivesMessages()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}

            -- 'earliest' in the Assign string sets OFFSET_BEGINNING directly,
            -- independent of auto.offset.reset config
            local consumer = Kafka.NewConsumer({{
                ['bootstrap.servers'] = bootstrap,
                ['group.id']          = 'test_kitsune',
                ['auto.offset.reset'] = 'latest'   -- config says latest…
            }})
            -- …but the ':earliest' keyword in the Assign string overrides it
            local co = consumer:Assign({{topic .. ':' .. part .. ':earliest'}})

            local deadline = Time() + 10000
            local received = 0
            while Time() < deadline do
                local ok2, data = coroutine.resume(co, false)
                if not ok2 then error(tostring(data)) end
                if data and data.ErrorCode == 0 then
                    received = received + 1
                    if received >= 1 then break end
                end
                if not data then Sleep(50) end
            end

            coroutine.resume(co, true)
            consumer:Close()
            return tostring(received >= 1)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task Consumer_Seek_RepositionsToSpecificOffset()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}
            local runId     = tostring(Time())
            local count     = 3
            local prefix    = 'seek-specific-' .. runId .. '-'

            local producer = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})

            -- Snapshot the current end — our messages will land at this offset onwards
            local ok0, lo0, hi0 = producer:GetOffsets(topic, part)
            assert(ok0, tostring(lo0))

            for i = 1, count do
                local ok, err = producer:Send(topic, nil, prefix .. i, nil, part)
                assert(ok, err or 'Send ' .. i .. ' failed')
            end
            producer:Close()

            -- Assign from earliest so the partition is active from the start
            local consumer = Kafka.NewConsumer({{
                ['bootstrap.servers'] = bootstrap,
                ['group.id']          = 'test_kitsune',
                ['auto.offset.reset'] = 'earliest'
            }})
            local co = consumer:Assign({{topic .. ':' .. part}})

            -- Warm up: drive polls for a fixed 3 s window to ensure librdkafka has
            -- fully established the partition assignment before Seek is called.
            -- On Linux, early responses can arrive before the internal state is ready;
            -- a time-based loop is safer than breaking after the first poll.
            local warmDeadline = Time() + 3000
            while Time() < warmDeadline do
                coroutine.resume(co, false)
                Sleep(50)
            end

            -- Seek to hi0 (the pre-produce high-water mark) — this skips all older
            -- messages and positions us exactly at the first message we produced
            local ok, err = consumer:Seek(topic, part, hi0)
            assert(ok, err or 'Seek failed')

            -- Now collect exactly our count messages
            local deadline = Time() + 15000
            local found = 0
            while Time() < deadline do
                local ok2, data = coroutine.resume(co, false)
                if not ok2 then error(tostring(data)) end
                if data and data.ErrorCode == 0 and
                   string.sub(data.Value, 1, #prefix) == prefix then
                    found = found + 1
                    if found >= count then break end
                end
                if not data then Sleep(50) end
            end

            coroutine.resume(co, true)
            consumer:Close()
            return tostring(found) .. '/' .. tostring(count)
        ");
        r.String.ShouldBe("3/3");
    }

    [KafkaFact]
    public async Task Consumer_DroppedBeforeCoroutineFinishes_DoesNotCrash()
    {
        using KitsuneEngine engine = new();

        // Regression for the consumer lifetime bug:
        // if the consumer is GC'd while a coroutine still holds a reference to
        // state->owner, the next poll would use-after-free. The registry anchor
        // introduced to fix that must keep the consumer alive.
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'

            local co
            do
                local consumer = Kafka.NewConsumer({{
                    ['bootstrap.servers'] = bootstrap,
                    ['group.id']          = 'test_kitsune',
                    ['auto.offset.reset'] = 'latest'
                }})
                co = consumer:Subscribe({{topic}})
                -- consumer goes out of scope here; the local block ends
            end

            -- Force a collection cycle; consumer should NOT be destroyed yet
            -- because the coroutine still holds its registry anchor
            collectgarbage('collect')

            -- Drive one poll — must not crash or error
            local ok, data = coroutine.resume(co, false)
            coroutine.resume(co, true)  -- clean stop releases the anchor

            return tostring(ok)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task Consumer_GetGroupOffsets_WithPartitionFilter()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}
            local groupId   = 'test_kitsune'
            local key       = topic .. ':' .. part

            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            local ok0, lo0, hi0 = p:GetOffsets(topic, part)
            assert(ok0, tostring(lo0))
            local ok, err = p:SetGroupOffsets(groupId, {{[key] = hi0}})
            assert(ok, tostring(err))

            -- GetGroupOffsets with an explicit partition list
            local ok2, offsets = p:GetGroupOffsets(groupId, {{key}})
            p:Close()
            if not ok2 then return 'err:' .. tostring(offsets) end
            return tostring(offsets[key] == hi0)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task Producer_DescribeGroups_MultipleGroups()
    {
        using KitsuneEngine engine = new();
        const string g1 = "test_kitsune";
        const string g2 = "test_kitsune2";
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}
            local g1        = '{g1}'
            local g2        = '{g2}'

            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            local ok0, lo0, hi0 = p:GetOffsets(topic, part)
            assert(ok0, tostring(lo0))
            local key = topic .. ':' .. part

            -- Create both groups via committed offsets
            local ok1, e1 = p:SetGroupOffsets(g1, {{[key] = hi0}})
            local ok2, e2 = p:SetGroupOffsets(g2, {{[key] = hi0}})
            assert(ok1, tostring(e1))
            assert(ok2, tostring(e2))

            local ok, descs = p:DescribeGroups({{g1, g2}})
            p:Close()
            if not ok then return 'err:' .. tostring(descs) end
            return tostring(#descs == 2)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task CreateTopic_WithReplicationFactor_Succeeds()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topicName = 'kitsune-rf-' .. tostring(Time())

            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            -- replication_factor = 1 (arg 5), timeout = default
            local ok, err = p:CreateTopic(topicName, 1, nil, nil, 1)
            if not ok then p:Close(); return 'failed:' .. tostring(err) end

            p:DestroyTopic(topicName)
            p:Close()
            return tostring(ok)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task GetTopicConfig_NonExistentTopic_ReturnsError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local p = Kafka.NewProducer({{['bootstrap.servers'] = '{Bootstrap()}'}})
            local ok, cfg = p:GetTopicConfig('kitsune-does-not-exist-' .. tostring(Time()))
            p:Close()
            return tostring(not ok) .. ':' .. tostring(type(cfg) == 'string')
        ");
        r.String.ShouldBe("true:true");
    }

    [KafkaFact]
    public async Task Consumer_Seek_WithEarliestKeyword_ReceivesMessages()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}
            local runId     = tostring(Time())

            local consumer = Kafka.NewConsumer({{
                ['bootstrap.servers'] = bootstrap,
                ['group.id']          = 'test_kitsune',
                ['auto.offset.reset'] = 'latest'
            }})
            local co = consumer:Assign({{topic .. ':' .. part}})

            -- Warm-up: drive polls for a fixed 5 s window so librdkafka has fully
            -- established the Assign-based partition assignment before Seek is called.
            -- On Linux, early nil responses can arrive before the assignment is stable;
            -- a time-based loop avoids breaking out prematurely.
            local warmDeadline = Time() + 5000
            while Time() < warmDeadline do
                coroutine.resume(co, false)
                Sleep(50)
            end

            -- Seek to earliest using the string keyword
            local ok, err = consumer:Seek(topic, part, 'earliest')
            assert(ok, err or 'Seek failed')

            -- Should now receive messages from the beginning of the partition
            local deadline = Time() + 10000
            local received = 0
            while Time() < deadline do
                local ok2, data = coroutine.resume(co, false)
                if not ok2 then error(tostring(data)) end
                if data and data.ErrorCode == 0 then
                    received = received + 1
                    break
                end
                if not data then Sleep(50) end
            end

            coroutine.resume(co, true)
            consumer:Close()
            return tostring(received >= 1)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task Consumer_Assign_WithLatestKeyword_NoOldMessages()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}
            local runId     = tostring(Time())

            -- 'latest' in the Assign string should mean no old messages come through
            local consumer = Kafka.NewConsumer({{
                ['bootstrap.servers'] = bootstrap,
                ['group.id']          = 'test_kitsune',
                ['auto.offset.reset'] = 'earliest'  -- config says earliest...
            }})
            -- ...but ':latest' in the string overrides it
            local co = consumer:Assign({{topic .. ':' .. part .. ':latest'}})

            -- Poll for 2 seconds; no old messages should arrive
            local deadline = Time() + 2000
            local received = 0
            while Time() < deadline do
                local ok2, data = coroutine.resume(co, false)
                if not ok2 then error(tostring(data)) end
                if data and data.ErrorCode == 0 then received = received + 1 end
                if not data then Sleep(50) end
            end

            coroutine.resume(co, true)
            consumer:Close()
            return tostring(received == 0)
        ");
        r.String.ShouldBe("true");
    }

    [KafkaFact]
    public async Task Producer_GetOffsets_ReturnsLowHigh()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}

            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
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
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}

            local c = Kafka.NewConsumer({{['bootstrap.servers'] = bootstrap}})
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
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local bootstrap = '{Bootstrap()}'
            local topic     = '{Topic()}'
            local part      = {Partition()}

            local p = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})

            local ok1, lo1, hi1 = p:GetOffsets(topic, part)
            assert(ok1, tostring(lo1))

            local ok, err = p:Send(topic, 'offs-key', 'offs-val', nil, part)
            assert(ok, err or 'Send failed')
            p:Close()

            -- Re-open a new producer to query (ensures fresh metadata)
            local p2 = Kafka.NewProducer({{['bootstrap.servers'] = bootstrap}})
            local ok2, lo2, hi2 = p2:GetOffsets(topic, part)
            p2:Close()
            assert(ok2, tostring(lo2))

            return tostring(hi2 > hi1) .. ' before=' .. hi1 .. ' after=' .. hi2
        ");
        r.String.ShouldStartWith("true");
    }

    [KafkaFact]
    public async Task Consumer_AutoCommitToggle_DoesNotError()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local c = Kafka.NewConsumer({{
                ['bootstrap.servers'] = '{Bootstrap()}',
                ['group.id']          = 'test_kitsune'
            }})
            local co = c:Subscribe({{'{Topic()}'}})
            co:AutoCommit(false)
            co:AutoCommit(true)
            coroutine.resume(co, true)
            c:Close()
            return 'ok'
        ");
        r.String.ShouldBe("ok");
    }

    [KafkaFact]
    public async Task StressTest_OneProducerTwoConsumers_SharedGroupSplitPartitions()
    {
        using KitsuneEngine engine = new();
        string guid = Guid.NewGuid().ToString("N");
        const int countPerPartition = 50;
        const int totalCount = countPerPartition * 2;
        string stressTopic = $"stress2-{guid}";
        string groupId = $"stress2-{guid}";

        // Create a 2-partition topic and wait for it to appear in metadata so
        // both consumers can each be assigned one partition by the group coordinator.
        await engine.ExecuteStringAsync($@"
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
        ");

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
            local co = consumer:Assign({{'{stressTopic}:{partition}:earliest'}})

            local seen     = {{}}
            local found    = 0
            local deadline = Time() + 90000

            while Time() < deadline do
                local ok, d = coroutine.resume(co, false)
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

            coroutine.resume(co, true)
            consumer:Close()
            return tostring(found)
        ";

        try
        {
            // Start consumers and producer concurrently.  Consumers use Assign so they
            // start reading immediately without waiting for group rebalance.
            var consumer1Task = Task.Run(async () =>
            {
                using KitsuneEngine e = new();
                return await e.ExecuteStringAsync(MakeConsumerLua(0)).ConfigureAwait(false);
            });
            var consumer2Task = Task.Run(async () =>
            {
                using KitsuneEngine e = new();
                return await e.ExecuteStringAsync(MakeConsumerLua(1)).ConfigureAwait(false);
            });
            var producerTask = Task.Run(async () =>
            {
                using KitsuneEngine e = new();
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
            await engine.ExecuteStringAsync($@"
                local p = Kafka.NewProducer({{['bootstrap.servers']='{Bootstrap()}'}})
                p:DestroyTopic('{stressTopic}')
                p:Close()
            ");
        }
    }

    // -- Cleanup utility -------------------------------------------------------
    [KafkaFact]
    public async Task Cleanup_DeleteTestConsumerGroups()
    {
        using KitsuneEngine engine = new();
        LuaValue r = await engine.ExecuteStringAsync($@"
            local p = Kafka.NewProducer({{['bootstrap.servers']='{Bootstrap()}'}})

            local function startsWith(s, prefix)
                return string.sub(s, 1, #prefix) == prefix
            end

            local function isTestGroup(id)
                return startsWith(id, 'test_')
                    or startsWith(id, 'stress-')
                    or startsWith(id, 'stress2-')
                    or startsWith(id, 'kitsune-')
            end

            local lines   = {{}}
            local deleted = 0

            -- Retry up to 6 times with 10 s gaps.  Ghost members from consumers
            -- closed via NO_CONSUMER_CLOSE remain Active until the session timeout
            -- (~45 s) expires; waiting in short increments lets us delete each
            -- group as soon as the broker marks it Empty, without a single long wait.
            for attempt = 1, 6 do
                local ok, groups = p:ListGroups()
                assert(ok, tostring(groups))

                local pending = 0
                for _, g in ipairs(groups) do
                    local id = g.GroupId
                    if isTestGroup(id) then
                        local ok2, err2 = p:DeleteGroup(id)
                        if ok2 then
                            deleted = deleted + 1
                            lines[#lines+1] = 'DELETED  ' .. id
                        else
                            pending = pending + 1
                            lines[#lines+1] = 'SKIPPED  ' .. id .. ' (' .. tostring(err2) .. ')'
                        end
                    end
                end

                if pending == 0 then break end
                lines[#lines+1] = '--- attempt ' .. attempt .. ' done, ' .. pending .. ' still active, waiting 10 s ---'
                if attempt < 6 then Sleep(10000) end
            end

            p:Close()
            lines[#lines+1] = 'total deleted: ' .. deleted
            local result = table.concat(lines, '\n');
            print(result)
            return result
        ");
        r.String.ShouldNotBeNull();
        r.String.ShouldContain("total deleted:");
    }

    // Parses KITSUNE_KAFKA_TEST=host:port:topic:partition
    private static string Bootstrap()
    {
        var parts = Environment.GetEnvironmentVariable("KITSUNE_KAFKA_TEST")!.Split(':');
        return $"{parts[0]}:{parts[1]}";
    }

    private static string Topic()
    {
        var parts = Environment.GetEnvironmentVariable("KITSUNE_KAFKA_TEST")!.Split(':');
        return parts[2];
    }

    private static int Partition()
    {
        var parts = Environment.GetEnvironmentVariable("KITSUNE_KAFKA_TEST")!.Split(':');
        return parts.Length > 3 && int.TryParse(parts[3], out int p) ? p : 0;
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
