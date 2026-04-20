# Lua Environment API Reference

A comprehensive reference for all available functions in the Lua environment.

---

## Table of Contents

- [Global Functions](#global-functions)
- [Mutex](#mutex)
- [Redis](#redis)
- [CSV](#csv)
- [Kafka](#kafka)
- [Archive](#archive)
- [Stream](#stream)
- [Base64](#base64)
- [Aes](#aes)
- [Process](#process)
- [HttpClient](#httpclient)
- [HttpServer](#httpserver)
- [Hashing (SHA256, MD5, SHA1)](#hashing)
- [MySQL](#mysql)
- [Postgres](#postgres)
- [Timer](#timer)
- [SQLite](#sqlite)
- [Json](#json)
- [Wchar](#wchar)
- [Identifier](#identifier)
- [DateTime](#datetime)
- [Decimal](#decimal)
- [MongoDB](#mongodb)
- [FileSystem](#filesystem)
---

## Global Functions

### CRC Functions

```lua
int CRC32(stringdata, opt existingcrc)
int CRC64(data)
```
- `CRC32`: Calculate a CRC32 checksum
- `CRC64`: Calculate a CRC64 (works with stream and wchar, converts non-strings via `tostring`)

### Time & Sleep

```lua
nil Sleep(opt int)
nil Yield()
int Time()
ms Runtime()
```
- `Sleep`: Yield the current coroutine for the specified ms (default 0). Falls back to a blocking OS sleep when called outside a scheduler-managed coroutine.
- `Yield`: Cooperatively yield the current coroutine back to the scheduler immediately (no sleep delay). For inline sync calls, this briefly releases Lua access so the scheduler and variable bridge can service their queues before the call is resumed.
- `Time`: Get current Unix epoch in milliseconds
- `Runtime`: Get runtime in milliseconds

### Error Handling

```lua
string, code GetLastError(opt lasterrorcode)
```
Retrieves the last error code as a message and code.

### Shell

```lua
bool ShellExecute(file, parameter)   -- Windows only
```

### Memory

```lua
int GetMemory()
```
Returns memory in bytes used by Lua.

### String Functions

```lua
bool string.equal(str1, str2)
```
Compares two strings ignoring case.

### Environment Variables

```lua
int setenv(var, value, override)
string (or nil) getenv(var)
```
`getenv` returns an empty string when a variable is unset.

### Table Functions

```lua
object table.first(table, function(key, value) ... end)
array table.select(table, function(key, value) ... end)
```
- `table.first`: Returns first non-nil result from delegate
- `table.select`: Returns all non-nil values as an array

### DNS & Network

```lua
string or array Dns(name, full default false)
string or nil GetComputerName()
```
- `Dns`: If `full=true`, returns an array of objects with fields `Type` (`"IPV4"`/`"IPV6"`) and `IP`
- If `full=false`, returns first IPv4 address or `nil`
- `GetComputerName`: Retrieve fully qualified computer name

### Memory Status

```lua
int GlobalMemoryStatus(opt type)
```

**Type values:**
| Value | Description |
|-------|-------------|
| 0 | Percentage in use (default) |
| 1 | Total KB of physical memory |
| 2 | Free KB of physical memory |
| 3 | Total KB of paging file |
| 4 | Free KB of paging file |
| 5 | Total KB of virtual memory |
| 6 | Free KB of virtual memory |

### Miscellaneous

```lua
table BencodeDecode(binarystring)
bool GetIsAdmin()
```

### Global Variables

| Variable | Description |
|----------|-------------|
| `c` | Table with special characters 0-31 (e.g., `c.LF = '\n'`) |
| `ARGS[1]` | The script file being run (or `"cmd"` in REPL mode) |
| `ARGS[2..n]` | Additional command-line parameters passed after the script name |
| `ID` | Integer ID of the currently running coroutine (set by the scheduler before each resume) |
| `VERSION` | Engine version string (e.g. `"1.0.0"`) |
| `CPUID` | CPU identifier string returned by the CPUID instruction |
| `DEBUG` | `true` in debug builds; not defined in release builds |

---

## Mutex

```lua
Mutex Mutex.Open(name)
-- or on failure:
nil, errorCode Mutex.Open(name)
bool Mutex:Lock(opt timeout)
nil Mutex:Unlock()
islocked, name, internalid Mutex:Info()
```

| Function | Description |
|----------|-------------|
| `Open` | Opens/creates a named mutex (returns `nil, errorCode` on failure) |
| `Lock` | Lock mutex (waits infinitely if no timeout). Returns `true` on success/already-held |
| `Unlock` | Unlocks the mutex |
| `Info` | Get mutex information |

---

## Redis

### Connection

```lua
Redis Redis.Open(host, port, opt useTls, opt timeout, opt sslOptions, opt password)
```

**SSL Options:**
- `cacert`, `capath`, `cert`, `privatekey`, `servername`
- `verifymode`: 0=none, 1=peer, 2=fail if no peer cert, 4=once, 8=handshake

### Commands

```lua
reply Redis:Command(command, arg, arg, arg, ...)
```

**Reply types:**
| Value | Type |
|-------|------|
| 1 | REDIS_REPLY_STRING |
| 2 | REDIS_REPLY_ARRAY |
| 3 | REDIS_REPLY_INTEGER |
| 4 | REDIS_REPLY_NIL |
| 5 | REDIS_REPLY_STATUS |
| 6 | REDIS_REPLY_ERROR |
| 7 | REDIS_REPLY_DOUBLE |
| 8 | REDIS_REPLY_BOOL |
| 9 | REDIS_REPLY_MAP |
| 10 | REDIS_REPLY_SET |
| 11 | REDIS_REPLY_ATTR |
| 12 | REDIS_REPLY_PUSH |
| 13 | REDIS_REPLY_BIGNUM |
| 14 | REDIS_REPLY_VERB |

### Data Types

```lua
RedisKey   Redis:GetKey(key)
RedisString Redis:GetString(key)
RedisValue Redis:GetHashset(key)
RedisValue Redis:GetList(key)
RedisValue Redis:GetSet(key)
RedisValue Redis:GetSortedSet(key)
RedisStream Redis:GetStream(key)
RedisJson   Redis:GetJson(key)
```

### RedisStream

```lua
id RedisStream:Add({key=value})
id, data RedisStream:Read(opt id, opt blocktime)
int RedisStream:Trim(int or id)
```

### RedisKey

```lua
bool RedisKey:Delete()
string RedisKey:Type()
int RedisKey:GetTTL()
bool RedisKey:SetTTL(ms)
```

### RedisString

```lua
int    RedisString:GetTTL()
bool   RedisString:SetTTL(ms)
string RedisString:Set(newValue)      -- returns old value, or nil if key was new
string RedisString:GetOrSet(newValue) -- alias: GetSet
string RedisString:Delete()           -- returns value before deletion
byte   RedisString:At(n)             -- byte at 1-based position; nil if out of range
length RedisString:len()
```

Metamethod shortcuts:
- `#str` — same as `len()`
- `str[n]` — same as `At(n)` (read)
- `str[n] = byte` — writes byte via `SETRANGE`
- `str1 .. str2` — concatenates, returning a Lua string
- `pairs(str)` — iterates bytes as `(position, byte)` pairs

### Iterator Example

```lua
for key in redis do
    print(key)
end
```

### Pub/Sub

```lua
thread, errmsg  Redis:Subscribe(channel, ...)
thread, errmsg  Redis:PSubscribe(pattern, ...)
```

Opens a **dedicated connection** and sends `SUBSCRIBE` / `PSUBSCRIBE` for the given channels or patterns. Returns a coroutine thread on success, or `nil, errmsg` on failure.

Drive the coroutine with `coroutine.resume(co, stop_flag)`:

- Pass `false` (or nothing) to poll for the next message.
- Pass `true` to unsubscribe, free the dedicated connection, and let the coroutine die.
- When a message arrives the coroutine **yields** rather than returning, so drive it in a loop.

| Resume result | Meaning |
|---|---|
| `true, channel, message` | A message arrived on `channel` |
| `true, pattern, channel, message` | A `PSubscribe` message matched `pattern` on `channel` |
| `true` (no extra values) | Subscribe/unsubscribe acknowledgement — resume again |
| `true, nil, errmsg` | Connection error; coroutine is now dead |

```lua
-- Subscribe example
local co = assert(redis:Subscribe('news', 'alerts'))
while coroutine.status(co) == 'suspended' do
    local ok, ch, msg = coroutine.resume(co)
    if ch then print(ch, msg) end
    if done then coroutine.resume(co, true) end
end
```

### RedisJson

`Redis:GetJson(key)` returns a `RedisJson` object representing the root path (`$`) of a RedisJSON key. Paths are built by chaining field names or 1-based integer indices via `__index`.

```lua
RedisJson  redis:GetJson(key)

value      json:Get()         -- fetch decoded value at current path
nil        json:Set(value)    -- write value at current path
int        json:Delete()      -- delete at current path; returns count removed
string     json:Type()        -- JSON type string: "null", "boolean", "integer",
                              --   "number", "string", "object", "array"
int        json:Length()      -- array length at current path
```

Metamethod shortcuts:
- `json.field` — descends into object field (path chaining, returns new `RedisJson`)
- `json[n]` — descends into array element at 1-based index `n` (0 raises an error)
- `json.field = value` / `json[n] = value` — calls `Set`
- `#json` — calls `Length`
- `tostring(json)` — shows key and accumulated path
- `json()` — `__call`: returns key name and Redis type string
- `pairs(json)` — iterates object keys/values or array elements at current path

```lua
local j = redis:GetJson('config')
print(j.version:Get())         -- scalar at $.version
print(j.servers[1].host:Get()) -- nested path $.servers[0].host
j.debug:Set(false)             -- JSON set
j.servers[2]:Delete()          -- JSON del
print(j:Type())                -- e.g. "object"
print(j.items:Length())        -- array length
```

---

## CSV

```lua
table   CSV.Decode(str_or_wchar [, delimiter])
string  CSV.Encode(rows [, delimiter])
iter    CSV.DecodeFromFunction(fn [, delimiter])
object  CSV.New([delimiter])
```

| Function | Description |
|----------|-------------|
| `Decode` | Decode a complete CSV string or Wchar into a result table |
| `Encode` | Encode an array-of-arrays into a UTF-8 CSV string |
| `DecodeFromFunction` | Return a generic-`for` iterator that streams rows from a supplier function |
| `New` | Return a CSV object with a bound delimiter (or auto-detect when omitted) |

The optional `delimiter` argument accepts:
- A single-character string: `","` `";"` `"|"` `"\t"`
- An integer codepoint: `string.byte(";")` → `59`
- The string `"auto"` or boolean `true` to trigger automatic delimiter detection
- Omitting it (or passing `nil`) defaults to `","` for the direct functions, and `"auto"` for `CSV.New()`

### CSV.Decode

```lua
table CSV.Decode(str_or_wchar [, delimiter])
```

Decodes a complete CSV string or Wchar object. Returns a table with two keys:

| Key | Type | Description |
|-----|------|-------------|
| `Comments` | array of Wchar | Lines beginning with `*` at the top of the file, with the leading `*` stripped |
| `Rows` | array of arrays | Each inner array is one row; each field is a Wchar object |

Leading spaces and tabs before each field are stripped. Quoted fields follow RFC 4180: `""` inside a quoted field becomes a literal `"`.

```lua
local t = CSV.Decode("* header\na,b,c\n1,2,3")
-- t.Comments[1] == " header"
-- t.Rows[1][1]  == "a"
-- t.Rows[2][3]  == "3"

local t = CSV.Decode("a;b;c\n1;2;3", "auto")  -- sniffer detects ";"
local t = CSV.Decode("a;b;c", ";")             -- explicit delimiter
```

> **Memory note:** `Decode` converts the entire input to an internal wide-character buffer before parsing begins. A UTF-8 string of N bytes requires approximately 2×N bytes of additional heap memory for the conversion. For multi-megabyte files, use `DecodeFromFunction` (or `csv:DecodeFromFunction`) with a `Stream` or a chunked supplier function so that peak memory stays bounded to the chunk size rather than the whole file.

### CSV.Encode

```lua
string CSV.Encode(rows [, delimiter])
```

Encodes an array-of-arrays into a UTF-8 CSV string. Each field is converted via `tostring`, so Wchar fields are converted to UTF-8 automatically. Fields containing the delimiter, a double-quote, a newline, or **leading whitespace** are wrapped in double-quotes with inner quotes escaped as `""` (RFC 4180). Rows are joined with `\n`.

```lua
local s = CSV.Encode({{"hello", "world"}, {"foo", "bar"}})
-- s == "hello,world\nfoo,bar"

local s = CSV.Encode({{"value with, comma"}})
-- s == '"value with, comma"'

local s = CSV.Encode({{"a", "b"}}, ";")  -- semicolon delimiter
-- s == "a;b"
```

### CSV.DecodeFromFunction

```lua
iterator CSV.DecodeFromFunction(fn [, delimiter])
```

Returns a generic `for` iterator. On each iteration the supplier function `fn` is called with no arguments and should return a chunk of CSV data as a plain string or Wchar object. The iterator stops when `fn` returns `nil`, `false`, or an empty string. Each iteration yields one row as a sequential table of Wchar fields.

The parser handles chunk boundaries that fall in the middle of a field or row transparently — no alignment of chunks to row boundaries is required.

When `delimiter` is `"auto"` or omitted on a `CSV.New()` object, the sniffer runs once on the first chunk and is not called again.

```lua
-- Stream a large file in 4 KB chunks
local f = io.open("data.csv", "r")
for row in CSV.DecodeFromFunction(function() return f:read(4096) end) do
    print(tostring(row[1]), tostring(row[2]))
end
f:close()

-- Auto-detect delimiter from the stream
for row in CSV.DecodeFromFunction(mySupplierFn, "auto") do ... end
```

> **Note:** Comment lines (starting with `*`) are not detected in streaming mode and appear as regular rows.

### CSV.New

```lua
object CSV.New([delimiter])
```

Returns a lightweight CSV object with `Decode`, `Encode`, and `DecodeFromFunction` methods that all use the bound delimiter. Omitting `delimiter` (or passing `nil`) binds `"auto"` so every `Decode` / `DecodeFromFunction` call sniffs the delimiter from its input independently.

```lua
-- Auto-detect: each Decode call sniffs its own input
local csv = CSV.New()
local t1 = csv:Decode("a,b,c\n1,2,3")  -- detects ","
local t2 = csv:Decode("a;b;c\n1;2;3")  -- detects ";"

-- Fixed delimiter for a known format
local sc = CSV.New(";")
local t  = sc:Decode("a;b;c")
local s  = sc:Encode({{"a", "b", "c"}})  -- "a;b;c"
for row in sc:DecodeFromFunction(fn) do ... end
```

> `Encode` on a `CSV.New()` (auto-detect) object uses `","` as the output delimiter since auto-detection has no meaning when producing output. Use `CSV.New(";")` if you need a specific delimiter for both reading and writing.

### Delimiter auto-detection (sniffer)

Candidates tried in preference order: `,` `\t` `;` `|`

The sniffer scans up to the first 5 lines, counts each candidate's occurrences per line (quoted fields are ignored), and picks the candidate whose count is most consistent across lines. Falls back to `,` when no candidate appears consistently (e.g. single-column data or empty input).

---

### Edge cases and defined behaviours

| Situation | Behaviour |
|-----------|-----------|
| **Empty input** | `CSV.Decode("")` produces `{Rows={}}` — zero rows, empty `Rows` table |
| **Trailing newline** | A single trailing `\n` does **not** create an extra row (the newline is consumed as the end-of-row sentinel) |
| **Double trailing newline** | `"a,b\n\n"` produces a second empty row `[""]` |
| **Leading whitespace in unquoted fields** | Spaces and tabs before a field value are stripped during decode. `Encode` quotes fields with leading whitespace to preserve round-trip fidelity |
| **Trailing whitespace in unquoted fields** | Preserved as-is; only *leading* whitespace is stripped |
| **Unquoted field containing `"`** | Treated leniently: the `"` turns on quote-mode mid-field. `hel"lo"world` → `helloworld` |
| **Multi-character delimiter** | Only the first character is used; `CSV.Decode(s, "||")` behaves as `|` |
| **Non-ASCII delimiter** | Matched at the byte level in `Encode`; works correctly for all printable ASCII delimiters (`,` `;` `|` `\t` etc.) |
| **`"` as delimiter** | Not supported; the parser uses `"` as the quoting character |
| **Wchar delimiter argument** | Not accepted by the direct functions; pass a single-character string or integer codepoint instead (or use `CSV.New()`) |
| **`*` comment mid-file** | Only lines at the very start of the input are checked for `*`; a `*` anywhere else is a regular field character |
| **Sniffer on single-line input** | Any consistently-occurring candidate wins; for a tie or no candidates, falls back to `,` |
| **`CSV.New()` Encode delimiter** | Uses `,` — auto-detect has no meaning for output. Bind an explicit delimiter (`CSV.New(";")`) if you need a specific character for both reading and writing |

---

## Kafka

### Creation

```lua
KafkaProducer  Kafka.NewProducer(opt conf)
KafkaConsumer  Kafka.NewConsumer(opt conf)
```

`conf` is an optional table of librdkafka configuration key/value pairs.  
Default `group.id` values: `"LUAP"` (producer), `"LUAC"` (consumer).

---

### KafkaProducer

#### Producing

```lua
bool, errmsg  producer:Send(topic, key, value [, headers [, partition]])
```

- `key` — may be `nil` for keyless messages
- `headers` — optional table of string key/value pairs: `{source='app', version='1'}`
- `partition` — optional integer; omit (or pass `nil`) for automatic partitioning

#### Offsets & metadata

```lua
bool, low, high  producer:GetOffsets(topic, partition [, timeout_ms])
bool, metadata   producer:GetMetadata([timeout_ms])
```

`GetMetadata` returns `true, meta` where `meta` is:
```lua
{
  Brokers = { {Id=N, Host='...', Port=N}, ... },
  Topics  = { {Name='...', ErrorCode=N, Error='...', Partitions={...}}, ... },
  OrigBrokerId   = N,
  OrigBrokerName = '...',
}
```

#### Topic admin

```lua
bool, errmsg  producer:CreateTopic(name, partitions [, retention_ms [, retention_bytes [, replication_factor [, timeout_ms]]]])
bool, errmsg  producer:DestroyTopic(name [, timeout_ms])
bool, config  producer:GetTopicConfig(name [, timeout_ms])
bool, errmsg  producer:SetTopicConfig(name, {['key']='value', ...} [, timeout_ms])
```

- `CreateTopic` — `nil` retention/replication values use the broker default (`-1`).
- `GetTopicConfig` — returns `true, table` where the table maps config names to their current string values (e.g. `{['retention.ms']='86400000', ...}`).
- `SetTopicConfig` — uses `IncrementalAlterConfigs`; only the keys present in the table are changed, all other config is untouched.

#### Group admin

```lua
bool, groups   producer:ListGroups([timeout_ms])
bool, descs    producer:DescribeGroups({groupId, ...} [, timeout_ms])
bool, errmsg   producer:DeleteGroup(groupId [, timeout_ms])
bool, offsets  producer:GetGroupOffsets(groupId [, partitions [, timeout_ms]])
bool, errmsg   producer:SetGroupOffsets(groupId, {['topic:N']=offset, ...} [, timeout_ms])
bool, errmsg   producer:DeleteGroupOffsets(groupId, {'topic:N', ...} [, timeout_ms])
```

**`ListGroups`** returns `true, { {GroupId, State}, ... }`.

**`DescribeGroups`** returns `true, { desc, ... }` where each `desc` is:
```lua
{
  GroupId     = 'my-group',
  State       = 'Stable',       -- 'Unknown'|'PreparingRebalance'|'CompletingRebalance'|'Stable'|'Dead'|'Empty'
  Protocol    = 'range',        -- partition assignor
  Error       = '',             -- non-empty string on per-group error
  Coordinator = { Id=N, Host='...', Port=N },
  Members = {
    { ClientId='...', ConsumerId='...', Host='...',
      Partitions = { {Topic='...', Partition=N}, ... } },
    ...
  },
}
```

**`GetGroupOffsets`** — `partitions` is an optional array `{'topic:N', ...}`; omit or pass `nil` to retrieve all committed partitions. Returns `true, {['topic:N']=offset, ...}`.

**`SetGroupOffsets`** — sets committed offsets using `AlterConsumerGroupOffsets`. The group must be inactive (no live members).

**`DeleteGroupOffsets`** — removes committed offsets for the listed partitions. After deletion the partition's next start position is governed by `auto.offset.reset`. The group must be inactive.

**`DeleteGroup`** — deletes the group entirely. The group must have no active members.

```lua
bool, errmsg  producer:Close()
```

---

### KafkaConsumer

All topic-admin and group-admin methods available on `KafkaProducer` are also available on `KafkaConsumer` with identical signatures.

#### Consuming

```lua
coroutine  consumer:Subscribe({'topic', ...})
coroutine  consumer:Assign({'topic:partition[:offset]', ...})
```

**Offset keyword in `Assign`:**

| String | librdkafka offset | Behaviour |
|--------|-------------------|-----------|
| `"topic:N"` | `OFFSET_STORED` | Uses committed offset; falls back to `auto.offset.reset` |
| `"topic:N:earliest"` | `OFFSET_BEGINNING` | Always starts from message 0 |
| `"topic:N:latest"` | `OFFSET_END` | Starts after the current last message |
| `"topic:N:123"` | `123` | Starts from exact offset 123 |

Both methods return a **Lua thread** (coroutine).

#### Driving the consume coroutine

```lua
ok, data = coroutine.resume(co, stop_flag)
```

- Pass `false` (or any falsy value) to poll for the next message.
- Pass `true` to stop: the coroutine frees its resources and dies cleanly.
- Returns `true, nil` when no message is available yet (call again after a short sleep).
- Returns `true, message` when a message arrives.
- Returns `false, errmsg` if the coroutine encountered an error.

**Message table fields:**

| Field | Type | Description |
|-------|------|-------------|
| `Value` | string | Message payload |
| `Key` | string or nil | Message key |
| `Topic` | string | Topic name |
| `Partition` | number | Partition index |
| `Offset` | number | Offset within the partition |
| `Timestamp` | number | Message timestamp (ms) |
| `ErrorCode` | number | librdkafka error code (0 = success) |
| `Error` | string | Error description |
| `Headers` | table | Key/value header table |

#### Coroutine methods

```lua
co:AutoCommit(bool)   -- enable (true) or disable (false) automatic offset commit
```

#### Manual commit

```lua
bool, errmsg  consumer:Commit(message_data)
```

After a successful commit the message handle is cleared; calling `Commit` on the same data a second time returns `false, errmsg`.

#### Seeking

```lua
bool, errmsg  consumer:Seek(topic, partition, offset [, timeout_ms])
```

Repositions an already-assigned, already-polling partition. `offset` accepts a number or the keywords `"earliest"`, `"latest"`, `"stored"`. Uses `rd_kafka_seek_partitions` internally; the most reliable pattern is a **specific numeric offset** obtained from `GetOffsets`.

```lua
bool, errmsg  consumer:Close()
```

---

### Module-level utility

```lua
string  Kafka.Logs([filename])
```

Returns (and optionally saves to file) the accumulated librdkafka log output.

---

## Archive

```lua
Archive Archive.OpenRead(filename, opt usewchar)
array   Archive:Entries()
file, size Archive:SetEntry(index)
data    Archive:Read(opt buffer)
string  Archive:ReadAll()
```

**Entries returns:** Array of tables with `Name` and `Size`

- **`ReadAll`** — reads the entire current entry into a single Lua string in one call. More convenient than looping with `Read` for entries that must be consumed completely.

---

## Stream

### Creation

```lua
Stream Stream.Create(opt string)
Stream Stream.Create(backendfunction)
Stream Stream.Open(filename, mode)
```

- **No argument** — creates a new empty in-memory stream.
- **String argument** — creates an in-memory stream pre-loaded with the string contents, with the position reset to 0.
- **Function argument** — creates a stream backed by the provided Lua function. The function is called with an opcode as its first argument and must handle all `STREAM_OP_*` operations it wishes to support. It must return the capability bitmask when called with `STREAM_OP_OPEN` (0).
- **`Open(filename, mode)`** — opens a file as a stream. `mode` follows standard C `fopen` conventions: `"rb"`, `"wb"`, `"r"`, `"w"`, `"ab"`, etc. Raises an error if the file cannot be opened.

### Custom Backend Functions

A backend function is called as `backend(opcode, arg)` whenever the stream engine needs to perform an operation. The function must handle at minimum `STREAM_OP_OPEN` and `STREAM_OP_CLOSE`; all other opcodes only need to be handled if the corresponding capability flag is advertised.

**Opcodes:**
| Value | Constant | Arg | Expected return |
|-------|----------|-----|-----------------|
| 0 | `STREAM_OP_OPEN` | — | Integer capability bitmask (`STREAM_CAP_*`) |
| 1 | `STREAM_OP_CLOSE` | — | `true` or `false [, errmsg]` |
| 2 | `STREAM_OP_READ` | `len` (0 = read all remaining) | String of up to `len` bytes, or `nil` / `false [, errmsg]` at EOF / on error |
| 3 | `STREAM_OP_WRITE` | `data` (string) | `true` or `false [, errmsg]` |
| 4 | `STREAM_OP_CURPOS` | — | Integer: current byte position |
| 5 | `STREAM_OP_LEN` | — | Integer: total byte length |
| 6 | `STREAM_OP_SETPOS` | `pos` (integer) | `true` or `false [, errmsg]` |
| 7 | `STREAM_OP_INFO` | — | Any value — returned as `backendInfo` from `GetInfo()` |
| 8 | `STREAM_OP_HASDATA` | — | Integer bytes ready (>1), `true` (ready, count unknown), or `false`/`nil` (nothing available yet) |

**Capability flags advertised via `STREAM_OP_OPEN`:**
| Value | Constant | Enables |
|-------|----------|---------|
| 1 | `STREAM_CAP_READ` | `Read`, `ReadByte`, `ReadUtf8`, typed reads, `Compress`/`Decompress` source |
| 2 | `STREAM_CAP_WRITE` | `Write`, `WriteByte`, `WriteUtf8`, typed writes, `Compress`/`Decompress` destination |
| 4 | `STREAM_CAP_SEEK` | `Seek`, `pos`, `len`, `SetByte` with position, `PeekByte` (requires both `CAP_READ` and `CAP_SEEK`) |

> **Note:** There is no `STREAM_CAP_PEEK` flag. `PeekByte` is gated on `CAP_READ | CAP_SEEK` — any seekable readable stream supports it via the save-pos / read / restore-pos path.

**Example — read/write in-memory backend:**

```lua
local function makeStream()
    local OPEN, CLOSE, READ, WRITE = 0, 1, 2, 3
    local CURPOS, LEN, SETPOS, INFO = 4, 5, 6, 7
    local CAP_READ, CAP_WRITE, CAP_SEEK = 1, 2, 4

    local buf = ''
    local pos = 0

    return Stream.Create(function(op, arg)
        if op == OPEN then
            return CAP_READ + CAP_WRITE + CAP_SEEK

        elseif op == CLOSE then
            buf = nil
            return true

        elseif op == READ then
            if pos >= #buf then return nil end
            local n = (arg == 0) and (#buf - pos) or arg
            local chunk = buf:sub(pos + 1, pos + n)
            pos = pos + #chunk
            return chunk

        elseif op == WRITE then
            -- overwrite at current position, extend if needed
            buf = buf:sub(1, pos) .. arg .. buf:sub(pos + #arg + 1)
            pos = pos + #arg
            return true

        elseif op == CURPOS then
            return pos

        elseif op == LEN then
            return #buf

        elseif op == SETPOS then
            pos = math.max(0, math.min(arg, #buf))
            return true

        elseif op == INFO then
            return { pos = pos, len = #buf, type = 'lua' }
        end
    end)
end

local s = makeStream()
s:Write('hello world')
s:Seek(6)
print(s:Read())   -- "world"
print(s:pos())    -- 11
```

### Read/Write Operations

```lua
bool, err   Stream:WriteByte(byte)
byte        Stream:ReadByte()
byte        Stream:PeekByte(opt pos)
void        Stream:SetByte(byte, opt position)
int         Stream:Write(string or Wchar, opt size)
bool        Stream:WriteUtf8(str)
string, int Stream:ReadUtf8()
Wchar       Stream:ReadWchar(opt n)
string      Stream:Read(opt length)
bool/int    Stream:HasData()
int         Stream:Id()
nil         Stream:Close()
```

- **`Write`** accepts a `string`, `Wchar`, `number`, or `boolean`. A `Wchar` is written as raw UTF-16 LE bytes (2 bytes per code unit); use `WriteUtf8` instead to write its UTF-8 encoding. The optional `size` argument limits the number of bytes written. Returns the number of bytes written, or `0` on failure.
- **`WriteUtf8`** converts a Lua string from Latin-1/byte values to proper UTF-8 before writing.
- **`ReadWchar`** reads `n` UTF-16 LE code units (each 2 bytes) from the current position and returns a `Wchar`. If `n` is omitted or `nil`, reads all remaining bytes. Returns `nil` if the stream is not readable or there are no complete code units available.
- **`HasData`** — non-blocking availability check. For sync (seekable) streams returns the number of bytes remaining as an integer, or `false` at EOF. For async streams (vtable with `hasdata`) returns `true` if data is ready in the buffer, `false` if nothing is available yet (more may arrive later — `false` is **not** EOF for async streams). For fn backends dispatches `STREAM_OP_HASDATA`; returns `nil`/`false` if the backend has no handler. **Never yields.**
- **`Id`** — returns a stable integer identity value for this stream, suitable for use as a cache key or for distinguishing two stream references. Calls the backend's `getid` if available; otherwise falls back to the native pointer value.
- **`Close`** — explicitly frees the stream's resources and marks it unusable. Called automatically by the GC; safe to call early when resources should be released promptly.

### Stream Info

```lua
capsTable, backendInfo Stream:GetInfo()
length Stream:len()
pos Stream:pos()
void Stream:Seek(opt pos)
```

`GetInfo()` returns two values:
- `capsTable` — `{ Caps = number }` where `Caps` is the capability bitmask (`STREAM_CAP_*` flags)
- `backendInfo` — backend-defined; for in-memory streams: `{ pos, len, alloc }`

**`STREAM_CAP_*` flags:**
| Value | Constant | Description |
|-------|----------|-------------|
| 1 | `STREAM_CAP_READ` | Stream supports read operations |
| 2 | `STREAM_CAP_WRITE` | Stream supports write operations |
| 4 | `STREAM_CAP_SEEK` | Stream supports seeking (`Seek`, `pos`) |

In-memory streams created with `Stream.Create()` have all three flags set (`Caps = 7`).

### Compression

```lua
Stream          Stream:Compress(opt level, opt deststream)
Stream          Stream:Decompress(opt level, opt deststream)
nil, errmsg     Stream:Compress(...)   -- on failure
nil, errmsg     Stream:Decompress(...) -- on failure
Stream          Stream.Compress(source, opt level, opt deststream)
Stream          Stream.Decompress(source, opt level, opt deststream)
```

Both functions work on **Windows and Linux** and accept **sync or async** source streams. Both read the source from position **0** in 64 KB chunks, yielding cooperatively for async sources, and write the result to the destination.

- The instance form (`stream:Compress()`) uses the stream itself as the source.
- The static module form (`Stream.Compress(source)`) accepts any readable stream — including async streams created with a custom function backend.
- If `deststream` is omitted or `nil`, a new in-memory stream is created, written to, rewound to position 0, and returned.
- If `deststream` is provided it is written to **at its current position** and returned as-is (no automatic seek).
- On failure (non-readable source, non-writable destination, or internal error) both return `nil, errmsg`.

**Compression level** (`level` argument to `Compress`):

| Value | Meaning |
|-------|---------|
| -1 | Default — equivalent to level 6 (omitting the argument uses this) |
| 0 | No compression — data is stored uncompressed |
| 1 | Fastest / best speed |
| 2–8 | Intermediate levels |
| 9 | Maximum compression / slowest |

The `level` argument to `Decompress` is accepted for API consistency but is silently ignored — decompression always recovers the original data regardless of the level used to compress it.

**Wire format** (produced by `Compress`, consumed by `Decompress`):

A sequence of one or more chunks followed by an end-of-stream sentinel:

```
[ uint32_le  uncompressedSize ]
[ uint32_le  compressedSize   ]
[ compressedSize bytes        ]   ← zlib-format: 2-byte header + deflate + 4-byte Adler32
```

The sentinel is a pair of zero-valued `uint32` fields (`uncompressedSize == 0`). Each chunk corresponds to one 64 KB (65 536 byte) block of input, except the last chunk which may be smaller.

### Typed Read/Write

```lua
bool Stream:WriteFloat() / number Stream:ReadFloat()
bool Stream:WriteDouble() / number Stream:ReadDouble()
bool Stream:WriteShort() / int Stream:ReadShort()
bool Stream:WriteUnsignedShort() / int Stream:ReadUnsignedShort()
bool Stream:WriteInt() / int Stream:ReadInt()
bool Stream:WriteUnsignedInt() / int Stream:ReadUnsignedInt()
bool Stream:WriteLong() / int Stream:ReadLong()
bool Stream:WriteUnsignedLong() / int Stream:ReadUnsignedLong()
Wchar Stream:ReadWchar(opt n)
```

---

## Base64

```lua
base64string Base64.Encode(string)
string Base64.Decode(base64string)
string Base64.GetEncodeTable()
void Base64.SetEncodeTable(encodetablestring)
```

---

## Aes

```lua
Aes Aes.Create(key, opt iv, opt usectr)
data Aes:Encrypt(data)
data Aes:Decrypt(data)
nil Aes:SetIV(opt iv)
```

Creates AES-256-CBC, AES-256-ECB, or AES-256-CTR context.

---

## Process

```lua
table Process.All()
Process Process.Open(opt id)
Process Process.Start(app, cmd, directory, noconsole, opt redirectinputoutput)
string Process:ReadFromPipe(opt buffersize)
int Process:WriteToPipe()
string Process:ReadErrorFromPipe(opt buffersize)
bool Process:Stop()
int/nil Process:GetExitCode()
int Process:GetID()
string Process:GetName()
number Process:GetCPU()              -- Windows only
number Process:GetRAM()
int/bool Process:Priority(opt prio)  -- Windows only
int, int Process:Affinity(opt newmask) -- Windows only
array Process:Threads()              -- Windows only
```

---

## HttpClient

The `HttpClient` global

### Creation and utilities

```lua
HttpClient HttpClient.Create()
string     HttpClient.UrlEncode(str)
string     HttpClient.UrlDecode(str)
```

| Function | Description |
|----------|-------------|
| `Create` | Create a new HTTP client |
| `UrlEncode` | Percent-encode a string; unreserved characters (`A–Z a–z 0–9 - _ . ~`) pass through unchanged |
| `UrlDecode` | Decode a percent-encoded string; `+` is decoded as a space |

### Client configuration

```lua
nil client:SetTimeout(ms)
nil client:SetFollowRedirects(bool)
nil client:SetVerifySSL(bool)
nil client:SetDefaultHeader(name, value)
nil client:SetBinary(bool)
```

| Function | Description |
|----------|-------------|
| `SetTimeout` | Request timeout in milliseconds. `0` = no timeout (default) |
| `SetFollowRedirects` | Follow HTTP redirects. Default `true` |
| `SetVerifySSL` | Verify SSL certificates. Default `true` |
| `SetDefaultHeader` | Add a header sent with every request on this client |
| `SetBinary` | When `true`, `Write` calls on WebSocket connections from this client send binary frames instead of text frames. Default `false` |

### Buffered request

```lua
coroutine, errmsg client:Request(method, url, opt body, opt headers, opt outStream)
```

Returns a coroutine immediately. Drive it with `coroutine.resume` until a non-nil result table is returned. `body` is an optional string. `headers` is an optional per-request header table. `outStream` is an optional writable `Stream`; when provided the response body is written there and `Contents` in the result is `nil`.

**Result table:**

| Field | Type | Description |
|-------|------|-------------|
| `Code` | integer or nil | HTTP status code; `nil` on transport error |
| `Status` | string | Status text (e.g. `"OK"`) or transport error message |
| `Contents` | string or nil | Response body; `nil` when `outStream` was provided |
| `Headers` | table | Response headers keyed by header name |

### Streaming request

```lua
Stream, errmsg client:Stream(method, url, opt body, opt headers)
```

Returns an async read-only `Stream` and yields the calling coroutine until response headers have arrived. Call `stream:GetInfo()` for metadata, then `stream:Read()` in a loop to receive body chunks. Must be driven from inside a coroutine.

`stream:GetInfo()` returns:

| Field | Type | Description |
|-------|------|-------------|
| `Code` | integer | HTTP status code |
| `Status` | string | Status text |
| `Headers` | table | Response headers keyed by header name |
| `Url` | string | Effective URL after any redirects |

### WebSocket connection

```lua
Stream, errmsg client:Connect(url, opt headers)
```

Connects to a WebSocket endpoint and yields the calling coroutine until the HTTP 101 upgrade completes. Returns an async `Stream`. Must be driven from inside a coroutine.

Binary frame mode is controlled per-client: call `client:SetBinary(true)` before writing to send binary frames; `client:SetBinary(false)` to switch back to text frames (the default).

`ws:GetInfo()` returns metadata about the **last received** frame:

| Field | Type | Description |
|-------|------|-------------|
| `Binary` | boolean | `true` if the last received frame was binary |
| `Opcode` | integer | WebSocket opcode (1 = text, 2 = binary, 8 = close, 9 = ping, 10 = pong) |
| `BytesLeft` | integer | Bytes remaining for fragmented frames; `0` for a complete frame |

### Examples

```lua
-- Buffered GET
local client = HttpClient.Create()
client:SetTimeout(8000)
local co = client:Request('GET', 'https://httpbin.org/get')
local ok, result
repeat ok, result = coroutine.resume(co) until result ~= nil
print(result.Code, result.Contents)

-- Streaming GET (must run inside a coroutine)
local stream = client:Stream('GET', 'https://httpbin.org/get')
local info = stream:GetInfo()
local chunk = stream:Read()
while chunk do io.write(chunk); chunk = stream:Read() end
stream:Close()

-- WebSocket echo (must run inside a coroutine)
local ws = client:Connect('wss://echo.websocket.org')
ws:Read()                         -- drain server welcome frame
ws:Write('hello')
print(ws:Read())                  -- "hello"

client:SetBinary(true)
ws:Write('\xDE\xAD\xBE\xEF')    -- binary frame
client:SetBinary(false)
ws:Close()
```

---

## HttpServer

An embedded HTTP/1.1 server backed by [Mongoose](https://github.com/cesanta/mongoose). The server runs entirely inside the Lua coroutine that drives its `Accept()` loop — no background threads are created. TLS is supported via Mongoose's built-in mbedTLS integration.

### Creation

```lua
HttpServer, errmsg  HttpServer.Listen(address [, tlsOpts])
```

Binds to `address` (e.g. `"0.0.0.0:8080"` or `"127.0.0.1:443"`). Returns the server on success, or `nil, errmsg` on failure. The optional `tlsOpts` table enables TLS:

| Field | Type | Description |
|-------|------|-------------|
| `cert` | string | Path to PEM certificate file |
| `key` | string | Path to PEM private key file |
| `ca` | string | Path to CA certificate file (optional, for mutual TLS) |

```lua
-- Plain HTTP
local server = assert(HttpServer.Listen("0.0.0.0:8080"))

-- HTTPS
local server = assert(HttpServer.Listen("0.0.0.0:443", {
    cert = "/etc/ssl/cert.pem",
    key  = "/etc/ssl/key.pem",
}))
```

### Coroutine pump

```lua
coroutine  server:Accept()
```

Returns a coroutine (the same one on repeated calls — idempotent). Drive it with `coroutine.resume`:

- `coroutine.resume(co)` — polls Mongoose, advances any active stream senders, and yields one pending `HttpRequest` when available. Returns `true, HttpRequest` when a request is ready, or `true` with no second value when idle.
- `coroutine.resume(co, true)` — **stop flag**: tears down the server and lets the coroutine die cleanly.

```lua
local co = server:Accept()
while coroutine.status(co) == 'suspended' do
    local ok, req = coroutine.resume(co)
    if req and req:IsFinished() then
        req:GetResponse():Send('hello')
    end
end
```

### Server methods

```lua
nil  server:SetOnDisconnect(fn)
nil  server:Close()
```

| Method | Description |
|--------|-------------|
| `SetOnDisconnect` | Register a `function(req)` called when a connection closes (after the response is sent or on error) |
| `Close` | Tear down the server immediately. Idempotent — safe to call more than once. `__gc` calls this automatically |

---

### HttpRequest

One `HttpRequest` object exists per connection for its lifetime. It is updated in-place on each HTTP message and queued to the `Accept()` coroutine.

```lua
string   req:GetUrl()        -- full path + query string, e.g. "/api/items?id=1"
string   req:GetMethod()     -- HTTP verb: "GET", "POST", "PUT", "DELETE", …
string   req:GetBody()       -- request body (empty string when none)
table    req:GetHeaders()    -- lowercase header names → values
string   req:GetIp()         -- remote address + port, e.g. "127.0.0.1:54321"
integer  req:GetId()         -- unique integer identity (the connection pointer)
bool     req:IsFinished()    -- true once headers and body have been fully received
table    req:GetContext()    -- per-connection Lua table; created lazily, persists across resumes
HttpResponse req:GetResponse() -- returns the paired response object
string   req:GetError()      -- error message string, or nil when no error
```

---

### HttpResponse

```lua
nil   resp:SetCode(code)
nil   resp:SetHeader(name, value)
bool  resp:Send(opt body)
bool  resp:Reject(code, message)
```

| Method | Description |
|--------|-------------|
| `SetCode(code)` | Override the HTTP status code. Default: `200` |
| `SetHeader(name, value)` | Add a response header. May be called multiple times |
| `Send(opt body)` | Send the response. `body` may be omitted (no body), a `string`, or a readable `Stream`. Returns `false` when the request is not yet finished |
| `Reject(code, message)` | Send a minimal error response with the given status code and plain-text body |

#### Stream responses

When `body` is a `Stream`:

- **Seekable stream** (`CAP_READ + CAP_SEEK`): `Content-Length` is determined from `stream:len()` and the body is sent with a known length.
- **Non-seekable stream** (`CAP_READ` only): `Transfer-Encoding: chunked` is used. The coroutine pump reads 64 KB chunks per iteration until the stream returns empty or `nil`.

```lua
-- Non-seekable → chunked
local function make_stream(data)
    local pos = 0
    return Stream.Create(function(op, arg)
        if op == 0 then return 1   -- CAP_READ only, no CAP_SEEK
        elseif op == 2 then
            local chunk = data:sub(pos + 1, pos + arg)
            pos = pos + #chunk
            return chunk
        end
    end)
end
resp:Send(make_stream('hello world'))

-- Seekable → Content-Length
local s = Stream.Create('hello world')
resp:Send(s)
```

---

### Examples

#### Simple GET handler

```lua
local server = assert(HttpServer.Listen("0.0.0.0:8080"))
local co = server:Accept()
while coroutine.status(co) == 'suspended' do
    local ok, req = coroutine.resume(co)
    if not ok then error(req) end
    if req and req:IsFinished() then
        local resp = req:GetResponse()
        resp:SetHeader('Content-Type', 'application/json')
        resp:Send('{"status":"ok"}')
    end
end
```

#### POST echo with status code

```lua
local server = assert(HttpServer.Listen("0.0.0.0:8080"))
local co = server:Accept()
while coroutine.status(co) == 'suspended' do
    local ok, req = coroutine.resume(co)
    if req and req:IsFinished() then
        if req:GetMethod() == 'POST' then
            req:GetResponse():Send(req:GetBody())
        else
            req:GetResponse():Reject(405, 'Method Not Allowed')
        end
    end
end
```

#### Disconnect callback

```lua
local server = assert(HttpServer.Listen("0.0.0.0:8080"))
server:SetOnDisconnect(function(req)
    print('disconnected', req:GetIp())
end)
local co = server:Accept()
while coroutine.status(co) == 'suspended' do
    local ok, req = coroutine.resume(co)
    if req and req:IsFinished() then
        req:GetResponse():Send('bye')
    end
end
```

#### Graceful stop via stop flag

```lua
local server = assert(HttpServer.Listen("0.0.0.0:8080"))
local co = server:Accept()
coroutine.resume(co)          -- start the pump
-- ... handle requests ...
coroutine.resume(co, true)    -- stop: tears down the server, coroutine dies
```

#### Chunked streaming response

```lua
local server = assert(HttpServer.Listen("0.0.0.0:8080"))
local co = server:Accept()
while coroutine.status(co) == 'suspended' do
    local ok, req = coroutine.resume(co)
    if req and req:IsFinished() then
        -- A non-seekable stream triggers Transfer-Encoding: chunked
        local data = string.rep('x', 200000)
        local pos  = 0
        local stream = Stream.Create(function(op, arg)
            if op == 0 then return 1  -- CAP_READ only
            elseif op == 2 then
                local chunk = data:sub(pos + 1, pos + arg)
                pos = pos + #chunk
                return chunk
            end
        end)
        req:GetResponse():Send(stream)
    end
end
```

---

## Hashing

### SHA256

```lua
SHA256 SHA256.New()
nil SHA256:Update(data)
hexstring, 32bytes SHA256:Finish()
```

### MD5

```lua
MD5 MD5.New()
nil MD5:Update(data)
hexstring, 16bytes MD5:Finish()
```

### SHA1

```lua
SHA1 SHA1.New()
nil SHA1:Update(data)
hexstring, 20bytes SHA1:Finish()
```

---

## MySQL

Connects to a MySQL/MariaDB database. All I/O is driven by the MySQL 8.0 nonblocking API (`mysql_real_query_nonblocking`, `mysql_store_result_nonblocking`) so **no background thread is ever created**. The connection is always configured with `utf8mb4` encoding automatically.

```lua
conn, errmsg  MySQL.Connect(host, user, password, database, opt port, opt timeout)
co, errmsg    conn:Query(sql, opt params)
ok, n|errmsg  conn:NonQuery(sql, opt params, opt cancelFn)
ok, v|errmsg  conn:Scalar(sql, opt params, opt cancelFn)
ok, rows|errmsg conn:QueryAll(sql, opt params, opt cancelFn)
bool          conn:IsBusy()
string        conn:EscapeValue(value)
nil           conn:Close()
```

| Function | Description |
|----------|-------------|
| `Connect` | Connect to MySQL, yielding the caller cooperatively during the TCP + auth handshake. Returns the connection on success, or `nil, errmsg` on failure. `port` defaults to `3306`, `timeout` defaults to `10` seconds |
| `Query` | Returns a **Lua coroutine** immediately without blocking. Drive it with `coroutine.resume` as described below. Returns `nil, errmsg` if the connection is already busy |
| `NonQuery` | Helper — drives a query to completion and returns `true, rowcount` (integer), or `false, errmsg` on error. Designed for INSERT / UPDATE / DELETE |
| `Scalar` | Helper — returns `true, col1value` (first column of the first row), or `true, nil` when no rows matched, or `false, errmsg` on error |
| `QueryAll` | Helper — collects every row into an array of integer-keyed row arrays and returns `true, rows`, or `false, errmsg` on error |
| `IsBusy` | Returns `true` while a query coroutine is still alive on this connection |
| `EscapeValue` | Escape a string with `mysql_real_escape_string`. Returns the escaped value **without** surrounding quotes |
| `Close` | Close the connection and free all resources. Safe to call multiple times |

### Helper methods (recommended API)

All three helpers yield the **outer** Kitsune coroutine cooperatively during the async wait, so other coroutines continue to run. An optional zero-argument `cancelFn` is called between each poll; if it returns truthy the query is stopped early and the helper returns `false, "cancelled"`.

```lua
local conn = assert(MySQL.Connect("127.0.0.1", "user", "pass", "mydb"))

-- INSERT / UPDATE / DELETE
local ok, affected = conn:NonQuery(
    "UPDATE users SET name = ? WHERE id = ?", {"Alice", 1})
if not ok then error(affected) end
print(affected .. " row(s) updated")

-- Single value
local ok, name = conn:Scalar("SELECT name FROM users WHERE id = ?", {1})
if not ok then error(name) end
print(name)  -- nil when no row matched

-- All rows
local ok, rows = conn:QueryAll("SELECT id, name FROM users")
if not ok then error(rows) end
for i = 1, #rows do
    print(rows[i][1], rows[i][2])
end

-- Cancel mid-stream
local stop = false
local ok, rows = conn:QueryAll("SELECT id FROM big_table", nil,
    function() return stop end)
```

### Raw coroutine protocol (advanced)

`conn:Query(sql, params)` returns a real Lua coroutine `co`. Drive it with `coroutine.resume` to control streaming directly.

#### Yield protocol

| `coroutine.resume` returns | Meaning |
|---|---|
| `true, nil` + status `"suspended"` | Query / store still in progress — resume again |
| `true, <integer>` | Done — integer is the affected / row count |
| `true, <string>` | Done — string is a query-level error message |
| `true, {col1, col2, …}` | One data row (integer-keyed, 1-based) |
| `true, nil` + status `"dead"` | All rows consumed, C buffer freed |
| `false, <string>` | Coroutine raised a Lua error |

Pass a truthy value as the **first argument** of any `coroutine.resume` call to send the **stop flag**: the coroutine immediately frees the result buffer, clears the connection's busy state, and dies cleanly.

```lua
local conn = assert(MySQL.Connect("127.0.0.1", "user", "pass", "mydb"))
local co   = assert(conn:Query("SELECT id, name FROM users WHERE active = ?", {1}))

-- Phase 1: drive the async state machine until rowcount arrives
local ok, val = coroutine.resume(co)
while ok and val == nil and coroutine.status(co) == "suspended" do
    ok, val = coroutine.resume(co)
end
if not ok then error(val) end          -- coroutine error
if type(val) == "string" then error(val) end  -- query-level error
local rowcount = val                   -- integer

-- Phase 2: stream rows one at a time
ok, val = coroutine.resume(co)
while ok and val ~= nil do
    print(val[1], val[2])              -- val[1] = id, val[2] = name
    ok, val = coroutine.resume(co)
end

-- Stop early at any phase (frees C buffer immediately)
coroutine.resume(co, true)
```

### Parameterized queries

Pass an array table as the second argument to `Query`, `NonQuery`, `Scalar`, or `QueryAll`. `?` placeholders are substituted in order. Missing or `nil` entries become SQL `NULL`. The following Lua types are accepted as parameter values:

| Parameter type | Sent as |
|----------------|---------|
| `nil` | SQL `NULL` |
| `string` | escaped string |
| `number` / `integer` | stringified |
| `boolean` | `"1"` / `"0"` |
| `Wchar` | UTF-8 encoded string |
| `Identifier` | canonical string (`xxxxxxxx-xxxx-…` or 24-char hex) |
| `DateTime` | ISO 8601 string (`YYYY-MM-DDTHH:MM:SS.mmmZ`) |
| `Decimal` | decimal string (e.g. `"123.456"`) |
| `table` | JSON-encoded string |

```lua
conn:NonQuery("INSERT INTO t (a, b, c) VALUES (?, ?, ?)", {"hello", nil, 3.14})
conn:Scalar("SELECT name FROM users WHERE id = ?", {42})
```

### MySQL type mapping

| MySQL type | Lua type |
|------------|----------|
| TINYINT, SMALLINT, MEDIUMINT, INT, BIGINT | integer |
| FLOAT, DOUBLE, BIT | number |
| DECIMAL, NEWDECIMAL | `Decimal` ¹ |
| TINYBLOB, BLOB, MEDIUMBLOB, LONGBLOB | `LuaStream` |
| DATE, DATETIME, TIMESTAMP | `DateTime` ¹ |
| all others (VARCHAR, TEXT, YEAR, TIME, ENUM, JSON, …) | string |

> ¹ Falls back to a plain string when parsing fails (e.g. non-standard server format).

> **Note:** MySQL has no native boolean type. `TINYINT(1)` columns return integer `1` or `0`.

---

## Postgres

Connects to a PostgreSQL database using libpq. All I/O is driven by the libpq async API so **no background thread is ever created**. The connection is always configured with `UTF8` client encoding automatically.

```lua
conn, errmsg    Postgres.Connect(conninfo)
co, errmsg      conn:Query(sql, opt params)
ok, n|errmsg    conn:NonQuery(sql, opt params)
ok, v|errmsg    conn:Scalar(sql, opt params)
ok, rows|errmsg conn:QueryAll(sql, opt params)
bool            conn:IsBusy()
string          conn:EscapeValue(value)
nil             conn:Close()
```

| Function | Description |
|----------|-------------|
| `Connect` | Connect using a libpq connection string (e.g. `"host=localhost user=postgres password=secret dbname=mydb connect_timeout=5"`). Returns the connection on success, or `nil, errmsg` on failure |
| `Query` | Returns a **Lua coroutine** immediately without blocking. Drive it with `coroutine.resume` as described below. Returns `nil, errmsg` if the connection is already busy |
| `NonQuery` | Helper — drives a query to completion and returns `true, rowcount` (integer), or `false, errmsg` on error. Designed for INSERT / UPDATE / DELETE |
| `Scalar` | Helper — returns `true, col1value` (first column of the first row), or `true, nil` when no rows matched, or `false, errmsg` on error |
| `QueryAll` | Helper — collects every row into an array of integer-keyed row arrays and returns `true, rows`, or `false, errmsg` on error |
| `IsBusy` | Returns `true` while a query coroutine is still alive on this connection |
| `EscapeValue` | Escape a string using `PQescapeLiteral`. The result **includes** surrounding single quotes (e.g. `'O''Reilly'`) |
| `Close` | Close the connection and free all resources. Safe to call multiple times |

### Connection String

```
"host=127.0.0.1 port=5432 user=postgres password=secret dbname=mydb connect_timeout=5"
```

### Helper methods (recommended API)

All three helpers yield the **outer** Kitsune coroutine cooperatively during the async wait, so other coroutines continue to run.

```lua
local conn = assert(Postgres.Connect("host=127.0.0.1 user=postgres password=secret dbname=mydb"))

-- INSERT / UPDATE / DELETE
local ok, affected = conn:NonQuery(
    "UPDATE users SET name = $1 WHERE id = $2", {"Alice", 1})
if not ok then error(affected) end
print(affected .. " row(s) updated")

-- Single value
local ok, name = conn:Scalar("SELECT name FROM users WHERE id = $1", {1})
if not ok then error(name) end
print(name)  -- nil when no row matched

-- All rows
local ok, rows = conn:QueryAll("SELECT id, name FROM users")
if not ok then error(rows) end
for i = 1, #rows do
    print(rows[i][1], rows[i][2])
end
```

### Raw coroutine protocol (advanced)

`conn:Query(sql, params)` returns a real Lua coroutine `co`. The protocol is identical to [MySQL](#mysql).

#### Yield protocol

| `coroutine.resume` returns | Meaning |
|---|---|
| `true, nil` + status `"suspended"` | Query still in progress — resume again |
| `true, <integer>` | Done — integer is the affected / row count |
| `true, <string>` | Done — string is a query-level error message |
| `true, {col1, col2, …}` | One data row (integer-keyed, 1-based) |
| `true, nil` + status `"dead"` | All rows consumed |
| `false, <string>` | Coroutine raised a Lua error |

Pass a truthy value as the **first argument** of any `coroutine.resume` call to send the **stop flag**: the coroutine immediately frees the result buffer and dies cleanly.

### Parameterized Queries

Pass an array table as the second argument to `Query`, `NonQuery`, `Scalar`, or `QueryAll`. Uses PostgreSQL native `$1`, `$2`, … placeholders. Missing or `nil` entries are sent as SQL `NULL`. The following Lua types are accepted as parameter values:

| Parameter type | Sent as |
|----------------|---------|
| `nil` | SQL `NULL` |
| `string` | string |
| `number` / `integer` | stringified |
| `boolean` | `"true"` / `"false"` |
| `Wchar` | UTF-8 encoded string |
| `Identifier` | canonical string (`xxxxxxxx-xxxx-…` or 24-char hex) |
| `DateTime` | ISO 8601 string (`YYYY-MM-DDTHH:MM:SS.mmmZ`) |
| `Decimal` | decimal string (e.g. `"123.456"`) |
| `table` | JSON-encoded string |

```lua
conn:Query("SELECT * FROM users WHERE id = $1", {42})
conn:NonQuery("INSERT INTO t (a, b, c) VALUES ($1, $2, $3)", {"hello", nil, 3.14})
```

### PostgreSQL Type OID Mapping

| OID | PostgreSQL type | Lua type |
|-----|-----------------|----------|
| 16 | BOOL | boolean |
| 20 | INT8 (bigint) | integer |
| 21 | INT2 (smallint) | integer |
| 23 | INT4 (integer) | integer |
| 700 | FLOAT4 (real) | number |
| 701 | FLOAT8 (double precision) | number |
| 1700 | NUMERIC | `Decimal` ¹ |
| 2950 | UUID | `Identifier` ¹ |
| 1082 | DATE | `DateTime` ¹ |
| 1083 | TIME | `DateTime` ¹ |
| 1266 | TIMETZ | `DateTime` ¹ |
| 1114 | TIMESTAMP | `DateTime` ¹ |
| 1184 | TIMESTAMPTZ | `DateTime` ¹ |
| all others | TEXT, VARCHAR, BYTEA, JSON, etc. | string |

> ¹ Falls back to a plain string when parsing fails (e.g. non-standard server format).

---

## Timer

```lua
Timer Timer.New()
bool Timer:IsRunning()
nil Timer:Reset()
nil Timer:Start()
nil Timer:Stop()
number Timer:Elapsed()
```

---

## SQLite

```lua
SQLite SQLite.Open(opt filename, opt mode)
bool, txt SQLite:Query(querystring, preparedstatements)
nil   SQLite:Finish()
bool  SQLite:Fetch()
table SQLite:GetRow(opt index)
nil   SQLite:RegisterFunction(function, name, args)
nil   SQLite:RegisterAggregateFunction(function, name, args)
nil   SQLite:ToggleWidechar(bool)
nil   SQLite:SetBusyHandler(opt fn)
nil   SQLite:Close()
```

**Mode:** 0=single thread, 1=multithreaded, 2=serialized

- **`SetBusyHandler(opt fn)`** — registers a callback invoked when a table is locked. The function receives the SQLite instance and the retry count; return truthy to retry, falsy to abort. Pass no argument or `nil` to remove an existing handler.

---

## Json

```lua
Json    Json.New(opt pretty)          -- primary constructor
Json    Json.Create(opt pretty)       -- alias for New (backward compat)
value   Json.Null                     -- unique null sentinel (lightuserdata)
string  json:Encode(value)
value   json:Decode(string | fn | stream)
bool    json:EncodeIntoStream(stream, value)
value   json:DecodeFromStream(stream)
nil     json:Dispose()
```

| Function | Description |
|----------|-------------|
| `New` / `Create` | Create a new Json instance. Pass `true` for pretty-printed output (2 spaces per indent level) |
| `Json.Null` | The unique lightuserdata sentinel that encodes to/decodes from JSON `null`. Compare with `== Json.Null` |
| `Encode` | Encode a Lua value to a JSON string |
| `Decode` | Decode JSON from a string, a chunk-reader function, or a `Stream`. Returns the decoded value |
| `EncodeIntoStream` | Encode `value` and write the JSON bytes directly into `stream`. Returns `true` on success, or `false, errmsg` if the stream is not writable |
| `DecodeFromStream` | Decode one JSON value from `stream`. Returns the decoded value, or `nil, errmsg` if the stream is not readable |
| `Dispose` | Explicitly free the internal output buffer; called automatically by the GC |

### Null Sentinel

JSON `null` decodes to Lua `nil` by default (and `nil` cannot be stored in a table). Use `Json.Null` as a distinguishable sentinel:

```lua
local json = Json.New()

-- Encoding: Json.Null → null
local s = json:Encode({value = Json.Null})  -- {"value":null}

-- Decoding: null → Json.Null
local t = json:Decode(s)
if t.value == Json.Null then
    print("was null")
end
```

### Decode Input Forms

```lua
-- From string
local t = json:Decode('{"x":1}')

-- From a chunk-reader function (called repeatedly; return nil/empty to stop)
local t = json:Decode(function() return file:read(4096) end)

-- From a Stream (sync or async)
local t = json:Decode(myStream)
```

### Type Mapping

| Lua type | JSON type |
|----------|-----------|
| `nil` | `null` (omitted from object fields) |
| `Json.Null` | `null` |
| `boolean` | `true` / `false` |
| integer | number (no decimal point) |
| float | number (trailing zeros trimmed, e.g. `3.5`) |
| `string` | string |
| `Wchar` | string (UTF-8 via `ToUtf8`) |
| `Identifier` | string (canonical UUID or OID hex) |
| `DateTime` | string (ISO 8601, e.g. `"2024-06-01T12:00:00.000Z"`) |
| `Decimal` | number (no quotes — preserves numeric semantics) |
| `LuaStream` | string (all bytes from offset 0; stream position preserved; `null` if not readable+seekable) |
| `table` | object `{}` or array `[]` depending on keys |
| `NaN` | `null` |
| `±Infinity` | `null` |

### Notes

- **Circular references** raise an error: `Json: recursion detected`
- **Table classification**: pure sequential integer-keyed tables (`{1, 2, 3}`) encode as JSON arrays; all others encode as objects
- **UTF-8 strings** pass through the encoder unescaped. Only control characters (U+0000–U+001F) are hex-escaped as `\uXXXX`
- **`Wchar` values** are converted to UTF-8 before encoding
- **`LuaStream`** must be both readable and seekable; unreachable streams encode as `null`

### Examples

```lua
local json = Json.New()

-- Basic encode/decode
local s = json:Encode({name = "Alice", scores = {10, 20, 30}})
local t = json:Decode(s)
print(t.name, t.scores[1])

-- Null sentinel
local t2 = json:Decode('{"x":null}')
print(t2.x == Json.Null)   -- true

-- Pretty print
local pretty = Json.New(true)
print(pretty:Encode({a = 1, b = {2, 3}}))

-- Stream encode
local s = Stream.Create()
json:EncodeIntoStream(s, {hello = "world"})
s:Seek(0)
print(s:Read())

-- Stream decode
local s2 = Stream.Create('{"key":"val"}')
print(json:DecodeFromStream(s2).key)

-- Chunked decode from file
local f = io.open("data.json", "r")
local t = json:Decode(function() return f:read(4096) end)
f:close()
```

---

## Wchar

### Creation

```lua
Wchar Wchar.FromAnsi(str)
Wchar Wchar.FromBytes(array or widestring or int)
Wchar Wchar.FromUtf8(str)
nil Wchar.Setlocale(codepage)
```

### Conversion

```lua
string Wchar:ToUtf8()
string Wchar:ToAnsi()
array Wchar:ToBytes()
```

### Operations

```lua
int Wchar:At(index)
array Wchar:Codepoints()
Wchar Wchar:ToLower()
Wchar Wchar:ToUpper()
Wchar Wchar:Substring(start, opt length)
int Wchar:Find(substring, opt offset)
```

### Metamethods

- `tostring`: same as `ToUtf8`
- `..` (concat): returns new Wchar
- `#` (length): returns length
- `==` (equal): compares Wchars

---

## Identifier

A typed userdata for unique identifiers. Supports UUID (RFC 4122 v4, 16 bytes) and OID (MongoDB ObjectID, 12 bytes).

### Constructors

```lua
Identifier Identifier.NewUUID()
Identifier Identifier.NewOID()
Identifier Identifier.FromString(str)
Identifier Identifier.FromBytes(bytes)
```

- `NewUUID`: generates a new RFC 4122 v4 UUID. On Windows uses `CoCreateGuid`; on Linux uses `getrandom`.
- `NewOID`: generates a new MongoDB-compatible ObjectID (4-byte Unix timestamp + 5 random bytes + 3-byte counter).
- `FromString`: parses a 36-character UUID string (`xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`) or a 24-character hex OID string.
- `FromBytes`: wraps a 16-byte (UUID) or 12-byte (OID) binary string into an Identifier.

### Methods

```lua
string  id:GetType()    -- "UUID" or "OID"
string  id:AsBytes()    -- raw bytes (16 for UUID, 12 for OID)
string  id:AsString()   -- same as tostring(id)
bool    id:IsEmpty()    -- true when all bytes are zero
```

### Metamethods

```lua
tostring(id)   -- canonical string representation
id == other    -- true when type, length, and bytes all match
```

---

## DateTime

A typed userdata representing a point in time with an associated UTC offset. Internally stored as 100-nanosecond ticks since `0001-01-01 00:00:00 UTC` (identical to the .NET `DateTime`/`DateTimeOffset` epoch). All comparisons operate on UTC ticks; the offset is display information only.

### Constructors

```lua
DateTime  DateTime.Now()
DateTime  DateTime.UtcNow()
DateTime  DateTime.New(year, month, day, opt hour, opt minute, opt second, opt millisecond, opt offsetMinutes)
DateTime  DateTime.FromUnixSeconds(ts [, offsetMinutes])
DateTime  DateTime.FromUnixMilliseconds(ms [, offsetMinutes])
DateTime  DateTime.Parse(str [, fallbackOffsetMinutes])   -- returns nil on failure
```

| Function | Description |
|----------|-------------|
| `Now` | Current local time with the system's UTC offset |
| `UtcNow` | Current UTC time (offset = 0) |
| `New` | Construct from individual components. `offsetMinutes` is the UTC offset in minutes `[-840, +840]`; defaults to 0 (UTC) |
| `FromUnixSeconds` | Wrap a Unix timestamp (seconds, may be fractional) into a DateTime |
| `FromUnixMilliseconds` | Wrap a Unix timestamp in integer milliseconds |
| `Parse` | Parse an ISO 8601 / SQL datetime string (`YYYY-MM-DD[T HH:MM[:SS[.fff]]][Z\|±HH:MM]`). If no offset is embedded in the string the optional `fallbackOffsetMinutes` is applied. Returns `nil` on parse failure |

### Component Getters

```lua
int  dt:Year()
int  dt:Month()
int  dt:Day()
int  dt:Hour()
int  dt:Minute()
int  dt:Second()
int  dt:Millisecond()
int  dt:DayOfWeek()        -- 0=Sunday, 1=Monday, ..., 6=Saturday
int  dt:OffsetMinutes()    -- UTC offset in minutes
bool dt:IsEmpty()          -- true when ticks == 0
```

### Conversion

```lua
number    dt:UnixSeconds()          -- fractional Unix timestamp in seconds
int       dt:UnixMilliseconds()     -- Unix timestamp in integer milliseconds
DateTime  dt:ToUtc()                -- same instant, offset forced to 0
DateTime  dt:ToLocal()              -- same instant, offset set to system local offset
DateTime  dt:ToOffset(minutes)      -- same instant, offset changed to given minutes
string    dt:Format(opt fmt)        -- strftime format string; ISO 8601 when omitted
string    dt:AsString(opt fmt)      -- alias for Format
```

### Arithmetic

```lua
DateTime  dt:AddDays(n)
DateTime  dt:AddHours(n)
DateTime  dt:AddMinutes(n)
DateTime  dt:AddSeconds(n)
DateTime  dt:AddMilliseconds(n)
```

All `Add*` functions accept fractional numbers and return a new `DateTime` with the same offset.

### Metamethods

| Metamethod | Behaviour |
|------------|-----------|
| `tostring(dt)` | ISO 8601 string, e.g. `"2024-06-01T12:00:00.000Z"` or with offset `"...+02:00"` |
| `dt1 == dt2` | `true` when UTC ticks are equal (offset is ignored) |
| `dt1 < dt2` | UTC tick comparison |
| `dt1 <= dt2` | UTC tick comparison |
| `dt1 - dt2` | Difference in **seconds** as a `number` |

### Examples

```lua
local now = DateTime.Now()
print(now)                          -- "2024-06-01T14:30:00.000+02:00"
print(now:UnixMilliseconds())       -- Unix ms integer

local utc = DateTime.UtcNow()
print(utc:Year(), utc:Month(), utc:Day())

local dt = DateTime.New(2024, 1, 15, 9, 0, 0, 0, 60)  -- +01:00
print(dt:ToUtc())                   -- "2024-01-15T08:00:00.000Z"

local parsed = DateTime.Parse("2024-06-01T12:00:00Z")
print(parsed - DateTime.UtcNow())   -- seconds until/since that moment

local tomorrow = DateTime.Now():AddDays(1)
print(tomorrow:Format("%Y-%m-%d"))  -- strftime format
```

---

## Decimal

A typed userdata for exact base-10 arithmetic with up to 34 significant digits. Backed by a 128-bit coefficient + sign + scale representation (equivalent to .NET `decimal` / MongoDB `Decimal128`). All arithmetic operators are overloaded so `Decimal` values can be used with `+`, `-`, `*`, `/`, `%`, and unary `-` directly.

### Constructors

```lua
Decimal  Decimal.FromString(str)   -- e.g. "123.456", "-0.001"; nil on failure
Decimal  Decimal.FromNumber(n)     -- convert Lua number (lossy for floats)
Decimal  Decimal.Zero()            -- returns 0
```

### Methods

```lua
string   dec:ToString()    -- canonical decimal string, alias: AsString()
string   dec:AsString()
number   dec:ToNumber()    -- convert to Lua number (lossy)
int      dec:Scale()       -- digits after decimal point
int      dec:Precision()   -- total significant digits
bool     dec:IsEmpty()     -- true when value is zero
bool     dec:IsNegative()  -- true when value < 0
Decimal  dec:Abs()         -- absolute value
Decimal  dec:Round(scale)  -- round to given decimal places
Decimal  dec:Truncate(scale) -- truncate to given decimal places
Decimal  dec:Add(other)
Decimal  dec:Sub(other)
Decimal  dec:Mul(other)
Decimal  dec:Div(other)
```

### Metamethods

| Metamethod | Behaviour |
|------------|-----------|
| `tostring(dec)` | Same as `ToString()` |
| `dec1 == dec2` | Value equality |
| `dec1 < dec2` | Less-than comparison |
| `dec1 <= dec2` | Less-or-equal comparison |
| `dec1 + dec2` | Addition |
| `dec1 - dec2` | Subtraction |
| `dec1 * dec2` | Multiplication |
| `dec1 / dec2` | Division |
| `dec1 % dec2` | Modulo |
| `-dec` | Unary negation |

### Examples

```lua
local a = Decimal.FromString("123.456")
local b = Decimal.FromString("0.001")
print(a + b)             -- "123.457"
print(a * b)             -- "0.123456"
print(a:Round(2))        -- "123.46"
print(a:Scale())         -- 3
print(a:Precision())     -- 6
print(Decimal.FromNumber(math.pi):ToString())  -- "3.14159265358979..."
```

---

## MongoDB

Connects to a MongoDB server using the [libmongoc](https://mongoc.org/) driver. All CRUD operations are dispatched to a **persistent background worker thread** and the calling coroutine yields cooperatively while the operation is in flight — no blocking of the Lua scheduler. Requires the engine to be compiled with `KITSUNE_MONGO`.

### Connection

```lua
Mongo, errmsg  Mongo.Connect(uri)
```

`uri` is a standard [MongoDB connection string](https://www.mongodb.com/docs/manual/reference/connection-string/) (e.g. `"mongodb://localhost:27017"`). Performs an eager ping to verify connectivity. Returns the connection on success, or `nil, errmsg` on failure.

### CRUD Operations

All operations are **asynchronous**: they validate arguments, queue the work on the background thread, and return immediately. Call `Wait()` or `GetResult()` to retrieve the outcome. Only one operation may be in flight per connection at a time.

```lua
bool, errmsg  mongo:Find(db, collection, filter [, limit [, skip [, opts]]])
bool, errmsg  mongo:FindOne(db, collection, filter [, opts])
bool, errmsg  mongo:InsertOne(db, collection, document)
bool, errmsg  mongo:InsertMany(db, collection, documents)
bool, errmsg  mongo:UpdateOne(db, collection, filter, update [, opts])
bool, errmsg  mongo:UpdateMany(db, collection, filter, update [, opts])
bool, errmsg  mongo:DeleteOne(db, collection, filter)
bool, errmsg  mongo:DeleteMany(db, collection, filter)
bool, errmsg  mongo:Aggregate(db, collection, pipeline [, opts])
bool, errmsg  mongo:Command(db, command)
bool, errmsg  mongo:CountDocuments(db, collection, filter [, opts])
```

- All `filter`, `update`, `opts`, `document`, `command` arguments are Lua tables that are converted to BSON automatically.
- `documents` for `InsertMany` is an array of tables.
- `pipeline` for `Aggregate` is an array of stage tables.
- `limit` and `skip` are optional integers.
- Returns `true, nil` on successful dispatch, or `false, errmsg` if the connection is closed or already busy.

### Async Control

```lua
bool          mongo:IsFinished()   -- true when no operation is running
nil           mongo:Wait()         -- yield until current operation completes
nil           mongo:Cancel()       -- request cancellation; yields until done
result, errmsg mongo:GetResult()   -- yield if needed, then return the result
nil           mongo:Close()        -- close connection and free resources
```

**`GetResult` return values by operation:**

| Operation | `result` on success |
|-----------|---------------------|
| `Find`, `Aggregate` | Array of document tables |
| `FindOne` | Single document table, or `nil` if not found |
| `CountDocuments` | Integer count |
| `InsertOne`, `InsertMany`, `UpdateOne`, `UpdateMany`, `DeleteOne`, `DeleteMany`, `Command` | Reply document table |

On error: `nil, errmsg`.

### BSON ↔ Lua Type Mapping

| BSON type | Lua type |
|-----------|----------|
| `UTF8` | string |
| `INT32`, `INT64` | integer |
| `DOUBLE` | number |
| `BOOL` | boolean |
| `NULL` | nil |
| `DOCUMENT` | table (string keys) |
| `ARRAY` | table (integer keys, 1-based) |
| `OID` (12 bytes) | `Identifier` (OID) |
| `UUID` binary (16 bytes) | `Identifier` (UUID) |
| `DATE_TIME` | `DateTime` (UTC, offset = 0) |
| `DECIMAL128` | `Decimal` |
| `BINARY` (other subtypes) | `LuaStream` |
| `TIMESTAMP` | table `{t=ordinal, i=increment}` |
| `REGEX` | string `"/pattern/options"` |

When writing Lua → BSON, `Identifier`, `DateTime`, `Decimal`, `Wchar`, and `LuaStream` values are also recognised and serialised to their corresponding BSON types.

### Example

```lua
local mongo = assert(Mongo.Connect("mongodb://localhost:27017"))

-- Insert
mongo:InsertOne("mydb", "users", {name = "Alice", age = 30})
local reply, err = mongo:GetResult()
if not reply then error(err) end

-- Find
mongo:Find("mydb", "users", {age = {["$gte"] = 18}}, 10)
local docs, err = mongo:GetResult()
if not docs then error(err) end
for i, doc in ipairs(docs) do
    print(doc.name, doc.age)
end

-- CountDocuments
mongo:CountDocuments("mydb", "users", {})
local count = assert(mongo:GetResult())
print(count .. " users")

-- Cancel a slow find
mongo:Find("mydb", "big_collection", {})
mongo:Cancel()   -- yields until cancelled

mongo:Close()
```

---

## FileSystem

All path arguments accept either a plain Lua `string` (UTF-8) or a `Wchar` object.
On Windows the W-API is used internally so non-ASCII filenames are handled correctly.
On Linux the POSIX UTF-8 API is used directly — no wide-char handling is needed.

### File and Directory Operations

```lua
Array   FileSystem.GetAll(path)
Array   FileSystem.GetFiles(path)
Array   FileSystem.GetDirectories(path)
FileInfo FileSystem.GetFileInfo(path)
file    FileSystem.Open(path, mode)
bool    FileSystem.Copy(source, destination, overwrite)
bool    FileSystem.Move(source, destination)
bool    FileSystem.Delete(source)
bool    FileSystem.CreateDirectory(path)
bool    FileSystem.RemoveDirectory(path)
bool    FileSystem.Rename(source, destination)
bool    FileSystem.SetAttributes(path, attributemask)
```

| Function | Description |
|----------|-------------|
| `GetAll` | Returns an array of `FileInfo` tables for every entry (files **and** directories) in `path` |
| `GetFiles` | Returns an array of filenames (strings/Wchar) for all regular files in `path` |
| `GetDirectories` | Returns an array of directory names for all subdirectories in `path` |
| `GetFileInfo` | Returns a `FileInfo` table for `path`, or `nil` if the path does not exist |
| `Open` | Open a file and return a Lua `io` file handle. `mode` follows standard C `fopen` conventions: `"rb"`, `"wb"`, `"r"`, `"w"`, etc. |
| `Copy` | Copy `source` to `destination`. Pass `true` for `overwrite` to allow replacing an existing file |
| `Move` | Move (rename across directories) `source` to `destination` |
| `Delete` | Delete a file or empty directory |
| `CreateDirectory` | Create a directory at `path`. Returns `true` on success |
| `RemoveDirectory` | Remove an **empty** directory at `path`. Returns `true` on success |
| `Rename` | Rename `source` to `destination` (same filesystem) |
| `SetAttributes` | *(Windows only)* Set Win32 file attribute flags |

### FileInfo table

Returned by `GetFileInfo` and `GetAll`:

| Field | Type | Description |
|-------|------|-------------|
| `FileName` | string or Wchar | Entry name (without path) |
| `isFolder` | boolean | `true` when the entry is a directory |
| `Size` | number | File size in bytes (`0` for directories) |
| `Creation` | number | Creation time as Unix timestamp |
| `Access` | number | Last access time as Unix timestamp |
| `Write` | number | Last write time as Unix timestamp |
| `Link` | string | *(optional)* Symlink target path, present only when the entry is a symbolic link |
| `AlternateFileName` | string or Wchar | *(Windows only)* 8.3 short name |
| `Attributes` | number | *(Windows only)* Win32 `FILE_ATTRIBUTE_*` bitmask |

### Path and Directory Utilities

```lua
string  FileSystem.CurrentDirectory()
bool    FileSystem.SetCurrentDirectory(path)
string  FileSystem.GetTempFileName()
Array   FileSystem.GetDrives(opt drive)
Wchar   FileSystem.GetSpecialFolder(csidl)   -- Windows only
```

| Function | Description |
|----------|-------------|
| `CurrentDirectory` | Returns the process current working directory as a plain UTF-8 string |
| `SetCurrentDirectory` | Changes the current working directory. Returns `true` on success |
| `GetTempFileName` | Creates a temporary file and returns its path as a string |
| `GetDrives` | Returns an array of drive tables (see below). Pass a single drive letter string to query one drive only |
| `GetSpecialFolder` | *(Windows only)* Returns a `Wchar` path for a CSIDL folder constant |

### Drive table (from `GetDrives`)

| Field | Type | Description |
|-------|------|-------------|
| `Drive` | string | Drive letter (`"C"`) on Windows, `"/"` on Linux |
| `Type` | number | *(Windows only)* `GetDriveType` value |
| `TotalNumberOfBytes` | number | Total capacity in bytes |
| `TotalNumberOfFreeBytes` | number | Free bytes |
| `FreeBytesAvailableToCaller` | number | Free bytes available to the current user |

### CSIDL Constants (Windows)

| Value | Folder |
|-------|--------|
| `0x0000` | Desktop |
| `0x0005` | My Documents |
| `0x000d` | My Music |
| `0x000e` | My Videos |
| `0x0010` | Desktop Directory |
| `0x001a` | AppData |

---
