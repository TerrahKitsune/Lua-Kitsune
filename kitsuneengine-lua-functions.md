# Lua Environment API Reference

A comprehensive reference for all available functions in the Lua environment.

---

## Table of Contents

- [Global Functions](#global-functions)
- [Hardware](#hardware)
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
- [WebSocket](#websocket)
- [Hashing (SHA256, MD5, SHA1)](#hashing)
- [MySQL](#mysql)
- [Postgres](#postgres)
- [Timer](#timer)
- [SQLite](#sqlite)
- [Json](#json)
- [Wchar](#wchar)
- [UInt](#uint)
- [TimeSpan](#timespan)
- [Identifier](#identifier)
- [DateTime](#datetime)
- [Decimal](#decimal)
- [MongoDB](#mongodb)
- [FileSystem](#filesystem)
- [Xml](#xml)
- [Yaml](#yaml)
- [Toml](#toml)
- [Ini](#ini)
- [AliveToken](#alivetoken)
- [Tasks](#tasks)
- [Llama](#llama)
- [ToolSuite](#toolsuite)
- [Third-Party Notices](#third-party-notices)
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
nil Sleep(opt ms)
nil Sleep(token)
nil Sleep(token, ms)
nil Yield()
nil Pause()
int Time()
ms Runtime()
```
- `Sleep`: Yield the current coroutine cooperatively without blocking any OS thread. Falls back to a blocking OS sleep when called outside a scheduler-managed coroutine.
  - `Sleep(ms)` — sleep for at least `ms` milliseconds (default `0`).
  - `Sleep(token)` — sleep until the `AliveToken` is disposed, expired, or a linked parent dies. Returns immediately if the token is already dead.
  - `Sleep(token, ms)` or `Sleep(ms, token)` — sleep until whichever comes first: token death or the millisecond deadline.
- `Yield`: Cooperatively yield the current coroutine back to the scheduler immediately (no sleep delay). For inline sync calls, this briefly releases Lua access so the scheduler and variable bridge can service their queues before the call is resumed.
- `Pause`: Suspend the current coroutine indefinitely until `task:Resume()` is called externally. The coroutine's status becomes `TaskStatus.Paused` and it will not be resumed by the scheduler on its own. A no-op when called outside a scheduler-managed async coroutine (e.g. from an inline call or a registered function callback).
- `Time`: Get current Unix epoch in milliseconds.
- `Runtime`: Get runtime in milliseconds.

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
| `VERSION` | Engine version string (e.g. `"1.0.0"`) |
| `CPUID` | CPU identifier string returned by the CPUID instruction |
| `DEBUG` | `true` in debug builds; not defined in release builds |

> **Script arguments:** When a Lua script is launched with extra arguments (via `KitsuneExecuteFileAsync`, `ExecuteString`, etc.), those arguments are available as `...` inside the chunk body. For file-based scripts the convention is `local path, arg1, arg2 = ...`. To obtain the current coroutine's integer id from inside the script, call `Tasks.GetCurrentId()`.

---

## Hardware

Read-only hardware sensor and system information module. On **Windows**, temperature and CPU load data come from PDH (`\Thermal Zone Information` and `\Processor`); battery uses `GetSystemPowerStatus`. On **Linux**, all sensors are read from `/sys/class/hwmon/`, `/proc/`, and `/sys/class/power_supply/`.

> **Note:** On Windows, temperature data comes from the PDH `\Thermal Zone Information` counter (no admin rights required). Fan RPM and voltage sensors are not available on Windows without vendor drivers.

```lua
table or nil  Hardware.CpuTemp()
table         Hardware.CpuThreadsLoad()
number or nil Hardware.CpuLoad()
table or nil  Hardware.Memory()
string or nil Hardware.CpuName()
table or nil  Hardware.Battery()
table or nil  Hardware.GpuMemory()   -- Windows only; nil on Linux
table or nil  Hardware.GpuLoad()     -- Windows only; nil on Linux
```

### Hardware.CpuTemp

```lua
table or nil Hardware.CpuTemp()
```

Returns an array of tables — one per thermal zone found — each with `Name` (string) and `Value` (°C, number). Returns `nil` when no valid thermal zones are found.

- **Windows:** Tries `\Thermal Zone Information(*)\High Precision Temperature` first, then `\Temperature`. Both counters report tenths of Kelvin; zones below 200 K (uninitialised) are filtered out.
- **Linux:** Reads from `/sys/class/hwmon/` chips named `coretemp`, `k10temp`, `zenpower`, or `cpu_thermal`.

```lua
local temps = Hardware.CpuTemp()
if temps then
    for _, t in ipairs(temps) do
        print(string.format("%s: %.1f°C", t.Name, t.Value))
    end
end
```

### Hardware.CpuThreadsLoad

```lua
table Hardware.CpuThreadsLoad()
```

Returns a flat `{[ThreadKey] = percent}` table with the load percentage (0–100) for every hardware thread.

- **Windows:** Keys are processor group/index strings (e.g. `"0,0"`, `"0,1"`). Uses a persistent PDH query — no sleep needed between calls.
- **Linux:** Keys are `"cpu0"`, `"cpu1"`, etc., computed from `/proc/stat` deltas between consecutive calls. The first call always returns 0 for all threads (no prior baseline).

```lua
local t = Hardware.CpuThreadsLoad()
local keys = {}
for k in pairs(t) do keys[#keys+1] = k end
table.sort(keys)
for _, k in ipairs(keys) do
    print(string.format("%-8s %.1f%%", k, t[k]))
end
```

### Hardware.CpuLoad

```lua
number or nil Hardware.CpuLoad()
```

Returns overall CPU utilisation as a percentage (0–100).

- **Windows:** Uses PDH (`\Processor(_Total)\% Processor Time`). The **first call** always returns `0` (baseline collection); subsequent calls return the delta since the previous call.
- **Linux:** Reads `/proc/stat` and computes the delta between consecutive calls. The first call returns `0`.

```lua
Hardware.CpuLoad()          -- prime the baseline
Sleep(1000)
local pct = Hardware.CpuLoad()
print(string.format("CPU: %.1f%%", pct))
```

### Hardware.Memory

```lua
table or nil Hardware.Memory()
```

Returns a table with system memory statistics (all values in **MB** except `LoadPercent`):

| Field | Type | Description |
|-------|------|-------------|
| `TotalPhys` | integer | Total physical RAM |
| `AvailPhys` | integer | Available physical RAM |
| `TotalSwap` | integer | Total page/swap file size |
| `AvailSwap` | integer | Available page/swap space |
| `LoadPercent` | integer | Memory load percentage (0–100) |

```lua
local m = Hardware.Memory()
if m then
    print(string.format("RAM: %d MB used / %d MB total (%d%%)",
        m.TotalPhys - m.AvailPhys, m.TotalPhys, m.LoadPercent))
end
```

### Hardware.CpuName

```lua
string or nil Hardware.CpuName()
```

Returns the CPU brand string (e.g. `"Intel(R) Core(TM) i7-9700K @ 3.60GHz"`).

- **Windows / Linux x86-64:** Uses the CPUID instruction leaf `0x80000002–4`.
- **Linux non-x86:** Reads the `model name` field from `/proc/cpuinfo`.

```lua
print("CPU:", Hardware.CpuName())
```

### Hardware.Battery

```lua
table or nil Hardware.Battery()
```

Returns a table with battery status, or `nil` if no battery is present (desktop machine).

| Field | Type | Description |
|-------|------|-------------|
| `Percent` | integer or nil | Charge level 0–100; `nil` if unknown |
| `ACLine` | boolean | `true` when plugged in (AC power or full) |
| `Charging` | boolean | `true` when actively charging |
| `SecondsRemaining` | integer or nil | Estimated seconds of battery life remaining; `nil` if unknown or plugged in |

- **Windows:** Uses `GetSystemPowerStatus`.
- **Linux:** Reads from `/sys/class/power_supply/` (first device with `type == "Battery"`).

```lua
local bat = Hardware.Battery()
if bat then
    local h = bat.SecondsRemaining and math.floor(bat.SecondsRemaining / 3600) or 0
    local m = bat.SecondsRemaining and math.floor((bat.SecondsRemaining % 3600) / 60) or 0
    print(string.format("Battery: %s%%  %s  (%dh %02dm)",
        tostring(bat.Percent),
        bat.Charging and "Charging" or (bat.ACLine and "Plugged in" or "Discharging"),
        h, m))
else
    print("No battery")
end
```

### Hardware.GpuMemory

```lua
table or nil Hardware.GpuMemory()
```

**Windows only** — returns `nil` on Linux.

Returns a table keyed by adapter friendly name. Each value is a table with memory usage in **MB**:

| Field | Type | Description |
|-------|------|-------------|
| `DedicatedUsageMB` | integer | Dedicated GPU VRAM currently in use |
| `SharedUsageMB` | integer | Shared system memory used by the GPU |
| `TotalCommittedMB` | integer | Total committed GPU memory (dedicated + shared) |

Uses `GPU Adapter Memory` PDH counters — the same source as Windows Task Manager's GPU memory bars. Each adapter is identified by its DXGI `AdapterLuid`, resolved to the friendly adapter description string.

```lua
local mem = Hardware.GpuMemory()
if mem then
    for adapter, m in pairs(mem) do
        print(string.format("%s: %d MB dedicated, %d MB shared",
            adapter, m.DedicatedUsageMB, m.SharedUsageMB))
    end
end
```

### Hardware.GpuLoad

```lua
table or nil Hardware.GpuLoad()
```

**Windows only** — returns `nil` on Linux.

Returns a table keyed by adapter friendly name. Each value is a table mapping engine type strings to utilisation percentages (0–100). Engine types include `"3d"`, `"copy"`, `"videoencode"`, `"videodecode"`, `"compute 0"`, etc. — exactly what the driver exposes.

Uses a **persistent PDH query** on `GPU Engine\Utilization Percentage`, aggregating all per-process per-engine instances into a single per-adapter total for each engine type. No sleep is needed between calls.

```lua
local load = Hardware.GpuLoad()
if load then
    for adapter, engines in pairs(load) do
        print(adapter)
        for etype, pct in pairs(engines) do
            if pct > 0 then
                print(string.format("  %-20s %.1f%%", etype, pct))
            end
        end
    end
end
```

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

#### Coroutine methods

```lua
co:SetAliveToken(token)  -- attach an AliveToken; when disposed the coroutine unsubscribes and dies cleanly (same as resuming with true). Pass nil to detach
```

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
| `New` | Return a CSV object with a bound delimiter (or auto-detect when omitted) || `New` | Return a CSV object with a bound delimiter (or auto-detect when omitted) |

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
co:AutoCommit(bool)      -- enable (true) or disable (false) automatic offset commit
co:SetAliveToken(token)  -- attach an AliveToken; when disposed the coroutine stops cleanly (same as resuming with true). Pass nil to detach
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
Stream Stream.New(opt string)
Stream Stream.New(backendfunction)
Stream Stream.Open(filename, mode)
```

- **No argument** — creates a new empty in-memory stream.
- **String argument** — creates an in-memory stream pre-loaded with the string contents, with the position reset to 0.
- **Function argument** — creates a stream backed by the provided Lua function. The function is called with an opcode as its first argument and must handle all `STREAM_OP_*` operations it wishes to support. It must return the capability bitmask when called with `STREAM_OP_OPEN` (0).
- **`Open(filename, mode)`** — opens a file as a stream.

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

    return Stream.New(function(op, arg)
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

In-memory streams created with `Stream.New()` have all three flags set (`Caps = 7`).

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

### Custom-type Reads

Custom userdata types can be written with `Stream:Write(value)` and read back with dedicated typed-read functions. All reads return `nil` on a short read or non-readable stream.

```lua
UInt       Stream:ReadUInt()        -- reads 8 bytes (uint64, native endian)
Decimal    Stream:ReadDecimal()     -- reads 24 bytes (LuaDecimal struct layout)
Identifier Stream:ReadIdentifier()  -- reads 16 bytes (UUID raw bytes)
DateTime   Stream:ReadDateTime()    -- reads 10 bytes (int64 ticks + int16 offset_minutes)
TimeSpan   Stream:ReadTimeSpan()    -- reads 8 bytes (int64 ticks)
```

**`Write` wire formats for custom types:**

| Type | Bytes written | Format |
|------|--------------|--------|
| `UInt` | 8 | `uint64_t`, native endian |
| `Decimal` | 24 | `LuaDecimal` struct (`uint64 lo`, `uint64 hi`, `int16 scale`, `uint8 negative`, 5 pad) |
| `Identifier` | 12 or 16 | raw bytes — 16 for UUID, 12 for OID |
| `DateTime` | 10 | `int64_t ticks` + `int16_t offset_minutes` |
| `TimeSpan` | 8 | `int64_t ticks` |

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
Aes Aes.New(key, opt iv, opt usectr)
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

`HttpClient` has no `Dispose` method — it holds no sockets or connections of its own. Each `Request`, `Call`, `Stream`, and `Connect` creates its own curl handle that is freed when the coroutine completes. The client itself is released by the garbage collector. To cancel in-flight requests early, attach an `AliveToken` via `client:SetAliveToken(token)` and call `token:Dispose()`.

### Creation and utilities

```lua
HttpClient HttpClient.New()
string     HttpClient.UrlEncode(str)
string     HttpClient.UrlDecode(str)
```

| Function | Description |
|----------|-------------|
| `New` | Create a new HTTP client |
| `UrlEncode` | Percent-encode a string; unreserved characters (`A–Z a–z 0–9 - _ . ~`) pass through unchanged |
| `UrlDecode` | Decode a percent-encoded string; `+` is decoded as a space |

### Client configuration

```lua
nil      client:SetTimeout(ms)
nil      client:SetFollowRedirects(bool)
nil      client:SetVerifySSL(bool)
nil      client:SetDefaultHeader(name, value)
nil      client:SetAliveToken(token)
TimeSpan client:GetTimestamp()
```

| Function | Description |
|----------|-------------|
| `SetTimeout` | Request timeout in milliseconds. `0` = no timeout (default) |
| `SetFollowRedirects` | Follow HTTP redirects. Default `true` |
| `SetVerifySSL` | Verify SSL certificates. Default `true` |
| `SetDefaultHeader` | Add a header sent with every request on this client |
| `SetAliveToken(token)` | Attach an `AliveToken` to this client. While the token is alive requests proceed normally. When disposed: `Request` returns `nil, "aborted"` immediately (no coroutine is created); `Call` returns `nil, "aborted"`. Pass `nil` to detach |
| `GetTimestamp` | Returns the round-trip duration of the most recently **completed** `Request()` call as a `TimeSpan`. The clock starts just before the request is submitted to curl and stops when the last response byte is received. Returns a zero `TimeSpan` if no request has completed yet on this client |

### Buffered request

```lua
coroutine, errmsg client:Request(method, url, opt body, opt headers, opt outStream)
```

Returns a coroutine immediately. Drive it with `coroutine.resume` until a non-nil result table is returned. `body` is an optional string. `headers` is an optional per-request header table. `outStream` is an optional writable `Stream`; when provided the response body is written there and `Contents` in the result is `nil`.

### Simple blocking call

```lua
result        = client:Call(method, url [, headers [, body]])
nil, errmsg   = client:Call(...)   -- on transport failure
```

Drives the request to completion internally, yielding the outer coroutine cooperatively on each poll. Returns the same result table as `Request` on success, or `nil, errmsg` on transport failure (e.g. `"Timeout"`, `"Could not resolve host"`, curl error text).

Argument order is optimised for the common case where headers are needed more often than a body:

| Arg | Type | Description |
|-----|------|-------------|
| `method` | string | HTTP verb: `"GET"`, `"POST"`, etc. |
| `url` | string | Target URL |
| `headers` | table (opt) | Per-request header table `{["X-Key"]="value"}` |
| `body` | string (opt) | Request body |

```lua
-- Simple GET — no coroutine boilerplate
local result = HttpClient.New():Call("GET", "https://httpbin.org/get")
print(result.Code, result.Contents)

-- POST with headers and body
local client = HttpClient.New()
client:SetTimeout(5000)
local result, err = client:Call("POST", "https://api.example.com/data",
    {["Content-Type"] = "application/json"},
    '{"key":"value"}')
if not result then
    print("failed:", err)   -- e.g. "Timeout", "Could not resolve host: ..."
else
    print(result.Code, result.Contents)
    print("round-trip:", client:GetTimestamp():TotalMilliseconds(), "ms")
end
```

> **Note:** `Call` must be used from inside a Kitsune-managed coroutine (the scheduler, or a coroutine driven by `coroutine.resume`). It yields cooperatively while waiting — it does not block the OS thread.

**Result table:**

| Field | Type | Description |
|-------|------|-------------|
| `Code` | integer or nil | HTTP status code; `nil` on transport error |
| `Status` | string | Status text (e.g. `"OK"`) or transport error message |
| `Contents` | string or nil | Response body |
| `Headers` | table | Response headers keyed by header name |

### Streaming request

```lua
coroutine, errmsg client:Stream(method, url, opt body, opt headers)
```

Returns a **coroutine** immediately. Drive it with `coroutine.resume` until it yields a `Stream` userdata — that is the response body stream. Call `stream:GetInfo()` for metadata, then `stream:Read()` in a loop to receive body chunks. Must be driven from inside a coroutine.

```lua
-- Inside a coroutine:
local co = client:Stream('GET', 'https://example.com/feed')
local ok, stream = coroutine.resume(co)
while ok and type(stream) ~= 'userdata' do
    ok, stream = coroutine.resume(co)
end
local chunk = stream:Read()
while chunk do io.write(chunk); chunk = stream:Read() end
stream:Close()
```

`stream:GetInfo()` returns:

| Field | Type | Description |
|-------|------|-------------|
| `Code` | integer | HTTP status code |
| `Status` | string | Status text |
| `Headers` | table | Response headers keyed by header name |
| `Url` | string | Effective URL after any redirects |

### WebSocket connection

```lua
coroutine, errmsg client:Connect(url, opt headers)
```

Returns a **coroutine** immediately. Drive it with `coroutine.resume` until it yields a `WebSocket` userdata — that is the live connection. Yields the calling coroutine cooperatively until the HTTP 101 upgrade completes. See the [WebSocket](#websocket) section for the full API on the returned object.

### Examples

```lua
-- Buffered GET
local client = HttpClient.New()
client:SetTimeout(8000)
local co = client:Request('GET', 'https://httpbin.org/get')
local ok, result
repeat ok, result = coroutine.resume(co) until result ~= nil
print(result.Code, result.Contents)

-- Streaming GET (must run inside a coroutine)
local co = client:Stream('GET', 'https://httpbin.org/get')
local ok, stream = coroutine.resume(co)
while ok and type(stream) ~= 'userdata' do ok, stream = coroutine.resume(co) end
local info = stream:GetInfo()
local chunk = stream:Read()
while chunk do io.write(chunk); chunk = stream:Read() end
stream:Close()

-- WebSocket echo (must run inside a coroutine)
local co = client:Connect('wss://echo.websocket.org')
local ok, ws = coroutine.resume(co)
while ok and type(ws) ~= 'userdata' do ok, ws = coroutine.resume(co) end
local welcome = ws:Poll()   -- drain optional server welcome frame
ws:Send('hello')
local msg = ws:Read()       -- yields until message arrives
if msg then print(msg:GetData()) end  -- "hello"

-- Binary frame
ws:Send('\xDE\xAD\xBE\xEF', true)  -- second arg = binary
ws:Dispose()
```

---

## HttpServer

An embedded HTTP/1.1 server backed by [libevent](https://libevent.org/). The server runs entirely inside the Lua coroutine that drives its `Accept()` loop — no background threads are created.

### Creation

```lua
HttpServer, errmsg  HttpServer.Listen(address)
```

Binds to `address` (e.g. `"0.0.0.0:8080"` or `"127.0.0.1:9000"`). Returns the server on success, or `nil, errmsg` on failure.

```lua
local server = assert(HttpServer.Listen("0.0.0.0:8080"))
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
nil  server:SetAliveToken(token)
nil  server:Close()
```

| Method | Description |
|--------|-------------|
| `SetOnDisconnect` | Register a `function(req)` called when a connection closes (after the response is sent or on error) |
| `SetAliveToken` | Attach an `AliveToken` to this server. When the token is disposed the `Accept()` coroutine tears down the server and dies cleanly — identical to `coroutine.resume(co, true)`. Pass `nil` to detach |
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
nil        resp:SetCode(code)
nil        resp:SetHeader(name, value)
bool       resp:Send(opt body)
bool       resp:Reject(code, message)
WebSocket  resp:UpgradeToWebSocket()
```

| Method | Description |
|--------|-------------|
| `SetCode(code)` | Override the HTTP status code. Default: `200` |
| `SetHeader(name, value)` | Add a response header. May be called multiple times |
| `Send(opt body)` | Send the response. `body` may be omitted (no body), a `string`, or a readable `Stream`. Returns `false` when the request is not yet finished |
| `Reject(code, message)` | Send a minimal error response with the given status code and plain-text body |
| `UpgradeToWebSocket()` | Upgrade the HTTP connection to a WebSocket session. Sends HTTP 101 immediately and returns a `WebSocket` userdata. The `HttpRequest` and `HttpResponse` objects must not be used after this call. See the [WebSocket](#websocket) section for the full API |

#### Stream responses

When `body` is a `Stream`:

- **Seekable stream** (`CAP_READ + CAP_SEEK`): `Content-Length` is determined from `stream:len()` and the body is sent with a known length.
- **Non-seekable stream** (`CAP_READ` only): `Transfer-Encoding: chunked` is used. The coroutine pump reads 64 KB chunks per iteration until the stream returns empty or `nil`.

```lua
-- Non-seekable → chunked
local function make_stream(data)
    local pos = 0
    return Stream.New(function(op, arg)
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
local s = Stream.New('hello world')
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
        local stream = Stream.New(function(op, arg)
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

#### WebSocket server

```lua
local server = assert(HttpServer.Listen("0.0.0.0:8080"))
local co = server:Accept()
local ws = nil
while coroutine.status(co) == 'suspended' do
    local ok, req = coroutine.resume(co)
    if req and req:IsFinished() and not ws then
        ws = req:GetResponse():UpgradeToWebSocket()
    end
    if ws then
        local msg = ws:Poll()
        if msg then
            ws:Send(msg:GetData())  -- echo
        end
    end
end
```

---

## WebSocket

A unified WebSocket connection handle used for both **client** connections (created via `client:Connect()`) and **server** connections (created via `resp:UpgradeToWebSocket()`). Messages are queued internally by the network layer and consumed through `Poll()` (non-blocking) or `Read()` (yielding).

### WebSocket methods

```lua
WebSocketMessage  ws:Poll()                   -- non-blocking: dequeue next message or nil
WebSocketMessage  ws:Read()                   -- yield until next message arrives or connection closes
bool              ws:Send(data, opt binary)    -- send a text (default) or binary frame
bool              ws:IsConnected()             -- true while the connection is open
integer           ws:GetId()                  -- stable non-zero integer identity
table             ws:GetContext()             -- per-connection Lua table, created lazily
nil               ws:SetMaxMessageSize(bytes) -- cap incoming message size (0 = uncapped)
nil               ws:Dispose()               -- close the connection and free resources
```

| Method | Description |
|--------|-------------|
| `Poll()` | Non-blocking. Advances the network layer and dequeues the next `WebSocketMessage` from the internal queue, or returns `nil` if none is ready. Never yields. |
| `Read()` | Yields the current coroutine until a `WebSocketMessage` is available, then returns it. Returns `nil` when the connection is closed. |
| `Send(data, opt binary)` | Send `data` (string) as a WebSocket frame. Pass `true` as the second argument to send a binary frame; default is a text frame. Returns `false` if the connection is closed. **Note:** server-side connections only support text frames — pass `binary = false` or omit it. |
| `IsConnected()` | Returns `true` while the underlying connection is open. |
| `GetId()` | Returns a stable non-zero integer that uniquely identifies this connection for its lifetime. |
| `GetContext()` | Returns a per-connection Lua table. Created lazily on first call; persists for the lifetime of the connection. Use it to store per-connection state. |
| `SetMaxMessageSize(bytes)` | Set the maximum allowed incoming message size in bytes. Messages exceeding the cap are dropped. `0` disables the cap (default). |
| `Dispose()` | Send a WS CLOSE frame (if still connected), close the underlying connection, and free all resources. Idempotent — safe to call more than once. Called automatically by `__gc`. |

### WebSocketMessage

Returned by `ws:Poll()` and `ws:Read()`.

```lua
string   msg:GetData()   -- message payload as a Lua string
integer  msg:GetType()   -- message type constant (see below)
```

**Message type constants:**

| Value | Meaning |
|-------|---------|
| `1` | Text frame |
| `2` | Binary frame |
| `8` | Close |
| `9` | Ping |
| `10` | Pong |

### Client WebSocket example

```lua
-- client:Connect() returns a coroutine; drive it until it yields the WebSocket
local client = HttpClient.New()
client:SetVerifySSL(false)
local co = client:Connect('wss://echo.websocket.org')
local ok, ws = coroutine.resume(co)
while ok and type(ws) ~= 'userdata' do ok, ws = coroutine.resume(co) end

-- optional: drain server welcome frame
local welcome = ws:Poll()

-- text echo
ws:Send('hello')
local msg = ws:Read()           -- yields until reply arrives
print(msg:GetData())            -- "hello"
print(msg:GetType())            -- 1 (text)

-- binary frame
ws:Send('\xDE\xAD', true)
local bin = ws:Read()
print(bin:GetType())            -- 2 (binary)

-- clean up
ws:Dispose()
```

### Server WebSocket example

```lua
-- UpgradeToWebSocket() is called on the HttpResponse once a request arrives.
-- After the upgrade the HttpRequest/HttpResponse must not be used.
-- The Accept() pump must keep running to drive libevent I/O.
local server = assert(HttpServer.Listen('0.0.0.0:8080'))
local co = server:Accept()
local ws = nil
while coroutine.status(co) == 'suspended' do
    local ok, req = coroutine.resume(co)
    if not ok then error(req) end
    -- Upgrade on the first finished request
    if req and req:IsFinished() and not ws then
        ws = req:GetResponse():UpgradeToWebSocket()
    end
    -- Service the WebSocket connection
    if ws then
        local msg = ws:Poll()
        if msg then
            if msg:GetType() == 8 then  -- close frame
                ws:Dispose()
                ws = nil
            else
                ws:Send(msg:GetData())  -- echo back as text
            end
        end
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
ok, n|errmsg  conn:NonQuery(sql, opt params)
ok, v|errmsg  conn:Scalar(sql, opt params)
ok, rows|errmsg conn:QueryAll(sql, opt params)
bool          conn:IsBusy()
string        conn:EscapeValue(value)
nil           conn:SetAliveToken(token)
nil           conn:Close()
```

| Function | Description |
|----------|-------------|
| `Connect` | Connect to MySQL, yielding the caller cooperatively during the TCP + auth handshake. Returns the connection on success, or `nil, errmsg` on failure. `port` defaults to `3306`, `timeout` defaults to `10` seconds |
| `Query` | Returns a **Lua coroutine** immediately without blocking. Drive it with `coroutine.resume` as described below. Returns `nil, errmsg` if the connection is already busy |
| `NonQuery` | Helper — drives a query to completion and returns `true, rowcount` (integer), or `false, errmsg` on error. Designed for INSERT / UPDATE / DELETE |
| `Scalar` | Helper — returns `true, col1value` (first column of the first row), or `true, nil` when no rows matched, or `false, errmsg` on error |
| `QueryAll` | Helper — collects every row into an array of integer-keyed row arrays and returns `true, rows`, or `false, errmsg` on error |
| `SetAliveToken` | Attach an `AliveToken` to this connection. If the token is disposed while a helper is polling, it stops early and returns `false, "cancelled"`. Pass `nil` to detach |
| `IsBusy` | Returns `true` while a query coroutine is still alive on this connection |
| `EscapeValue` | Escape a string with `mysql_real_escape_string`. Returns the escaped value **without** surrounding quotes |
| `Close` | Close the connection and free all resources. Safe to call multiple times |

### Helper methods (recommended API)

All three helpers yield the **outer** Kitsune coroutine cooperatively during the async wait, so other coroutines continue to run. Attach an `AliveToken` via `conn:SetAliveToken(token)` to cancel any in-progress helper early; it returns `false, "cancelled"` when the token is disposed.

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

-- Cancel mid-stream with an AliveToken
local token = AliveToken.New()
conn:SetAliveToken(token)
-- disposing the token from another coroutine will stop the helper early
local ok, rows = conn:QueryAll("SELECT id FROM big_table")
-- returns false, "cancelled" if token was disposed during the query
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
nil             conn:SetAliveToken(token)
nil             conn:Close()
```

| Function | Description |
|----------|-------------|
| `Connect` | Connect using a libpq connection string (e.g. `"host=localhost user=postgres password=secret dbname=mydb connect_timeout=5"`). Returns the connection on success, or `nil, errmsg` on failure |
| `Query` | Returns a **Lua coroutine** immediately without blocking. Drive it with `coroutine.resume` as described below. Returns `nil, errmsg` if the connection is already busy |
| `NonQuery` | Helper — drives a query to completion and returns `true, rowcount` (integer), or `false, errmsg` on error. Designed for INSERT / UPDATE / DELETE |
| `Scalar` | Helper — returns `true, col1value` (first column of the first row), or `true, nil` when no rows matched, or `false, errmsg` on error |
| `QueryAll` | Helper — collects every row into an array of integer-keyed row arrays and returns `true, rows`, or `false, errmsg` on error |
| `SetAliveToken` | Attach an `AliveToken` to this connection. If the token is disposed while a helper is polling, it stops early and returns `false, "cancelled"`. Pass `nil` to detach |
| `IsBusy` | Returns `true` while a query coroutine is still alive on this connection |
| `EscapeValue` | Escape a string using `PQescapeLiteral`. The result **includes** surrounding single quotes (e.g. `'O''Reilly'`) |
| `Close` | Close the connection and free all resources. Safe to call multiple times |

### Connection String

```
"host=127.0.0.1 port=5432 user=postgres password=secret dbname=mydb connect_timeout=5"
```

### Helper methods (recommended API)

All three helpers yield the **outer** Kitsune coroutine cooperatively during the async wait, so other coroutines continue to run. Attach an `AliveToken` via `conn:SetAliveToken(token)` to cancel any in-progress helper early; it returns `false, "cancelled"` when the token is disposed.

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
Timer  Timer.New()
bool   Timer:IsRunning()
nil    Timer:Reset()
nil    Timer:Start()
number Timer:Stop()
number Timer:Elapsed()
TimeSpan Timer:ElapsedTimeSpan()
```

| Function | Description |
|----------|-------------|
| `New` | Create a new timer (not started) |
| `IsRunning` | Returns `true` while the timer is running |
| `Reset` | Stop and zero all counters |
| `Start` | Start (or resume) the timer. If already started, the current interval is accumulated first |
| `Stop` | Stop the timer and return elapsed ms for the last interval |
| `Elapsed` | Total accumulated elapsed time in milliseconds as a `number`. Returns `0` if never started |
| `ElapsedTimeSpan` | Same duration as `Elapsed` but returned as a `TimeSpan` userdata. Returns a zero `TimeSpan` if never started |

---

## SQLite

```lua
SQLite      SQLite.Open(opt filename, opt mode)
bool, txt   SQLite:Query(sql, opt params)
nil         SQLite:Finish()
bool        SQLite:Fetch()
table|value SQLite:GetRow(opt index)
nil         SQLite:RegisterFunction(function, name, args)
nil         SQLite:RegisterAggregateFunction(function, name, args)
nil         SQLite:ToggleWidechar(bool)
nil         SQLite:SetBusyHandler(opt fn)
nil         SQLite:Close()
```

**Mode:** 0=single thread, 1=multithreaded, 2=serialized

| Function | Description |
|----------|-------------|
| `Open` | Open an SQLite database. Omit `filename` (or pass `nil`) for an in-memory database |
| `Query` | Prepare and execute `sql`. Returns `true, "ROW"` if the first row is ready, `true, "DONE"` when there are no rows (DML or empty SELECT), or `false, errmsg` on error. Call `Fetch` / `GetRow` to consume results |
| `Finish` | Finalize the current prepared statement early, allowing a new `Query` before all rows have been consumed |
| `Fetch` | Advance to the next result row. Returns `true` while a row is available, `false` when exhausted |
| `GetRow` | Without arguments (or `0`): returns the current row as a string-keyed table `{columnName = value, ...}` — **not** an integer-indexed array. With a positive 1-based integer index: returns that single column value directly. Returns `nil` if the index is out of range or there is no active row |
| `RegisterFunction` | Register a scalar Lua function callable from SQL. `args` is the number of expected arguments (-1 for variadic) |
| `RegisterAggregateFunction` | Register an aggregate Lua function. Called per row with `(false, …args)` and once at the end with `(true)` to collect the final result |
| `ToggleWidechar` | When `true`, text columns are returned as `Wchar` instead of plain Lua strings |
| `SetBusyHandler` | Register a callback invoked when a table is locked. Receives `(sqlite, retryCount)`; return truthy to retry, falsy to abort. Pass `nil` or no argument to remove |
| `Close` | Close the database connection |

### Prepared Statements (named parameters)

`Query` supports named parameters using the `:name` placeholder syntax. **Anonymous positional parameters (`?`) are not supported** and will cause an error.

Pass parameters as a **table** or a **function**:

```lua
-- Table: keys match parameter names (without the leading colon)
db:Query('INSERT INTO users VALUES (:id, :name)', {id = 1, name = 'Alice'})

-- Function: called once per parameter with the name (no leading colon),
-- returns the value to bind
db:Query('SELECT * FROM users WHERE id = :id', function(param)
    if param == 'id' then return 42 end
end)
```

Supported bind types: `nil` → NULL, integer → INTEGER, float → REAL, boolean → INTEGER (0/1), string → TEXT, `Wchar` → TEXT (UTF-16), `Stream` → NULL.

### Query Workflow

```lua
local db = SQLite.Open()          -- in-memory database

-- DDL / DML — consume with a single Fetch()
db:Query('CREATE TABLE t (id INTEGER, name TEXT)')
db:Fetch()

db:Query('INSERT INTO t VALUES (:id, :name)', {id = 1, name = 'Alice'})
db:Fetch()

-- SELECT — loop with Fetch(), read each row with GetRow()
db:Query('SELECT id, name FROM t ORDER BY id')
while db:Fetch() do
    local row = db:GetRow()           -- string-keyed table: {id=1, name='Alice'}
    print(row.id, row.name)
end

-- GetRow with index (positive 1-based integer) returns a single column value
db:Query('SELECT name FROM t WHERE id = :id', {id = 1})
db:Fetch()
local name = db:GetRow(1)            -- 'Alice'  (index 1 = first column)

-- Finish() discards remaining rows so the next Query can proceed
db:Query('SELECT id FROM t ORDER BY id')
db:Fetch()                            -- reads first row only
db:Finish()                           -- skip the rest

db:Close()
```

### Return Values from Query

| Second return | Meaning |
|---------------|---------|
| `"ROW"` | First row is ready; call `Fetch()` / `GetRow()` to read results |
| `"DONE"` | Statement completed with no (more) rows (typical for DDL/DML or empty SELECT) |
| `false, errmsg` | Preparation or execution error |

---

## Json

```lua
Json    Json.New(opt pretty)          -- primary constructor
value   Json.Null                     -- unique null sentinel (lightuserdata)
value   Json.EmptyObject              -- unique empty-object sentinel (lightuserdata)
string  json:Encode(value)
value   json:Decode(string | fn | stream)
bool    json:EncodeIntoStream(stream, value)
value   json:DecodeFromStream(stream)
nil     json:Dispose()
```

| Function | Description |
|----------|-------------|
| `New` | Create a new Json instance. Pass `true` for pretty-printed output (2 spaces per indent level) |
| `Json.Null` | The unique lightuserdata sentinel that encodes to/decodes from JSON `null`. Compare with `== Json.Null` |
| `Json.EmptyObject` | The unique lightuserdata sentinel that encodes to/decodes from JSON `{}`. Only produced during decode when `SetEncodeEmptyObject(true)` is active. Compare with `== Json.EmptyObject` |
| `Encode` | Encode a Lua value to a JSON string |
| `Decode` | Decode JSON from a string, a chunk-reader function, or a `Stream`. Returns the decoded value |
| `EncodeIntoStream` | Encode `value` and write the JSON bytes directly into `stream`. Returns `true` on success, or `false, errmsg` if the stream is not writable |
| `DecodeFromStream` | Decode one JSON value from `stream`. Returns the decoded value, or `nil, errmsg` if the stream is not readable |
| `SetDecodeNull(bool)` | Control how JSON `null` is decoded. Default `false` — decodes as Lua `nil` (falsy, coalescing works). Pass `true` to decode as the `Json.Null` sentinel instead (truthy, round-trip safe but lossy on re-encode if value was nil) |
| `SetEncodeEmptyObject(bool)` | Control how empty Lua tables are encoded and how `{}` is decoded. Default `false` — empty tables encode as `[]` and `{}` decodes as an empty Lua table. Pass `true` to encode empty tables as `{}` and decode `{}` as the `Json.EmptyObject` sentinel (round-trip safe) |
| `Dispose` | Explicitly free the internal output buffer; called automatically by the GC |

### Null Sentinel

By default JSON `null` decodes to Lua `nil` — falsy, so coalescing with `or` works naturally. Call `json:SetDecodeNull(true)` to decode `null` as the `Json.Null` sentinel instead, which is **truthy** and survives a round-trip through `Encode`. Without `SetDecodeNull(true)`, re-encoding a decoded object will omit any keys whose value was `null` (since `nil` in a Lua table means absent).

> **Internal modules always use the sentinel.** The Kitsune engine bridge and all built-in integrations (MySQL, Postgres, Redis/RedisJSON, MongoDB, etc.) decode JSON with `nullAsSentinel = true` internally. This means any JSON `null` that arrives through those modules is already the `Json.Null` sentinel — no `SetDecodeNull` call is required on your side. This behaviour is intentional: when JSON comes from a database or protocol layer, preserving the distinction between "field is null" and "field is absent" is almost always the right default.

```lua
local json = Json.New()

-- Default behaviour: null → nil (falsy, coalescing works)
local t = json:Decode('{"value":null}')
print(t.value or "default")     -- "default"  ✓

-- WARNING: round-trip is lossy by default — nil keys are omitted
print(json:Encode(t))           -- []  (empty table encodes as array)

-- SetDecodeNull(true): null → Json.Null (truthy, round-trip safe)
local json2 = Json.New():SetDecodeNull(true)
local t2 = json2:Decode('{"value":null}')
if t2.value == Json.Null then
    print("was null")           -- prints
end
print(t2.value or "default")   -- prints Json.Null userdata, NOT "default"

-- Re-encode preserves null
print(json2:Encode(t2))         -- {"value":null}

-- Values from internal modules already use the sentinel:
-- local row = mysql_result_row  →  row.nullable_col == Json.Null  (not nil)
```

### EmptyObject Sentinel

By default an empty Lua table (`{}`) encodes as a JSON array (`[]`), which is indistinguishable from an empty JSON object. Call `json:SetEncodeEmptyObject(true)` to opt into the `Json.EmptyObject` sentinel:

- **Encoding** — an empty Lua table encodes as `{}` instead of `[]`. The `Json.EmptyObject` lightuserdata also always encodes as `{}`.
- **Decoding** — a JSON `{}` (empty object) is decoded as the `Json.EmptyObject` sentinel instead of an empty Lua table, making round-trips lossless.

`Json.EmptyObject` is a distinct lightuserdata address from `Json.Null`. Both can coexist in the same instance.

```lua
local json = Json.New()
json:SetEncodeEmptyObject(true)

-- Empty table now encodes as {}
print(json:Encode({}))                  -- {}

-- Non-empty tables are unaffected
print(json:Encode({1, 2, 3}))           -- [1,2,3]
print(json:Encode({x = 1}))             -- {"x":1}

-- Decode {} → Json.EmptyObject sentinel
local v = json:Decode('{}')
print(v == Json.EmptyObject)            -- true
print(v == Json.Null)                   -- false

-- Sentinel in a table round-trips as {}
print(json:Encode({meta = Json.EmptyObject}))  -- {"meta":{}}

-- Chaining
local j2 = Json.New():SetEncodeEmptyObject(true)
print(j2:Encode({}))                    -- {}

-- Restore default ([] for empty tables)
json:SetEncodeEmptyObject(false)
print(json:Encode({}))                  -- []
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
local s = Stream.New()
json:EncodeIntoStream(s, {hello = "world"})
s:Seek(0)
print(s:Read())

-- Stream decode
local s2 = Stream.New('{"key":"val"}')
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

## UInt

A typed userdata for unsigned 64-bit integers. Covers values above `2^63 - 1` that cannot be represented losslessly as a Lua integer or `number`. All arithmetic operators are overloaded so `UInt` values work with `+`, `-`, `*`, `/`, `%`, `&`, `|`, `~`, `^`, `<<`, `>>`, and unary `~` directly. Comparisons (`==`, `<`, `<=`) are also overloaded.

### Constructors

```lua
UInt  UInt.FromString(str)      -- parse decimal string; nil on failure
UInt  UInt.FromNumber(n)        -- convert Lua number (truncates to uint64)
UInt  UInt.FromUnsigned(n)      -- reinterpret raw bit pattern of a Lua integer as uint64
UInt  UInt.Zero()               -- returns 0
```

### Methods

```lua
string  u:ToString()     -- decimal string representation, alias: AsString()
string  u:AsString()
number  u:ToNumber()     -- convert to Lua number (lossy above 2^53)
int     u:ToInteger()    -- reinterpret as signed int64 (bit pattern preserved)
uint    u:ToUnsigned()   -- same value as a Lua integer (wraps for values > INT64_MAX)
bool    u:IsZero()       -- true when value is 0
```

### Arithmetic & Bitwise Metamethods

| Metamethod | Behaviour |
|------------|-----------|
| `tostring(u)` | Same as `ToString()` |
| `u1 == u2` | Value equality |
| `u1 < u2` | Less-than comparison |
| `u1 <= u2` | Less-or-equal comparison |
| `u1 + u2` | Addition (wraps on overflow) |
| `u1 - u2` | Subtraction (wraps on underflow) |
| `u1 * u2` | Multiplication (wraps) |
| `u1 / u2` | Integer division |
| `u1 % u2` | Modulo |
| `u1 & u2` | Bitwise AND |
| `u1 \| u2` | Bitwise OR |
| `u1 ~ u2` | Bitwise XOR |
| `~u` | Bitwise NOT |
| `u1 << n` | Left shift |
| `u1 >> n` | Right shift |

### Examples

```lua
local max = UInt.FromString('18446744073709551615')
print(max)                        -- "18446744073709551615"
print(max + UInt.FromString('1')) -- "0"  (wraps)

local u = UInt.FromNumber(255)
print(u & UInt.FromNumber(0xF0))  -- "240"
print(u:ToNumber())               -- 255.0
```

---

## TimeSpan

A typed userdata representing a signed duration in 100-nanosecond ticks (identical to the .NET `TimeSpan` representation). All comparison operators are overloaded; arithmetic (`+`, `-`, `*`, `/`) and unary negation are also supported.

### Constructors

```lua
TimeSpan  TimeSpan.FromDays(n)
TimeSpan  TimeSpan.FromHours(n)
TimeSpan  TimeSpan.FromMinutes(n)
TimeSpan  TimeSpan.FromSeconds(n)
TimeSpan  TimeSpan.FromMilliseconds(n)
TimeSpan  TimeSpan.FromTicks(n)       -- raw 100-ns tick count
TimeSpan  TimeSpan.Zero()
```

All constructors accept fractional `number` arguments.

### Component Getters

```lua
int     ts:Days()
int     ts:Hours()
int     ts:Minutes()
int     ts:Seconds()
int     ts:Milliseconds()
int     ts:Ticks()           -- raw 100-ns signed tick count
bool    ts:IsNegative()      -- true when duration < 0
bool    ts:IsEmpty()         -- true when ticks == 0
```

### Conversion

```lua
number  ts:TotalDays()
number  ts:TotalHours()
number  ts:TotalMinutes()
number  ts:TotalSeconds()
number  ts:TotalMilliseconds()
```

### Metamethods

| Metamethod | Behaviour |
|------------|-----------|
| `tostring(ts)` | Canonical string, e.g. `"01:30:00.000"` or `"-00:00:30.000"` |
| `ts1 == ts2` | Tick equality |
| `ts1 < ts2` | Less-than comparison |
| `ts1 <= ts2` | Less-or-equal comparison |
| `ts1 + ts2` | Duration addition |
| `ts1 - ts2` | Duration subtraction |
| `ts * n` | Scale by a number |
| `ts / n` | Divide by a number |
| `-ts` | Negate the duration |

### Examples

```lua
local hour = TimeSpan.FromHours(1)
local min  = TimeSpan.FromMinutes(90)
print(hour + min)               -- "02:30:00.000"
print(-TimeSpan.FromSeconds(5)) -- "-00:00:05.000"
print(min:TotalHours())         -- 1.5
print(min:Hours(), min:Minutes()) -- 1  30

-- Measure elapsed time with a Timer
local t = Timer.New()
t:Start()
Sleep(100)
local elapsed = t:ElapsedTimeSpan()
print(elapsed:TotalMilliseconds())  -- ~100
```

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
bool          mongo:IsFinished()       -- true when no operation is running
nil           mongo:Wait()             -- yield until current operation completes
nil           mongo:Cancel()           -- request cancellation; yields until done
nil           mongo:SetAliveToken(token) -- attach an AliveToken; cancels automatically when disposed. Pass nil to detach
result, errmsg mongo:GetResult()       -- yield if needed, then return the result
nil           mongo:Close()            -- close connection and free resources
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

-- Cancel a slow find explicitly
mongo:Find("mydb", "big_collection", {})
mongo:Cancel()   -- yields until cancelled

-- Or use an AliveToken for automatic cancellation
local token = AliveToken.New()
mongo:SetAliveToken(token)
mongo:Find("mydb", "big_collection", {})
token:Dispose()          -- cancels mid-wait; GetResult/Wait redirects into cancel path
mongo:GetResult()        -- coroutine dies cleanly once worker acknowledges

mongo:Close()
```

---

## Xml

An XML serialization module backed by [pugixml](https://pugixml.org/). Supports encoding Lua tables to XML strings and decoding XML strings back to Lua tables.

```lua
Xml    Xml.New(opt indent)    -- primary constructor
string xml:Encode(table)      -- encode a Lua node table to an XML string
table  xml:Decode(string)     -- decode an XML string to a Lua node table
nil    xml:Dispose()          -- explicitly free the instance (also called by GC)
```

| Function | Description |
|----------|-------------|
| `New` | Create a new Xml instance. Pass `true` for indented output (one tab per level); default is compact (no indentation) |
| `Encode` | Encode a Lua node table to an XML string. Always prepends an `<?xml version="1.0" encoding="UTF-8"?>` declaration |
| `Decode` | Parse an XML string and return the root element as a Lua node table. Returns `nil, errmsg` on parse failure |
| `Dispose` | Explicitly release the instance; called automatically by the GC |

### Node Table Structure

Every XML element is represented as a Lua table with four fields:

| Field | Type | Description |
|-------|------|-------------|
| `tag` | string | The element name (e.g. `"person"`) |
| `attr` | array | Sequential array of `{key, value}` tables — one per attribute, in document order |
| `text` | string | Concatenated text content of the element (PCDATA and CDATA nodes), or `""` when none |
| `children` | array | Sequential array of child element node tables, in document order |

### Examples

```lua
local xml = Xml.New()

-- Decode
local doc = xml:Decode([[
<person id="1" active="true">
    <name>Alice</name>
    <score>42</score>
</person>
]])
print(doc.tag)              -- "person"
print(doc.attr[1].key)      -- "id"
print(doc.attr[1].value)    -- "1"
print(doc.children[1].tag)  -- "name"
print(doc.children[1].text) -- "Alice"

-- Encode
local node = {
    tag  = "person",
    attr = { {key="id", value="1"} },
    text = "",
    children = {
        { tag="name", attr={}, text="Alice", children={} },
        { tag="score", attr={}, text="42", children={} },
    },
}
local s = xml:Encode(node)
print(s)
-- <?xml version="1.0" encoding="UTF-8"?><person id="1"><name>Alice</name><score>42</score></person>

-- Indented output
local pretty = Xml.New(true)
print(pretty:Encode(node))

-- Error handling
local doc, err = xml:Decode("not < valid > xml <<<")
if not doc then print("Parse error:", err) end
```

### Notes

- `Encode` expects every node table to have a non-empty `tag` field; an error is raised otherwise.
- Attributes are written in the order they appear in the `attr` array.
- Mixed content (elements that have both `text` and `children`) is supported: `text` is appended as a PCDATA node before the child elements.
- `Decode` returns only the first root element; XML comments, processing instructions, and the XML declaration are ignored in the output table.
- Input must be UTF-8 encoded. The encoder always writes UTF-8.

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

## Yaml

A YAML serialization module backed by [libyaml](https://github.com/yaml/libyaml). Supports encoding Lua tables to YAML strings and decoding YAML strings back to Lua values. Implements YAML 1.1.

```lua
Yaml    Yaml.New(opt pretty)    -- primary constructor
string  yaml:Encode(value)      -- encode a Lua value to a YAML string
value   yaml:Decode(string)     -- decode a YAML string to a Lua value
nil     yaml:Dispose()          -- explicitly free the instance (also called by GC)
```

| Function | Description |
|----------|-------------|
| `New` | Create a new Yaml instance. Pass `true` for block/pretty style (one entry per line); default is flow style (compact, inline) |
| `Encode` | Encode a Lua value to a YAML string |
| `Decode` | Parse a YAML string and return the decoded Lua value |
| `Dispose` | Explicitly release the instance; called automatically by the GC |

### Scalar Type Coercion (Decode)

Plain (unquoted) scalars are coerced to Lua types using YAML 1.1 rules:

| YAML scalar | Lua type |
|-------------|----------|
| `null`, `~`, `Null`, `NULL`, empty | `nil` |
| `true`, `True`, `TRUE`, `yes`, `Yes`, `YES`, `on`, `On`, `ON` | `boolean` `true` |
| `false`, `False`, `FALSE`, `no`, `No`, `NO`, `off`, `Off`, `OFF` | `boolean` `false` |
| Valid integer literal (`0`, `-1`, `0xFF`, `0777`) | `integer` |
| Valid float literal (`3.14`, `1e10`) | `number` |
| Anything else | `string` |

Quoted scalars (`"..."` or `'...'`) are always decoded as strings regardless of content.

### Type Mapping (Encode)

| Lua type | YAML output |
|----------|-------------|
| `nil` | `null` |
| `boolean` | `true` / `false` |
| integer | plain scalar (e.g. `42`) |
| float | plain scalar (e.g. `3.14`) |
| `string` | double-quoted scalar |
| `Wchar` | double-quoted scalar (UTF-8 encoded) |
| `Identifier` | double-quoted scalar (canonical string) |
| `DateTime` | double-quoted scalar (ISO 8601) |
| `Decimal` | double-quoted scalar (decimal string) |
| `table` (sequential integer keys) | sequence (`[]` / block `- ` style) |
| `table` (other keys) | mapping (`{}` / block `key: value` style) |
| functions, threads, unsupported userdata | `null` |

### Notes

- **Circular references** raise an error: `Yaml: recursion detected`
- **Table classification**: pure sequential integer-keyed tables (`{1, 2, 3}`) encode as YAML sequences; all others encode as mappings
- **Style**: `Yaml.New()` (flow) produces compact single-line output; `Yaml.New(true)` (block) produces human-readable multi-line output. Both styles decode correctly by the other instance
- **Anchors and aliases** in input YAML are resolved transparently by libyaml before the binding layer ever sees them
- **Multi-document YAML** — only the first document is decoded

### Examples

```lua
local yaml = Yaml.New()

-- Basic encode/decode
local s = yaml:Encode({name = 'Alice', scores = {10, 20, 30}})
local t = yaml:Decode(s)
print(t.name, t.scores[1])   -- Alice  10

-- Decode a hand-written YAML string
local cfg = yaml:Decode([[
host: localhost
port: 5432
debug: true
]])
print(cfg.host, cfg.port, cfg.debug)  -- localhost  5432  true

-- Block/pretty style
local pretty = Yaml.New(true)
print(pretty:Encode({a = 1, b = {2, 3}}))
-- a: 1
-- b:
-- - 2
-- - 3

-- Round-trip all basic types
local orig = {s='hello', n=42, f=3.14, bt=true, bf=false, arr={1,2,3}}
local t2 = yaml:Decode(yaml:Encode(orig))
print(t2.s, t2.n, t2.bt)   -- hello  42  true

-- Instance reuse
local s1 = yaml:Encode({a=1})
local s2 = yaml:Encode({b=2})
print(yaml:Decode(s1).a)   -- 1
print(yaml:Decode(s2).b)   -- 2
```

---

## Toml

A TOML serialization module. Decoding is backed by [tomlc99](https://github.com/cktan/tomlc99); encoding is a hand-written C implementation. Supports the full TOML v1.0 specification for decoding and all common types for encoding.

```lua
Toml    Toml.New(opt pretty)    -- primary constructor
string  toml:Encode(table)      -- encode a Lua table to a TOML string
table   toml:Decode(string)     -- decode a TOML string to a Lua table
nil     toml:Dispose()          -- explicitly free the instance (also called by GC)
```

| Function | Description |
|----------|-------------|
| `New` | Create a new Toml instance. Pass `true` for indented output (2 spaces per level); default is compact |
| `Encode` | Encode a Lua table to a TOML string. The top-level value **must** be a table (TOML always has a root mapping) |
| `Decode` | Parse a TOML string and return a Lua table. Returns `nil, errmsg` on parse failure |
| `Dispose` | Explicitly release the instance; called automatically by the GC |

### Type Mapping

#### Decode (TOML → Lua)

| TOML type | Lua type |
|-----------|----------|
| String | `string` |
| Integer | `integer` |
| Float (`inf`, `-inf`, `nan` included) | `number` |
| Boolean | `boolean` |
| Array | table (sequential integer keys, 1-based) |
| Table / inline table | table (string keys) |
| Array of tables (`[[section]]`) | table (sequential integer keys, each element a table) |
| Datetime / Date / Time | `string` (ISO 8601 format, e.g. `"2024-06-01T12:00:00Z"`) |

#### Encode (Lua → TOML)

| Lua type | TOML output |
|----------|-------------|
| `boolean` | `true` / `false` |
| `integer` | integer scalar |
| `float` | float scalar (always includes `.` or `e` so TOML recognises it as float) |
| `string` | basic string (double-quoted, with escapes) |
| `Wchar` | basic string (UTF-8 encoded) |
| `Identifier` | basic string (canonical UUID or OID hex) |
| `DateTime` | bare datetime scalar (no quotes — native TOML datetime type) |
| `Decimal` | basic string |
| `table` (sequential integer keys) used as value | inline array `[...]` |
| `table` (string keys) at root or as sub-key | `[section]` header block |
| `table` used as array-of-tables element | `[[section]]` header block |
| `nil`, functions, unsupported types | empty string `""` |

### Key Quoting

Keys that consist only of `A–Z a–z 0–9 - _` are written as bare keys. All other keys are written as double-quoted basic strings.

### Notes

- **Circular references** raise an error: `Toml: recursion detected`
- **Top-level value must be a table** — `Encode` raises an error if passed a non-table value, because TOML documents always have a root mapping
- **Sub-tables** are emitted as `[dotted.path]` section headers after all scalar keys at the current level
- **Arrays of tables** are emitted as `[[dotted.path]]` blocks, one per element
- **Datetime** values are emitted without quotes as native TOML datetimes; decoded datetimes come back as ISO 8601 strings
- **Parse errors** are returned as `nil, errmsg` rather than raised as Lua errors

### Examples

```lua
local toml = Toml.New()

-- Basic encode/decode
local s = toml:Encode({host = 'localhost', port = 5432, debug = true})
local t = toml:Decode(s)
print(t.host, t.port, t.debug)  -- localhost  5432  true

-- Decode a hand-written TOML string
local cfg = toml:Decode([[
title = "My App"

[database]
host = "localhost"
port   = 5432

[server]
debug = true
tags  = ["web", "api"]
]])
print(cfg.title)             -- My App
print(cfg.database.host)     -- localhost
print(cfg.server.tags[1])    -- web

-- Nested tables encode as section headers
local s2 = toml:Encode({
    app = { name = 'kitsune', version = '1.0' },
    log = { level = 'info' },
})
print(s2)
-- [app]
-- name = "kitsune"
-- version = "1.0"
-- [log]
-- level = "info"

-- Indented output
local pretty = Toml.New(true)
print(pretty:Encode({x = 1, y = 2}))

-- Error handling
local v, err = toml:Decode('this is !!! not toml')
if not v then print('Parse error:', err) end

-- Instance reuse
local s1 = toml:Encode({a=1})
local s3 = toml:Encode({b=2})
print(toml:Decode(s1).a)   -- 1
print(toml:Decode(s3).b)   -- 2
```

---

## AliveToken

A lightweight cancellation-token userdata. One token can be shared across multiple coroutines and tasks; calling `Dispose` on any reference immediately makes `IsAlive()` return `false` everywhere that holds the same token. Tokens become disposed automatically when garbage-collected. An optional timeout makes the token expire automatically after a fixed number of milliseconds.

```lua
AliveToken  AliveToken.New(opt timeoutMs)          -- create a live token; optional timeout in ms
bool        token:IsAlive()                        -- true while not disposed and not timed out
nil         token:Dispose()                        -- cancel / dispose the token immediately
nil         token:ErrorIfDead(opt msg)             -- luaL_error if disposed or timed out
nil         token:Link(parent1, parent2, ...)      -- die when any linked parent dies
```

| Function | Description |
|----------|-------------|
| `New(opt timeoutMs)` | Create a live token. Pass a positive integer to set an automatic timeout in milliseconds; the token expires after that duration when `IsAlive`, `ErrorIfDead`, or any internal poll point checks it. Pass nothing (or `0`) for a token that only expires via `Dispose` |
| `IsAlive` | Returns `true` while the token is alive. Checks the timeout and all linked parents on every call |
| `Dispose` | Marks the token as disposed immediately, regardless of timeout. Idempotent |
| `ErrorIfDead` | Raises a Lua error if the token is disposed or timed out |
| `Link` | Attach one or more parent tokens. The child token becomes dead whenever any linked parent is disposed, timed out, or itself has a dead parent. Can be called multiple times to add more parents incrementally. Propagates through chains (grandparent → parent → child) |

### Timeout

When `timeoutMs` is given, liveness is checked lazily on every call to `IsAlive`, `ErrorIfDead`, or any internal C++ poll point (`HelperWaitCont`, `consume_cont`, `accept_body`, `pubsub_cont`, etc.). The token's `alive` flag is set to `0` the first time the deadline is found to have passed — there is no background timer or thread.

```lua
-- Expires automatically after 5 seconds
local token = AliveToken.New(5000)

-- Without timeout — only Dispose() stops it
local token = AliveToken.New()
```

`tostring` on a timed token includes the remaining milliseconds while alive:

```
AliveToken(alive, 4823 ms remaining)
AliveToken(disposed)
```

### Example — cooperative cancellation across coroutines

```lua
local token = AliveToken.New()

-- Worker coroutine: polls until cancelled
local worker = coroutine.create(function()
    while token:IsAlive() do
        SomeWork()
        Sleep(100)
    end
end)

-- Somewhere else: cancel and the worker loop exits on its next iteration
token:Dispose()

-- Guard pattern: raise an error if the token was cancelled
token:ErrorIfDead()                          -- default message
token:ErrorIfDead("operation was aborted")   -- custom message

-- Deadline pattern: auto-cancel after 30 seconds
local deadline = AliveToken.New(30000)
conn:SetAliveToken(deadline)
local ok, rows = conn:QueryAll("SELECT * FROM big_table")
-- returns false, "cancelled" if query takes more than 30 s

-- Linked tokens: child dies when any parent dies
local appToken   = AliveToken.New()   -- global shutdown token
local reqToken   = AliveToken.New()   -- per-request timeout token
local childToken = AliveToken.New()
childToken:Link(appToken, reqToken)   -- dies if either parent dies
conn:SetAliveToken(childToken)

-- Chain: grandparent -> parent -> child
local gp = AliveToken.New()
local parent = AliveToken.New()
local child  = AliveToken.New()
parent:Link(gp)
child:Link(parent)
gp:Dispose()              -- child is now dead too
```

### Notes

- `Dispose` is idempotent — calling it multiple times is safe.
- `__gc` calls `Dispose` automatically, so tokens created inside a scope that exits will cancel themselves when collected.
- `Link` stores references to parent tokens in the registry — parents are kept alive for at least as long as the child.
- Linked parents are checked lazily on every `IsAlive` / `ErrorIfDead` / poll call; there is no background thread.
- All modules that accept `SetAliveToken` (`HttpClient`, `HttpServer`, `MySQL`, `Postgres`, `MongoDB`, `Redis Subscribe`, `Kafka consumer`) check the token through the same `alivetoken_tick` function — linking, timeout, and dispose all work transparently.

---

## Ini

A pure C INI file encoder and decoder with no third-party dependencies. Supports the common INI conventions used by Windows applications, game configs, and legacy tools — sections, key/value pairs, comments, quoted values, and inline comments.

```lua
Ini    Ini.New()           -- primary constructor
string ini:Encode(table)   -- encode a two-level Lua table to an INI string
table  ini:Decode(string)  -- decode an INI string to a two-level Lua table
nil    ini:Dispose()       -- explicitly free the instance (also called by GC)
```

| Function | Description |
|----------|-------------|
| `New` | Create a new Ini instance |
| `Encode` | Encode a two-level Lua table to an INI string. Top-level keys are section names; their values must be tables of string key/value pairs |
| `Decode` | Parse an INI string and return a two-level Lua table |
| `Dispose` | Explicitly release the instance; called automatically by the GC |

### Table Structure

Both `Encode` and `Decode` use a consistent two-level structure:

```lua
{
    __global = { key = "value", ... },  -- keys before any section header
    sectionName = { key = "value", ... },
    ...
}
```

The `"__global"` pseudo-section holds any key/value pairs that appear before the first `[section]` header in the file. When encoding, bare scalar values at the top level of the table are also treated as global keys.

### Decode Behaviour

| Feature | Behaviour |
|---------|-----------|
| Comment lines | Lines starting with `;` or `#` are ignored |
| Inline comments | Text after `;` or `#` (outside quotes) is stripped |
| Quoted values | Double-quoted values (`"hello world"`) have their quotes stripped |
| Separator | Both `=` and `:` are accepted as key/value separators |
| Whitespace | Leading/trailing whitespace around keys and values is trimmed |
| Empty lines | Ignored |
| All values | Always returned as `string` — no type coercion |

### Encode Behaviour

| Lua type | INI output |
|----------|------------|
| `string` | written as-is |
| `integer` | stringified (e.g. `42`) |
| `float` | stringified (e.g. `3.14`) |
| `boolean` | `true` or `false` |
| `UInt` | decimal string (e.g. `18446744073709551615`) |
| `Identifier` | canonical string (UUID or OID hex) |
| `DateTime` | ISO 8601 string (e.g. `2024-06-01T12:00:00.000Z`) |
| `Decimal` | decimal string (e.g. `123.456`) |
| `TimeSpan` | canonical string (e.g. `01:30:00.000`) |
| `table` (nested) | not supported as a value — skipped silently |
| other types | skipped silently |

Section headers are emitted as `[sectionName]` followed by `key = value` lines. A blank line is appended after each section.

### Examples

```lua
local ini = Ini.New()

-- Decode a hand-written INI string
local t = ini:Decode([[
; Application config
[database]
host = localhost
port = 5432
debug = false

[server]
name = myapp
mode = production
]])
print(t.database.host)   -- localhost
print(t.database.port)   -- 5432  (always a string)
print(t.server.name)     -- myapp

-- Encode a Lua table
local s = ini:Encode({
    database = { host = 'localhost', port = 5432 },
    server   = { name = 'myapp', debug = false },
})
print(s)
-- [database]
-- host = localhost
-- port = 5432
-- ...

-- Global keys (before any section)
local t2 = ini:Decode('version=1\nname=app\n[db]\nhost=localhost\n')
print(t2.__global.version)  -- 1
print(t2.__global.name)     -- app
print(t2.db.host)           -- localhost

-- Encode global keys via __global pseudo-section
local s2 = ini:Encode({
    __global = { version = '1', name = 'app' },
    db       = { host = 'localhost' },
})

-- Inline comments and quoted values
local t3 = ini:Decode('[s]\npath="C:/my files" ; root dir\n')
print(t3.s.path)  -- C:/my files
```

### Notes

- **All decoded values are strings** — INI has no type system. Compare with `== '5432'` not `== 5432`
- **No nesting** — INI supports exactly two levels: section → key → value. Sub-tables inside a section are skipped during encode
- **No standard** — the parser is lenient and accepts the most common conventions. It does not enforce any particular INI dialect
- **Instance reuse** — the same instance can be used for multiple `Encode`/`Decode` calls

---

## Tasks

A native task module for spawning and tracking Lua coroutines. `Tasks.New` starts a coroutine immediately and returns a lightweight handle (`LuaTask` userdata). The handle holds only the coroutine's integer `id`; all state lives in the scheduler's slot.

Multiple handles may refer to the same slot (via `Tasks.Open`). The slot is kept alive by a reference count (`luaRefCount`). When the last Lua handle is GC'd or disposed, `fireAndForget` is set on the slot so the scheduler auto-compacts it when the coroutine finishes. The coroutine itself is **not cancelled** — it continues running to completion. Slots created via the public C API (`KitsuneExecuteStringAsync`, etc.) are flagged `apiOwned`; handle GC never touches their lifecycle.

### Error handling for fire-and-forget tasks

When a coroutine runs fire-and-forget (no live handle watching it), any error at completion is routed to the global task error handler if one is set, or printed to `stderr` otherwise.

```lua
Tasks.SetErrorHandler(function(id, err)
    print("Task " .. id .. " error: " .. err)
end)

-- Pass nil to clear the handler (stderr fallback is restored)
Tasks.SetErrorHandler(nil)
```

### Status Constants

```lua
TaskStatus.None      = 0   -- slot has been freed / compacted (id no longer valid)
TaskStatus.Idle      = 1   -- runnable, waiting for a scheduler tick
TaskStatus.Sleeping  = 2   -- waiting out a Sleep() deadline or AliveToken
TaskStatus.Running   = 3   -- currently executing inside lua_resume
TaskStatus.Done      = 4   -- finished successfully; result available via GetResult
TaskStatus.Faulted   = 5   -- finished with a runtime or Lua error; call GetError
TaskStatus.Cancelled = 6   -- interrupted by Cancel() or engine shutdown
TaskStatus.Inline    = 7   -- running as an inline sync call (RunString / RunFunction etc.)
TaskStatus.Paused    = 8   -- suspended inside the coroutine via Pause(); waiting for Resume()
TaskStatus.Waiting   = 9   -- suspended inside the coroutine via task:Wait(); can be force-woken via Resume()
```

### Construction

```lua
Task   Tasks.New(fn, arg1, arg2, ...)
Task   Tasks.Open(id)
int    Tasks.GetCurrentId()           -- id of the currently executing coroutine, or nil
```

- **`Tasks.New`** — starts `fn` immediately as an async coroutine with any extra arguments passed as function parameters on the first resume. Returns a `Task` handle with `luaRefCount = 1`. The coroutine is already queued and running — there is no separate `Start()` call.
- **`Tasks.Open`** — opens an existing coroutine by integer `id` (from `task:GetId()` or `Tasks.GetCurrentId()`). Returns `nil` if the slot does not exist or has already been released. Increments `luaRefCount` and clears `fireAndForget` so the slot is not auto-compacted while the handle is alive.
- **`Tasks.GetCurrentId`** — returns the integer id of the coroutine that is currently executing on the scheduler, or `nil` when called outside a scheduler-managed context. Replaces the removed `ID` global.

### Identity

```lua
int    task:GetId()     -- coroutine id; 0 when the handle has been disposed
```

### Naming

```lua
bool   task:SetName(name)   -- false if id==0 or name already taken by another coroutine
string task:GetName()       -- nil if id==0 or no name set
int    Tasks.GetIdByName(name)  -- returns the id of the running task with that name, or nil
```

Names are optional human-readable labels. Name uniqueness is enforced across all live slots. `Tasks.GetIdByName` searches all non-released slots and returns `nil` if no live task with that name exists or if the task's id is 0.

### Status

```lua
int    task:GetStatus()   -- one of the TaskStatus constants above
bool   task:Finished()    -- true when id==0, slot gone, or coroutine reached a terminal state
```

`task:Finished()` returns `true` when the handle is inert (`id == 0`) or the slot no longer exists. Paused (`TaskStatus.Paused`) and waiting (`TaskStatus.Waiting`) coroutines are **not** considered finished.

### Pause / Resume

A running coroutine can suspend itself cooperatively and wait for an external resume signal. An optional value can be passed through the resume, delivered as the return value of `Pause()`.

```lua
-- Inside the coroutine:
local val = Pause()         -- suspends until resumed; returns the value passed to Resume(), or nil

-- From outside (another coroutine or C#):
task:Resume()               -- wake any suspended state (Pause/Sleep/Wait); no value delivered
task:Resume(value)          -- wake; value delivered only when the target was Paused — discarded for Sleep/Wait
```

| Suspended state | `Resume()` effect | Value delivered? |
|---|---|---|
| `Pause()` | Wakes and continues after `Pause()` | ✅ Yes — returned by `Pause()` |
| `Sleep(n)` | Wakes before deadline expires | ❌ No — discarded |
| `task:Wait()` | Wakes before target finishes | ❌ No — discarded |

`task:Resume()` also wakes coroutines suspended by `Sleep()` or `task:Wait()`. When a sleeping or waiting coroutine is force-resumed this way, any value passed to `Resume(value)` is **discarded** — those states do not have a return-value channel. Only `Pause()` delivers the value.

`Pause()` returns whatever value was provided to `task:Resume(value)`. If `Resume()` is called without an argument (or with `nil`), `Pause()` returns `nil`. This enables request/response patterns without shared globals:

```lua
local worker = Tasks.New(function()
    while true do
        local item = Pause()   -- wait for work
        if item == nil then break end
        process(item)
    end
end)

-- Dispatch work from another coroutine:
worker:Resume("job-1")
worker:Resume("job-2")
worker:Resume(nil)   -- signal shutdown
```

`Pause()` is a no-op when called outside a scheduler-managed coroutine (inline path, registered function callbacks, etc.).

### Wait

Suspends the calling coroutine until the target task reaches a terminal state (`Done`, `Faulted`, or `Cancelled`). Eliminates the need for a polling loop.

```lua
nil  task:Wait()                -- suspend until target finishes (no timeout)
nil  task:Wait(timeoutMs)       -- suspend until target finishes, or timeoutMs elapses
```

- Must be called from inside a running scheduler-managed coroutine (i.e. inside `Tasks.New`, `ExecuteString`, etc.).
- If the target task is already finished when `Wait` is called, it returns immediately without yielding.
- If the target handle is released (`id == 0`) or the slot no longer exists, it also returns immediately.
- The optional `timeoutMs` argument is a number of milliseconds after which the wait is abandoned regardless of the target's state. There is no return value indicating whether the wait timed out — call `task:Finished()` afterwards if you need to distinguish.
- Raises a Lua error if called outside a scheduler-managed coroutine.

```lua
-- Wait without timeout
local t = Tasks.New(function() Sleep(500) end)
t:Wait()            -- caller suspends here until t finishes
t:Dispose()

-- Wait with timeout
local t = Tasks.New(function() Sleep(10000) end)
t:Wait(1000)        -- gives up after 1 second even if t is still running
if not t:Finished() then
    t:Cancel()
end
t:Dispose()

-- Replace a poll loop:
-- Before:
--   while not t:Finished() do Sleep(10) end
-- After:
t:Wait()
```

### Results

```lua
string task:GetError()        -- error string, or nil when no error or task still running
value  task:GetResult()       -- typed result value, or nil when task has not finished yet
value  task:ConsumeResult()   -- like GetResult, but immediately frees the result and releases the slot
```

`GetResult` is non-destructive — the slot and its result stay pinned until all handles are GC'd. Use this when you need to read the result multiple times or keep the slot observable.

`ConsumeResult` frees the result data immediately after pushing it to the Lua stack and advances the slot to `RELEASED` so the scheduler can compact it on the next tick — no waiting for GC. Use this when you want to eagerly release a large result (e.g. a full database query table) as soon as it has been consumed. After calling `ConsumeResult`, `Finished()` returns `true` and `GetResult()` returns `nil`.

If the task faulted (has an error), `ConsumeResult` returns `nil` and still releases the slot — call `GetError()` before `ConsumeResult()` if you need the error message.

### Cancellation

```lua
task:Cancel()   -- signals the coroutine to be terminated before its next resume; no-op if already done
```

The coroutine is not stopped immediately; the scheduler sets `interrupted` and terminates it at the next scheduling opportunity. `GetStatus()` will return `TaskStatus.Cancelled` once compacted.

### Per-task error handler — OnError

`task:OnError(fn)` registers a callback that is invoked when **this specific task** faults (finishes with a Lua error). The handler receives the same `(id, err)` arguments as `Tasks.SetErrorHandler` and takes priority over the global handler.

```lua
Task  task:OnError(fn)   -- returns self for method chaining; pass nil to clear
```

- The handler fires whether the task is fire-and-forget or observed — unlike the global handler, which only fires for fire-and-forget tasks.
- Returns `self` so it can be chained immediately after `Tasks.New(...)`.
- Calling `OnError(nil)` removes any previously registered handler for this task.
- If both a per-task handler and the global handler are set, **only the per-task handler is called**.

```lua
-- Per-task handler (chained) — fires even for non-fire-and-forget
local t = Tasks.New(function() error("oops") end)
    :OnError(function(id, err)
        print("task " .. id .. " failed: " .. err)
    end)
t:Wait()
t:Dispose()

-- Fire-and-forget with per-task handler — no global handler needed
Tasks.New(function() error("boom") end)
    :OnError(function(id, err) log("task error", id, err) end)
    :Dispose()

-- Clear a previously set handler
t:OnError(nil)
```

### Handle lifecycle — Dispose and fire-and-forget

- If the coroutine is already **done** — the slot is released immediately.
- If the coroutine is **still running** — it continues until it finishes, then the slot is auto-compacted by the scheduler. Any error is forwarded to `Tasks.SetErrorHandler` (or printed to `stderr` if no handler is set).
- If the coroutine is **paused** — it is cancelled (interrupted flag set) so it does not hang indefinitely.

Call `Dispose()` (or chain `:Dispose()` immediately) whenever a task is intended to be fire-and-forget. Without it, the handle keeps `luaRefCount` elevated and prevents the slot from being treated as unobserved — errors will not reach the error handler, and the slot stays live until GC collects the handle non-deterministically.

```lua
-- Fire and forget — errors routed to SetErrorHandler / stderr
Tasks.New(function() doWork() end):Dispose()

-- Equivalent: let the variable go out of scope and be collected
-- (but :Dispose() is preferred for deterministic behaviour)
do
    local t = Tasks.New(function() doWork() end)
end  -- t collected on next GC; Dispose() is more explicit
```

### GetAllIds

```lua
table   Tasks.GetAllIds()        -- array of all live (non-released) coroutine ids
int     Tasks.ActiveCount()      -- number of currently live slots (id != 0)
int     Tasks.MaxSlots           -- maximum number of concurrent coroutine slots (256)
```

`Tasks.ActiveCount()` returns the number of slots that are currently occupied (have a non-zero id). This includes coroutines in any state — running, sleeping, paused, waiting, done-but-not-yet-compacted, etc. Use it together with `Tasks.MaxSlots` to implement back-pressure before hitting the hard slot limit that causes `Tasks.New` to raise an error.

```lua
-- Back-pressure: wait until a slot is free before spawning
while Tasks.ActiveCount() >= Tasks.MaxSlots do
    Sleep(10)
end
Tasks.New(function() doWork() end):Dispose()
```

### Examples

```lua
-- Observe a task and read its result
local task = Tasks.New(function(a, b)
    Sleep(100)
    return a + b
end, 10, 32)

while not task:Finished() do
    Sleep(10)
end
print(task:GetError())   -- nil on success
print(task:GetResult())  -- 42
task:Dispose()           -- idempotent; handle released

-- Fire and forget with error handler
Tasks.SetErrorHandler(function(id, err)
    print("[TASK ERROR] " .. id .. ": " .. err)
end)
Tasks.New(function() error("oops") end):Dispose()

-- Per-task error handler — takes priority over the global handler
local t = Tasks.New(function() error("per-task error") end)
    :OnError(function(id, err)
        print("caught by per-task handler: " .. err)
    end)
t:Wait()
t:Dispose()

-- Pause / Resume pattern
local task = Tasks.New(function()
    print("step 1")
    Pause()        -- suspends here
    print("step 2")
end)

while task:GetStatus() ~= TaskStatus.Paused do
    Sleep(5)
end
task:Resume()  -- unblocks step 2
while not task:Finished() do Sleep(5) end
task:Dispose()

-- Open a coroutine from its id (e.g. from the ID global inside a running coroutine)
local watcher = Tasks.Open(someId)
if watcher then
    print(watcher:GetStatus())
    watcher:Dispose()
end

-- Cancel a running task
local t = Tasks.New(function()
    while true do Sleep(10) end
end)
Sleep(50)
t:Cancel()
while not t:Finished() do Sleep(5) end
t:Dispose()

-- Wait for a task to finish (no poll loop needed)
local t = Tasks.New(function() Sleep(200) return 99 end)
t:Wait()
print(t:GetResult())  -- 99
t:Dispose()

-- Wait with a timeout
local slow = Tasks.New(function() Sleep(10000) end)
slow:Wait(500)         -- give up after 500 ms
if not slow:Finished() then
    slow:Cancel()
end
slow:Dispose()
```

---

## Llama

A local LLM inference module backed by [llama.cpp](https://github.com/ggml-org/llama.cpp). Runs GGUF models on CPU or GPU (CUDA). All inference is dispatched to a **persistent background worker thread** per context; the calling coroutine uses non-blocking `Poll()` calls cooperatively — no OS thread is blocked. **Windows only** in the current build.

> **Note:** `Llama.CreateContext` lazily initialises the llama.cpp and ggml backends on first call. The CUDA backend is loaded automatically when `ggml-cuda.dll` is present in the output directory.

### Module-level

```lua
LlamaContext  Llama.CreateContext(opt opts)
table         Llama.GetLogs()
```

#### Llama.CreateContext

```lua
LlamaContext  Llama.CreateContext(opt opts)
```

Creates a new inference context. The worker thread is started immediately. Returns a `LlamaContext` userdata.

**`opts` fields (all optional):**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `n_gpu_layers` | integer | `99` | Number of model layers to offload to GPU. `99` offloads all layers |
| `n_ctx` | integer | `4096` | Context window size in tokens |
| `n_threads` | integer | `0` | CPU inference threads. `0` = auto-detect (hardware concurrency) |
| `n_batch` | integer | `512` | Prompt batch size |
| `flash_attn` | boolean | `false` | Enable Flash Attention |
| `model_ttl_ms` | integer | `300000` | Milliseconds of idle time before the model is automatically unloaded. `0` disables auto-unload |

```lua
-- Default context (all layers on GPU, 4096 context window)
local ctx = Llama.CreateContext()

-- Custom context
local ctx = Llama.CreateContext({
    n_gpu_layers = 32,
    n_ctx        = 8192,
    n_threads    = 8,
    model_ttl_ms = 0,     -- never auto-unload
})
```

#### Llama.GetLogs

```lua
table  Llama.GetLogs()
```

Drains and returns the accumulated llama.cpp / ggml log lines since the last call as an array of strings. The internal buffer holds up to 500 entries; older entries are dropped when the buffer is full.

```lua
local logs = Llama.GetLogs()
for _, line in ipairs(logs) do io.write(line) end
```

---

### LlamaContext methods

All methods are available both as `Llama.Method(ctx, ...)` and as `ctx:Method(...)`.

```lua
bool          ctx:SetModel(path [, opts])
bool          ctx:LoadModel()
bool          ctx:UnloadModel()
bool          ctx:IsModelLoaded()
bool          ctx:IsReady()
bool          ctx:Generate(messages [, opts])
ok, data      ctx:Poll()
bool          ctx:Stop()
bool          ctx:Reset()
float[]       ctx:Embed(text)
table         ctx:Info()
bool          ctx:Dispose()
```

---

#### ctx:SetModel

```lua
true  ctx:SetModel(path)
nil, errmsg  ctx:SetModel(path)
```

Sets the path to the GGUF model file. Does not load it — call `LoadModel` afterwards. Returns `nil, "busy"` if the worker is currently busy.

```lua
ctx:SetModel([[C:\Models\qwen3-0.6b-q8_0.gguf]])
```

---

#### ctx:LoadModel

```lua
true         ctx:LoadModel()
nil, errmsg  ctx:LoadModel()
```

Queues a model load on the worker thread. Returns immediately; poll `ctx:IsReady()` to wait for completion, then call `ctx:Info()` only if you need to check for an error.

```lua
ctx:LoadModel()
while not ctx:IsReady() and ctx:Info().context.status ~= 'error' do Sleep(50) end
if ctx:Info().context.status == 'error' then
    error(ctx:Info().context.error)
end
```

---

#### ctx:UnloadModel

```lua
true         ctx:UnloadModel()
nil, errmsg  ctx:UnloadModel()
```

Queues a model unload on the worker thread. Returns `nil, "busy"` if a generation is in progress. Call `Stop()` first to cancel generation, then `UnloadModel`.

---

#### ctx:IsModelLoaded

```lua
bool  ctx:IsModelLoaded()
```

Returns `true` if a model is currently loaded and ready for inference.

---

#### ctx:IsReady

```lua
bool  ctx:IsReady()
```

Returns `true` if the context is idle with a model loaded — i.e. ready to accept a `Generate` call immediately.

---

#### ctx:Generate

```lua
true         ctx:Generate(messages [, opts] [, tools])
nil, errmsg  ctx:Generate(messages [, opts] [, tools])
```

Queues a generation request. `messages` is an array of chat message tables (OpenAI-format). Returns immediately; output is consumed via `Poll`.

Returns `nil, errmsg` when:
- `"already running"` — a generation is already in progress
- `"no model"` — no model has been set / loaded
- `"busy"` — the worker is occupied with another task
- `"empty messages"` — the messages array was empty or contained no valid entries
- `"disposed"` — the context has been disposed

**Message table fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `role` | string | yes | `"system"`, `"user"`, `"assistant"`, or `"tool"` |
| `content` | string | yes | Message text |
| `tool_call_id` | string | no | For `"tool"` role messages — the id of the tool call being responded to |
| `tool_calls` | string or table | no | For `"assistant"` role messages — JSON string or table of tool call objects (OpenAI format) |

**`opts` fields (all optional):**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `temperature` | number | `0.8` | Sampling temperature |
| `top_p` | number | `0.95` | Top-P nucleus sampling |
| `top_k` | integer | `40` | Top-K sampling |
| `min_p` | number | `0.05` | Min-P sampling |
| `seed` | integer | `-1` | RNG seed. `-1` = random |
| `max_tokens` | integer | `2048` | Maximum tokens to generate |

The optional `tools` argument (3rd positional arg when `opts` is present, 2nd otherwise) accepts either a **JSON string** or a **Lua table**. When a table is passed it is serialized automatically with empty tables encoded as `{}` so parameter schemas are preserved correctly.

```lua
ctx:Generate(
    {
        { role = 'system',    content = 'You are a helpful assistant.' },
        { role = 'user',      content = 'What is 2 + 2?' },
    },
    { temperature = 0.3, max_tokens = 512 }
)
```

---

#### ctx:Poll

```lua
ok, data  ctx:Poll()
```

Non-blocking. Drains the next token or event from the worker queue.

**Return values:**

| Return | Type | Description |
|--------|------|-------------|
| `ok` | boolean | `true` while generation is in progress; `false` when done |
| `data` | table or nil | `nil` when nothing is ready yet; otherwise a table with `text` and `type` fields |

**`data.type` values:**

| Value | Description |
|-------|-------------|
| `"token"` | Regular output token text |
| `"reasoning"` | Token inside a `<think>...</think>` block (Qwen3, DeepSeek-R1, QwQ) |
| `"tool_calls"` | `data.text` is a JSON array of tool call objects |
| `"error"` | `data.text` contains the error message |

When `ok` is `false` there is no more data; the poll loop should exit.

```lua
local ok, data = ctx:Poll()
while ok do
    if data then
        if data.type == 'error' then error(data.text) end
        if data.type == 'token'     then io.write(data.text) end
        if data.type == 'reasoning' then -- discard or log end
        if data.type == 'tool_calls' then
            local calls = Json.New():Decode(data.text)
            -- handle tool calls
        end
    end
    Sleep(10)
    ok, data = ctx:Poll()
end
```

---

#### ctx:Stop

```lua
bool  ctx:Stop()
```

Signals the worker to abort the current generation at the next token boundary. Non-blocking — returns immediately. The context status returns to `"idle"` asynchronously. Always returns `true`.

---

#### ctx:Reset

```lua
true         ctx:Reset()
nil, errmsg  ctx:Reset()
```

Clears the KV cache without unloading the model. Use between multi-turn conversations to start a fresh session. Returns `nil, "busy"` if a generation is in progress.

---

#### ctx:Embed

```lua
float[]      ctx:Embed(text)
nil, errmsg  ctx:Embed(text)
```

Generates an embedding vector for `text`. Blocks (yields) until the embedding is complete. The model must support embeddings. Returns a sequential table of floats, or `nil, errmsg` on failure.

> **Note:** Embedding and generation use different llama.cpp context configurations. Not all models support both.

```lua
local vec = assert(ctx:Embed("hello world"))
print(#vec)  -- embedding dimension
```

---

#### ctx:Info

```lua
table  ctx:Info()
```

Returns a snapshot of the context state. The returned table has two sub-tables: `context` (always present) and `model` (present only when a model is loaded).

**`info.context` fields:**

| Field | Type | Description |
|-------|------|-------------|
| `status` | string | `"idle"`, `"loading"`, `"generating"`, `"unloading"`, or `"error"` |
| `n_ctx` | integer | Configured context window size |
| `n_gpu_layers` | integer | Configured GPU layer count |
| `n_threads` | integer | CPU thread count (resolved from hardware concurrency when 0) |
| `n_batch` | integer | Batch size |
| `model_ttl_ms` | integer | Auto-unload timeout in milliseconds |
| `model_path` | string or nil | Path set via `SetModel`, or `nil` |
| `error` | string or nil | Last error message, or `nil` |
| `last_used` | number or nil | Seconds since last generation completed, or `nil` if never used |
| `tokens_used` | integer | Tokens currently occupying the KV cache |
| `tokens_available` | integer | Remaining tokens available in the context window |

**`info.model` fields (nil when no model is loaded):**

| Field | Type | Description |
|-------|------|-------------|
| `desc` | string | Model description string from the GGUF metadata |
| `arch` | string | Model architecture (e.g. `"llama"`, `"qwen2"`) |
| `context_length` | integer | Model's native maximum context length |
| `n_params` | integer | Total parameter count |
| `n_embd` | integer | Embedding dimension |
| `n_layer` | integer | Number of transformer layers |
| `size_bytes` | integer | Model size in bytes |
| `chat_template` | string | Jinja2 chat template string from the GGUF metadata |
| `n_gpu_layers` | integer | Configured GPU layer count |
| `gpu_layer_count` | integer | Layers actually offloaded to GPU |
| `cpu_layer_count` | integer | Layers running on CPU |
| `gpu_percent` | number | Percentage of layers on GPU |
| `cpu_percent` | number | Percentage of layers on CPU |
| `capabilities` | array | String array of detected model capabilities (e.g. `"embedding"`, `"completion"`) |

```lua
local info = ctx:Info()
print(info.context.status)            -- "idle"
print(info.context.tokens_used)       -- 0
if info.model then
    print(info.model.desc)            -- "Qwen3-0.6B Q8_0"
    print(info.model.n_params)        -- parameter count
    print(info.model.gpu_percent)     -- e.g. 100.0
end
```

---

#### ctx:Dispose

```lua
bool  ctx:Dispose()
```

Signals the worker thread to shut down, unloads the model, and frees all resources. Idempotent — safe to call multiple times. The GC calls this automatically, but explicit disposal is recommended to release GPU memory promptly.

---

### Tool calling

Pass an OpenAI-format tools JSON string as `opts.tools` to `Generate`. When the model produces a tool call the output is detected automatically and returned by `Poll` with `data.type == "tool_calls"` and `data.text` set to a JSON array:

```json
[{"name": "get_weather", "arguments": {"city": "Paris"}}]
```

Supported detection formats: single JSON object, JSON array of objects, and XML `<tool_call>...</tool_call>` tags (Mistral / Hermes style).

```lua
-- tools can be a Lua table (serialized automatically) or a JSON string
local tools = {
    {
        type     = 'function',
        ['function'] = {
            name        = 'get_weather',
            description = 'Get the weather for a city',
            parameters  = {
                type       = 'object',
                properties = { city = { type = 'string' } },
                required   = { 'city' },
            },
        },
    },
}

-- Pass tools as the third argument (after opts, or second if no opts)
ctx:Generate(
    {{ role = 'user', content = 'What is the weather in Paris?' }},
    { temperature = 0.3 },
    tools
)

-- Or with a pre-encoded JSON string:
ctx:Generate(
    {{ role = 'user', content = 'What is the weather in Paris?' }},
    nil,
    Json.New():Encode(tools)
)

local ok, data = ctx:Poll()
while ok do
    if data then
        if data.type == 'error' then error(data.text) end
        if data.type == 'tool_calls' then
            local calls = Json.New():Decode(data.text)
            print(calls[1].name, calls[1].arguments.city)
        end
    end
    Sleep(10)
    ok, data = ctx:Poll()
end
```

After handling tool results, pass them back as `"tool"` role messages and call `Generate` again without calling `Reset` (the KV cache is preserved between `Generate` calls within the same session).

---

### Reasoning models

Models with `<think>` support (Qwen3, DeepSeek-R1, QwQ) emit chain-of-thought tokens before their final answer. These are returned as `data.type == "reasoning"` by `Poll`.

```lua
local content, reasoning = '', ''
local ok, data = ctx:Poll()
while ok do
    if data then
        if data.type == 'error'     then error(data.text) end
        if data.type == 'token'     then content   = content   .. data.text end
        if data.type == 'reasoning' then reasoning = reasoning .. data.text end
    end
    Sleep(10)
    ok, data = ctx:Poll()
end
print('Reasoning:', reasoning)
print('Answer:',    content)
```

---

### Full example

```lua
local ctx = Llama.CreateContext({ n_gpu_layers = 99, n_ctx = 4096 })
ctx:SetModel([[C:\Models\qwen3-0.6b-q8_0.gguf]])
ctx:LoadModel()

-- Wait for the model to finish loading
while not ctx:IsReady() and ctx:Info().context.status ~= 'error' do Sleep(50) end
if ctx:Info().context.status == 'error' then error(ctx:Info().context.error) end
print('Model loaded:', info.model.desc)
print(string.format('GPU: %.0f%%  CPU: %.0f%%', info.model.gpu_percent, info.model.cpu_percent))

-- First turn
ctx:Generate(
    {
        { role = 'system', content = 'You are a helpful assistant.' },
        { role = 'user',   content = 'What is the capital of France?' },
    },
    { temperature = 0.3 }
)

local result = ''
local ok, data = ctx:Poll()
while ok do
    if data then
        if data.type == 'error' then error(data.text) end
        if data.type == 'token' then
            result = result .. data.text
            io.write(data.text)
        end
    end
    Sleep(10)
    ok, data = ctx:Poll()
end
print()

-- Second turn (KV cache preserved — no Reset needed)
ctx:Generate({{ role = 'user', content = 'And Germany?' }})

ok, data = ctx:Poll()
while ok do
    if data and data.type == 'token' then io.write(data.text) end
    Sleep(10)
    ok, data = ctx:Poll()
end
print()

ctx:Reset()    -- clear KV cache between sessions
ctx:Dispose()  -- free GPU memory and worker thread
```

---

### ToolSuite

`ToolSuite` is a higher-level userdata that manages OpenAI-compatible tool declarations and dispatches tool calls returned by a model. It hides the JSON serialisation required by `Generate(..., tools)` and the message-appending bookkeeping normally needed after `Poll` returns a `tool_calls` event.

#### Creation

```lua
ToolSuite  Llama.CreateToolSuite()
```

Creates an empty `ToolSuite` with no tools and no permission gate.

```lua
tostring(suite)   -- "ToolSuite(N tools)"
```

---

#### suite:AddTool

```lua
true  suite:AddTool(name, description, parameters, fn)
```

Registers a tool with the suite.

| Argument | Type | Description |
|----------|------|-------------|
| `name` | string | Tool name as the model will call it |
| `description` | string | Natural-language description of what the tool does |
| `parameters` | table | Sequential array of parameter descriptor tables (see below) |
| `fn` | function | Callback invoked when the model calls this tool |

**Parameter descriptor fields:**

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | yes | Parameter name |
| `type` | string | no | JSON Schema type (`"string"`, `"integer"`, `"number"`, `"boolean"`). Defaults to `"string"` |
| `description` | string | no | Human-readable description |
| `required` | boolean | no | Whether the parameter is required. Defaults to `false` |

The callback `fn` is called with arguments in the order the parameters were declared. Each argument is the decoded value from the model's `arguments` object. Returns whatever the tool result should be (coerced to string via `tostring`).

```lua
local suite = Llama.CreateToolSuite()
suite:AddTool(
    'get_weather',
    'Get the current weather for a city',
    {
        { name='city',  type='string',  description='City name', required=true  },
        { name='units', type='string',  description='"celsius" or "fahrenheit"', required=false },
    },
    function(city, units)
        -- city and units are the decoded argument values
        return 'It is 22 ' .. (units or 'celsius') .. ' in ' .. city
    end
)
```

---

#### suite:GetJson

```lua
string  suite:GetJson()
```

Returns the OpenAI-format JSON tools array for all registered tools. Pass the result directly to `ctx:Generate` as the `tools` argument, or use the `ToolSuite` userdata directly (it is accepted as-is).

```lua
print(suite:GetJson())
-- [{"type":"function","function":{"name":"get_weather",...}}]

-- Both forms are accepted by Generate:
ctx:Generate(messages, opts, suite:GetJson())  -- JSON string
ctx:Generate(messages, opts, suite)            -- userdata directly
```

---

#### suite:Call

```lua
number  suite:Call(messages)
```

Inspects the **last message** in `messages`. If it is an `assistant` message with a `tool_calls` field, decodes the JSON, dispatches each call to the matching registered function, and appends `{ role='tool', content=result, tool_call_id=id }` entries to `messages`.

Returns the number of tool replies appended (0 if the last message is not a tool call or no calls were decoded).

**Yield-safe:** both tool callbacks and the permission gate (see `suite:Callback`) use `lua_pcallk` internally, so they can call `Sleep`, `HttpClient:Call`, or any other yieldable engine function without stalling the application. The KitsuneEngine 1000-instruction ticker that forces coroutine yields mid-execution is also handled correctly.

If a tool name is not found in the suite, a `"Tool not found: <name>"` reply is appended and dispatch continues with the next call.

```lua
-- After Poll returns a tool_calls event:
local ok, data = ctx:Poll()
while ok do
    if data and data.type == 'tool_calls' then
        suite:Call(msgs)        -- dispatches and appends tool replies to msgs
        ctx:Generate(msgs)      -- continue the conversation
    end
    Sleep(10)
    ok, data = ctx:Poll()
end
```

---

#### suite:Callback

```lua
nil  suite:Callback(fn)
nil  suite:Callback(nil)     -- remove the gate
```

Registers an optional **permission gate** that is called before every tool invocation. Pass `nil` to remove a previously set gate.

The gate function receives:

| Argument | Type | Description |
|----------|------|-------------|
| `name` | string | Tool name the model wants to call |
| `args` | table or nil | Decoded arguments table, or `nil` if the model sent no arguments |

Return `true` to allow the call; return `false` (or any falsy value) to deny it. When denied, a `"error: permission denied"` reply is appended to messages and dispatch continues with the next call.

**Yield-safe:** the gate can call `Sleep`, show a UI prompt, or await any async operation — it uses `lua_pcallk` internally.

```lua
suite:Callback(function(name, args)
    -- name  = tool being requested
    -- args  = decoded argument table (or nil)
    -- This can yield — e.g. wait for a user to click Allow/Deny
    local allowed = UI.Ask('Allow the AI to call ' .. name .. '?')
    return allowed
end)

-- Simple allowlist
local ALLOWED = { get_weather = true, search = true }
suite:Callback(function(name, args)
    return ALLOWED[name] == true
end)

-- Remove the gate
suite:Callback(nil)
```

---

#### Complete tool-calling example

```lua
local ctx   = Llama.CreateContext()
local suite = Llama.CreateToolSuite()

suite:AddTool(
    'get_weather',
    'Get the current weather for a city',
    { { name='city', type='string', description='City name', required=true } },
    function(city)
        Sleep(0)            -- safe to yield inside the callback
        return 'Sunny, 22°C in ' .. city
    end
)

-- Optional: permission gate (yieldable)
suite:Callback(function(name, args)
    print('Model wants to call: ' .. name)
    return true    -- allow all tools
end)

ctx:SetModel([[C:\Models\qwen3-0.6b-q8_0.gguf]])
ctx:LoadModel()
while not ctx:IsReady() do Sleep(50) end

local msgs = {
    { role='system', content='You are a helpful assistant with access to tools.' },
    { role='user',   content='What is the weather in Paris?' },
}

ctx:Generate(msgs, { temperature=0.3 }, suite)

local ok, data = ctx:Poll()
while ok do
    if data then
        if data.type == 'error' then
            error(data.text)
        elseif data.type == 'token' then
            io.write(data.text)
        elseif data.type == 'tool_calls' then
            -- Dispatch all tool calls and append replies to msgs.
            -- msgs already contains the assistant tool_calls message
            -- (auto-appended by Poll when generation completed).
            suite:Call(msgs)
            -- Continue the conversation with tool results
            ctx:Generate(msgs, { temperature=0.3 }, suite)
        end
    end
    Sleep(10)
    ok, data = ctx:Poll()
end
print()

ctx:Dispose()
```

---

## Third-Party Notices


KitsuneEngine incorporates the following open-source libraries. Their copyright notices and license terms are reproduced below as required.

---

### llama.cpp

**Copyright © 2023–2026 The ggml authors**

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*License: [MIT](https://opensource.org/licenses/MIT)*

---

### pugixml

**Copyright © 2006–2026 Arseny Kapoulkine**

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*License: [MIT](https://opensource.org/licenses/MIT)*

---

### Lua

**Copyright © 1994–2024 Lua.org, PUC-Rio.**

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*License: [MIT](https://opensource.org/licenses/MIT)*

---

### SQLite

The author disclaims copyright to the SQLite source code. In place of a legal notice:

> May you do good and not evil.  
> May you find forgiveness for yourself and forgive others.  
> May you share freely, never taking more than you give.

*License: [Public Domain](https://www.sqlite.org/copyright.html)*

---

### hiredis

**Copyright © 2009–2011 Salvatore Sanfilippo**  
**Copyright © 2010–2014 Pieter Noordhuis**  
**Copyright © 2015 Matt Stancliff, Jan-Erik Rediger**

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
- Neither the name of Redis nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*License: [BSD 3-Clause](https://opensource.org/licenses/BSD-3-Clause)*

---

### msgpack-c

**Copyright © 2008–2009 FURUHASHI Sadayuki**

Distributed under the Boost Software License, Version 1.0.

> Permission is hereby granted, free of charge, to any person or organization obtaining a copy of the software and accompanying documentation covered by this license (the "Software") to use, reproduce, display, distribute, execute, and transmit the Software, and to prepare derivative works of the Software, and to permit third-parties to whom the Software is furnished to do so, all subject to the following: The copyright notices in the Software and this entire statement, including the above license grant, this restriction and the following disclaimer, must be included in all copies of the Software, in whole or in part, and all derivative works of the Software, unless such copies or derivative works are solely in the form of machine-executable object code generated by a source language processor. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*License: [Boost Software License 1.0](https://www.boost.org/LICENSE_1_0.txt)*

---

### libevent

**Copyright © 2000–2007 Niels Provos**
**Copyright © 2007–2012 Niels Provos and Nick Mathewson**

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
3. The name of the author may not be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*License: [BSD 3-Clause](https://opensource.org/licenses/BSD-3-Clause)*

---

### MySQL Connector/C (libmysql)

**Copyright © 2000, 2024, Oracle and/or its affiliates.**

This software is distributed under the GNU General Public License, version 2.0. The authors of MySQL hereby grant an additional permission to link the program and its derivative works with the separately licensed software listed in the FOSS License Exception at <http://oss.oracle.com/licenses/universal-foss-exception>.

*License: [GPL v2 with FOSS Exception](https://www.gnu.org/licenses/old-licenses/gpl-2.0.html)*

---

### PostgreSQL (libpq)

**Copyright © 1996–2024 The PostgreSQL Global Development Group**  
**Copyright © 1994 The Regents of the University of California**

Permission to use, copy, modify, and distribute this software and its documentation for any purpose, without fee, and without a written agreement is hereby granted, provided that the above copyright notice and this paragraph and the following two paragraphs appear in all copies.

IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, INCLUDING LOST PROFITS, ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATIONS TO PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.

*License: [PostgreSQL License](https://www.postgresql.org/about/licence/)*

---

### OpenSSL

**Copyright © 1998–2024 The OpenSSL Project Authors**

Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at <https://www.apache.org/licenses/LICENSE-2.0>.

*License: [Apache 2.0](https://www.apache.org/licenses/LICENSE-2.0)*

---

### mongo-c-driver and libbson

**Copyright © 2009–2024 MongoDB, Inc.**

Licensed under the Apache License, Version 2.0. You may obtain a copy of the License at <https://www.apache.org/licenses/LICENSE-2.0>.

*License: [Apache 2.0](https://www.apache.org/licenses/LICENSE-2.0)*

---

### zlib

**Copyright © 1995–2022 Jean-loup Gailly and Mark Adler**

This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

*License: [zlib License](https://zlib.net/zlib_license.html)*

---

### Zstandard (zstd)

**Copyright © Meta Platforms, Inc. and affiliates. All rights reserved.**

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

- Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
- Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
- Neither the name Facebook, nor Meta, nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.

*License: [BSD 3-Clause](https://opensource.org/licenses/BSD-3-Clause) (used under the BSD option of the dual BSD/GPLv2 license)*

---

### liblzma (XZ Utils)

The `liblzma` library used by this software is placed under the **BSD Zero Clause License (0BSD)**:

> Permission to use, copy, modify, and/or distribute this software for any purpose with or without fee is hereby granted.
>
> THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

*License: [0BSD](https://opensource.org/licenses/0BSD)*

---

### bzip2 / libbzip2

**Copyright © 1996–2019 Julian R Seward. All rights reserved.**

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
3. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
4. The name of the author may not be used to endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*License: [BSD-style](https://sourceware.org/bzip2/)*

---

### tomlc99

**Copyright © CK Tan**

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*License: [MIT](https://opensource.org/licenses/MIT)*

---

### libyaml

**Copyright
**Copyright © 2006–2016 Kirill Simonov**

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*License: [MIT](https://opensource.org/licenses/MIT)*

---

### libarchive

**Copyright © Tim Kientzle.

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer in this position and unchanged.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*License: [BSD 2-Clause](https://opensource.org/licenses/BSD-2-Clause)*

---

### utf8proc

**Copyright © 2014–2021 Steven G. Johnson, Jiahao Chen, Tony Kelman, Jonas Fonseca, and other contributors.**  
Originally developed by Jan Behrens and the Public Software Group.

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

This software also contains data derived from the Unicode Character Database, which is subject to the [Unicode, Inc. License Agreement](https://www.unicode.org/copyright.html).

*License: [MIT](https://opensource.org/licenses/MIT)*

---



