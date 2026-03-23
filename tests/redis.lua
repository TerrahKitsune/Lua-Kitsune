local helpers = require("tests.helpers")
local assert_table = helpers.assert_table
local run = helpers.run
local skip = helpers.skip

local redisConfig = require("tests.config").redis

run("Redis table exists", function()
    assert_table(Redis, "Redis")
end)

if not redisConfig.enabled then
    skip("Redis suite", "set config.redis.enabled = true to run Redis tests")
    return
end

local KEY_STR    = "Kitsune:test:str"
local KEY_HASH   = "Kitsune:test:hash"
local KEY_LIST   = "Kitsune:test:list"
local KEY_SET    = "Kitsune:test:set"
local KEY_ZSET   = "Kitsune:test:zset"
local KEY_STREAM = "Kitsune:test:stream"
local KEY_KEY    = "Kitsune:test:key"
local KEY_CMD    = "Kitsune:test:cmd"
local KEY_ITER1  = "Kitsune:test:iter1"
local KEY_ITER2  = "Kitsune:test:iter2"
local KEY_ITER3  = "Kitsune:test:iter3"

local redis

run("Redis.Open connects", function()
    redis = Redis.Open(redisConfig.host, redisConfig.port, redisConfig.useTls, redisConfig.timeout, redisConfig.sslOptions, redisConfig.password)
    assert(redis, "Redis.Open failed")
end)

if not redis then
    skip("Redis suite", "connection failed, skipping remaining tests")
    return
end

-- ── Setup ─────────────────────────────────────────────────────────────────────

run("Redis pre-test cleanup", function()
    redis:Command("DEL", KEY_STR, KEY_HASH, KEY_LIST, KEY_SET, KEY_ZSET, KEY_STREAM, KEY_KEY, KEY_CMD, KEY_ITER1, KEY_ITER2, KEY_ITER3)
end)

-- ── Command ───────────────────────────────────────────────────────────────────

run("Redis Command PING returns status", function()
    local reply = redis:Command("PING")
    assert(reply, "PING returned nil")
    assert(reply.Type == 5, "PING should return status (type 5), got: " .. tostring(reply.Type))
end)

run("Redis Command SET and GET", function()
    redis:Command("SET", KEY_STR, "raw")
    local reply = redis:Command("GET", KEY_STR)
    assert(reply and reply.Value == "raw", "expected 'raw', got: " .. tostring(reply and reply.Value))
    redis:Command("DEL", KEY_STR)
end)

-- ── String ────────────────────────────────────────────────────────────────────

run("Redis String Set and read via tostring", function()
    local s = redis:GetString(KEY_STR)
    s:Set("hello")
    assert(tostring(s) == "hello", "expected 'hello', got: " .. tostring(s))
end)

run("Redis String Set returns nil when key is new", function()
    redis:Command("DEL", KEY_STR)
    local s = redis:GetString(KEY_STR)
    local old = s:Set("first")
    assert(old == nil, "expected nil for new key, got: " .. tostring(old))
    assert(tostring(s) == "first", "expected 'first' after Set, got: " .. tostring(s))
end)

run("Redis String Set returns previous value", function()
    local s = redis:GetString(KEY_STR)
    local old = s:Set("world")
    assert(old == "first", "expected old value 'first', got: " .. tostring(old))
    assert(tostring(s) == "world", "expected 'world' after Set, got: " .. tostring(s))
end)

run("Redis String length via #", function()
    local s = redis:GetString(KEY_STR)
    assert(#s == 5, "expected length 5 ('world'), got: " .. tostring(#s))
end)

run("Redis String SetTTL and GetTTL", function()
    local s = redis:GetString(KEY_STR)
    s:SetTTL(3600)
    local ttl = s:GetTTL()
    assert(type(ttl) == "number" and ttl > 0, "expected positive TTL, got: " .. tostring(ttl))
    s:SetTTL(-1)
end)

run("Redis String Delete returns value and clears key", function()
    local s = redis:GetString(KEY_STR)
    local deleted = s:Delete()
    assert(deleted == "world", "expected 'world' from Delete, got: " .. tostring(deleted))
    assert(tostring(s) == "", "expected empty string after Delete, got: " .. tostring(s))
end)

run("Redis String Set distinguishes nil (new key) from empty-string value", function()
    -- Before fix: lua_pushlstring(NULL,0) silently pushed "" for a new key,
    -- making it impossible to tell apart "key was new" from "old value was empty string"
    redis:Command("DEL", KEY_STR)
    local s = redis:GetString(KEY_STR)
    local first = s:Set("")             -- new key → old value must be nil, not ""
    assert(first == nil, "new key should return nil, got: " .. tostring(first))
    local second = s:Set("x")          -- key holds "" → old value must be ""
    assert(second == "", "empty-string old value should return '', got: " .. tostring(second))
    local third = s:Set("")            -- key holds "x" → old value must be "x"
    assert(third == "x", "expected 'x', got: " .. tostring(third))
    redis:Command("DEL", KEY_STR)
end)

run("Redis String tostring on non-existent key returns empty string", function()
    redis:Command("DEL", KEY_STR)
    local s = redis:GetString(KEY_STR)
    assert(tostring(s) == "", "non-existent key should tostring to '', got: " .. tostring(s))
end)

run("Redis String GetOrSet sets when absent and keeps when present", function()
    redis:Command("DEL", KEY_STR)
    local s = redis:GetString(KEY_STR)
    assert(s:GetOrSet("default") == "default", "GetOrSet should set and return 'default' for absent key")
    assert(s:GetOrSet("other")   == "default", "GetOrSet should not overwrite existing key")
    redis:Command("DEL", KEY_STR)
end)

run("Redis String byte read and write via index operator", function()
    redis:Command("SET", KEY_STR, "ABC")
    local s = redis:GetString(KEY_STR)
    assert(s[1] == 65, "byte 1 should be 65 ('A'), got: " .. tostring(s[1]))
    assert(s[2] == 66, "byte 2 should be 66 ('B'), got: " .. tostring(s[2]))
    assert(s[3] == 67, "byte 3 should be 67 ('C'), got: " .. tostring(s[3]))
    assert(s[99] == nil, "out-of-range byte should return nil")
    s[1] = 88   -- 'X'
    assert(s[1] == 88, "byte write should update position 1 to 88 ('X')")
    assert(tostring(s) == "XBC", "expected 'XBC' after byte write, got: " .. tostring(s))
    redis:Command("DEL", KEY_STR)
end)

run("Redis String pairs iterates bytes with 1-based indices", function()
    redis:Command("SET", KEY_STR, "Hi")
    local s = redis:GetString(KEY_STR)
    local bytes = {}
    for idx, byte in pairs(s) do
        bytes[idx] = byte
    end
    assert(bytes[1] == 72,  "byte 1 should be 72 ('H'), got: "  .. tostring(bytes[1]))
    assert(bytes[2] == 105, "byte 2 should be 105 ('i'), got: " .. tostring(bytes[2]))
    assert(bytes[3] == nil, "no byte 3 for 2-char string")
    redis:Command("DEL", KEY_STR)
end)

run("Redis String concat returns a plain Lua string", function()
    redis:Command("SET", KEY_STR, "hello")
    local s = redis:GetString(KEY_STR)
    local r1 = s .. " world"
    assert(type(r1) == "string" and r1 == "hello world",
        "str .. ' world' should equal 'hello world', got: " .. tostring(r1))
    local r2 = "say: " .. s
    assert(type(r2) == "string" and r2 == "say: hello",
        "'say: ' .. str should equal 'say: hello', got: " .. tostring(r2))
    redis:Command("DEL", KEY_STR)
end)

run("Redis String call returns a RedisKey for the key", function()
    redis:Command("SET", KEY_STR, "v")
    local s   = redis:GetString(KEY_STR)
    local key = s()   -- __call with no args → RedisKey
    assert(key ~= nil, "s() should return a RedisKey")
    assert(tostring(key) == KEY_STR, "key name should match, got: " .. tostring(key))
    assert(key:Type() == "string", "key type should be 'string', got: " .. tostring(key:Type()))
    redis:Command("DEL", KEY_STR)
end)

-- ── Hashset ───────────────────────────────────────────────────────────────────

run("Redis Hashset set and get fields", function()
    local hash = redis:GetHashset(KEY_HASH)
    hash["name"]  = "kitsune"
    hash["score"] = "99"
    assert(hash["name"]    == "kitsune", "expected 'kitsune', got: " .. tostring(hash["name"]))
    assert(hash["score"]   == "99",      "expected '99', got: "      .. tostring(hash["score"]))
    assert(hash["missing"] == nil,       "missing field should be nil")
end)

run("Redis Hashset delete field", function()
    local hash = redis:GetHashset(KEY_HASH)
    hash["score"] = nil
    assert(hash["score"] == nil,       "expected nil after delete")
    assert(hash["name"]  == "kitsune", "name should survive score deletion")
end)

run("Redis Hashset pairs iterates all fields", function()
    local hash = redis:GetHashset(KEY_HASH)
    hash["a"] = "1"
    hash["b"] = "2"
    local found = {}
    for k, v in pairs(hash) do
        found[k] = v
    end
    assert(found["a"]    == "1",       "expected a=1, got: "          .. tostring(found["a"]))
    assert(found["b"]    == "2",       "expected b=2, got: "          .. tostring(found["b"]))
    assert(found["name"] == "kitsune", "expected name=kitsune, got: " .. tostring(found["name"]))
end)

run("Redis Hashset table values are JSON-encoded via JsonRef cache", function()
    -- JsonRef was a non-static global; a second Lua state would inherit a registry ref
    -- from the first state and dereference the wrong data.
    -- This test exercises the lazy-init path (first Set) and the reuse path (second Set).
    redis:Command("DEL", KEY_HASH)
    local hash = redis:GetHashset(KEY_HASH)
    hash["obj1"] = { kind = "user",   id = 1       }   -- initialises JsonRef
    hash["obj2"] = { kind = "weapon", name = "sword" }  -- reuses cached JsonRef
    local raw1 = hash["obj1"]
    local raw2 = hash["obj2"]
    assert(type(raw1) == "string", "table value should be a JSON string, got: " .. type(raw1))
    assert(type(raw2) == "string", "table value should be a JSON string, got: " .. type(raw2))
    local j = Json.Create()
    local d1, d2 = j:Decode(raw1), j:Decode(raw2)
    assert(d1 and d1.kind == "user"   and d1.id   == 1,       "obj1 JSON mismatch")
    assert(d2 and d2.kind == "weapon" and d2.name == "sword", "obj2 JSON mismatch")
    redis:Command("DEL", KEY_HASH)
end)

run("Redis Hashset empty hashset pairs produces no iterations", function()
    redis:Command("DEL", KEY_HASH)
    local hash = redis:GetHashset(KEY_HASH)
    local count = 0
    for k, v in pairs(hash) do count = count + 1 end
    assert(count == 0, "empty hashset pairs should yield 0 iterations, got: " .. count)
end)

run("Redis Hashset HDEL on absent field is a no-op", function()
    redis:Command("DEL", KEY_HASH)
    local hash = redis:GetHashset(KEY_HASH)
    hash["name"] = "kitsune"
    hash["ghost"] = nil   -- HDEL of non-existent field must not error
    assert(hash["name"] == "kitsune", "existing field should be unaffected by absent HDEL")
end)

-- ── List ──────────────────────────────────────────────────────────────────────

run("Redis List RPUSH and index", function()
    local list = redis:GetList(KEY_LIST)
    list[0] = "a"
    list[0] = "b"
    list[0] = "c"
    assert(#list   == 3,   "expected length 3, got: " .. tostring(#list))
    assert(list[1] == "a", "expected 'a' at 1, got: " .. tostring(list[1]))
    assert(list[2] == "b", "expected 'b' at 2, got: " .. tostring(list[2]))
    assert(list[3] == "c", "expected 'c' at 3, got: " .. tostring(list[3]))
end)

run("Redis List LPUSH prepends to front", function()
    -- Start fresh so abs(-1)=1 satisfies >= len for both calls
    redis:Command("DEL", KEY_LIST)
    local list = redis:GetList(KEY_LIST)
    list[-1] = "b"  -- LPUSH onto empty list (abs(-1)=1 >= 0)
    list[-1] = "a"  -- LPUSH onto len=1   (abs(-1)=1 >= 1)
    assert(list[1] == "a", "expected 'a' at front after two LPUSHes, got: " .. tostring(list[1]))
    assert(list[2] == "b", "expected 'b' at back, got: "                    .. tostring(list[2]))
    assert(#list   == 2,   "expected length 2, got: "                        .. tostring(#list))
end)

run("Redis List index 0 pops from front", function()
    local list = redis:GetList(KEY_LIST)
    local popped = list[0]
    assert(popped == "a", "expected 'a' from LPOP, got: " .. tostring(popped))
    assert(#list  == 1,   "expected length 1 after pop, got: " .. tostring(#list))
end)

run("Redis List LSET changes element in place when index is within bounds", function()
    redis:Command("DEL", KEY_LIST)
    local list = redis:GetList(KEY_LIST)
    list[0] = "x"   -- RPUSH → ["x"]
    list[0] = "y"   -- RPUSH → ["x","y"]
    list[0] = "z"   -- RPUSH → ["x","y","z"]
    list[2] = "Y"   -- LSET index 2 (abs(2-1)=1 < len=3) → ["x","Y","z"]
    assert(list[1] == "x", "list[1] should still be 'x'")
    assert(list[2] == "Y", "list[2] should be 'Y' after LSET, got: " .. tostring(list[2]))
    assert(list[3] == "z", "list[3] should still be 'z'")
    redis:Command("DEL", KEY_LIST)
end)

run("Redis List out-of-bounds index returns nil", function()
    redis:Command("DEL", KEY_LIST)
    local list = redis:GetList(KEY_LIST)
    assert(list[1] == nil, "index 1 on empty list should return nil")
    list[0] = "a"
    assert(list[2] == nil, "index 2 on 1-element list should return nil")
    redis:Command("DEL", KEY_LIST)
end)

-- ── Set ───────────────────────────────────────────────────────────────────────

run("Redis Set add and membership check", function()
    local s = redis:GetSet(KEY_SET)
    s["apple"]  = true
    s["banana"] = true
    s["cherry"] = true
    assert(s["apple"]  == true,  "expected apple to be a member")
    assert(s["banana"] == true,  "expected banana to be a member")
    assert(s["mango"]  == false, "expected mango to not be a member")
end)

run("Redis Set cardinality via #", function()
    local s = redis:GetSet(KEY_SET)
    assert(#s == 3, "expected cardinality 3, got: " .. tostring(#s))
end)

run("Redis Set remove member", function()
    local s = redis:GetSet(KEY_SET)
    s["banana"] = false
    assert(s["banana"] == false, "expected banana removed")
    assert(#s          == 2,     "expected cardinality 2, got: " .. tostring(#s))
end)

run("Redis Set SRANDMEMBER returns a member without removing it", function()
    local s = redis:GetSet(KEY_SET)
    local member = s[0]
    assert(type(member) == "string", "SRANDMEMBER should return a string, got: " .. type(member))
    assert(#s == 2, "cardinality should be unchanged after SRANDMEMBER, got: " .. tostring(#s))
end)

run("Redis Set SPOP removes and returns a member", function()
    local s = redis:GetSet(KEY_SET)
    local popped = s[-1]
    assert(type(popped) == "string", "SPOP should return a string, got: " .. type(popped))
    assert(#s == 1, "cardinality should decrease after SPOP, got: " .. tostring(#s))
end)

run("Redis Set SRANDMEMBER and SPOP return nil on empty set", function()
    -- Before fix: element[0]->str was dereferenced on REDIS_REPLY_STRING (not an array);
    -- reply->element is NULL for non-array types, so any non-nil reply would crash.
    -- This exercises the nil branch to confirm the guard holds for empty sets too.
    redis:Command("DEL", KEY_SET)
    local s = redis:GetSet(KEY_SET)
    assert(s[0]  == nil, "SRANDMEMBER on empty set should return nil")
    assert(s[-1] == nil, "SPOP on empty set should return nil")
end)

run("Redis Set pairs iterates all members", function()
    redis:Command("DEL", KEY_SET)
    local s = redis:GetSet(KEY_SET)
    s["p"] = true
    s["q"] = true
    s["r"] = true
    local members = {}
    for _, member in pairs(s) do
        members[member] = true
    end
    assert(members["p"], "pairs should yield 'p'")
    assert(members["q"], "pairs should yield 'q'")
    assert(members["r"], "pairs should yield 'r'")
    local count = 0
    for _ in pairs(members) do count = count + 1 end
    assert(count == 3, "pairs should yield exactly 3 members, got: " .. count)
    redis:Command("DEL", KEY_SET)
end)

-- ── SortedSet ─────────────────────────────────────────────────────────────────

run("Redis SortedSet add and score lookup", function()
    local zs = redis:GetSortedSet(KEY_ZSET)
    zs["alpha"] = 10
    zs["beta"]  = 20
    zs["gamma"] = 30
    assert(zs["alpha"] ~= nil, "alpha should have a score, got nil")
    assert(zs["beta"]  ~= nil, "beta should have a score, got nil")
    assert(zs["gamma"] ~= nil, "gamma should have a score, got nil")
    assert(tonumber(zs["alpha"]) == 10, "expected alpha=10, got: " .. tostring(zs["alpha"]))
    assert(tonumber(zs["gamma"]) == 30, "expected gamma=30, got: " .. tostring(zs["gamma"]))
end)

run("Redis SortedSet float score is stored correctly", function()
    local zs = redis:GetSortedSet(KEY_ZSET)
    zs["pi"] = 3.14
    assert(zs["pi"] ~= nil, "pi should have a score")
    assert(math.abs(tonumber(zs["pi"]) - 3.14) < 0.001, "expected pi≈3.14, got: " .. tostring(zs["pi"]))
    zs["pi"] = nil
end)

run("Redis SortedSet rank access", function()
    local zs = redis:GetSortedSet(KEY_ZSET)
    assert(zs[1] == "alpha", "expected 'alpha' at rank 1, got: " .. tostring(zs[1]))
    assert(zs[2] == "beta",  "expected 'beta' at rank 2, got: "  .. tostring(zs[2]))
    assert(zs[3] == "gamma", "expected 'gamma' at rank 3, got: " .. tostring(zs[3]))
end)

run("Redis SortedSet remove member", function()
    local zs = redis:GetSortedSet(KEY_ZSET)
    zs["beta"] = nil
    assert(zs[1] == "alpha", "expected 'alpha' at rank 1 after removal, got: " .. tostring(zs[1]))
    assert(zs[2] == "gamma", "expected 'gamma' at rank 2 after removal, got: " .. tostring(zs[2]))
end)

run("Redis SortedSet pairs iterates members with scores", function()
    local zs = redis:GetSortedSet(KEY_ZSET)
    local results = {}
    for member, score in pairs(zs) do
        results[member] = score
    end
    assert(results["alpha"] == 10,  "expected alpha=10, got: "  .. tostring(results["alpha"]))
    assert(results["gamma"] == 30,  "expected gamma=30, got: "  .. tostring(results["gamma"]))
    assert(results["beta"]  == nil, "beta should be absent after removal")
end)

run("Redis SortedSet non-existent member score returns nil", function()
    local zs = redis:GetSortedSet(KEY_ZSET)
    assert(zs["missing"] == nil, "non-existent member score should be nil")
end)

run("Redis SortedSet out-of-range rank returns nil", function()
    local zs = redis:GetSortedSet(KEY_ZSET)
    assert(zs[100] == nil, "out-of-range rank should return nil")
end)

run("Redis SortedSet empty sorted set pairs produces no iterations", function()
    redis:Command("DEL", KEY_ZSET)
    local zs = redis:GetSortedSet(KEY_ZSET)
    local count = 0
    for member, score in pairs(zs) do count = count + 1 end
    assert(count == 0, "empty sorted set pairs should yield 0 iterations, got: " .. count)
    redis:Command("DEL", KEY_ZSET)
end)

-- ── Stream ────────────────────────────────────────────────────────────────────

run("Redis Stream Add returns unique IDs", function()
    local stream = redis:GetStream(KEY_STREAM)
    local id1 = stream:Add({ event = "login",  user = "alice" })
    local id2 = stream:Add({ event = "logout", user = "alice" })
    assert(type(id1) == "string" and #id1 > 0, "Add should return an ID string, got: " .. tostring(id1))
    assert(type(id2) == "string" and #id2 > 0, "Add should return an ID string, got: " .. tostring(id2))
    assert(id1 ~= id2, "each Add should produce a unique ID")
end)

run("Redis Stream Read from beginning", function()
    local stream = redis:GetStream(KEY_STREAM)
    local id, data = stream:Read("0")
    assert(id,                          "Read should return an ID")
    assert(type(data) == "table",       "Read should return a data table, got: " .. type(data))
    assert(data.event == "login",       "expected event='login', got: "  .. tostring(data.event))
    assert(data.user  == "alice",       "expected user='alice', got: "   .. tostring(data.user))
end)

run("Redis Stream Read advances past first entry", function()
    local stream = redis:GetStream(KEY_STREAM)
    local id1 = stream:Read("0")
    local id2, data2 = stream:Read(id1)
    assert(id2,                          "second Read should return an ID")
    assert(data2.event == "logout",      "expected event='logout', got: " .. tostring(data2.event))
end)

run("Redis Stream Trim to max length", function()
    local stream = redis:GetStream(KEY_STREAM)
    local trimmed = stream:Trim(1)
    assert(type(trimmed) == "number", "Trim should return trimmed count, got: " .. type(trimmed))
    assert(trimmed >= 0,              "trimmed count should be non-negative, got: " .. tostring(trimmed))
end)

run("Redis Stream Add succeeds after Trim (CleanReply fix)", function()
    -- Before fix: redisstream_trim never called CleanReply; the stale reply object
    -- sat in the LuaRedis struct and corrupted the next command issued on the same context.
    redis:Command("DEL", KEY_STREAM)
    local stream = redis:GetStream(KEY_STREAM)
    stream:Add({ seq = "1" })
    stream:Add({ seq = "2" })
    local trimmed = stream:Trim(1)
    assert(trimmed == 1, "expected 1 entry trimmed, got: " .. tostring(trimmed))
    -- Without CleanReply the stale reply header would be treated as the next command's result
    local id = stream:Add({ seq = "3" })
    assert(type(id) == "string" and #id > 0, "Add after Trim should return a valid ID, got: " .. tostring(id))
    local _, data = stream:Read("0")
    assert(data ~= nil and data.seq ~= nil, "Read after Trim+Add should return valid data")
    redis:Command("DEL", KEY_STREAM)
end)

-- ── Key iterator ──────────────────────────────────────────────────────────────

run("Redis key iterator finds Kitsune test keys", function()
    redis:Command("SET", KEY_ITER1, "a")
    redis:Command("SET", KEY_ITER2, "b")
    redis:Command("SET", KEY_ITER3, "c")
    local found = {}
    for key in redis, redis, nil do
        local name = tostring(key)
        if name:find("^Kitsune:test:iter") then
            found[name] = true
        end
    end
    assert(found[KEY_ITER1], "should find iter1")
    assert(found[KEY_ITER2], "should find iter2")
    assert(found[KEY_ITER3], "should find iter3")
end)

-- ── Key type ──────────────────────────────────────────────────────────────────

run("Redis GetKey tostring returns the key name", function()
    redis:Command("SET", KEY_KEY, "v")
    local k = redis:GetKey(KEY_KEY)
    assert(tostring(k) == KEY_KEY, "tostring(key) should return key name, got: " .. tostring(k))
    redis:Command("DEL", KEY_KEY)
end)

run("Redis GetKey Type returns the Redis type string", function()
    redis:Command("SET",   KEY_KEY, "str_value")     ; assert(redis:GetKey(KEY_KEY):Type() == "string", "string key") ; redis:Command("DEL", KEY_KEY)
    redis:Command("RPUSH", KEY_KEY, "a")              ; assert(redis:GetKey(KEY_KEY):Type() == "list",   "list key")   ; redis:Command("DEL", KEY_KEY)
    redis:Command("HSET",  KEY_KEY, "f", "v")         ; assert(redis:GetKey(KEY_KEY):Type() == "hash",   "hash key")   ; redis:Command("DEL", KEY_KEY)
    redis:Command("SADD",  KEY_KEY, "m")              ; assert(redis:GetKey(KEY_KEY):Type() == "set",    "set key")    ; redis:Command("DEL", KEY_KEY)
    redis:Command("ZADD",  KEY_KEY, "1", "m")         ; assert(redis:GetKey(KEY_KEY):Type() == "zset",   "zset key")   ; redis:Command("DEL", KEY_KEY)
end)

run("Redis GetKey Delete returns true for existing key and false for absent", function()
    redis:Command("SET", KEY_KEY, "v")
    local k = redis:GetKey(KEY_KEY)
    assert(k:Delete() == true,  "Delete on existing key should return true")
    assert(k:Delete() == false, "Delete on absent key should return false")
end)

run("Redis GetKey GetTTL returns -1 for persistent key", function()
    redis:Command("SET", KEY_KEY, "v")
    local k = redis:GetKey(KEY_KEY)
    assert(k:GetTTL() == -1, "persistent key TTL should be -1, got: " .. tostring(k:GetTTL()))
    redis:Command("DEL", KEY_KEY)
end)

run("Redis GetKey SetTTL and GetTTL round-trip", function()
    redis:Command("SET", KEY_KEY, "v")
    local k = redis:GetKey(KEY_KEY)
    k:SetTTL(60000)                       -- 60 seconds in milliseconds
    local ttl = k:GetTTL()
    assert(type(ttl) == "number" and ttl > 0, "TTL should be positive after SetTTL, got: " .. tostring(ttl))
    k:SetTTL(-1)                          -- remove TTL
    assert(k:GetTTL() == -1, "TTL should be -1 after removal, got: " .. tostring(k:GetTTL()))
    redis:Command("DEL", KEY_KEY)
end)

run("Redis key iterator yields RedisKey objects with working methods", function()
    redis:Command("DEL",   KEY_ITER1, KEY_ITER2)
    redis:Command("SET",   KEY_ITER1, "a")
    redis:Command("RPUSH", KEY_ITER2, "x")
    local found = {}
    for key in redis, redis, nil do
        local name = tostring(key)
        if name == KEY_ITER1 or name == KEY_ITER2 then
            found[name] = key:Type()
        end
    end
    assert(found[KEY_ITER1] == "string", "iter1 should be type 'string', got: " .. tostring(found[KEY_ITER1]))
    assert(found[KEY_ITER2] == "list",   "iter2 should be type 'list', got: "   .. tostring(found[KEY_ITER2]))
    redis:Command("DEL", KEY_ITER1, KEY_ITER2)
end)

-- ── Command reply types ───────────────────────────────────────────────────────

run("Redis Command returns REDIS_REPLY_INTEGER for INCR", function()
    redis:Command("SET", KEY_CMD, "0")
    local reply = redis:Command("INCR", KEY_CMD)
    assert(reply.Type  == 3, "INCR should return integer type (3), got: " .. tostring(reply.Type))
    assert(reply.Value == 1, "INCR on '0' should return 1, got: "         .. tostring(reply.Value))
    redis:Command("DEL", KEY_CMD)
end)

run("Redis Command returns REDIS_REPLY_NIL for GET on missing key", function()
    redis:Command("DEL", KEY_CMD)
    local reply = redis:Command("GET", KEY_CMD)
    assert(reply.Type  == 4,   "GET missing key should return nil type (4), got: " .. tostring(reply.Type))
    assert(reply.Value == nil, "GET missing key value should be nil")
end)

run("Redis Command returns REDIS_REPLY_ARRAY for LRANGE", function()
    redis:Command("DEL", KEY_CMD)
    redis:Command("RPUSH", KEY_CMD, "a", "b", "c")
    local reply = redis:Command("LRANGE", KEY_CMD, "0", "-1")
    assert(reply.Type  == 2, "LRANGE should return array type (2), got: "    .. tostring(reply.Type))
    assert(type(reply.Value) == "table", "LRANGE value should be a table")
    assert(#reply.Value == 3, "LRANGE should return 3 elements, got: " .. tostring(#reply.Value))
    redis:Command("DEL", KEY_CMD)
end)

run("Redis Command WRONGTYPE error propagates as Lua error via pcall", function()
    redis:Command("SET", KEY_CMD, "not-a-list")
    local ok, err = pcall(function() redis:Command("LPUSH", KEY_CMD, "v") end)
    assert(not ok, "WRONGTYPE command should raise a Lua error")
    assert(type(err) == "string" and err:find("WRONGTYPE"),
        "error should mention WRONGTYPE, got: " .. tostring(err))
    redis:Command("DEL", KEY_CMD)
end)

-- ── Cleanup ───────────────────────────────────────────────────────────────────

run("Redis post-test cleanup", function()
    -- Scan and delete every key that starts with Kitsune:test:
    -- ^ anchors the pattern to the beginning of the string, so keys that merely
    -- contain "Kitsune:test:" somewhere in the middle are not affected.
    for key in redis, redis, nil do
        if tostring(key):find("^Kitsune:test:") then
            key:Delete()
        end
    end
end)
