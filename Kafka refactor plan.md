# Kafka Refactor Plan

## Goal

Replace the current fragmented Kafka API (topic objects, manual poll loops, separate subscribe/assign per partition) with a clean producer/consumer model where:

- **Producers** send with a single `Send` call using a topic name string
- **Consumers** subscribe or assign via an array of topics, receiving a coroutine as the consume handle
- **Messages** are plain data tables with fields matching the old `GetData()` output, but using `Value` instead of `Payload`
- **AutoCommit** is configurable on the coroutine handle
- **Manual commit** is done via `consumer:Commit(data)` where `data` is the table yielded by the coroutine

This is a **barebones first pass**. Admin/utility operations (`GetMetadata`, `GetGroups`, `GetOffsets`, `AlterConfig`, etc.) are deferred to a follow-up.

---

## New Lua API Surface

### Module-level

```lua
KafkaProducer  Kafka.NewProducer(config)
KafkaConsumer  Kafka.NewConsumer(config)
string         Kafka.Logs(opt filename)    -- unchanged, module-level
```

`config` is a string-keyed table of librdkafka config properties (same as today). Default `group.id` is `"LUAP"` for producers, `"LUAC"` for consumers if not specified.

---

### KafkaProducer

```lua
bool, errmsg  producer:Send(topic, key, value, opt headers)
nil           producer:Close()
```

`Send` signature vs old API:
- `topic` is a plain **string** (no topic object required)
- `key` comes **before** `value`
- `headers` is an optional string-keyed table
- `partition` is not exposed — uses librdkafka's default partitioner

---

### KafkaConsumer

```lua
KafkaConsumeCoroutine  consumer:Subscribe(topics)
KafkaConsumeCoroutine  consumer:Assign(topics)
bool, errmsg           consumer:Commit(data)
nil                    consumer:Close()
```

**`Subscribe(topics)`** — takes an array of topic name strings:
```lua
local co = consumer:Subscribe({"MyTopic", "SomeOtherTopic"})
```
Calls `rd_kafka_subscribe` with all topics at once.

**`Assign(topics)`** — takes an array of `"topic"` or `"topic:partition"` strings:
```lua
local co = consumer:Assign({"MyTopic:0", "MyTopic:1", "OtherTopic"})
```
If `:N` suffix is absent or `N` is not a valid integer, assigns `RD_KAFKA_PARTITION_UA` (all partitions). Calls `rd_kafka_assign` once with the full partition list.

**`Commit(data)`** — commits using a lightuserdata handle stored in `data`:
```lua
consumer:Commit(data)
```
Returns `false, "already committed or nil data"` if the handle is already cleared. Calls `rd_kafka_commit_message` synchronously. Clears the handle after commit to prevent double-commit.

---

### KafkaConsumeCoroutine

A **Lua thread** (coroutine) with a custom metatable so both `coroutine.resume` and method calls work on the same object.

```lua
nil  co:AutoCommit(bool)  -- default true
```

**Resuming:**
```lua
local ok, data = coroutine.resume(co, shouldQuit)
```

- `shouldQuit` truthy → coroutine cleans up and dies cleanly (`ok=true`, `data=nil`)
- `shouldQuit` falsy → poll once
  - Message available → yields data table
  - No message → yields `nil` (caller should `Sleep` and retry)
- `ok=false` → coroutine errored (e.g. consumer was closed)

**Data table fields:**

| Field | Type | Notes |
|-------|------|-------|
| `Key` | string or nil | `nil` when no key was set |
| `Value` | string | Renamed from old `Payload` |
| `Topic` | string | |
| `Partition` | int | |
| `Offset` | int | |
| `Timestamp` | int | ms, from `rd_kafka_message_timestamp` |
| `Latency` | int | µs, from `rd_kafka_message_latency` |
| `Error` | string | `rd_kafka_err2str` (present on success too) |
| `ErrorCode` | int | |
| `Headers` | table | String-keyed; empty table when no headers |

The `rd_kafka_message_t*` needed for `Commit` is stored internally using a static C sentinel as a lightuserdata key — not visible from Lua's `pairs`.

---

## New C Types

### `LuaKafkaProducer` (`luakafkaproducer.h`)

```c
typedef struct LuaKafkaProducer {
    rd_kafka_t* rd;
} LuaKafkaProducer;
```

Metaname: `"KAFKAPRODUCER"`

---

### `LuaKafkaConsumer` (`luakafkaconsumer.h`)

```c
typedef struct LuaKafkaConsumer {
    rd_kafka_t* rd;
} LuaKafkaConsumer;
```

Metaname: `"KAFKACONSUMER"`

---

### `LuaKafkaConsumeState` (`luakafkaconsumer.h`)

Shared state that both the coroutine body and `AutoCommit` access:

```c
typedef struct LuaKafkaConsumeState {
    rd_kafka_t* owner;           // borrowed — consumer owns the lifetime
    bool autocommit;             // default true
    int poll_timeout_ms;         // default 0 (non-blocking)
    rd_kafka_message_t* pending; // last polled message, destroyed on next resume
} LuaKafkaConsumeState;
```

Metaname: `"KAFKACONSUMERSTATE"` (internal — not exposed to Lua directly)

---

## Coroutine Design

### Creating the coroutine (`Subscribe` / `Assign`)

```
1. Validate consumer is open.
2. Build rd_kafka_topic_partition_list_t from the topics array argument.
3. Call rd_kafka_subscribe (Subscribe) or rd_kafka_assign (Assign).
4. Destroy the partition list.
5. Create LuaKafkaConsumeState userdata with owner, autocommit=true, poll_timeout_ms=0.
6. lua_newthread(L)  →  new Lua thread on the main stack.
7. lua_xmove the state userdata onto the new thread's stack, then push
   ConsumeCoroutineBody as a C closure with the state as upvalue[1].
8. Store a registry ref: thread_pointer → state_pointer (lightuserdata),
   keyed by the thread's lua_State* pointer, so AutoCommit can find the state.
9. Set KAFKACONSUMECOROUTINE metatable on the thread (has __index → AutoCommit).
10. Return the thread (1 value).
```

### `ConsumeCoroutineBody` C function

```
First resume args: (state_userdata)   [pushed onto thread stack before first resume]
Subsequent resume args: (shouldQuit)

loop:
  shouldQuit = lua_toboolean(L, 1)
  if shouldQuit:
    if state->pending: rd_kafka_message_destroy(state->pending); state->pending = NULL
    return 0  -- coroutine dies, caller gets ok=true, nil
  if state->pending:
    rd_kafka_message_destroy(state->pending); state->pending = NULL
  msg = rd_kafka_consumer_poll(state->owner, state->poll_timeout_ms)
  if not msg:
    lua_yieldk(L, 0, cont, 0)  -- yield nil
    continue
  if state->autocommit:
    rd_kafka_commit_message(state->owner, msg, 0)
  state->pending = msg
  push_consume_message(L, msg)  -- builds data table
  lua_yieldk(L, 1, cont, 0)    -- yield data table
```

Uses `lua_yieldk` with a continuation (`cont`) that re-enters the loop. This is the Lua 5.4 C coroutine pattern.

### `AutoCommit` method

```c
int ConsumeAutoCommit(lua_State* L) {
    // self is a thread; look up its state via registry
    lua_pushlightuserdata(L, (void*)lua_tothread(L, 1));
    lua_gettable(L, LUA_REGISTRYINDEX);
    LuaKafkaConsumeState* state = (LuaKafkaConsumeState*)lua_touserdata(L, -1);
    if (state)
        state->autocommit = lua_toboolean(L, 2);
    return 0;
}
```

---

## `consumer:Commit(data)` Implementation

```c
static const char MSG_KEY = 0;  // address used as lightuserdata key

int ConsumerCommit(lua_State* L) {
    LuaKafkaConsumer* consumer = lua_tokafkaconsumer(L, 1);
    if (!lua_istable(L, 2)) {
        lua_pushboolean(L, false);
        lua_pushstring(L, "expected data table");
        return 2;
    }
    lua_pushlightuserdata(L, (void*)&MSG_KEY);
    lua_gettable(L, 2);
    rd_kafka_message_t* msg = (rd_kafka_message_t*)lua_touserdata(L, -1);
    if (!msg) {
        lua_pushboolean(L, false);
        lua_pushstring(L, "already committed or nil data");
        return 2;
    }
    lua_pushlightuserdata(L, (void*)&MSG_KEY);
    lua_pushnil(L);
    lua_settable(L, 2);
    rd_kafka_resp_err_t err = rd_kafka_commit_message(consumer->rd, msg, 0);
    lua_pushboolean(L, !err);
    if (err) {
        lua_pushstring(L, rd_kafka_err2str(err));
        return 2;
    }
    return 1;
}
```

---

## Message Building Helper

`push_consume_message(lua_State* L, const rd_kafka_message_t* msg)` — called inside the coroutine body, pushes a data table onto `L`:

```c
static const char MSG_KEY = 0;

static void push_consume_message(lua_State* L, const rd_kafka_message_t* msg) {
    lua_createtable(L, 0, 11);
    int t = lua_gettop(L);

    // Key (nil when absent)
    lua_pushstring(L, "Key");
    if (msg->key && msg->key_len > 0)
        lua_pushlstring(L, (const char*)msg->key, msg->key_len);
    else
        lua_pushnil(L);
    lua_settable(L, t);

    // Value
    lua_pushstring(L, "Value");
    lua_pushlstring(L, (const char*)msg->payload, msg->len);
    lua_settable(L, t);

    // Topic
    lua_pushstring(L, "Topic");
    lua_pushstring(L, msg->rkt ? rd_kafka_topic_name(msg->rkt) : NULL);
    lua_settable(L, t);

    // Partition, Offset, ErrorCode, Error
    lua_pushstring(L, "Partition"); lua_pushinteger(L, msg->partition); lua_settable(L, t);
    lua_pushstring(L, "Offset");    lua_pushinteger(L, msg->offset);    lua_settable(L, t);
    lua_pushstring(L, "ErrorCode"); lua_pushinteger(L, msg->err);       lua_settable(L, t);
    lua_pushstring(L, "Error");     lua_pushstring(L, rd_kafka_err2str(msg->err)); lua_settable(L, t);

    // Timestamp
    rd_kafka_timestamp_type_t tstype;
    lua_pushstring(L, "Timestamp");
    lua_pushinteger(L, rd_kafka_message_timestamp(msg, &tstype));
    lua_settable(L, t);

    // Latency
    lua_pushstring(L, "Latency");
    lua_pushinteger(L, rd_kafka_message_latency(msg));
    lua_settable(L, t);

    // Headers
    lua_pushstring(L, "Headers");
    rd_kafka_headers_t* headers;
    if (rd_kafka_message_headers(msg, &headers) == RD_KAFKA_RESP_ERR_NO_ERROR) {
        size_t cnt = rd_kafka_header_cnt(headers);
        lua_createtable(L, 0, (int)cnt);
        const char* name; const char* data; size_t dsz;
        for (size_t i = 0; i < cnt; i++) {
            if (rd_kafka_header_get_all(headers, i, &name, (const void**)&data, &dsz) == RD_KAFKA_RESP_ERR_NO_ERROR) {
                lua_pushstring(L, name);
                lua_pushlstring(L, data, dsz);
                lua_settable(L, -3);
            }
        }
    } else {
        lua_createtable(L, 0, 0);
    }
    lua_settable(L, t);

    // Internal commit handle (lightuserdata, invisible to pairs)
    lua_pushlightuserdata(L, (void*)&MSG_KEY);
    lua_pushlightuserdata(L, (void*)msg);
    lua_settable(L, t);
}
```

---

## File Plan

### Delete

| File | Reason |
|------|--------|
| `luakafkatopic.h` | Topic-object consumer replaced entirely |
| `luakafkatopic.cpp` | Same |
| `luakafkamessage.h` | Replaced by inline `push_consume_message` helper |
| `luakafkamessage.cpp` | Same |

### Create

| File | Contents |
|------|----------|
| `luakafkaproducer.h` | `LuaKafkaProducer` struct, `lua_pushkafkaproducer`, `lua_tokafkaproducer`, function declarations |
| `luakafkaproducer.cpp` | `CreateProducer`, `ProducerSend`, `ProducerGC`, `ProducerToString` |
| `luakafkaconsumer.h` | `LuaKafkaConsumer`, `LuaKafkaConsumeState` structs and declarations |
| `luakafkaconsumer.cpp` | `CreateConsumer`, `ConsumerSubscribe`, `ConsumerAssign`, `ConsumerCommit`, `ConsumerGC`, `ConsumerToString`, `ConsumeCoroutineBody`, `ConsumeAutoCommit`, `push_consume_message` |

### Modify

| File | Changes |
|------|---------|
| `luakafka.h` / `luakafka.cpp` | Keep only logger, `GetLastLogs`, and the `errorbuffer`. Remove all the old consumer/producer/topic functions. |
| `luakafkamain.cpp` | Register `KAFKAPRODUCER` and `KAFKACONSUMER` metatables + the `KAFKACONSUMECOROUTINE` thread metatable. Module table exposes only `NewProducer`, `NewConsumer`, `Logs`. |
| `kafkahelpers.h/cpp` | Keep `lua_tokafkaconf`. Remove `lua_tokafkatopicconf` (no longer needed). Add `parse_topic_partition` helper (see below). |

### Linux portability

All new files include `"platform.h"` instead of `<Windows.h>`. No `<Windows.h>` direct includes. librdkafka itself is cross-platform so no extra guards are needed for the Kafka API calls.

---

## Topic-Partition String Parsing (`Assign`)

```c
// out_topic must be a caller-provided buffer of at least topic_buflen bytes.
// out_partition is set to RD_KAFKA_PARTITION_UA if no valid ":N" suffix.
static void parse_topic_partition(const char* input, char* out_topic,
                                   size_t topic_buflen, int32_t* out_partition) {
    *out_partition = RD_KAFKA_PARTITION_UA;
    const char* colon = strrchr(input, ':');
    if (colon) {
        char* end;
        long p = strtol(colon + 1, &end, 10);
        if (end != colon + 1 && *end == '\0' && p >= 0) {
            *out_partition = (int32_t)p;
            size_t len = (size_t)(colon - input);
            if (len >= topic_buflen)
                len = topic_buflen - 1;
            strncpy(out_topic, input, len);
            out_topic[len] = '\0';
            return;
        }
    }
    strncpy(out_topic, input, topic_buflen - 1);
    out_topic[topic_buflen - 1] = '\0';
}
```

---

## Registration Pattern (wchar pattern)

```c
// Producer
static const luaL_Reg kafkaproducerfunctions[] = {
    { "Send",  ProducerSend },
    { "Close", ProducerGC   },
    { NULL, NULL }
};
static const luaL_Reg kafkaproducermeta[] = {
    { "__gc",       ProducerGC       },
    { "__tostring", ProducerToString },
    { NULL, NULL }
};

// Consumer
static const luaL_Reg kafkaconsumerfunctions[] = {
    { "Subscribe", ConsumerSubscribe },
    { "Assign",    ConsumerAssign    },
    { "Commit",    ConsumerCommit    },
    { "Close",     ConsumerGC        },
    { NULL, NULL }
};
static const luaL_Reg kafkaconsumermeta[] = {
    { "__gc",       ConsumerGC       },
    { "__tostring", ConsumerToString },
    { NULL, NULL }
};

// Consume coroutine thread metatable
static const luaL_Reg kafkacoroutinemeta[] = {
    { "AutoCommit", ConsumeAutoCommit },
    { NULL, NULL }
};
```

`luaopen_kafka` registers all three metatables with `__index = functions table`, then returns the module table containing only `NewProducer`, `NewConsumer`, `Logs`.

---

## Multiple Producers / Consumers

Each `Kafka.NewProducer` and `Kafka.NewConsumer` call allocates its own `LuaKafkaProducer` / `LuaKafkaConsumer` userdata with an independent `rd_kafka_t*` handle. librdkafka handles are fully self-contained (own connection, own config, own internal threads), so any number of producers and consumers can coexist — across different clusters, different group IDs, or different topic sets:

```lua
local p1 = Kafka.NewProducer({["bootstrap.servers"] = "cluster1:9092"})
local p2 = Kafka.NewProducer({["bootstrap.servers"] = "cluster2:9092"})

local c1 = Kafka.NewConsumer({["bootstrap.servers"] = "cluster1:9092", ["group.id"] = "groupA"})
local c2 = Kafka.NewConsumer({["bootstrap.servers"] = "cluster1:9092", ["group.id"] = "groupB"})

local co1 = c1:Subscribe({"TopicA"})
local co2 = c2:Subscribe({"TopicB", "TopicC"})
```

Each coroutine polls its own `rd_kafka_t*` queue — they are completely independent.

**What does not work:** calling `Subscribe`/`Assign` twice on the *same* consumer instance and running both coroutines concurrently. `rd_kafka_subscribe` replaces the existing subscription, and `rd_kafka_consumer_poll` drains a single shared queue — messages would be split unpredictably between the two coroutines. To consume from different topic sets with different rules, use separate consumer instances.

---

## Open Questions / Decisions Needed

1. **Producer flush on `Close`**: Should `Close` call `rd_kafka_flush(rd, 5000)` before `rd_kafka_destroy`? Recommended by librdkafka to avoid losing in-flight messages.

2. **Consumer `rd_kafka_consumer_close`**: Must be called before `rd_kafka_destroy`. Should happen in `ConsumerGC`. Any concern about blocking in GC?

3. **Re-subscribe behaviour**: Calling `Subscribe`/`Assign` a second time replaces the current subscription (librdkafka allows it). The old coroutine stays live — user's responsibility to stop it first. Document this.

4. **`poll_timeout_ms`**: Default `0` (non-blocking). Expose a setter on the coroutine (`co:SetTimeout(ms)`) or leave for a follow-up?
