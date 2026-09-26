# Lua Environment API Reference

A comprehensive reference for all available functions in the Lua environment.

---

## Table of Contents

- [Userdata Return Values (read first)](#userdata-return-values-read-first)
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
- [TCP](#tcp)
- [Hashing (SHA256, MD5, SHA1)](#hashing)
- [MySQL](#mysql)
- [Postgres](#postgres)
- [Timer](#timer)
- [SQLite](#sqlite)
- [DuckDB](#duckdb)
- [Json](#json)
- [MsgPack](#msgpack)
- [Text](#text)
- [UInt](#uint)
- [TimeSpan](#timespan)
- [Identifier](#identifier)
- [DateTime](#datetime)
- [Decimal](#decimal)
- [MongoDB](#mongodb)
- [FileSystem](#filesystem)
- [Image](#image)
- [Sound](#sound)
- [Xml](#xml)
- [Yaml](#yaml)
- [Toml](#toml)
- [Ini](#ini)
- [AliveToken](#alivetoken)
- [Tasks](#tasks)
- [Llama](#llama)
- [ToolSuite](#toolsuite)
- [MCP](#mcp)
- [Third-Party Notices](#third-party-notices)
---

## Userdata Return Values (read first)

All text in the engine is a plain **UTF-8 Lua string** on every platform, both in and out: filenames, CSV fields, SQLite columns, archive entry names, clipboard text, and so on. Non-ASCII text such as `é`, `€`, CJK or emoji is fully supported. Use Lua's built-in `utf8` library for codepoint-level work (`utf8.len`, `utf8.codepoint`, `utf8.offset`, `utf8.char`), and the [`Text`](#text) module for Unicode case mapping, UTF-16 and legacy code pages.

A few functions do return a **userdata** object (`type(v) == "userdata"`) where you might expect a plain string or number: typed values such as `Decimal`, `DateTime`, `Identifier`, `UInt` and `TimeSpan`, listed below. These values print fine but **are not Lua strings or numbers**. Convert them with `tostring(v)` (or `v:ToNumber()`) before doing anything string- or number-like with them.

File paths are UTF-8 as well, on every platform and in every host (including .NET applications): `FileSystem.*`, `Stream.Open`, `Image.Open`/`Save`, `Sound.Open`/`Save`, `Archive.OpenRead`, `Process.Start`, `getenv`/`setenv`, script files run by the host, and the standard Lua functions that take a filename, command or environment name (`io.open`, `io.lines(filename)`, `io.input(filename)`, `io.output(filename)`, `io.popen`, `dofile`, `loadfile`, `require`, `os.remove`, `os.rename`, `os.getenv`, `os.execute`, `os.tmpname`) all handle non-ASCII text. On Windows, where the C runtime would read these in the ANSI code page, the engine replaces those functions with equivalents that behave the same but use the wide (UTF-16) C runtime; `package.path` and `package.cpath` are likewise built from the UTF-8 environment and executable directory.

### What works and what does not

| Operation on a userdata `v` | Result |
|------------------------------|--------|
| `tostring(v)`, `print(v)`, `string.format("%s", v)` | Works: gives the UTF-8 string or the canonical text form |
| `json:Encode(v)`, `csv:Encode(...)`, database parameters | Works: converted automatically |
| `v == "literal"` | **Always `false`**. Lua never calls `__eq` for a userdata compared with a string. Use `tostring(v) == "literal"` |
| `tbl[v]` / `tbl[v] = x` | **Does not match a string key**. The userdata itself is the key. Use `tbl[tostring(v)]` |
| `v:lower()`, `v:find(...)`, `v:match(...)`, `v:gsub(...)`, `v:sub(...)` | **Error** ("attempt to call a nil value"): string methods do not exist on userdata |
| `string.lower(v)`, `string.find(v, ...)`, `string.len(v)`, and so on | **Error** ("string expected, got DATETIME") |
| `table.concat({v, ...})` | **Error** ("invalid value (userdata)") |
| `"prefix" .. v` | **Error** ("attempt to concatenate a DECIMAL value") |
| `math.floor(v)`, `v == 1.5` where `v` is a `Decimal` or `UInt` | **Error** / **`false`**. Use `v:ToNumber()` (lossy) or compare against another `Decimal` / `UInt` |

Idiom: when a value may be a userdata and you only need it as text, normalize it once as soon as it comes back:

```lua
local ok, rows = conn:QueryAll("SELECT id, created FROM orders")   -- Postgres: UUID, TIMESTAMP
for _, row in ipairs(rows) do
    local id = tostring(row[1])        -- Identifier -> "xxxxxxxx-xxxx-..."
    local created = tostring(row[2])   -- DateTime   -> "2024-06-01T12:00:00.000Z"
end
```

### Where userdata are returned instead of a plain value

| Returned by | Userdata | When |
|-------------|----------|------|
| `Tasks` / shared values | same type that was stored | A `UInt`, `DateTime` and so on round-trips as the same userdata |
| MySQL `DECIMAL`, Postgres `NUMERIC`, MongoDB `DECIMAL128` | `Decimal` | Always (falls back to a string if parsing fails) |
| MySQL `DATE`/`DATETIME`/`TIMESTAMP`, Postgres date/time types, MongoDB `DATE_TIME` | `DateTime` | Always (falls back to a string if parsing fails) |
| Postgres `UUID`, MongoDB `OID` / UUID binary | `Identifier` | Always |
| MySQL `BIGINT UNSIGNED` and `BIT(64)`, MsgPack positive integers | `UInt` | Only when the value is greater than `2^63 - 1`. Smaller values are plain integers |
| `Stream:ReadUInt64` / `ReadDecimal` / `ReadIdentifier` / `ReadDateTime` / `ReadTimeSpan` | matching type | Always |
| `Stream:ReadUnsignedLong` | `UInt` | Only when the value is greater than `2^63 - 1` |
| `Timer:ElapsedTimeSpan()`, `client:GetTimestamp()` (HttpClient) | `TimeSpan` | Always |

---

## Global Functions

### CRC Functions

```lua
int CRC32(stringdata, opt existingcrc)
int CRC64(data)
```
- `CRC32`: Calculate a CRC32 checksum
- `CRC64`: Calculate a CRC64 over a string's bytes (non-strings are converted via `tostring`; a `Stream` raises an error). The unsigned 64-bit result is returned as a Lua integer, so it can be negative. For a checksum over UTF-16 text, pass `Text.ToUtf16(str)`

### Time & Sleep

```lua
nil Sleep(opt ms)
nil Sleep(token)
nil Sleep(token, ms)
nil Yield(opt didWork)
value Pause()
int Time()
number Runtime()
```
- `Sleep`: Yield the current coroutine cooperatively without blocking any OS thread. Falls back to a blocking OS sleep when called outside a scheduler-managed coroutine; inside an engine coroutine that can't yield (and not on the scheduler thread) it raises "Sleep() cannot be called from a non-yieldable context".
  - `Sleep(ms)` — sleep for at least `ms` milliseconds (default `0`).
  - `Sleep(token)` — sleep until the `AliveToken` is disposed, expired, or a linked parent dies. Returns immediately if the token is already dead.
  - `Sleep(token, ms)` or `Sleep(ms, token)` — sleep until whichever comes first: token death or the millisecond deadline.
- `Yield`: Cooperatively yield the current coroutine back to the scheduler immediately (no sleep delay). `Yield(true)` also tells the scheduler the coroutine did work, so it doesn't idle-sleep before the next round. For inline sync calls, this briefly releases Lua access so the scheduler and variable bridge can service their queues before the call is resumed. Raises "Yield() cannot be called from a non-yieldable context" where it can't yield.
- `Pause`: Suspend the current coroutine indefinitely until `task:Resume(value)` is called externally, then return `value` (nothing if none was given). The coroutine's status becomes `TaskStatus.Paused` and it will not be resumed by the scheduler on its own. A no-op when called outside a scheduler-managed async coroutine (e.g. from an inline call or a registered function callback).
- `Time`: Get current Unix epoch in milliseconds.
- `Runtime`: Elapsed milliseconds as a number (with a fractional part): inside a scheduler coroutine, since that coroutine started; otherwise since the engine started.

### Error Handling

```lua
string, code GetLastError(opt lasterrorcode)
```
Retrieves the last error code as a message (UTF-8, localized by the OS, without trailing newline) and code. The default code is `GetLastError()` on Windows and `errno` on Linux (message from `strerror`).

### Shell

```lua
bool ShellExecute(file, parameter)   -- Windows only
```
Opens `file` (a path, URL or program) with its associated application; `file` and `parameter` are UTF-8.

### Memory

```lua
int GetMemory()
```
Returns memory in bytes used by Lua (a 64-bit integer, so values above 2 GB are correct).

### String Functions

```lua
bool string.equal(str1, str2)
```
Compares two strings ignoring ASCII case only (use `Text.Lower(a) == Text.Lower(b)` for other letters).

### Environment Variables

```lua
int setenv(var, value, opt override)   -- override defaults to false
string (or nil) getenv(var)
```
Names and values are UTF-8 and reach child processes unchanged. `getenv` returns `nil` when a variable is unset. `setenv` returns `0` on success; with `override` false an existing variable is left as it is. These use the Unicode environment on Windows in every host; Lua's `os.getenv` reads the same environment (also as UTF-8), so it sees values set with `setenv`.

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
- If `full=false`, returns the first IPv4 address, or `nil` when the name doesn't resolve or has no IPv4 address (for example `Dns("::1")`)
- `GetComputerName`: Retrieve the computer name (fully qualified DNS name on Windows; `gethostname` on Linux)

### Memory Status

```lua
int GlobalMemoryStatus(opt type)
```

**Type values** (sizes are KB on Windows, **MB** on Linux; on Linux 5/6 repeat 1/2):
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
nil Break()   -- no-op (reserved debugging hook)
nil Test()    -- no-op (reserved)
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

Read-only hardware sensor and system information module. On **Windows**, temperature and CPU load data come from PDH (`\Thermal Zone Information`, `\Processor` and `\Processor Information`); battery uses `GetSystemPowerStatus`. On **Linux**, all sensors are read from `/sys/class/hwmon/`, `/proc/`, and `/sys/class/power_supply/`.

> **Note:** On Windows, temperature data comes from the PDH `\Thermal Zone Information` counter (no admin rights required). Fan RPM and voltage sensors are not available on Windows without vendor drivers.

```lua
table or nil  Hardware.CpuTemp()
table         Hardware.CpuThreadsLoad()
number or nil Hardware.CpuLoad()
table or nil  Hardware.Memory()
string or nil Hardware.CpuName()
table or nil  Hardware.Battery()
table or nil  Hardware.GpuMemory()   -- Windows: table (may be empty); nil on Linux
table or nil  Hardware.GpuLoad()     -- Windows: table (may be empty); nil on Linux
table         Hardware.DiskIO()
table         Hardware.NetworkIO()
```

### Hardware.CpuTemp

```lua
table or nil Hardware.CpuTemp()
```

Returns `nil` when no valid sensors are found. The element type differs by platform:

- **Windows:** an array of tables, one per thermal zone, each with `Name` (string) and `Value` (°C, number). Tries `\Thermal Zone Information(*)\High Precision Temperature` first, then `\Temperature`. Both counters report tenths of Kelvin; zones below 200 K (uninitialised) are filtered out.
- **Linux:** an array of plain numbers (°C), read from `/sys/class/hwmon/` chips named `coretemp`, `k10temp`, `zenpower`, or `cpu_thermal`.

```lua
local temps = Hardware.CpuTemp()
if temps then
    for i, t in ipairs(temps) do
        if type(t) == "table" then   -- Windows
            print(string.format("%s: %.1f°C", t.Name, t.Value))
        else                         -- Linux
            print(string.format("sensor %d: %.1f°C", i, t))
        end
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

- **Windows:** Uses the CPUID instruction leaf `0x80000002–4`.
- **Linux:** Reads the `model name` field from `/proc/cpuinfo`.

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

**Windows only** — returns `nil` on Linux. On Windows it always returns a table, which is empty if the counters can't be read.

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

**Windows only** — returns `nil` on Linux. On Windows it always returns a table, which is empty if the counters can't be read.

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

### Hardware.DiskIO / Hardware.NetworkIO

```lua
table Hardware.DiskIO()
table Hardware.NetworkIO()
```

- `DiskIO` returns `{[disk] = {ReadBytesPerSec, WriteBytesPerSec, ActivePercent}}`; `ActivePercent` is Windows only.
- `NetworkIO` returns `{[adapter] = {RecvBytesPerSec, SendBytesPerSec, TotalBytesPerSec}}`.
- **Windows:** persistent PDH queries (rates per second); no sleep needed between calls.
- **Linux:** computed from `/proc/diskstats` and `/proc/net/dev` deltas. The values are **bytes since the previous call**, not per second, and the first call returns an empty table.

```lua
Hardware.NetworkIO()   -- prime the baseline (needed on Linux)
Sleep(1000)
for nic, io in pairs(Hardware.NetworkIO()) do
    print(nic, io.RecvBytesPerSec, io.SendBytesPerSec)
end
```

---

## Mutex

```lua
Mutex Mutex.Open(name)
-- or on failure:
nil, errorCode Mutex.Open(name)
bool Mutex:Lock(opt timeoutMs)
nil Mutex:Unlock()
islocked, name, internalid Mutex:Info()
```

| Function | Description |
|----------|-------------|
| `Open` | Opens/creates a named mutex (returns `nil, errorCode` on failure) |
| `Lock` | Lock the mutex. `timeoutMs` defaults to **0, a non-blocking try** that returns `false` if the mutex is held; a negative value waits forever; a positive value waits up to that many ms. Returns `true` on success/already-held, `false` on timeout |
| `Unlock` | Unlocks the mutex |
| `Info` | Get mutex information. `internalid` is the OS handle as an integer on Windows and always `0` on Linux |

---

## Redis

### Connection

```lua
Redis Redis.Open(host, opt port, opt useTls, opt timeout, opt sslOptions, opt password)
nil   Redis:Dispose()   -- close the connection (also done by the GC); returns true
```

`port` defaults to 6379 and `timeout` (the connect timeout, in **seconds**) to 10. `Open` raises an error on any failure (connect, TLS, AUTH); it never returns `nil`, so wrap it in `pcall` if the server may be down.

**SSL Options:**
- `cacert`, `capath`, `cert`, `privatekey`, `servername`
- `verifymode`: 0=none, 1=peer, 2=fail if no peer cert, 4=once, 8=handshake

### Commands

```lua
reply Redis:Command(command, arg, arg, arg, ...)
```

The reply is a table `{Type = int, Value = ...}`. For arrays, maps, sets and pushes, `Value` is an array of nested reply tables of the same shape. A server error **raises** a Lua error ("Redis error: ..."), so type 6 never comes back at the top level. BOOL replies (RESP3, e.g. after `HELLO 3`) come back with `Value` as a Lua boolean, DOUBLE replies as a number, INTEGER replies as an integer; STRING, STATUS, BIGNUM and VERB replies come back as strings.

```lua
local r = redis:Command("GET", "counter")
if r.Type == 1 then print(r.Value) elseif r.Type == 4 then print("missing") end
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

### RedisValue (hash, list, set, sorted set)

`GetHashset`, `GetList`, `GetSet` and `GetSortedSet` return a `RedisValue`. It has no methods; everything goes through indexing, assignment, `#`, `pairs` and calling it:

| Kind | Read | Write | `#v` | `pairs(v)` |
|------|------|-------|------|------------|
| Hash | `h[field]` → string or nil (HGET) | `h[field] = v` (HSET; tables are JSON-encoded), `h[field] = nil` (HDEL) | always 0 | field, value |
| List | `l[i]` 1-based, negative counts from the end; **`l[0]` pops (LPOP) and removes the item** | `l[i] = v` (LSET), or RPUSH/LPUSH when `i` is 0 or out of range | LLEN | — |
| Set | `s[member]` → boolean (SISMEMBER); `s[0]` random member; `s[-1]` pops (SPOP); `s[i]` (i ≥ 1) from an SSCAN snapshot | `s[member] = truthy` (SADD), `nil`/`false` (SREM) | SCARD | — |
| Sorted set | `z[i]` member at rank `i`; `z[member]` score as a string | `z[member] = number` (ZADD), any other value (ZREM) | always 0 | member, score |

`v()` returns the `RedisKey` and the Redis type as an integer.

### RedisStream

```lua
id          RedisStream:Add({field=value})
id, data    RedisStream:Read(opt id, opt blockMs)
int         RedisStream:Trim(maxlen_or_minid)
```

- `Add` returns the new entry id.
- `Read` returns the first message *after* `id` (default `"0-0"`) as `id, {field=value}`, waiting up to `blockMs` (default 0) for one; it returns **no values** when there is none.
- `Trim(n)` trims to `n` entries (MAXLEN) given an integer, or removes entries older than an id (MINID) given a string; returns the number removed.
- `stream()` returns the `RedisKey`.

### RedisKey

```lua
bool RedisKey:Delete()
string RedisKey:Type()
int RedisKey:GetTTL()     -- nil if the key doesn't exist, -1 if it has no TTL
bool RedisKey:SetTTL(ms)  -- ms <= 0 removes the TTL (PERSIST)
```

### RedisString

```lua
int    RedisString:GetTTL()
bool   RedisString:SetTTL(ms)
string RedisString:Set(newValue)      -- returns old value, or nil if key was new
string RedisString:GetOrSet(newValue) -- alias: GetSet; returns the existing value without
                                      -- overwriting it, or sets and returns newValue if the key is missing
string RedisString:Delete()           -- returns value before deletion
byte   RedisString:At(n)             -- byte (0-255) at 1-based position; nil if out of range
length RedisString:len()
```

Metamethod shortcuts:
- `#str` — same as `len()`
- `str[n]` — same as `At(n)` (read); bytes are returned as 0-255
- `str[n] = byte` — writes byte via `SETRANGE`
- `str1 .. str2` — concatenates, returning a Lua string
- `pairs(str)` — iterates bytes as `(position, byte)` pairs
- `tostring(str)` — the current value (GET), or `""` if the key is missing
- `str()` — the `RedisKey`

### Iterator Example

Iterating the connection yields every key as a `RedisKey` userdata (not a string):

```lua
for key in redis do
    print(tostring(key))
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
| `true` (no extra values) | No message yet, or a subscribe/unsubscribe acknowledgement — resume again later (polling never blocks) |
| `true, nil, errmsg` | Connection error; coroutine is now dead |

A dead `AliveToken` (see below) ends the coroutine the same way as resuming with `true`.

#### Coroutine methods

```lua
co:SetAliveToken(token)  -- attach an AliveToken; when disposed the coroutine unsubscribes and dies cleanly (same as resuming with true). Pass nil to detach
```

```lua
-- Subscribe example
local co = assert(redis:Subscribe('news', 'alerts'))
while coroutine.status(co) == 'suspended' do
    local ok, ch, msg = coroutine.resume(co)
    if ch then print(ch, msg) else Sleep(10) end   -- don't busy-spin while idle
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
iterator   json:Pairs()       -- same as pairs(json)
```

Path segments named after a method (`Get`, `Set`, `Delete`, `Type`, `Length`, `Pairs`) resolve to the method, not the JSON field: `j.Type` is the method. Avoid those field names, or read the parent with `Get()` and index the result.

Metamethod shortcuts:
- `json.field` — descends into object field (path chaining, returns new `RedisJson`)
- `json[n]` — descends into array element at 1-based index `n` (0 raises an error)
- `json.field = value` / `json[n] = value` — calls `Set`
- `#json` — calls `Length`
- `tostring(json)` — shows key and accumulated path
- `json()` — `__call`: same as `json:Get()`
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

Every operation is available in two forms: as a module-level function that takes an optional delimiter as its last argument, or as a method on a CSV object created with `CSV.New(delimiter)` that uses the object's bound delimiter.

```lua
-- Module-level forms: delimiter defaults to ","
table   CSV.Decode(str [, delimiter])
string  CSV.Encode(rows [, delimiter])
iter    CSV.DecodeFromFunction(fn_or_stream [, delimiter])

-- CSV object forms: delimiter bound by New (defaults to auto-detect)
object  CSV.New([delimiter])
table   csv:Decode(str)
string  csv:Encode(rows)
iter    csv:DecodeFromFunction(fn_or_stream)
```

| Function | Description |
|----------|-------------|
| `New` | Return a CSV object with a bound delimiter (or auto-detect when omitted) |
| `Decode` | Decode a complete CSV string into a result table |
| `Encode` | Encode an array-of-arrays into a UTF-8 CSV string |
| `DecodeFromFunction` | Return a generic-`for` iterator that streams rows from a supplier function or a readable `Stream` |

The methods on a CSV object take no delimiter argument (an extra argument is silently ignored); use the module-level form or another `CSV.New(delimiter)` object for a different delimiter. `delimiter` accepts:
- A single-character string: `","` `";"` `"|"` `"\t"`
- An integer codepoint: `string.byte(";")` → `59`
- The string `"auto"` or boolean `true` to trigger automatic delimiter detection (`Encode` always writes `","` in that case)
- `false` for `","`
- Omitting it (or passing `nil`) means `","` for the module-level functions and `"auto"` for `CSV.New`

```lua
local t = CSV.Decode("a,b\n1,2")            -- t.Rows[2][2] == "2"
local t = CSV.Decode("a;b\n1;2", ";")       -- explicit delimiter
local t = CSV.Decode("a;b\n1;2", "auto")    -- sniff the delimiter
local s = CSV.Encode({{"a", "b"}}, "|")     -- "a|b"
for row in CSV.DecodeFromFunction(fn, ";") do ... end
```

### csv:Decode

```lua
table csv:Decode(str)
table CSV.Decode(str [, delimiter])
```

Decodes a complete CSV string. Returns a table with two keys:

| Key | Type | Description |
|-----|------|-------------|
| `Comments` | array of strings | Lines beginning with `*` at the top of the file, with the leading `*` stripped |
| `Rows` | array of arrays | Each inner array is one row; each field is a UTF-8 string |

Fields are always plain UTF-8 Lua strings, including fields with non-ASCII text. Use `tonumber(field)` for numeric columns.

Input must be UTF-8. A leading UTF-8 BOM (as in Excel's "CSV UTF-8" export) is skipped for every source type, and a character split across chunks from `DecodeFromFunction` is reassembled. For a file in a legacy code page (Excel's plain "CSV" export is in the system ANSI code page, e.g. 1252), convert it first: `csv:Decode(Text.FromCodepage(data, 1252))`. `Encode` writes UTF-8 without a BOM; prepend `"\xEF\xBB\xBF"` if the file will be opened in Excel.

Leading spaces and tabs before each field are stripped. Quoted fields follow RFC 4180: `""` inside a quoted field becomes a literal `"`.

```lua
local t = CSV.New(","):Decode("* header\na,b,c\n1,2,3")
-- t.Comments[1] == " header"
-- t.Rows[1][1]  == "a"
-- t.Rows[2][3]  == "3"

local t = CSV.New():Decode("a;b;c\n1;2;3")   -- auto-detect: sniffer finds ";"
local t = CSV.New(";"):Decode("a;b;c")       -- explicit delimiter
local t = CSV.Decode("a;b;c", ";")           -- same, module-level form
```

> **Memory note:** `Decode` converts the entire input to an internal wide-character buffer before parsing begins. A UTF-8 string of N bytes requires approximately 2×N bytes of additional heap memory for the conversion (4×N on Linux). For multi-megabyte files, use `csv:DecodeFromFunction` with a `Stream` or a chunked supplier function so that peak memory stays bounded to the chunk size rather than the whole file.

### csv:Encode

```lua
string csv:Encode(rows)
string CSV.Encode(rows [, delimiter])
```

Encodes an array-of-arrays into a UTF-8 CSV string. Each field is converted via `tostring`. Fields containing the delimiter, a double-quote, a newline, or **leading whitespace** are wrapped in double-quotes with inner quotes escaped as `""` (RFC 4180). Rows are joined with `\n`.

```lua
local csv = CSV.New(",")
local s = csv:Encode({{"hello", "world"}, {"foo", "bar"}})
-- s == "hello,world\nfoo,bar"

local s = csv:Encode({{"value with, comma"}})
-- s == '"value with, comma"'

local s = CSV.New(";"):Encode({{"a", "b"}})  -- semicolon delimiter
-- s == "a;b"
```

### csv:DecodeFromFunction

```lua
iterator csv:DecodeFromFunction(fn_or_stream)
iterator CSV.DecodeFromFunction(fn_or_stream [, delimiter])
```

Returns a generic `for` iterator. With a function, on each iteration the supplier `fn` is called with no arguments and should return a chunk of CSV data as a string; the iterator stops when `fn` returns `nil`, `false`, or an empty string. With a readable `Stream`, chunks are read with `stream:Read()` until it is exhausted. Each iteration yields one row as a sequential table of UTF-8 string fields.

The parser handles chunk boundaries that fall in the middle of a field or row transparently — no alignment of chunks to row boundaries is required.

When `delimiter` is `"auto"` (or omitted on a `CSV.New()` object), the sniffer runs once on the first chunk and is not called again.

```lua
-- Stream a large file in 4 KB chunks
local f = io.open("data.csv", "r")
for row in CSV.New(","):DecodeFromFunction(function() return f:read(4096) end) do
    print(row[1], row[2])
end
f:close()

-- Auto-detect delimiter from the stream
for row in CSV.New():DecodeFromFunction(Stream.Open("data.csv")) do ... end
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
| **Empty input** | `csv:Decode("")` produces `{Rows={}}` — zero rows, empty `Rows` table |
| **Trailing newline** | A single trailing `\n` does **not** create an extra row (the newline is consumed as the end-of-row sentinel) |
| **Double trailing newline** | `"a,b\n\n"` produces a second empty row `[""]` |
| **Leading whitespace in unquoted fields** | Spaces and tabs before a field value are stripped during decode. `Encode` quotes fields with leading whitespace to preserve round-trip fidelity |
| **Trailing whitespace in unquoted fields** | Preserved as-is; only *leading* whitespace is stripped |
| **Unquoted field containing `"`** | Treated leniently: the `"` turns on quote-mode mid-field. `hel"lo"world` → `helloworld` |
| **Multi-character delimiter** | Only the first character is used; `CSV.New("||")` behaves as `|` |
| **Non-ASCII delimiter** | Matched at the byte level in `Encode`; works correctly for all printable ASCII delimiters (`,` `;` `|` `\t` etc.) |
| **`"` as delimiter** | Not supported; the parser uses `"` as the quoting character |
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

`conf` is an optional table of librdkafka configuration key/value pairs (values must be strings or numbers; an unknown or invalid key raises an error).  
Default `group.id` values: `"LUAP"` (producer), `"LUAC"` (consumer). Both constructors return `nil, errmsg` if the client can't be created.

---

### KafkaProducer

#### Producing

```lua
bool, errmsg  producer:Send(topic, key, value [, headers [, partition]])
```

- `key` — may be `nil` for keyless messages
- `value` — required string
- `headers` — optional table of string key/value pairs: `{source='app', version='1'}`
- `partition` — optional integer; omit (or pass `nil`) for automatic partitioning
- `true` means the message was **queued** for sending, not that the broker received it

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
nil  producer:Close()
```

`Close()` returns nothing. Afterwards every other producer method raises "Producer not open".

---

### KafkaConsumer

The offsets/metadata and group-admin methods available on `KafkaProducer` (`GetOffsets`, `GetMetadata`, `GetTopicConfig`, `SetTopicConfig`, `ListGroups`, `DescribeGroups`, `DeleteGroup`, `GetGroupOffsets`, `SetGroupOffsets`, `DeleteGroupOffsets`) are also available on `KafkaConsumer` with identical signatures. `CreateTopic` and `DestroyTopic` are **producer only**.

#### Subscribing and assigning

```lua
bool, errmsg  consumer:Subscribe({'topic', ...})
bool, errmsg  consumer:Assign({'topic:partition[:offset]', ...})
```

Both methods apply the subscription or assignment immediately and return `true` on success, or `false, errmsg` on failure. Use `assert` to surface errors:

```lua
assert(consumer:Subscribe({'my-topic'}))
assert(consumer:Assign({'my-topic:0:earliest'}))
```

`Subscribe` uses Kafka's consumer-group rebalance protocol (supply `group.id` in conf). `Assign` pins specific partitions directly without a group coordinator.

**Offset keywords for `Assign`:**

| Entry format | librdkafka offset | Behaviour |
|---|---|---|
| `"topic:N"` or `"topic:N:stored"` | `OFFSET_STORED` | Uses committed offset; falls back to `auto.offset.reset` |
| `"topic:N:earliest"` or `"topic:N:beginning"` | `OFFSET_BEGINNING` | Always starts from message 0 |
| `"topic:N:latest"` or `"topic:N:end"` | `OFFSET_END` | Starts after the current last message |
| `"topic:N:123"` | `123` | Starts from exact offset 123 |

#### Polling

```lua
bool, msg  consumer:Poll()
```

Non-blocking. Returns one of three states:

- `false, errmsg` — consumer is in an unrecoverable error state; raise or handle the error.
- `true, msg` — a message or status event was received; inspect `msg.ErrorCode` (0 = real data message).
- `true` *(nil second value)* — nothing available right now; call `Yield()` or `Sleep()` before the next poll.

Offsets are committed automatically. Each call to `Poll()` commits the offset from the previous polled message (if any) before fetching the next one. `auto.commit` is forced off in the librdkafka config to keep commit timing under explicit control.

```lua
assert(consumer:Subscribe({'my-topic'}))
while running do
    local ok, msg = consumer:Poll()
    if not ok then
        error(msg)
    elseif msg then
        if msg.ErrorCode == 0 then
            -- real message
            print(msg.Value)
        else
            -- status / error event surfaced by librdkafka
            print('kafka event:', msg.Error)
        end
    else
        Yield()
    end
end
consumer:Close()
```

**Message table fields:**

| Field | Type | Description |
|-------|------|-------------|
| `Value` | string | Message payload |
| `Key` | string or nil | Message key |
| `Topic` | string | Topic name |
| `Partition` | number | Partition index |
| `Offset` | number | Offset within the partition |
| `Timestamp` | number | Message timestamp (ms) |
| `Latency` | number | Producer-to-broker latency (ms) |
| `ErrorCode` | number | librdkafka error code (0 = success) |
| `Error` | string | Error description |
| `Headers` | table | Key/value header table |

#### Seeking

```lua
bool, errmsg  consumer:Seek(topic, partition, offset [, timeout_ms])
```

Repositions an already-assigned partition. `offset` accepts a number or the keywords `"earliest"`/`"beginning"`, `"latest"`/`"end"`, `"stored"`. `timeout_ms` defaults to 5000. Uses `rd_kafka_seek_partitions` internally.

```lua
nil  consumer:Close()
```

`Close()` returns nothing. Afterwards `Poll()` returns `false, "Consumer not open"` and the other methods raise "Consumer not open".

---

### Module-level utility

```lua
string  Kafka.Logs([filename])
```

Returns the librdkafka log output collected since the previous call and **clears** it (the buffer holds about the last 10 KB). `filename` does not save that text: it opens a log file (in append mode) that **future** log lines are also written to; `""` closes it.

---

## Archive

```lua
Archive Archive.OpenRead(filename)        -- nil, errmsg on failure
array   Archive:Entries()                 -- nil, errmsg on read errors
name, size Archive:SetEntry(index)        -- 1-based; nil, "EOF" past the end; nil, errmsg on error
data    Archive:Read(opt maxBytes)        -- default 1024; nil at the end of the entry
string  Archive:ReadAll()
```

**Entries returns:** Array of tables with `Name` and `Size`. `Read` and `ReadAll` raise an error if no entry has been selected with `SetEntry`.

- **Entry names** (`Entries()[i].Name` and the first return value of `SetEntry`) are always UTF-8 strings, including non-ASCII names.

- **`ReadAll`** — reads the entire current entry into a single Lua string in one call. More convenient than looping with `Read` for entries that must be consumed completely.

- **`SetEntry` reopens the archive file.** If the file can no longer be opened (deleted, renamed, locked) it returns `nil, errmsg` and no entry is selected; the `Archive` object stays safe to use and to garbage-collect.

---

## Stream

### Creation

```lua
Stream Stream.New(opt string)
Stream Stream.New(backendfunction)
Stream Stream.Open(filename, opt mode)
```

- **No argument** — creates a new empty in-memory stream.
- **String argument** — creates an in-memory stream pre-loaded with the string contents, with the position reset to 0.
- **Function argument** — creates a stream backed by the provided Lua function. The function is called with an opcode as its first argument and must handle all `STREAM_OP_*` operations it wishes to support. It must return the capability bitmask when called with `STREAM_OP_OPEN` (0); a non-number or 0 raises "Backend function failed to open".
- **`Open(filename, opt mode)`** — opens a file as a stream; `mode` defaults to `"rb"`. `filename` is UTF-8 on every platform (non-ASCII paths work on Windows). Raises "Stream.Open: cannot open '<file>' (<error>)" if the file can't be opened.

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
| 1 | `STREAM_CAP_READ` | `Read`, `ReadByte`, `ReadUtf8`, typed reads, `len` (a readable function backend must handle `STREAM_OP_LEN` if `len()` is called), `Compress`/`Decompress` source |
| 2 | `STREAM_CAP_WRITE` | `Write`, `WriteByte`, `WriteUtf8`, typed writes, `Compress`/`Decompress` destination |
| 4 | `STREAM_CAP_SEEK` | `Seek`, `pos`, `SetByte` with position, `PeekByte` (requires both `CAP_READ` and `CAP_SEEK`) |

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
bool        Stream:WriteByte(byte)      -- false if byte isn't 0-255 or the stream isn't writable
int         Stream:ReadByte()           -- -1 at EOF or when not readable
int         Stream:PeekByte(opt pos)    -- -1 at EOF, when not readable, or without CAP_SEEK
void        Stream:SetByte(byte, opt position)
int         Stream:Write(value, opt size)
bool        Stream:WriteUtf8(str)
string, int Stream:ReadUtf8()
int         Stream:WriteUtf16(str)
string      Stream:ReadUtf16(opt n)
string      Stream:Read(opt length)
bool/int    Stream:HasData()
int         Stream:Id()
nil         Stream:Close()
```

- **`Write`** accepts a `string` (written as raw bytes), `number` (always an 8-byte `double`, integers included), `boolean` (1 byte), or one of the typed userdata listed under [Custom-type Reads](#custom-type-reads); other userdata write 0 bytes. The optional `size` argument limits the number of bytes written. Returns the number of bytes written, or `0` on failure.
- **`WriteUtf8`** converts a Lua string from Latin-1/byte values to proper UTF-8 before writing.
- **`WriteUtf16`** encodes a UTF-8 string as UTF-16 LE (2 bytes per code unit, 4 for characters outside the BMP, no BOM) and writes it. Returns the number of bytes written, or `0` if the stream is not writable.
- **`ReadUtf16`** reads `n` UTF-16 LE code units (2 bytes each) from the current position and returns them decoded as a UTF-8 string. If `n` is omitted or `nil`, reads all remaining bytes. Returns `nil` if the stream is not readable or no complete code unit is available. Unpaired surrogates decode to U+FFFD.
- **`HasData`** — non-blocking availability check. For sync (seekable) streams returns the number of bytes remaining as an integer when more than 1 byte remains, `true` when exactly 1 byte remains, `false` at EOF, and `-1` if the stream was closed. For async streams (vtable with `hasdata`) returns `true` if data is ready in the buffer, `false` if nothing is available yet (more may arrive later — `false` is **not** EOF for async streams). For fn backends dispatches `STREAM_OP_HASDATA`; returns `nil`/`false` if the backend has no handler. **Never yields.**
- **`Id`** — returns a stable integer identity value for this stream, suitable for use as a cache key or for distinguishing two stream references. Calls the backend's `getid` if available; otherwise falls back to the native pointer value.
- **`Close`** — explicitly frees the stream's resources and marks it unusable. Called automatically by the GC; safe to call early when resources should be released promptly.

### Stream Info

```lua
capsTable, backendInfo Stream:GetInfo()
length Stream:len()    -- nil without CAP_READ
pos Stream:pos()       -- nil without CAP_SEEK
bool Stream:Seek(pos)  -- false if the stream isn't seekable
```

`GetInfo()` returns two values for sync streams:
- `capsTable` — `{ Caps = number }` where `Caps` is the capability bitmask (`STREAM_CAP_*` flags)
- `backendInfo` — backend-defined; in-memory streams: `{ pos, len, alloc, type = "memory" }`; file streams: `{ pos, len, mode, name, type = "file" }`

Async streams (such as HTTP response streams) return a single backend table instead, may yield, and have no caps table.

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
- If `deststream` is provided it is written to **at its current position** and returned as-is (no automatic seek). Exception: `Decompress` with an async source ignores `deststream` and always returns a new in-memory stream.
- A non-readable source or non-writable destination returns `nil, errmsg`. Internal failures **raise** errors instead: "compression failed (N)", "truncated compressed stream", "decompression failed (N)", "out of memory".

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
bool Stream:WriteFloat(value) / number Stream:ReadFloat()
bool Stream:WriteDouble(value) / number Stream:ReadDouble()
bool Stream:WriteShort(value) / int Stream:ReadShort()
bool Stream:WriteUnsignedShort(value) / int Stream:ReadUnsignedShort()
bool Stream:WriteInt(value) / int Stream:ReadInt()
bool Stream:WriteUnsignedInt(value) / int Stream:ReadUnsignedInt()
bool Stream:WriteLong(value) / int Stream:ReadLong()
bool Stream:WriteUnsignedLong(value) / int-or-UInt Stream:ReadUnsignedLong()   -- UInt userdata when the value is > 2^63 - 1
int Stream:WriteUtf16(str) / string Stream:ReadUtf16(opt n)
```

`WriteUnsignedLong` takes a Lua integer, so a `UInt` returned by `ReadUnsignedLong` can't be written back with it; use `stream:Write(uint)` for that.

### Custom-type Reads

Custom userdata types can be written with `Stream:Write(value)` and read back with dedicated typed-read functions. All reads return `nil` on a short read or non-readable stream.

```lua
UInt       Stream:ReadUInt64()      -- reads 8 bytes (uint64, native endian)
Decimal    Stream:ReadDecimal()     -- reads decimal text up to a NUL, "\n" or EOF (max 63 chars)
Identifier Stream:ReadIdentifier()  -- reads 16 bytes (UUID raw bytes)
DateTime   Stream:ReadDateTime()    -- reads 10 bytes (int64 ticks + int16 offset_minutes)
TimeSpan   Stream:ReadTimeSpan()    -- reads 8 bytes (int64 ticks)
```

**`Write` wire formats for custom types:**

| Type | Bytes written | Format |
|------|--------------|--------|
| `UInt` | 8 | `uint64_t`, native endian |
| `Decimal` | variable | the canonical decimal text (e.g. `"12.5"`, 4 bytes), no terminator; write a `"\0"` or `"\n"` after it if more data follows, so `ReadDecimal` knows where it ends |
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

- `Decode("")` returns `""`. `Decode` returns `nil` if the input length isn't a multiple of 4. It doesn't validate characters or skip whitespace, so strip line breaks first.
- `SetEncodeTable` raises unless given exactly 64 unique bytes, and it changes the table for everything in the process that uses `Base64`.

---

## Aes

```lua
Aes Aes.New(key, opt iv, opt usectr)
data Aes:Encrypt(data)
data Aes:Decrypt(data)
nil Aes:SetIV(opt iv)
```

Creates an AES-256 context:

- `key` is up to 32 bytes; shorter keys are zero-padded, longer ones raise "Key length must be 32 bytes".
- Without an IV (`nil` or `""`) the mode is **ECB** and `usectr` is ignored. With an IV (up to 16 bytes, zero-padded) the mode is **CBC**, or **CTR** when `usectr == true`.
- `Encrypt` always applies PKCS#7 padding (CTR included). `Decrypt` raises if the length isn't a multiple of 16, or "Invalid aes padding".
- In CBC and CTR the context chains across calls, so call `SetIV()` to start a new message. `SetIV(nil)` (or `""`) restores the original IV; it does nothing in ECB. `SetIV(iv)` takes up to 16 bytes and zero-pads shorter IVs the same way `Aes.New` does; longer ones raise "IV length must be 16 bytes".

---

## Process

```lua
table Process.All()                  -- {[pid] = name}, or nil on failure
Process Process.Open(opt id)          -- no id or 0: the current process; nil on failure
Process Process.Start(app, cmd, directory, noconsole, opt redirect)   -- nil, errmsg on failure
string Process:ReadFromPipe(opt buffersize)
int Process:WriteToPipe(data)        -- bytes written, or -1
string Process:ReadErrorFromPipe(opt buffersize)
bool Process:Stop(opt exitcode)
int/nil Process:GetExitCode()
int Process:GetID()
string Process:GetName()
number Process:GetCPU()              -- Windows only
number Process:GetRAM()
int/bool Process:Priority(opt prio)  -- Windows only
int, int Process:Affinity(opt newmask) -- Windows only
array Process:Threads()              -- Windows only
```

- `app`, `cmd` and `directory` are UTF-8, so non-ASCII paths and arguments work. When `directory` is `nil` the child starts in the current directory.
- `redirect` is `true` (redirect stdin, stdout and stderr) or a bitmask: 1 = stdin, 2 = stdout, 4 = stderr. `noconsole` is Windows only.
- On Linux the command runs as `/bin/sh -c <cmd or app>`, and a `directory` becomes `cd '<dir>' && …`.
- `ReadFromPipe` / `ReadErrorFromPipe` don't block: they return `nil` when nothing is available. The default buffer is 1 MB, and one read returns at most `buffersize - 1` bytes.
- `Stop(exitcode)` terminates the process with that exit code on Windows; Linux sends SIGTERM.
- `Priority()` with no argument returns the current priority class (for example `32` = `NORMAL_PRIORITY_CLASS`), or `nil, errmsg` on failure; with an argument it sets the class and returns a boolean.
- `Affinity` returns the process and system masks from *before* any change, or `nil, errmsg` on failure. Masks use the full pointer width, so bits above 31 work in 64-bit builds.
- `Threads()` returns `{ {ID, BasePrio, DeltaPrio}, … }` or `nil, errmsg`.
- Names from `Process.All()` and `GetName()` are UTF-8.
- **Pipe output is the child's raw bytes.** Many Windows console programs write in the OEM or ANSI code page rather than UTF-8. Convert with `Text.FromCodepage(out, 850)` (or the relevant code page) when the output isn't UTF-8.

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
| `SetAliveToken(token)` | Attach an `AliveToken` to this client. While the token is alive requests proceed normally. If the token is already dead when a request starts, `Request` returns `nil, "aborted"` (no coroutine is created) and `Call` returns `nil, "aborted"`. If it dies while a `Request` is in flight, the request coroutine finishes with **no values**. Pass `nil` to detach |
| `GetTimestamp` | Returns the round-trip duration of the most recently **completed** `Request()` call as a `TimeSpan`. The clock starts just before the request is submitted to curl and stops when the last response byte is received. Returns a zero `TimeSpan` if no request has completed yet on this client |

### Buffered request

```lua
coroutine, errmsg client:Request(method, url, opt body, opt headers, opt outStream)
```

Returns a coroutine immediately. Drive it with `coroutine.resume` until it is dead (`coroutine.status(co) == "dead"`); its last results are the result table. `body` is an optional string, or a native `Stream` (not a Lua-function stream) for a streaming upload. `headers` is an optional per-request header table. `outStream` is an optional writable `Stream`; when provided the response body is written there and `Contents` in the result is `nil`.

### Simple blocking call

```lua
result        = client:Call(method, url [, headers [, body]])
nil, errmsg   = client:Call(...)   -- on transport failure
```

Drives the request to completion internally, yielding the outer coroutine cooperatively on each poll. Returns the same result table as `Request` on success, or `nil, errmsg` on transport failure, where `errmsg` is curl's error text (e.g. `"Operation timed out after 5000 milliseconds ..."`, `"Could not resolve host: ..."`).

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
    print("failed:", err)   -- curl error text, e.g. "Could not resolve host: ..."
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
| `Contents` | string or nil | Response body (absent on a transport error) |
| `Headers` | table or nil | Response headers keyed by header name (absent on a transport error) |

### Streaming request

```lua
Stream-or-coroutine, errmsg client:Stream(method, url, opt body, opt headers)
```

Returns **either** the response body `Stream` directly (when the headers arrive on the first internal poll) **or** a coroutine. Drive a coroutine with `coroutine.resume` until it finishes; it returns the `Stream`, or `nil, errmsg` on failure. Always check the type of the first result and handle `nil, errmsg`. Call `stream:GetInfo()` for metadata, then `stream:Read()` in a loop to receive body chunks. Must be driven from inside a coroutine. A `Stream` request body isn't supported here (`nil, "stream body not supported..."`).

```lua
-- Inside a coroutine:
local function openStream(client, method, url)
    local r, err = client:Stream(method, url)
    if type(r) ~= 'thread' then return r, err end      -- Stream or nil, err
    local ok, stream, err2
    repeat ok, stream, err2 = coroutine.resume(r) until coroutine.status(r) == 'dead'
    if not ok then return nil, stream end
    return stream, err2
end

local stream, err = openStream(client, 'GET', 'https://example.com/feed')
if not stream then error(err) end
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
WebSocket, errmsg client:Connect(url, opt headers)
```

Must be called from inside a coroutine or task: it **yields the calling coroutine** until the HTTP 101 upgrade completes, then returns the live `WebSocket` (it does not return a coroutine). On failure it returns `nil, errmsg` (`"aborted"` or curl's error text). The client's `SetTimeout` applies as the connect timeout. See the [WebSocket](#websocket) section for the full API on the returned object.

### Examples

```lua
-- Buffered GET
local client = HttpClient.New()
client:SetTimeout(8000)
local co = client:Request('GET', 'https://httpbin.org/get')
local ok, result
repeat ok, result = coroutine.resume(co) until coroutine.status(co) == 'dead'
if ok and result then print(result.Code, result.Contents) end

-- Streaming GET (must run inside a coroutine): see openStream above
local stream = assert(openStream(client, 'GET', 'https://httpbin.org/get'))
local info = stream:GetInfo()
local chunk = stream:Read()
while chunk do io.write(chunk); chunk = stream:Read() end
stream:Close()

-- WebSocket echo (must run inside a coroutine)
local ws, err = client:Connect('wss://echo.websocket.org')
if not ws then error(err) end
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
HttpServer, errmsg  HttpServer.Listen(address, opt options)
```

Binds to `address`: `"host:port"` (e.g. `"0.0.0.0:8080"`, `"127.0.0.1:9000"`), `":port"`, or an `http://` / `https://` URL. Returns the server on success, or `nil, errmsg` on failure. TLS isn't supported: passing `options.cert` returns `nil, "TLS not supported..."`.

```lua
local server = assert(HttpServer.Listen("0.0.0.0:8080"))
```

### Coroutine pump

```lua
coroutine  server:Accept()
```

Returns a coroutine (the same one on repeated calls — idempotent). Drive it with `coroutine.resume`:

- `coroutine.resume(co)` — polls the server, advances any active stream senders, and yields one pending `HttpRequest` when available. Returns `true, HttpRequest` when a request is ready, or `true` with no second value when idle.
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
| `SetOnDisconnect` | Register a `function(req)` called when each response finishes writing (so once per request under keep-alive), or when the TCP connection closes before a response was sent |
| `SetAliveToken` | Attach an `AliveToken` to this server. When the token is disposed the `Accept()` coroutine tears down the server and dies cleanly — identical to `coroutine.resume(co, true)`. Pass `nil` to detach |
| `Close` | Tear down the server immediately. Idempotent — safe to call more than once. `__gc` calls this automatically |

---

### HttpRequest

A new `HttpRequest` object is created for every HTTP request and queued to the `Accept()` coroutine. Requests are only dispatched once complete, so `IsFinished()` is always `true` for them.

```lua
string   req:GetUrl()        -- full path + query string, e.g. "/api/items?id=1"
string   req:GetMethod()     -- HTTP verb: "GET", "POST", "PUT", "DELETE", …
string   req:GetBody()       -- request body (empty string when none)
table    req:GetHeaders()    -- lowercase header names → values
string   req:GetIp()         -- remote address + port, e.g. "127.0.0.1:54321"; "" after the response is sent
integer  req:GetId()         -- identity of this request; 0 after the response is sent
bool     req:IsFinished()    -- true once headers and body have been fully received
table    req:GetContext()    -- Lua table for this request; created lazily
HttpResponse req:GetResponse() -- returns the paired response object
string   req:GetError()      -- currently always nil
```

Read `GetIp()` / `GetId()` **before** sending the response if you need them (for example in logging or in `SetOnDisconnect`).

---

### HttpResponse

```lua
nil        resp:SetCode(code)
nil        resp:SetHeader(name, value)
bool       resp:Send(opt body)
nil        resp:Reject(code, message)
nil        resp:Close()
WebSocket  resp:UpgradeToWebSocket()   -- nil, errmsg on failure
```

| Method | Description |
|--------|-------------|
| `SetCode(code)` | Override the HTTP status code. Default: `200`. The status line gets the standard reason phrase for the code (`404 Not Found`, `500 Internal Server Error`, …); codes without a standard phrase get their class name (e.g. `499 Client Error`) |
| `SetHeader(name, value)` | Add a response header. May be called multiple times |
| `Send(opt body)` | Send the response. `body` may be omitted (no body), a `string`, or a readable `Stream`; other types raise an error. Returns `false` when the request is not yet finished |
| `Reject(code, message)` | Send a minimal error response with the given status code and plain-text body, with `Connection: close`, and the standard reason phrase for the code. Returns nothing |
| `Close()` | Send an empty 200 response with `Connection: close` |
| `UpgradeToWebSocket()` | Upgrade the HTTP connection to a WebSocket session. Sends HTTP 101 immediately and returns a `WebSocket` userdata, or `nil, errmsg`. The `HttpRequest` and `HttpResponse` objects must not be used after this call. See the [WebSocket](#websocket) section for the full API |

A response can only be finalized once: calling `Send` again, or `SetCode` / `SetHeader` after it, raises "HttpResponse: response already finalized" (or "connection is no longer alive").

#### Stream responses

When `body` is a `Stream`, the response is **always sent chunked** (`Transfer-Encoding: chunked`), seekable or not; `Content-Length` is not taken from `stream:len()`. The coroutine pump reads 64 KB per iteration until the stream returns empty or `nil`. For a known length, send a string, or set `Content-Length` yourself with `SetHeader`.

```lua
-- Function-backed stream → chunked
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

-- In-memory stream → also chunked
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
local ips = {}
server:SetOnDisconnect(function(req)
    print('done', ips[req])   -- req:GetIp() is "" once the response has been sent
    ips[req] = nil
end)
local co = server:Accept()
while coroutine.status(co) == 'suspended' do
    local ok, req = coroutine.resume(co)
    if req and req:IsFinished() then
        ips[req] = req:GetIp()
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
        -- Stream bodies are always sent with Transfer-Encoding: chunked
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
bool              ws:Ping()                    -- send a ping; false if closed
bool              ws:IsConnected()             -- true while the connection is open
integer           ws:GetId()                  -- stable non-zero integer identity
table             ws:GetContext()             -- per-connection Lua table, created lazily
nil               ws:SetMaxMessageSize(bytes) -- cap incoming message size (0 = uncapped)
nil               ws:Dispose()               -- close the connection and free resources
```

| Method | Description |
|--------|-------------|
| `Poll()` | Non-blocking. Dequeues the next `WebSocketMessage` from the internal queue, or returns `nil` if none is ready. Never yields. For client connections it also advances the network layer; server connections are driven by the `Accept()` pump, which must keep running. |
| `Read()` | Yields the current coroutine until a `WebSocketMessage` is available, then returns it (must be called inside a coroutine). Returns `nil` as soon as the connection is marked closed, without handing over a queued Close (8) message. |
| `Send(data, opt binary)` | Send `data` (string) as a WebSocket frame. Pass `true` as the second argument to send a binary frame; default is a text frame. Works on client and server connections. Returns `false` if the connection is closed or the payload is larger than `SetMaxMessageSize`. |
| `Ping()` | Send a ping frame. Returns `false` if the connection is closed. |
| `IsConnected()` | Returns `true` while the underlying connection is open. |
| `GetId()` | Returns a stable non-zero integer that uniquely identifies this connection for its lifetime. |
| `GetContext()` | Returns a per-connection Lua table. Created lazily on first call; persists for the lifetime of the connection. Use it to store per-connection state. |
| `SetMaxMessageSize(bytes)` | Set the maximum message size in bytes. Incoming messages exceeding the cap are dropped, and `Send` refuses larger payloads. `0` disables the cap (default). |
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
| `9` | Ping (client connections only) |
| `10` | Pong (client connections only) |

Server connections only queue text and binary messages.

### Client WebSocket example

```lua
-- Must run inside a coroutine/task: client:Connect() yields until connected
local client = HttpClient.New()
client:SetVerifySSL(false)
local ws, err = client:Connect('wss://echo.websocket.org')
if not ws then error(err) end

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

## TCP

A raw TCP listener/client module backed by [libevent](https://libevent.org/). The module is always available on platforms that include HTTP support (libevent is shared with `HttpServer` and `WebSocket`). Networking is non-blocking — `Accept()` and `Poll()` are polling calls that return immediately; use `Sleep()` between calls to yield cooperatively and let other coroutines run.

### Creation

```lua
TcpListener          TCP.StartListener(port)
nil, errmsg          TCP.StartListener(port)   -- on failure
TcpClient            TCP.Connect(host, port)
nil, errmsg          TCP.Connect(host, port)   -- on failure
```

| Function | Description |
|----------|-------------|
| `StartListener` | Bind a TCP listener on `port` (integer, 1–65535) on all IPv4 interfaces. Returns a `TcpListener` on success, or `nil, errmsg` on failure |
| `Connect` | Initiate a non-blocking TCP connection to `host:port`. Returns a `TcpClient` immediately — the connection may still be in progress. Poll `client:IsConnected()` to wait for it |

---

### TcpListener

```lua
client, nil         listener:Accept()           -- client is pending
nil, nil            listener:Accept()           -- no client pending yet
nil, errmsg         listener:Accept()           -- listener is disposed / error
table               listener:GetContext()
nil                 listener:Dispose()
```

| Method | Description |
|--------|-------------|
| `Accept()` | Pumps the libevent loop non-blocking and returns the next pending `TcpClient` from the accept queue. Returns `client, nil` when a new connection is ready, `nil, nil` when the queue is empty (call again after a `Sleep`), or `nil, errmsg` if the listener is disposed or an error occurred |
| `GetContext()` | Returns a per-listener Lua table created lazily on first call. Persists for the lifetime of the listener |
| `Dispose()` | Close the listener and free all resources. Idempotent — safe to call more than once. Called automatically by `__gc` |

---

### TcpClient

Returned by `TCP.Connect` (client-initiated) or by `listener:Accept()` (server-accepted). Both sides share the same API.

```lua
string              client:Poll()               -- data available
""                  client:Poll()               -- connected, no data yet (empty string)
nil, errmsg         client:Poll()               -- closed ("closed"), disposed ("client disposed") or error
bool, nil           client:Send(data)           -- sent ok
bool, errmsg        client:Send(data)           -- disposed or error
bool, opt errmsg    client:IsConnected()
string              client:GetIP()
int                 client:GetPort()
table               client:GetContext()
nil                 client:Dispose()
```

| Method | Description |
|--------|-------------|
| `Poll()` | Non-blocking. Returns the next chunk of received data as a string, `""` (an empty string) when no data is available yet (connection is still open), or `nil, errmsg` when the connection has been closed (`"closed"`), disposed (`"client disposed"`) or an error occurred. Never blocks |
| `Send(data)` | Write `data` (string) to the connection. Returns `true, nil` on success, or `false, errmsg` if the client is disposed or the write failed |
| `IsConnected()` | Returns `true` while the connection is established; may return `false, errmsg` |
| `GetIP()` | Returns the remote IP address string (e.g. `"127.0.0.1"`). For a client created with `TCP.Connect` it is the `host` string that was passed in, which may be a hostname |
| `GetPort()` | Returns the remote port as an integer |
| `GetContext()` | Returns a per-client Lua table created lazily on first call. Persists for the lifetime of the client |
| `Dispose()` | Close the connection and free all resources. Idempotent. Called automatically by `__gc` |

---

### Examples

#### Simple echo server

```lua
local listener = assert(TCP.StartListener(9000))

local server = Tasks.New(function()
    while true do
        local client = listener:Accept()
        if client then
            -- Handle each connection in its own task
            Tasks.New(function(c)
                while true do
                    local data, err = c:Poll()
                    if not data then break end  -- closed or error
                    if data ~= '' then
                        c:Send(data)            -- echo back
                    end
                    Sleep(1)
                end
                c:Dispose()
            end, client):Dispose()
        else
            Sleep(5)
        end
    end
end)

-- Stop after 30 s
Sleep(30000)
listener:Dispose()
server:Cancel()
```

#### TCP client

```lua
local client = assert(TCP.Connect('127.0.0.1', 9000))

-- Wait for the connection to be established
for i = 1, 50 do
    if client:IsConnected() then break end
    Sleep(10)
end
assert(client:IsConnected(), 'connection failed')

client:Send('hello')

-- Read reply
local reply = ''
for i = 1, 100 do
    local data, err = client:Poll()
    if not data then
        break   -- nil, errmsg means closed / error ("" means no data yet)
    end
    reply = reply .. data
    if reply ~= '' then break end
    Sleep(5)
end
print('got:', reply)
client:Dispose()
```

#### Server with per-connection context

```lua
local listener = assert(TCP.StartListener(9001))

Tasks.New(function()
    while true do
        local client = listener:Accept()
        if client then
            local ctx = client:GetContext()
            ctx.connected_at = Time()
            Tasks.New(function(c)
                local data, err = c:Poll()
                while data do
                    if data ~= '' then c:Send(data) end   -- "" = no data yet
                    Sleep(1)
                    data, err = c:Poll()
                end
                local ctx2 = c:GetContext()
                print('connection lasted', Time() - ctx2.connected_at, 'ms')
                c:Dispose()
            end, client):Dispose()
        else
            Sleep(5)
        end
    end
end):Dispose()
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

### Notes (all three)

- `data` should be a string; other values are hashed as `tostring(data)`. **Streams are not supported**: MD5 and SHA1 silently ignore a `Stream` argument, and SHA256 hashes its `tostring` form.
- `Finish()` finalizes once; calling it again returns the same hex string and raw bytes.
- `Update` after `Finish` raises "Cannot update already finished … digest".

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
| `Query` | Returns a **Lua coroutine** immediately without blocking. Drive it with `coroutine.resume` as described below |
| `NonQuery` | Helper — drives a query to completion and returns `true, rowcount` (integer), or `false, errmsg` on error. Designed for INSERT / UPDATE / DELETE |
| `Scalar` | Helper — returns `true, col1value` (first column of the first row), or `true, nil` when no rows matched, or `false, errmsg` on error |
| `QueryAll` | Helper — collects every row into an array of integer-keyed row arrays and returns `true, rows`, or `false, errmsg` on error |
| `SetAliveToken` | Attach an `AliveToken` to this connection. If the token is disposed while a helper is polling, it stops early and returns `false, "cancelled"`. Pass `nil` to detach |
| `IsBusy` | Returns `true` while a query coroutine is still alive on this connection |
| `EscapeValue` | Escape a string with `mysql_real_escape_string`. Returns the escaped value **without** surrounding quotes |
| `Close` | Close the connection and free all resources. Safe to call multiple times |

While another query is active on the connection, `Query` and the helpers return `nil, "Connection already has an active query"`. After `Close()`, `Query`, the helpers and `EscapeValue` **raise** "Connection is closed".

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
| `true, <string>` + status `"dead"` | Done — string is a query-level error message; the connection is already released |
| `true, {col1, col2, …}` | One data row (integer-keyed, 1-based) |
| `true, nil` + status `"dead"` | All rows consumed, C buffer freed |
| `false, <string>` | Coroutine raised a Lua error |

Pass a truthy value as the **first argument** of any `coroutine.resume` call to send the **stop flag**: in any phase, the coroutine frees the result buffer, clears the connection's busy state, and dies cleanly, so the connection is immediately reusable. If the stop flag arrives while the query is still in flight on the server (before the rowcount), the coroutine first waits for the server to finish that statement and discards its result — this blocks the engine thread until the server answers. Cancelling a helper with an `AliveToken` goes through the same path, so it waits too; MySQL can't abort a running statement from the same connection, so give long statements a server-side limit (e.g. `SET max_execution_time`) instead.

A query-level error string is the coroutine's final value: the coroutine is dead and the connection is free for the next query, with no extra resume needed.

```lua
local conn = assert(MySQL.Connect("127.0.0.1", "user", "pass", "mydb"))
local co   = assert(conn:Query("SELECT id, name FROM users WHERE active = ?", {1}))

-- Phase 1: drive the async state machine until rowcount arrives
local ok, val = coroutine.resume(co)
while ok and val == nil and coroutine.status(co) == "suspended" do
    ok, val = coroutine.resume(co)
end
if not ok then error(val) end          -- coroutine error
if type(val) == "string" then          -- query-level error (connection already released)
    error(val)
end
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

Pass an array table as the second argument to `Query`, `NonQuery`, `Scalar`, or `QueryAll`. `?` placeholders are substituted in order — **every** `?` in the SQL text is counted, including one inside a string literal, so don't put a literal `?` in parameterized SQL. Missing or `nil` entries become SQL `NULL`. Every non-NULL value, numbers included, is sent as a single-quoted, escaped literal. The following Lua types are accepted as parameter values:

| Parameter type | Sent as |
|----------------|---------|
| `nil` | SQL `NULL` |
| `string` | escaped string |
| `number` / `integer` | stringified |
| `boolean` | `'true'` / `'false'` (use `1` / `0` for `TINYINT` columns) |
| `Identifier` | canonical string (`xxxxxxxx-xxxx-…` or 24-char hex) |
| `DateTime` | ISO 8601 string (`...Z` for UTC, `...±HH:MM` with an offset) |
| `Decimal` | decimal string (e.g. `"123.456"`) |
| `UInt`, `TimeSpan` | their string form |
| `table` | JSON-encoded string |

```lua
conn:NonQuery("INSERT INTO t (a, b, c) VALUES (?, ?, ?)", {"hello", nil, 3.14})
conn:Scalar("SELECT name FROM users WHERE id = ?", {42})
```

### MySQL type mapping

| MySQL type | Lua type |
|------------|----------|
| TINYINT, SMALLINT, MEDIUMINT, INT, BIGINT | integer (`BIGINT UNSIGNED` values above `2^63 - 1` return a `UInt` userdata) |
| FLOAT, DOUBLE | number |
| BIT | integer (`b'10000001'` → `129`; a `BIT(64)` value above `2^63 - 1` returns a `UInt` userdata) |
| DECIMAL, NEWDECIMAL | `Decimal` ¹ |
| TINYTEXT, TEXT, MEDIUMTEXT, LONGTEXT | string |
| TINYBLOB, BLOB, MEDIUMBLOB, LONGBLOB (binary collation) | `LuaStream`; read with `tostring(v)` or `v:Read()` |
| DATE, DATETIME, TIMESTAMP | `DateTime` ¹ |
| all others (VARCHAR, CHAR, YEAR, TIME, ENUM, JSON, …) | string |

> ¹ Falls back to a plain string when parsing fails (e.g. non-standard server format).
>
> `Decimal`, `DateTime`, `Identifier` and `UInt` are **userdata**, not strings or numbers. `v == "2024-01-01"` or `v == 1.5` is always `false`, `"x" .. v` errors, and `math.*` rejects them. Use `tostring(v)`, `v:ToNumber()`, or compare against a value of the same type. See [Userdata Return Values](#userdata-return-values-read-first).

> **Note:** MySQL has no native boolean type. `TINYINT(1)` columns return integer `1` or `0`.

---

## Postgres

Connects to a PostgreSQL database using libpq. Queries are driven by the libpq async API so **no background thread is ever created**. `Connect` itself is **blocking** (`PQconnectdb`): the whole engine thread waits during the handshake, so set `connect_timeout` in the connection string. The connection is always configured with `UTF8` client encoding automatically.

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
| `Query` | Returns a **Lua coroutine** immediately without blocking. Drive it with `coroutine.resume` as described below |
| `NonQuery` | Helper — drives a query to completion and returns `true, rowcount` (integer), or `false, errmsg` on error. Designed for INSERT / UPDATE / DELETE |
| `Scalar` | Helper — returns `true, col1value` (first column of the first row), or `true, nil` when no rows matched, or `false, errmsg` on error |
| `QueryAll` | Helper — collects every row into an array of integer-keyed row arrays and returns `true, rows`, or `false, errmsg` on error |
| `SetAliveToken` | Attach an `AliveToken` to this connection. If the token is disposed while a helper is polling, it stops early and returns `false, "cancelled"`. Pass `nil` to detach |
| `IsBusy` | Returns `true` while a query coroutine is still alive on this connection |
| `EscapeValue` | Escape a string using `PQescapeLiteral`. The result **includes** surrounding single quotes (e.g. `'O''Reilly'`) |
| `Close` | Close the connection and free all resources. Safe to call multiple times |

While another query is active on the connection, `Query` and the helpers return `nil, "Connection already has an active query"`. After `Close()`, `Query` returns `nil, "Connection is closed"`, while the helpers and `EscapeValue` **raise** that error.

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
| `true, <string>` + status `"dead"` | Done — string is a query-level error message; the connection is already released |
| `true, {col1, col2, …}` | One data row (integer-keyed, 1-based) |
| `true, nil` + status `"dead"` | All rows consumed |
| `false, <string>` | Coroutine raised a Lua error |

Pass a truthy value as the **first argument** of any `coroutine.resume` call to send the **stop flag**: the coroutine frees the result buffer and dies cleanly (in any phase), and the connection is immediately reusable. If the query has already been sent, the stop also asks the server to cancel the statement and waits briefly for the connection to be free. Cancelling a helper with an `AliveToken` does the same.

As with MySQL, a query-level error string is the coroutine's final value: the coroutine is dead and the connection is free for the next query, with no extra resume needed.

### Parameterized Queries

Pass an array table as the second argument to `Query`, `NonQuery`, `Scalar`, or `QueryAll`. Uses PostgreSQL native `$1`, `$2`, … placeholders. Missing or `nil` entries are sent as SQL `NULL`. The following Lua types are accepted as parameter values:

| Parameter type | Sent as |
|----------------|---------|
| `nil` | SQL `NULL` |
| `string` | string |
| `number` / `integer` | stringified |
| `boolean` | `"true"` / `"false"` |
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
| 1083 | TIME | string (a time without a date doesn't parse) |
| 1266 | TIMETZ | string |
| 1114 | TIMESTAMP | `DateTime` ¹ |
| 1184 | TIMESTAMPTZ | `DateTime` carrying the session time zone's offset (`+00`, `-05`, `+05:30`) ¹ |
| 17 | BYTEA | string in Postgres's text form (`\x48656c6c6f` hex), not raw bytes; decode the hex yourself |
| all others | TEXT, VARCHAR, JSON, etc. | string |

> ¹ Falls back to a plain string when parsing fails (e.g. a non-ISO `DateStyle` such as `SQL` or `German`, a BC date such as `0044-03-15 BC`, `infinity`, or a pre-1900 local-mean-time offset with seconds such as `+00:53:28`).
>
> `Decimal`, `DateTime`, `Identifier` and `UInt` are **userdata**, not strings or numbers. `v == "2024-01-01"` or `v == 1.5` is always `false`, `"x" .. v` errors, and `math.*` rejects them. Use `tostring(v)`, `v:ToNumber()`, or compare against a value of the same type. See [Userdata Return Values](#userdata-return-values-read-first).

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
| `Stop` | Stop the timer and return elapsed ms for the last interval. Only call it after `Start()`: stopping a timer that was never started returns (and later accumulates) a meaningless value |
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
table       SQLite:GetColumns()
nil         SQLite:RegisterFunction(function, name, args)
nil         SQLite:RegisterAggregateFunction(function, name, args)
nil         SQLite:SetBusyHandler(opt fn)
nil         SQLite:Close()
```

**Mode:** omit it (or pass `nil`) to keep the journal mode the database already has - a WAL database stays WAL, a rollback-journal database stays as it is - while a new (empty) database is put in WAL mode; SQLite's threading config is then left at its compiled-in default. When given, non-zero forces WAL journal mode (`PRAGMA journal_mode=WAL`) and `0` forces `journal_mode=DELETE` on every open (this is persistent, so it converts the file). A given mode is also passed to `sqlite3_config` (0=single thread, 1=multithreaded, 2=serialized), which only has an effect before SQLite is first initialized in the process. `synchronous=NORMAL` is always set.

TEXT columns are always returned as plain UTF-8 Lua strings; BLOB columns as `LuaStream`.

Every database opened with `Open` gets a built-in SQL function **`Lua(script)`** that compiles and runs a Lua string and returns its result as TEXT (or NULL): `SELECT Lua('return 1+1')` → `'2'`. Extension loading (`load_extension`) is also enabled. Both run with the engine's full privileges, so never pass untrusted SQL to a database.

| Function | Description |
|----------|-------------|
| `Open` | Open an SQLite database. Omit `filename` (or pass `nil`) for an in-memory database. Raises an error on failure |
| `Query` | Prepare and execute `sql`. Returns `true, "ROW"` when the result has at least one row, `true, "DONE"` when it has none (DDL/DML or empty SELECT), or `false, errmsg` on error. DDL/DML have already run (and the statement is finalized) when `Query` returns `"DONE"`; no `Fetch()` is needed. For rows, loop with `while db:Fetch() do ... db:GetRow() ... end` - this sees **every** row, including the first (see [Reading rows](#reading-rows)) |
| `Finish` | Finalize the current statement without reading the remaining rows. Optional: starting a new `Query` also finalizes an unfinished statement |
| `Fetch` | Make the next row current and return `true`, or return `false` when there are no more rows. The **first** `Fetch()` after a `Query` that returned `"ROW"` makes row 1 current - it does not skip it. Also returns `false` right after `"DONE"` |
| `GetRow` | Without arguments (or `0`): returns the current row as a string-keyed table `{columnName = value, ...}` — **not** an integer-indexed array. With a positive 1-based integer index: returns that single column value directly. Returns `nil` if the index is out of range or there is no active row |
| `GetColumns` | Returns the current statement's column names as an ordered array `{'id', 'name', ...}` - duplicates such as `a.id, b.id` both appear, unlike `GetRow()`'s keys. Available once `Query` returned `"ROW"` and until the last `Fetch()`; returns an empty table when there is no active statement (after `"DONE"`, an empty SELECT, `Finish()` or the final `Fetch()`) |
| `RegisterFunction` | Register a scalar Lua function callable from SQL. `args` (required) is the exact number of arguments; variadic functions aren't supported (a negative value raises "SQLite function args can't be negative"). Like SQLite itself, a function is identified by name **and** argument count: `f` with 1 and `f` with 2 arguments can both be registered, registering the same name and count twice raises an error, and registering a built-in's name (e.g. `Lua`, `load_extension`) replaces it on that connection. A Lua number returned by the function becomes REAL (even `42`), a boolean becomes 0/1, a `Stream` becomes NULL, anything else is converted with `tostring`. BLOB arguments arrive as `LuaStream` |
| `RegisterAggregateFunction` | Register an aggregate Lua function. Called per row with `(false, …args)` and once at the end with `(true)` to collect the final result |
| `SetBusyHandler` | Register a callback invoked when a table is locked. Receives `(sqlite, retryCount)` (`retryCount` starts at 0). Return a truthy value to wait and retry, or a falsy value (`false`/`nil`) to give up, in which case the `Query`/`Fetch` that hit the lock fails (`Query` returns `false, "database is locked"`). An error raised inside the handler is swallowed and also gives up. Pass `nil` or no argument to remove |
| `Close` | Close the database connection |

### Prepared Statements (named parameters)

`Query` supports named parameters using the `:name` placeholder syntax (`@name` and `$name` work too). **Anonymous positional parameters (`?`) are not supported**: with a params table or function, `Query` returns `false, "Parameters contain a nameless parameter!"`; without params, a `?` simply binds NULL.

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

Supported bind types: `nil` → NULL, integer → INTEGER, float → REAL, boolean → INTEGER (0/1), string → TEXT. **Every other value binds NULL**: any userdata (including `Stream`, `DateTime`, `Decimal`, `Identifier` and `UInt`, so convert those with `tostring(v)` first), tables, functions, coroutines and lightuserdata such as `Json.Null`. Each placeholder is always bound exactly once, so an unsupported value never shifts the parameters after it.

### Query Workflow

```lua
local db = SQLite.Open()          -- in-memory database

-- DDL / DML run inside Query (returns true, "DONE")
db:Query('CREATE TABLE t (id INTEGER, name TEXT)')
db:Query('INSERT INTO t VALUES (:id, :name)', {id = 1, name = 'Alice'})

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

-- Read just the first row, then stop (Finish is optional - the next Query would also end it)
db:Query('SELECT id FROM t ORDER BY id')
db:Fetch()                            -- row 1 is current
db:Finish()                           -- discard rows 2..n

db:Close()
```

### Reading rows

`Fetch()` works like a cursor that starts *before* the first row. Each call makes the next row
current; `GetRow()` reads the current row. With three rows:

| Call | Returns | `GetRow()` then gives |
|------|---------|-----------------------|
| `db:Query('SELECT id FROM t ORDER BY id')` | `true, "ROW"` | row 1 (already readable) |
| 1st `db:Fetch()` | `true` | row 1 - **not** row 2 |
| 2nd `db:Fetch()` | `true` | row 2 |
| 3rd `db:Fetch()` | `true` | row 3 |
| 4th `db:Fetch()` | `false` | `nil` |

So `while db:Fetch() do ... end` reads rows 1, 2 and 3 - nothing is skipped, and no `Finish()` is
needed after it. Reading row 1 with `GetRow()` before the first `Fetch()` doesn't change this: the
first `Fetch()` still returns row 1. After `"DONE"` (or an empty SELECT) the first `Fetch()` returns
`false`.

### Return Values from Query

| Second return | Meaning |
|---------------|---------|
| `"ROW"` | The result has rows. Read them with `while db:Fetch() do ... end`; the first `Fetch()` gives row 1 |
| `"DONE"` | Statement completed with no rows (typical for DDL/DML or empty SELECT); `Fetch()` returns `false` |
| `false, errmsg` | Preparation or execution error |

---

## DuckDB

```lua
DuckDB      DuckDB.Open(opt filename)
bool, txt   DuckDB:Execute(sql, opt params)
bool, txt   DuckDB:Query(sql, opt params)
nil         DuckDB:Finish()
bool        DuckDB:Fetch()
table|value DuckDB:GetRow(opt index)
nil         DuckDB:Close()
```

Always compiled in (self-contained amalgamation, no external dependencies).

| Function | Description |
|----------|-------------|
| `Open` | Open a DuckDB database. Omit `filename` (or pass `nil`) for an in-memory database. Raises an error on failure |
| `Execute` | Prepare and execute `sql`. Returns `true, "ROW"` when the result has rows, `true, "DONE"` when it has none (DDL or empty SELECT), or `false, errmsg` on error. Unlike SQLite, **call `Fetch()` before the first `GetRow()`** (`GetRow()` returns `nil` until then); the usual `while db:Fetch() do ... end` loop works. INSERT/UPDATE/DELETE return `true, "ROW"` with a single row `{Count = n}` |
| `Query` | Alias for `Execute` |
| `Finish` | Destroy the current prepared statement and result early, allowing a new `Execute` before all rows have been consumed |
| `Fetch` | Advance to the next result row. Returns `true` while a row is available, `false` when exhausted |
| `GetRow` | Without arguments (or `0`): returns the current row as a string-keyed table `{columnName = value, ...}`. With a positive 1-based integer index: returns that single column value directly. Returns `nil` if the index is out of range or there is no active row |
| `Close` | Disconnect and close the database. Afterwards everything except `Finish`/`Close` raises "DuckDB instance has been closed" |

### Parameter binding

`Execute` / `Query` accept an optional second argument to bind positional parameters (`$1`, `$2`, … or `?`):

- **Array table** `{val1, val2, …}` — values bound by position
- **Function** `function(index) return val end` — called once per parameter index (1-based)

Supported Lua types: `nil` → NULL, `boolean` → BOOLEAN, integer → BIGINT, float → DOUBLE, string → VARCHAR. Anything else (userdata, tables) binds NULL; convert with `tostring` first.

### Type mapping

| DuckDB type | Lua type |
|-------------|----------|
| BOOLEAN | boolean |
| TINYINT … BIGINT, UTINYINT … UINTEGER | integer |
| UBIGINT | number (float; loses precision above 2^53) |
| FLOAT, DOUBLE | number |
| BLOB | string (raw bytes) |
| everything else (VARCHAR, DECIMAL, HUGEINT, DATE, TIMESTAMP, UUID, LIST, …) | string |
| NULL | nil (the key is absent from the `GetRow()` table) |

### Execute return values

| Return | Meaning |
|--------|---------|
| `true, "ROW"` | The result has rows. Call `Fetch()` to make row 1 current, then `GetRow()`; `while db:Fetch() do ... end` reads every row |
| `true, "DONE"` | Statement completed with no (more) rows (typical for DDL/DML or empty SELECT) |
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
json    json:SetDecodeNull(bool)          -- returns the instance (chainable)
json    json:SetEncodeEmptyObject(bool)   -- returns the instance (chainable)
nil     json:Dispose()
```

| Function | Description |
|----------|-------------|
| `New` | Create a new Json instance. Pass `true` for pretty-printed output (2 spaces per indent level) |
| `Json.Null` | The unique lightuserdata sentinel that encodes to/decodes from JSON `null`. Compare with `== Json.Null` |
| `Json.EmptyObject` | The unique lightuserdata sentinel that encodes to/decodes from JSON `{}`. Only produced during decode when `SetEncodeEmptyObject(true)` is active. Compare with `== Json.EmptyObject` |
| `Encode` | Encode a Lua value to a JSON string |
| `Decode` | Decode JSON from a string, a chunk-reader function, or a `Stream`. Returns the decoded value. Malformed JSON **raises** an error (e.g. `Json: unexpected end of input`, `Json: expected ':' at line N`), so use `pcall` for untrusted input. Anything after the first complete value is ignored. A non-readable stream returns `nil, "stream is not readable"`; an empty async stream returns `nil` |
| `EncodeIntoStream` | Encode `value` and write the JSON bytes directly into `stream`. Returns `true` on success, or `false, errmsg` if the stream is not writable |
| `DecodeFromStream` | Decode one JSON value from `stream`. Returns the decoded value, or `nil, errmsg` if the stream is not readable |
| `SetDecodeNull(bool)` | Control how JSON `null` is decoded. Default `false` — decodes as Lua `nil` (falsy, coalescing works). Pass `true` to decode as the `Json.Null` sentinel instead (truthy, round-trip safe but lossy on re-encode if value was nil) |
| `SetEncodeEmptyObject(bool)` | Control how empty Lua tables are encoded and how `{}` is decoded. Default `false` — empty tables encode as `[]` and `{}` decodes as an empty Lua table. Pass `true` to encode empty tables as `{}` and decode `{}` as the `Json.EmptyObject` sentinel (round-trip safe) |
| `Dispose` | Explicitly free the internal output buffer; called automatically by the GC |

### Null Sentinel

By default JSON `null` decodes to Lua `nil` — falsy, so coalescing with `or` works naturally. Call `json:SetDecodeNull(true)` to decode `null` as the `Json.Null` sentinel instead, which is **truthy** and survives a round-trip through `Encode`. Without `SetDecodeNull(true)`, re-encoding a decoded object will omit any keys whose value was `null` (since `nil` in a Lua table means absent).

> **Some internal modules use the sentinel.** JSON decoded by the engine bridge (values passed from the host), Redis/RedisJSON, the MCP server and `ToolSuite` uses `nullAsSentinel = true`, so a JSON `null` arriving through those is already `Json.Null`. **Database NULLs are different:** SQL `NULL` from MySQL, Postgres, SQLite and DuckDB, and BSON null from MongoDB, come back as Lua `nil`, not `Json.Null`.

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

-- JSON from Redis/RedisJSON already uses the sentinel; SQL NULL from
-- MySQL/Postgres/SQLite/DuckDB is plain nil:  row[2] == nil
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
| float | number formatted with `%.16g`: an integral float like `2.0` encodes as `2` (and decodes back as an integer), and the last digit can be rounded (`0.1 + 0.2` → `0.3`) |
| `string` | string |
| `Identifier` | string (canonical UUID or OID hex) |
| `DateTime` | string (ISO 8601, e.g. `"2024-06-01T12:00:00.000Z"`) |
| `TimeSpan` | string (e.g. `"00:00:01.000"`) |
| `Decimal` | number (no quotes — preserves numeric semantics) |
| `UInt` | number |
| `LuaStream` | string (all bytes from offset 0; stream position preserved; `null` if not readable+seekable) |
| `table` | object `{}` or array `[]` depending on keys; non-string object keys are converted with `tostring` (a sparse `{[1]=..,[3]=..}` becomes `{"1":..,"3":..}`) |
| functions, threads, other userdata/lightuserdata | `null` |
| `NaN` | `null` |
| `±Infinity` | `1e+9999` / `-1e+9999` |

### Notes

- **Circular references** raise an error: `Json: recursion detected`
- **Table classification**: pure sequential integer-keyed tables (`{1, 2, 3}`) encode as JSON arrays; all others encode as objects
- **UTF-8 strings** pass through the encoder unescaped. Only control characters (U+0000–U+001F) are hex-escaped as `\uXXXX`
- **Invalid UTF-8** (binary data or ANSI text) is replaced with U+FFFD, so the output is always valid JSON. Send binary data Base64-encoded or as a `LuaStream`
- **Decoding** skips a leading UTF-8 BOM for every source (string, function, stream). Unpaired `\uD800`–`\uDFFF` escapes decode to U+FFFD
- **Large integers** outside the 64-bit signed range decode saturated to ±9223372036854775807, without an error; send them as strings if you need them exactly
- **`LuaStream`** must be both readable and seekable; unreachable streams encode as `null`

### Examples

```lua
local json = Json.New()

-- Basic encode/decode
local s = json:Encode({name = "Alice", scores = {10, 20, 30}})
local t = json:Decode(s)
print(t.name, t.scores[1])

-- Null sentinel (needs SetDecodeNull(true); by default null decodes as nil)
local t2 = Json.New():SetDecodeNull(true):Decode('{"x":null}')
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

## MsgPack

Binary [MessagePack](https://msgpack.org/) encoder/decoder with the same instance shape as `Json`. The functions work as `MsgPack.Xxx(mp, …)` or `mp:Xxx(…)`.

```lua
MsgPack        MsgPack.New()
string         mp:Encode(value)
value|nil,err  mp:Decode(string | stream)
bool|false,err mp:EncodeIntoStream(stream, value)
value|nil,err  mp:DecodeFromStream(stream)
nil            mp:Dispose()
```

| Function | Description |
|----------|-------------|
| `New` | Create an encoder/decoder instance (no options) |
| `Encode` | Encode a Lua value and return the MessagePack bytes as a Lua string. Raises `MsgPack: recursion detected` on cyclic tables |
| `Decode` | Decode the first MessagePack value from a string (or from a `Stream`, same as `DecodeFromStream`). Trailing bytes are ignored. On bad input returns `nil, "MsgPack: parse error"` or `nil, "MsgPack: incomplete data"` (does **not** raise) |
| `EncodeIntoStream` | Encode `value` and write the bytes to `stream`. Returns `true`, or `false, "stream is not writable"`. Raises if argument 2 is not a stream |
| `DecodeFromStream` | Read the stream's available bytes and decode one value. On seekable streams, bytes belonging to the next message are pushed back, so repeated calls read consecutive messages. Returns `nil, errmsg` if the stream is not readable or the data is invalid or incomplete |
| `Dispose` | Free the internal buffer early; called automatically by the GC |

### Type mapping

| Lua → MessagePack | |
|---|---|
| `nil` | nil |
| `boolean` | true / false |
| integer | int (smallest encoding) |
| float | float64 |
| `string` | str (bytes passed through, no UTF-8 validation) |
| `table` with keys exactly `1..n` | array (an empty table encodes as an empty array) |
| any other `table` | map (keys encoded with their own types) |
| `UInt` | uint64 |
| `LuaStream` (readable + seekable) | bin (all bytes from offset 0; position preserved) |
| `DateTime`, `Identifier`, `Decimal`, `TimeSpan` | str (their string form) |
| functions, threads, lightuserdata (incl. `Json.Null`), other userdata | nil |

| MessagePack → Lua | |
|---|---|
| nil | `nil` (map entries with nil values are absent; a nil map key raises an error) |
| true / false | boolean |
| int (positive ≤ 2^63-1, or negative) | integer |
| positive int > 2^63-1 | `UInt` userdata |
| float32 / float64 | number |
| str | string |
| bin | `LuaStream` (in-memory, positioned at 0) |
| array | integer-keyed table |
| map | table |
| ext | `nil` (not supported) |

`DateTime`, `Decimal`, `Identifier` and `TimeSpan` don't round-trip: they come back as plain strings.

```lua
local mp = MsgPack.New()
local bytes = mp:Encode({id = 1, tags = {"a", "b"}})
local t = assert(mp:Decode(bytes))
print(t.id, t.tags[2])            -- 1   b

-- Several messages in one stream
local s = Stream.New()
mp:EncodeIntoStream(s, 1)
mp:EncodeIntoStream(s, 2)
s:Seek(0)
print(mp:DecodeFromStream(s), mp:DecodeFromStream(s))  -- 1   2
```

---

## Text

Helpers for the text conversions plain UTF-8 strings and Lua's `utf8` library don't cover. Every function takes and returns ordinary Lua strings. Malformed input never raises: invalid UTF-8 sequences and unpaired UTF-16 surrogates become U+FFFD (`"\xEF\xBF\xBD"`); code page conversions follow the platform's rules for unmappable characters (see below).

```lua
string  Text.Lower(str)
string  Text.Upper(str)
string  Text.ToUtf16(str)
string  Text.FromUtf16(bytes)
string  Text.FromCodepage(bytes, opt codepage)
string  Text.ToCodepage(str, opt codepage)
```

| Function | Description |
|----------|-------------|
| `Lower` / `Upper` | Unicode-aware case conversion (all scripts, not only ASCII: `"É"` ↔ `"é"`, `"Σ"` ↔ `"σ"`, `"Ж"` ↔ `"ж"`). Uses simple, locale-independent mappings, so there's no Turkish dotless-i special case. `string.lower` / `string.upper` only change ASCII letters |
| `ToUtf16` | Encodes a UTF-8 string as UTF-16 LE bytes (2 bytes per code unit, 4 for characters outside the BMP, no BOM), returned as a Lua string |
| `FromUtf16` | Decodes UTF-16 LE bytes to a UTF-8 string. A trailing odd byte is ignored, and a leading BOM is kept as U+FEFF |
| `FromCodepage` | Decodes bytes in a legacy code page to UTF-8. Undecodable bytes become U+FFFD on Linux; on Windows the system decoder is used, which may map undefined bytes to C1 control characters (e.g. `0x81` in 1252 → U+0081) or another fallback character instead |
| `ToCodepage` | Encodes a UTF-8 string into a legacy code page. On Windows characters without an exact mapping may be replaced by a best-fit look-alike (`ł` → `l`, `∞` → `8` in 1252), and only otherwise by `?`; on Linux they become `?` |

`codepage` is a Windows code page number, for example `1252` (Western European), `1250`, `1251`, `437`, `850`, `932` (Shift-JIS), `936`, `949`, `950`, `28591`–`28606` (ISO-8859-1 to -16), `20127` (ASCII) or `65001` (UTF-8). For UTF-16 use `ToUtf16` / `FromUtf16`. Omitted or `0` means the system ANSI code page on Windows and the locale's charset elsewhere. An unsupported code page raises an error.

```lua
-- Legacy Windows-1252 file -> UTF-8
local f = assert(io.open("legacy.txt", "rb"))
local text = Text.FromCodepage(f:read("a"), 1252)
f:close()

-- Case-insensitive comparison that works beyond ASCII
if Text.Lower(name) == Text.Lower("ÅSA") then ... end

-- Checksum over UTF-16 bytes, e.g. to match a hash computed by .NET/Windows code
local crc = CRC64(Text.ToUtf16("hello"))

-- Codepoint-level work uses Lua's utf8 library
print(utf8.len("héllo"))                   --> 5
for _, cp in utf8.codes("hé") do print(cp) end   --> 104, 233
```

See also `Stream:ReadUtf16` / `Stream:WriteUtf16` for reading and writing UTF-16 data directly.

---

## UInt

A typed userdata for unsigned 64-bit integers. Covers values above `2^63 - 1` that cannot be represented losslessly as a Lua integer or `number`. Arithmetic and bitwise operators are overloaded so `UInt` values work with `+`, `-`, `*`, `/`, `%`, `&`, `|`, `~`, `<<`, `>>`, and unary `~` directly (there is no `^`). Comparisons (`==`, `<`, `<=`) are also overloaded.

### Constructors

```lua
UInt  UInt.FromString(str)      -- parse decimal string; nil on failure
UInt  UInt.FromNumber(n)        -- truncates the fractional part; nil if n < 0 or > 2^64-1
UInt  UInt.FromUnsigned(n)      -- reinterpret raw bit pattern of a Lua integer as uint64
UInt  UInt.Zero()               -- returns 0
```

### Methods

```lua
string  u:ToString()     -- decimal string representation, alias: AsString()
string  u:AsString()
number  u:ToNumber()     -- convert to Lua number (lossy above 2^53)
int     u:ToInteger()    -- lower 63 bits as a non-negative integer (top bit cleared)
int     u:ToUnsigned()   -- the same 64 bits as a Lua integer (negative for values > 2^63-1)
bool    u:IsZero()       -- true when value is 0
UInt    u:Add(x)         -- same as u + x
UInt    u:Sub(x)         -- same as u - x
UInt    u:Mul(x)         -- same as u * x
UInt    u:Div(x)         -- same as u / x
```

The right-hand operand of the operators and of `Add`/`Sub`/`Mul`/`Div` may be a `UInt`, a Lua integer (its bits are reinterpreted), a non-negative number, or a decimal string: `UInt.FromNumber(5) + 1` is `6`. `/` and `%` by zero raise an error; shifting by 64 or more gives 0. Lua only calls `__eq` when both operands are userdata, so `u == 5` is always `false`: compare with `u == UInt.FromNumber(5)` or `u:ToNumber() == 5`.

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

All constructors except `FromTicks` (integer only) accept fractional `number` arguments.

### Component Getters

```lua
int     ts:Days()
int     ts:Hours()
int     ts:Minutes()
int     ts:Seconds()
int     ts:Milliseconds()
int     ts:Ticks()           -- raw 100-ns signed tick count
bool    ts:IsNegative()      -- true when duration < 0
bool    ts:IsZero()          -- true when ticks == 0
```

### Other Methods

```lua
string    ts:ToString()      -- same as tostring(ts); alias: AsString()
string    ts:AsString()
TimeSpan  ts:Abs()           -- absolute duration
TimeSpan  ts:Add(other)      -- same as ts + other
TimeSpan  ts:Sub(other)      -- same as ts - other
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
| `tostring(ts)` | Canonical string, e.g. `"01:30:00.000"` or `"-00:00:30.000"`; a day or more gets a days prefix, `D.HH:MM:SS.mmm` (`FromHours(25.5)` → `"1.01:30:00.000"`) |
| `ts1 == ts2` | Tick equality |
| `ts1 < ts2` | Less-than comparison (raises if the other operand isn't a `TimeSpan`) |
| `ts1 <= ts2` | Less-or-equal comparison (same) |
| `ts1 + ts2` | Duration addition (both must be `TimeSpan`) |
| `ts1 - ts2` | Duration subtraction (both must be `TimeSpan`) |
| `ts * n`, `n * ts` | Scale by a number |
| `ts / n` | Divide by a number (`n` must be a number; `/ 0` raises) |
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
- `FromString` and `FromBytes` return `nil` when the length or format is wrong. Hex digits in the UUID string form are not validated, so check untrusted input yourself.

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

`==` with a string or with another kind of userdata (e.g. a `DateTime`) is `false`.

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
| `New` | Construct from individual components. `offsetMinutes` is the UTC offset in minutes `[-840, +840]`; defaults to 0 (UTC). Raises an error for out-of-range components (hour 0-23, minute 0-59, second 0-59, millisecond 0-999, a day that doesn't exist in the month) or offset. The same offset range is enforced (with an error) by `FromUnix*`, `Parse` and `ToOffset` |
| `FromUnixSeconds` | Wrap a Unix timestamp (seconds, may be fractional) into a DateTime |
| `FromUnixMilliseconds` | Wrap a Unix timestamp in integer milliseconds |
| `Parse` | Parse an ISO 8601 / SQL datetime string (`YYYY-MM-DD[T HH:MM[:SS[.fff]]][Z\|±HH:MM]`). If no offset is embedded in the string the optional `fallbackOffsetMinutes` is applied. `±HHMM` without a colon and a whole-hour `±HH` (as Postgres prints it) are accepted too, and trailing text after a valid prefix is ignored (`"2024-06-01xyz"` parses). Returns `nil` on parse failure |

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

`Format` with a format string formats the value's own local time (its offset applied), but `%z` / `%Z` print the *system* time zone, not the value's offset; use `dt:OffsetMinutes()` or the default ISO 8601 output when the offset matters. Output is capped at 256 bytes.

### Arithmetic

```lua
DateTime  dt:AddDays(n)
DateTime  dt:AddHours(n)
DateTime  dt:AddMinutes(n)
DateTime  dt:AddSeconds(n)
DateTime  dt:AddMilliseconds(n)
DateTime  dt:AddTimeSpan(ts)
```

All `Add*` functions accept fractional numbers (`AddTimeSpan` takes a `TimeSpan`) and return a new `DateTime` with the same offset.

### Metamethods

| Metamethod | Behaviour |
|------------|-----------|
| `tostring(dt)` | ISO 8601 string, e.g. `"2024-06-01T12:00:00.000Z"` or with offset `"...+02:00"` |
| `dt1 == dt2` | `true` when UTC ticks are equal (offset is ignored) |
| `dt1 < dt2` | UTC tick comparison |
| `dt1 <= dt2` | UTC tick comparison |
| `dt1 - dt2` | Difference as a `TimeSpan` (use `(a - b):TotalSeconds()` for a number) |
| `dt + ts` | `DateTime` moved by a `TimeSpan` (the `DateTime` must be on the left: `ts + dt` raises) |
| `dt - ts` | `DateTime` moved back by a `TimeSpan` |

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
print((parsed - DateTime.UtcNow()):TotalSeconds())   -- seconds until/since that moment

local tomorrow = DateTime.Now():AddDays(1)
print(tomorrow:Format("%Y-%m-%d"))  -- strftime format
```

---

## Decimal

A typed userdata for exact base-10 arithmetic with up to 34 significant digits. Backed by a 128-bit coefficient + sign + scale representation (equivalent to .NET `decimal` / MongoDB `Decimal128`). All arithmetic operators are overloaded so `Decimal` values can be used with `+`, `-`, `*`, `/`, `%`, and unary `-` directly.

### Constructors

```lua
Decimal  Decimal.FromString(str)   -- e.g. "123.456", "-0.001", "+1.2E+5"; nil if no number is found
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
Decimal  dec:Round(opt scale)    -- round half away from zero to scale places (default 0)
Decimal  dec:Truncate(opt scale) -- truncate to scale places (default 0)
Decimal  dec:Add(other)
Decimal  dec:Sub(other)
Decimal  dec:Mul(other)
Decimal  dec:Div(other)
```

- `FromString` stops at the first character that isn't part of a number, so `"12abc"` gives `12`; validate untrusted input first if that matters. A negative `scale` is treated as 0.
- Operators, comparisons and `Add`/`Sub`/`Mul`/`Div` accept a `Decimal`, Lua integer, number or numeric string on either side (`Decimal.FromString("1.5") + 2` works). As with all userdata, `dec == 0` is always `false` (Lua only calls `__eq` for two userdata); use `dec:IsEmpty()` or compare with a `Decimal`.
- Division is not exact: the quotient is truncated to 10 extra decimal places (`1 / 3` → `0.3333333333`). Division or modulo by zero raises an error.
- `%` truncates and takes the dividend's sign (`-7 % 3` → `-1`), unlike Lua's floored `%` on numbers.

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
Mongo, errmsg  MongoDB.Connect(uri)
```

The global is `MongoDB` (there is no `Mongo` global).

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
- Returns `true, nil` on successful dispatch, or `false, errmsg` when the operation can't start: `"connection is closed"` (after `Close()`), `"operation already in progress"`, or for `InsertMany` `"InsertMany: document array is empty"` / `"InsertMany: array element is not a table"`.
- Invalid arguments **raise an error** instead: an invalid database or collection name, a non-integer `limit`/`skip`, wrong argument types, a string value or key that isn't valid UTF-8, NaN/infinity, or a table that references itself.

### Async Control

```lua
bool          mongo:IsFinished()       -- true when no operation is running
nil           mongo:Wait()             -- yield until current operation completes
nil           mongo:Cancel()           -- request cancellation; yields until done
nil           mongo:SetAliveToken(token) -- attach an AliveToken; cancels automatically when disposed. Pass nil to detach
result, errmsg mongo:GetResult()       -- yield if needed, then return the result
nil           mongo:Close()            -- close connection and free resources
```

- `GetResult` consumes the result: a second call (or a call with nothing dispatched, or after `Cancel()`) returns `nil, "no active operation"`.
- `Close()` cancels a running operation and waits (up to 15 s) for the worker **without yielding**, so it blocks the scheduler briefly; call `Wait()` or `Cancel()` first if an operation may be in flight. Afterwards every CRUD call returns `false, "connection is closed"`.

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
| `CODE`, `CODEWSCOPE`, `SYMBOL` | string |
| any other BSON type | nil |

When writing Lua → BSON:

- `Identifier`, `DateTime`, `Decimal` and `LuaStream` values are serialised to their corresponding BSON types. A `DateTime` is stored in UTC with millisecond precision (its offset and sub-millisecond part are lost).
- `UInt` is stored as INT64 (same 64 bits); `TimeSpan` as INT64 **milliseconds**.
- A table is written as a BSON array only when it has length > 0 and no string keys; everything else, including an empty table `{}`, becomes a document.
- Functions and other userdata are stored as null.

Lua strings are stored as BSON strings, which must be valid UTF-8. A string value or key that isn't (binary data, ANSI text) raises an error rather than storing data other drivers can't read. Store binary data as a `LuaStream`, which becomes BSON binary.

> `Identifier`, `DateTime` and `Decimal` are **userdata**. For example, `doc._id == "65f0c3..."` is always `false` and `"id: " .. doc._id` errors. Use `tostring(doc._id)`, or compare against `Identifier.FromString("65f0c3...")`. See [Userdata Return Values](#userdata-return-values-read-first).

### Example

```lua
local mongo = assert(MongoDB.Connect("mongodb://localhost:27017"))

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
mongo:GetResult()        -- returns nothing once the worker acknowledges the cancel
                         -- (or the normal result if the operation had already finished)

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
| `Decode` | Parse an XML string and return the root element as a Lua node table. Returns `nil, errmsg` on parse failure or `nil, "Xml: no root element"` when there is none; a non-string argument or nesting deeper than 512 levels raises an error |
| `Dispose` | Explicitly release the instance; called automatically by the GC |

### Node Table Structure

Every XML element is represented as a Lua table with four fields. `Decode` always fills in all four; for `Encode` only `tag` is required (`attr`, `text` and `children` are optional, so `{tag = "a"}` encodes as `<a/>`):

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
- Attributes are written in the order they appear in the `attr` array. Attribute values that aren't strings or numbers are written as `""`.
- When encoding, `text` may also be a number, `UInt`, `Decimal`, `Identifier`, `DateTime` or `TimeSpan` (written as its string form).
- Mixed content (elements that have both `text` and `children`) is supported: `text` is appended as a PCDATA node before the child elements.
- `Decode` returns only the first root element; XML comments, processing instructions, and the XML declaration are ignored in the output table.
- Input must be UTF-8 encoded. The encoder always writes UTF-8.

---

## FileSystem

All path arguments are UTF-8 strings.
On Windows the W-API is used internally so non-ASCII filenames are handled correctly; a path can be up to 1023 characters (UTF-16 code units, however many UTF-8 bytes that is), and a longer one raises `path is too long`.
On Linux the POSIX UTF-8 API is used directly — no wide-char handling is needed; the limit there is 1023 **bytes**.
The limit doesn't apply to `FileSystem.Open`.

All returned names and paths (`GetFiles`, `GetDirectories`, `GetAll` / `GetFileInfo` fields, `CurrentDirectory`, `GetTempFileName`, `GetSpecialFolder`) are plain UTF-8 strings on every platform, so non-ASCII names round-trip exactly and can be passed straight back into FileSystem functions.

### File and Directory Operations

```lua
Array   FileSystem.GetAll(path)
Array   FileSystem.GetFiles(path)
Array   FileSystem.GetDirectories(path)
FileInfo FileSystem.GetFileInfo(path)
file    FileSystem.Open(path, opt mode)
bool    FileSystem.Copy(source, destination, opt overwrite)
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
| `GetFiles` | Returns an array of filenames for all regular files in `path`. On Linux names starting with `.` are omitted and symlinks are not listed; Windows includes hidden files |
| `GetDirectories` | Returns an array of directory names for all subdirectories in `path` (on Linux dot-directories are included, symlinks are not) |
| `GetFileInfo` | Returns a `FileInfo` table for `path`, or `nil` if the path does not exist |
| `Open` | Open a file and return a standard Lua `io` file handle. Same contract as `io.open`: `mode` defaults to `"r"` and must be `"r"`, `"w"` or `"a"`, optionally followed by `+` and/or `b` (anything else raises an error, including a repeated `b` that `io.open` itself accepts); on failure returns `nil, "<path>: <error>", errno` |
| `Copy` | Copy `source` to `destination`. Pass `true` for `overwrite` to allow replacing an existing file (default `false`: the copy fails and returns `false` when `destination` already exists) |
| `Move` | Move (rename across directories) `source` to `destination` |
| `Delete` | Delete a file. On Linux it also deletes an empty directory; on Windows use `RemoveDirectory` for directories |
| `CreateDirectory` | Create a directory at `path`. Returns `true` on success |
| `RemoveDirectory` | Remove an **empty** directory at `path`. Returns `true` on success |
| `Rename` | Rename `source` to `destination` (same filesystem) |
| `SetAttributes` | *(Windows only)* Set Win32 file attribute flags. On Linux it exists but always returns `false` |

### FileInfo table

Returned by `GetFileInfo` and `GetAll`:

| Field | Type | Description |
|-------|------|-------------|
| `FileName` | string | Entry name (without path) |
| `isFolder` | boolean | `true` when the entry is a directory |
| `Size` | number | File size in bytes (`0` for directories on Windows; the directory's `st_size`, typically 4096, on Linux) |
| `Creation` | number | Creation time as a Unix timestamp (on Linux `st_ctime`, the last status change) |
| `Access` | number | Last access time as a Unix timestamp |
| `Write` | number | Last write time as a Unix timestamp |
| `isLink` | boolean | `true` when the entry is a link to another path: a symlink, or on Windows also a junction or volume mount point. Other reparse points (OneDrive/cloud placeholders, dedup, compressed files) are `false`. Recursive walks should skip folders where this is `true` to avoid cycles |
| `LinkType` | string | *(optional)* `"symlink"`, `"junction"` or `"mount"` (Windows volume mount point); present only for links |
| `Link` | string | *(optional)* Link target as stored: relative for relative symlinks, a drive path such as `C:\target` for junctions, `\\?\Volume{guid}\` for volume mount points. Present when `isLink` is `true` and the target could be read. On Linux a symlink to a directory also has `isFolder = true` |
| `isPlaceholder` | boolean | `true` when the entry is not (fully) stored on this device: an online-only OneDrive/cloud file or folder, or a file offloaded by storage management. Set from `FILE_ATTRIBUTE_RECALL_ON_DATA_ACCESS`, `FILE_ATTRIBUTE_RECALL_ON_OPEN` or `FILE_ATTRIBUTE_OFFLINE`. Reading such a file downloads it, and listing a placeholder folder can make the sync provider fetch its listing (slow, may stall while offline). Files set to "Always keep on this device" or already downloaded are `false`. Always `false` on Linux |
| `ReparseTag` | number | *(Windows only, optional)* Raw `IO_REPARSE_TAG_*` value, present on every reparse point (including non-link ones such as cloud placeholders) |
| `AlternateFileName` | string | *(Windows only)* 8.3 short name |
| `Attributes` | number | *(Windows only)* Win32 `FILE_ATTRIBUTE_*` bitmask |

The timestamps are Unix timestamps (whole seconds since 1970-01-01 UTC) on every platform, directly comparable with `os.time()`. On Linux, `GetAll` omits `Size` and the timestamps when the entry can't be `stat`ed.

A recursive walk that skips links cannot loop, and skipping placeholders keeps it local. Check `isLink` before `isFolder`, because junctions and directory symlinks are folders too (Linux bind mounts are not detected as links):

```lua
local function walk(dir, visit)
    for _, e in ipairs(FileSystem.GetAll(dir)) do
        if not (e.isLink or e.isPlaceholder) then
            local path = dir .. '/' .. e.FileName
            visit(path, e)
            if e.isFolder then walk(path, visit) end
        end
    end
end
```

### Path and Directory Utilities

```lua
string  FileSystem.CurrentDirectory()
bool    FileSystem.SetCurrentDirectory(path)
string  FileSystem.GetTempFileName(opt dirOnly)
Array   FileSystem.GetDrives(opt drive)
string  FileSystem.GetSpecialFolder(opt csidl)   -- Windows only
```

| Function | Description |
|----------|-------------|
| `CurrentDirectory` | Returns the process current working directory as a plain UTF-8 string |
| `SetCurrentDirectory` | Changes the current working directory. Returns `true` on success |
| `GetTempFileName` | Creates a temporary file and returns its path as a string. On Windows, `GetTempFileName(true)` returns the temp directory instead of creating a file (Linux ignores the argument) |
| `GetDrives` | Returns an array of drive tables (see below). On Windows, passing a drive letter (`"C"`) returns **that single drive table** (not an array), or `nil` for an invalid letter. Linux ignores the argument and always returns `{ {Drive = "/", ...} }` |
| `GetSpecialFolder` | *(Windows only)* Returns the path for a CSIDL folder constant (default `0x0010`, Desktop Directory) as a string, or `nil` on failure. Not registered on Linux (calling it raises "attempt to call a nil value") |

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

## Image

Pixel-level PNG loading/editing/saving, native to the engine (no external
process, no window/OpenGL context required). Decoding uses `stb_image`;
encoding uses `stb_image_write`; PNG `tEXt`/`iTXt` metadata tags are read and
written by a small hand-rolled chunk reader/writer on top of both. `img` is
an `Image` object returned by `Open`/`New`/`FromBytes`/`Crop`/`Resize`/`Clone`.

Pixels are RGBA8 (straight, non-premultiplied alpha). Coordinates are
0-indexed pixel coordinates (`x` = column, `y` = row, origin top-left) — not
Lua's usual 1-indexed convention. `Open` / `FromBytes` decode PNG, JPEG, BMP, TGA, GIF (first frame), PSD, HDR, PIC and PNM (everything `stb_image` reads); **saving is PNG only**, and metadata tags are read from PNG input only.

```lua
Image  Image.Open(path)
Image  Image.New(width, height)
Image  Image.FromBytes(data)

table  img:GetMetadata()
       img:SetMetadata(key, value)
number img:GetWidth()
number img:GetHeight()
int,int,int,int img:GetPixel(x, y)
       img:SetPixel(x, y, r, g, b, opt a)

Image  img:Crop(x, y, w, h)
Image  img:Resize(width, height, opt filter)
Image  img:Clone()

       img:FillRect(x, y, w, h, r, g, b, opt a)
       img:DrawLine(x1, y1, x2, y2, r, g, b, opt a, opt thickness, opt antialias)
       img:DrawCircle(cx, cy, radius, r, g, b, opt a, opt filled, opt antialias)
       img:Composite(otherImg, x, y, opt alpha)
int,int,int,int,int img:Diff(otherImg)

int    img:Save(path)
string img:ToBytes()

number,number,number Image.RGBtoHSV(r, g, b)
int,int,int          Image.HSVtoRGB(h, s, v)
number,number,number Image.RGBtoHSL(r, g, b)
int,int,int          Image.HSLtoRGB(h, s, l)

       img:Tint(r, g, b, opt strength)
       img:RecolorPalette(mapping)
       img:AdjustHSV(opt hueShift, opt saturationMul, opt valueMul, opt x, opt y, opt w, opt h)
       img:Dither(palette, opt amount)
       img:Outline(r, g, b, opt a, opt thickness)

       img:Stamp(brushImg, x, y, opt opacity)
       img:StrokePath(brushImg, points, opt spacing, opt opacity)

Image  img:FlipHorizontal()
Image  img:FlipVertical()
Image  img:Rotate90(opt clockwise)
Image  img:PadCanvas(left, top, right, bottom)
Image  img:DropShadow(offsetX, offsetY, blurRadius, r, g, b, opt a)

       img:Blur(radius, opt passes)
       img:Invert(opt x, opt y, opt w, opt h)
       img:Grayscale(opt strength, opt x, opt y, opt w, opt h)
       img:AdjustBrightnessContrast(opt brightness, opt contrast, opt x, opt y, opt w, opt h)
       img:Threshold(cutoff, r, g, b, opt a, opt useAlpha)
       img:ApplyMask(maskImg, opt useLuminance)
       img:Noise(opt amount, opt seed, opt monochrome, opt x, opt y, opt w, opt h)

       img:FillGradientLinear(x1, y1, r1, g1, b1, a1, x2, y2, r2, g2, b2, opt a2)   -- a1 must be passed (nil = 255)
       img:FillGradientRadial(cx, cy, radius, r1, g1, b1, a1, r2, g2, b2, opt a2)   -- a1 must be passed (nil = 255)
       img:FillPolygon(points, r, g, b, opt a)
       img:DrawText(text, x, y, r, g, b, opt a, opt scale)

table  img:ExtractPalette(opt n)
```

| Function | Description |
|----------|-------------|
| `Open` | Decodes an image file (PNG, JPEG, BMP, …) from disk into a new `Image`. Raises a Lua error if the file cannot be read or decoded |
| `New` | Creates a blank, fully-transparent `width`x`height` canvas |
| `FromBytes` | Decodes an image already held in memory (e.g. base64-decoded bytes) — no disk I/O |
| `GetMetadata` | Returns `{ width, height, tags = { key = value, ... } }`. `tags` merges every `tEXt`/`iTXt` chunk found at decode time (last chunk for a given key wins); `zTXt` and compressed `iTXt` chunks are not read. Keys and values are UTF-8 (`tEXt` chunks are Latin-1 by spec and are converted) |
| `SetMetadata` | Sets (or overwrites) one tag. Tags are written as uncompressed UTF-8 `iTXt` chunks by `Save`/`ToBytes` |
| `GetWidth` / `GetHeight` | Convenience accessors, equivalent to the fields on `GetMetadata()` |
| `GetPixel` | Returns `r, g, b, a` (0-255) at `(x, y)`. Errors if out of bounds |
| `SetPixel` | Overwrites the pixel at `(x, y)` directly (no blending). `a` defaults to 255. Errors if out of bounds |
| `Crop` | Returns a new `Image` containing the `w`x`h` rect at `(x, y)`. Non-mutating; carries tags over. Errors if the rect doesn't fit |
| `Resize` | Returns a new `Image` at `width`x`height`, non-mutating, carries tags over. `filter` is `"bilinear"` (default, smooth) or `"nearest"` (no blending -- crisp pixel-art edges when zooming in for pixel-perfect inspection, or scaling up retro-style art without smearing) |
| `Clone` | Returns an independent copy of the image, including tags |
| `FillRect` | Alpha-composites (`over`) a solid color into the `w`x`h` rect at `(x, y)`, mutating in place. Clipped silently to the canvas; `a` defaults to 255 |
| `DrawLine` | Alpha-composites a line from `(x1,y1)` to `(x2,y2)`, `thickness` pixels wide (default 1) |
| `DrawCircle` | Alpha-composites a circle at `(cx,cy)` with `radius`. `filled` defaults to `true`; pass `false` for a ~1px outline |
| `Composite` | Alpha-blends `otherImg` onto `img` at offset `(x, y)`. `alpha` (0-1, default 1) is an extra global opacity multiplier on top of `otherImg`'s own per-pixel alpha |
| `Diff` | Compares two same-sized images pixel-by-pixel. Returns `x, y, w, h, changedCount` — the bounding box of every differing pixel — so a hand-edited region can be found and transplanted without diffing the whole image. Returns `nil, nil, nil, nil, 0` if identical. Errors on a dimension mismatch |
| `Save` | Encodes to PNG (tags included) and writes to `path`. Returns the number of bytes written |
| `ToBytes` | Encodes to PNG (tags included) and returns it as a Lua string, with no disk I/O |
| `RGBtoHSV` / `HSVtoRGB` | Convert between 0-255 RGB and HSV (`h` in `[0,360)`, `s`/`v` in `[0,1]`). Pure math, no `Image` object involved |
| `RGBtoHSL` / `HSLtoRGB` | Convert between 0-255 RGB and HSL (`h` in `[0,360)`, `s`/`l` in `[0,1]`) |
| `Tint` | Recolors every opaque pixel toward `(r,g,b)`'s hue/saturation while keeping each pixel's own brightness (HSV value) — reuses one base sprite as a "team color"/"ore type" variant without losing its shading. `strength` (0-1, default 1) blends toward the tint |
| `RecolorPalette` | Exact color-swap recoloring. `mapping` is `{ {fromR,fromG,fromB, toR,toG,toB, opt tolerance}, ... }` — the first rule within `tolerance` (per-channel max difference, default 0 = exact) wins for each pixel |
| `AdjustHSV` | Adjusts hue (`hueShift`, degrees), saturation and value (`saturationMul`/`valueMul` multipliers, 1.0 = unchanged) over a region (defaults to the whole image) |
| `Dither` | Quantizes to `palette` (`{ {r,g,b}, ... }`) using 4x4 ordered (Bayer) dithering — breaks up flat color bands instead of hard banding. `amount` (default 32) is the dither strength in 0-255 units |
| `Outline` | Grows a colored, `thickness`-pixel border (default 1, `a` default 255) around the image's existing silhouette (based on a snapshot of the original alpha, so it doesn't bleed into itself) |
| `Stamp` | Pastes `brushImg` centered on `(x, y)`, alpha-blended, its own per-pixel alpha further scaled by `opacity` (default 1) |
| `StrokePath` | Drags `brushImg` along a polyline (`points` = `{ {x,y}, ... }`), stamping it every `spacing` pixels (default 1, minimum 0.5) of travelled distance so a fast stroke has no gaps |
| `FlipHorizontal` / `FlipVertical` | Return a new, mirrored `Image` (non-mutating, like `Crop`/`Resize`/`Clone`) |
| `Rotate90` | Returns a new `Image` rotated 90°, dimensions swapped. `clockwise` defaults to `true` |

`DrawLine` and `DrawCircle` both take a trailing optional `antialias` boolean (default `false`) — hard edges are usually what pixel art wants, so smooth, coverage-based edges are opt-in rather than the default.

| Function | Description |
|----------|-------------|
| `PadCanvas` | Returns a new, larger `Image` with the original pasted at `(left, top)` and the new border transparent |
| `DropShadow` | Renders a blurred, offset, colored silhouette of `img` into a new (larger) canvas — the `shadow.png` workflow the factorio-art pipeline currently does by hand. The canvas grows to fit both the blur falloff and however far `offsetX`/`offsetY` shift the silhouette, so large offsets (Factorio shadows commonly shift 40+ px) don't clip. Exact layout: `margin = blurRadius + 2 + max(|offsetX|, |offsetY|)` is added on every side, and the shadowed silhouette sits at `(margin + offsetX, margin + offsetY)`, so the original image aligns at `(margin, margin)`. Tags are not copied |
| `Blur` | Separable box blur, alpha-aware (won't bleed black into transparent edges). `passes` (default 3) approximates a Gaussian falloff |
| `Invert` | Inverts RGB (255-channel) over a region (defaults to the whole image); alpha is untouched |
| `Grayscale` | Desaturates toward luminance (`0.299R+0.587G+0.114B`); `strength` (0-1, default 1) blends between original and full grayscale |
| `AdjustBrightnessContrast` | `brightness` is additive (-255..255); `contrast` uses the standard contrast-correction-factor formula (-255..255, 0 = unchanged) |
| `Threshold` | Two-tone stencil: pixels at/above `cutoff` (0-255, luminance by default or alpha if `useAlpha`) become `(r,g,b,a)`, everything else becomes fully transparent |
| `ApplyMask` | Multiplies `img`'s alpha by `maskImg`'s alpha (or luminance, if `useLuminance`) — combine an arbitrary painted/generated shape as a stencil. Both images must be the same size |
| `Noise` | Deterministic per-pixel random offset (seeded, so the same `seed` always reproduces the same grain) over a region — texture/grit without an AI round-trip. `amount` defaults to 20 and `seed` to 12345. `monochrome` (default true) offsets all channels together so grain doesn't shift hue |
| `FillGradientLinear` | Alpha-composites a gradient between `(r1,g1,b1,a1)` at `(x1,y1)` and `(r2,g2,b2,a2)` at `(x2,y2)` across the whole image. `a1` sits in the middle of the argument list, so it can't be skipped: pass `nil` (255) or a value |
| `FillGradientRadial` | Alpha-composites a gradient from `(r1,g1,b1,a1)` at the center `(cx,cy)` to `(r2,g2,b2,a2)` at `radius`. As above, `a1` must be passed (`nil` for 255) |
| `FillPolygon` | Even-odd scanline fill of an arbitrary polygon (`points` = `{ {x,y}, ... }`) |
| `DrawText` | Draws `text` with a bundled 3x5 bitmap font, `scale` pixels per font-pixel (default 1). Only digits, space, `-`, `.`, `:` are supported today — enough for coordinate/measurement labels; anything else renders as a blank cell rather than erroring |
| `ExtractPalette` | Returns the top `n` (default 8) most common colors as `{ {r=,g=,b=,count=}, ... }`, ranked by frequency (colors are bucketed to 5 bits/channel so near-identical shades merge). `Dither` expects `{ {r,g,b}, ... }` with positional entries, so convert first: `local p = {}; for i, c in ipairs(pal) do p[i] = {c.r, c.g, c.b} end` (passing the result directly dithers everything to black) |

---

## Sound

PCM sample-level audio creation/editing, native to the engine (no external process, no playback/SDL_mixer dependency required). Decoding/encoding uses `dr_wav` for WAV and `libogg`/`libvorbis` for OGG. `snd` is a `Sound` object returned by `New`/`Tone`/`Noise`/`Open`/`FromBytes`/`Clone`/`Slice`/`Concat`/`Resample`.

Samples are stored internally as float (-1..1) regardless of the source file's format/bit depth, so edits don't ratchet quantization the way repeated int16 in-place edits would. Frames are 0-indexed (`frame` = 0..`GetFrameCount()-1`), matching `Image`'s 0-indexed pixel coordinates rather than Lua's 1-indexed convention.

`Open`/`FromBytes` auto-detect WAV vs OGG by sniffing the data's magic bytes (`RIFF` vs `OggS`) rather than trusting a file extension -- this matters most for `FromBytes`, which has no filename to go on. `Save`/`ToBytes` default to WAV (uncompressed 16-bit PCM -- the format Factorio's modding system and `SDL.Audio.LoadRaw` both accept directly) but take an optional `format` of `"ogg"` for roughly an order-of-magnitude smaller files at the cost of lossy compression -- useful when a mod's size cap makes shipping WAV assets impractical. OGG's `quality` follows libvorbis's own 0.0-1.0 VBR scale (~0.4 is roughly "average", ~0.6 is a good general-purpose default for game sound effects).

```lua
Sound  Sound.New(sampleRate, channels, frameCount)
Sound  Sound.Tone(sampleRate, channels, frameCount, frequency, opt waveform, opt amplitude)
Sound  Sound.Noise(sampleRate, channels, frameCount, opt amplitude, opt noiseType)
Sound  Sound.Open(path)
Sound  Sound.FromBytes(data)

number snd:GetSampleRate()
number snd:GetChannels()
number snd:GetFrameCount()
number snd:GetDuration()
number snd:GetSample(frame, channel)
       snd:SetSample(frame, channel, value)

Sound  snd:Clone()
Sound  snd:Slice(startFrame, frameCount)
Sound  snd:Concat(otherSnd)
Sound  snd:Resample(newSampleRate)
Sound  snd:ToMono()
Sound  snd:ToChannels(channels)

       snd:Mix(otherSnd, atFrame, opt gain)
       snd:ApplyGain(gain, opt startFrame, opt frameCount)
       snd:Fade(startFrame, frameCount, fromGain, toGain)
       snd:Normalize(opt targetPeak)
       snd:Reverse()
       snd:Filter(type, cutoffHz, opt Q)

number,number snd:GetPeak()
number        snd:GetRMS()

int    snd:Save(path, opt format, opt quality)
string snd:ToBytes(opt format, opt quality)
```

| Function | Description |
|----------|-------------|
| `New` | Creates a silent buffer with the given sample rate, channel count, and frame count |
| `Tone` | Generates a fixed-frequency waveform identically on every channel. `waveform` is `"sine"` (default), `"square"`, `"triangle"`, or `"saw"`. `amplitude` (0-1, default 1) |
| `Noise` | Generates noise at `amplitude` (0-1, default 1, exact peak regardless of type). `noiseType` is `"white"` (default, flat spectrum, every sample independent -- the harshest/most "static"-like), `"pink"` (more low-frequency energy, less high -- audibly softer than white), or `"brown"` (even more bottom-heavy, a simple leaky-integrator rumble -- reads as more distant/muffled). All three are still recognizably broadband noise/static in character; getting a genuinely organic hiss/steam texture is more of a sampling problem than something these generators (or `Filter` on top of them) can produce from scratch. Each channel gets independent noise. Uses the C `rand()`, so unlike `Image:Noise` there is no seed and the output isn't reproducible |
| `Open` | Decodes a WAV or OGG file from disk into a new `Sound` (format auto-detected). Raises a Lua error if the file cannot be read or decoded |
| `FromBytes` | Decodes a WAV or OGG buffer already held in memory -- no disk I/O |
| `GetSampleRate` / `GetChannels` / `GetFrameCount` | Convenience accessors |
| `GetDuration` | Length in seconds (`GetFrameCount() / GetSampleRate()`) |
| `GetSample` | Returns the sample (-1..1) at `(frame, channel)`. Errors if out of bounds |
| `SetSample` | Overwrites the sample at `(frame, channel)`, clamped to -1..1. Errors if out of bounds |
| `Clone` | Returns an independent copy |
| `Slice` | Returns a new `Sound` containing `frameCount` frames starting at `startFrame`. Errors if the range doesn't fit |
| `Concat` | Returns a new `Sound` with `otherSnd`'s frames appended after `snd`'s. Errors on a channel-count or sample-rate mismatch |
| `Resample` | Returns a new `Sound` at `newSampleRate`, linear-interpolated, with a proportionally scaled frame count |
| `ToMono` | Returns a new 1-channel `Sound`, each frame averaged across `snd`'s channels |
| `ToChannels` | Returns a new `Sound` at `channels` channels. Downmixes to mono (same averaging as `ToMono`) then broadcasts to every output channel; `channels == snd:GetChannels()` returns an independent copy, same as `Clone`. Use this before `Mix`/`Concat` when channel counts don't match -- those still error on a mismatch rather than silently converting |
| `Mix` | Additively mixes `otherSnd` into `snd` starting at `atFrame`, in place, scaled by `gain` (default 1) and clamped. Frames that fall outside `snd`'s bounds are clipped silently, like `Image:Composite` clips off-canvas pixels. Errors on a channel-count mismatch |
| `ApplyGain` | Multiplies samples by `gain` over `[startFrame, startFrame+frameCount)` (defaults to the whole buffer), in place, clamped |
| `Fade` | Applies a linear gain envelope from `fromGain` to `toGain` across `[startFrame, startFrame+frameCount)`, in place, clamped. Raises an error if the range doesn't fit (unlike `ApplyGain`, which clips the range). Call twice (e.g. 0→1 then 1→0) for a fade-in/fade-out |
| `Normalize` | Scales every sample so the buffer's peak absolute value becomes `targetPeak` (default 1.0; must be > 0, otherwise it raises). No-ops on silence |
| `Reverse` | Reverses frame order in place (all channels) |
| `Filter` | Applies a standard biquad (2nd-order IIR) filter to the *whole* buffer, in place -- `type` is `"lowpass"`, `"highpass"`, `"bandpass"`, or `"notch"`; `cutoffHz` must be between 0 and Nyquist (`sampleRate/2`); `Q` (default ~0.707, maximally-flat) controls resonance/bandwidth. Filters carry state between samples, so unlike `ApplyGain`/`Fade` there's no sub-range option -- filtering only part of a buffer would leave an audible click at the boundary. Useful for muffling a tone, shaping a click/pop's character, or changing `Noise`'s brightness |
| `GetPeak` | Returns `min, max` sample values across the whole buffer, measured from 0: `min` is never above 0 and `max` never below 0 (effectively `min(0, lowest), max(0, highest)`) |
| `GetRMS` | Returns the RMS (root-mean-square) level across the whole buffer |
| `Save` | Encodes and writes to `path`. `format` is `"wav"` (default, 16-bit PCM) or `"ogg"`; `quality` (OGG only, 0.0-1.0, default 0.6) is libvorbis's own VBR quality scale. Returns the number of bytes written |
| `ToBytes` | Encodes and returns it as a Lua string, with no disk I/O. Same `format`/`quality` args as `Save` |

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
| `Decode` | Parse a YAML string and return the decoded Lua value. Parse errors **raise** a Lua error (`Yaml: parse error ...`); a mapping key that decodes to `nil` (`null`, `~`, empty) is dropped along with its value |
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
| `Identifier` | double-quoted scalar (canonical string) |
| `DateTime` | double-quoted scalar (ISO 8601) |
| `Decimal` | double-quoted scalar (decimal string) |
| `TimeSpan` | double-quoted scalar |
| `UInt` | plain decimal scalar |
| `table` (sequential integer keys) | sequence (`[]` / block `- ` style) |
| `table` (other keys) | mapping (`{}` / block `key: value` style) |
| functions, threads, unsupported userdata | `null` |

### Notes

- **Circular references** raise an error: `Yaml: recursion detected`
- **Table classification**: pure sequential integer-keyed tables (`{1, 2, 3}`) encode as YAML sequences; all others encode as mappings
- **Style**: `Yaml.New()` (flow) produces compact single-line output; `Yaml.New(true)` (block) produces human-readable multi-line output. Both styles decode correctly by the other instance
- **Anchors and aliases** are resolved: `a: &x 5` / `b: *x` gives `b = 5`. An alias to a mapping or sequence returns the **same** Lua table as the anchor (not a copy), so modifying one modifies both; self-references produce a cyclic table (which `Encode` rejects). An alias to an undefined anchor raises `Yaml: undefined alias '*name'`
- **Merge keys** (`<<: *base` or `<<: [*a, *b]`) copy the source mappings' entries into the mapping; explicit keys win, and earlier sources in a list win over later ones
- **Keys**: a null (`~`) or NaN key is dropped, since a Lua table can't hold it; a complex key (`? [a, b]`) becomes a table key
- **Errors**: invalid YAML raises `Yaml: parse error in <context>: <problem> (line N, column M)`; nesting deeper than 1000 levels raises an error
- **Mapping keys** that are strings are always written double-quoted (`"a": 1`)
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
-- "a": 1
-- "b":
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
| `New` | Create a new Toml instance. With `pretty = true`, `Encode` puts a blank line before each section header and indents nested content by 2 spaces per level; the data it decodes to is identical to the compact output (default `false`) |
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
| Datetime / Date / Time | `string` (ISO 8601 format, e.g. `"2024-06-01T12:00:00Z"`; fractional seconds are dropped) |

#### Encode (Lua → TOML)

| Lua type | TOML output |
|----------|-------------|
| `boolean` | `true` / `false` |
| `integer` | integer scalar |
| `float` | float scalar (always includes `.` or `e` so TOML recognises it as float) |
| `string` | basic string (double-quoted, with escapes) |
| `Identifier` | basic string (canonical UUID or OID hex) |
| `DateTime` | bare datetime scalar (no quotes — native TOML datetime type) |
| `Decimal` | basic string |
| `TimeSpan` | basic string |
| `UInt` | bare integer |
| `table` (sequential integer keys) used as value | inline array `[...]` |
| empty `table` | inline empty table `{}` (decodes back as an empty table) |
| `table` (string keys) at root or as sub-key | `[section]` header block (nested: dotted `[a.b.c]`) |
| array whose elements are all tables | one `[[section]]` header block per element |
| `nil`, functions, unsupported types | empty string `""` |

### Key Quoting

Keys that consist only of `A–Z a–z 0–9 - _` are written as bare keys. All other keys are written as double-quoted basic strings. In dotted section headers each segment is quoted on its own, so `{["my key"] = {sub = {k = 1}}}` is written as `["my key".sub]`.

### Notes

- **Circular references** raise an error: `Toml: recursion detected`
- **Top-level value must be a table** — `Encode` raises an error if passed a non-table value, because TOML documents always have a root mapping
- **Sub-tables** are emitted as section headers after all plain keys at the current level. Nested sub-tables use dotted headers (`{app = {sub = {k = 1}}}` → `[app]` then `[app.sub]`) and round-trip at any depth
- **Arrays of tables** are emitted as `[[section]]` blocks, one per element, also with dotted paths when nested (`[[app.list]]`). Arrays that mix tables with other values, or hold arrays, are written inline instead
- **Empty sub-tables** are written as `key = {}` and decode back as empty tables
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

-- Deeper nesting uses dotted headers; pretty mode adds blank lines and indentation
local pretty = Toml.New(true)
print(pretty:Encode({ app = { name = 'kitsune', db = { port = 5432 } } }))
-- [app]
--   name = "kitsune"
--
--   [app.db]
--     port = 5432

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
AliveToken  AliveToken.App                         -- app-level token, killed at engine shutdown
```

`AliveToken.App` is created with the engine and killed when the engine shuts down, just before the Lua state closes. Link long-running work to it (`myToken:Link(AliveToken.App)`) so it stops cleanly on shutdown.

| Function | Description |
|----------|-------------|
| `New(opt timeoutMs)` | Create a live token. Pass a positive integer to set an automatic timeout in milliseconds; the token expires after that duration when `IsAlive`, `ErrorIfDead`, or any internal poll point checks it. Pass nothing (or `0`) for a token that only expires via `Dispose` |
| `IsAlive` | Returns `true` while the token is alive. Checks the timeout and all linked parents on every call |
| `Dispose` | Marks the token as disposed immediately, regardless of timeout. Idempotent |
| `ErrorIfDead` | Raises a Lua error if the token is disposed or timed out (default message "Cancellation token was cancelled") |
| `Link` | Attach one or more parent tokens. The child token becomes dead whenever any linked parent is disposed, timed out, or itself has a dead parent. Can be called multiple times to add more parents incrementally. Propagates through chains (grandparent → parent → child) |

### Timeout

When `timeoutMs` is given, liveness is checked lazily on every call to `IsAlive`, `ErrorIfDead`, or any internal C++ poll point (`HelperWaitCont`, `accept_body`, `pubsub_cont`, etc.). The token's `alive` flag is set to `0` the first time the deadline is found to have passed — there is no background timer or thread.

```lua
-- Expires automatically after 5 seconds
local token = AliveToken.New(5000)

-- Without timeout — only Dispose() stops it
local token = AliveToken.New()
```

`tostring` on a timed token includes the remaining milliseconds while alive (a timed-out token also shows as `disposed`):

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
- All modules that accept `SetAliveToken` (`HttpClient`, `HttpServer`, `WebSocket`, `MySQL`, `Postgres`, `MongoDB`, `Redis Subscribe`) and `Sleep(token)` check the token through the same `alivetoken_tick` function — linking, timeout, and dispose all work transparently. Kafka has no `SetAliveToken`.

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

The `"__global"` pseudo-section holds any key/value pairs that appear before the first `[section]` header in the file. `Decode` **always** creates it, even when empty, so skip it when iterating sections with `pairs(result)`. When encoding, bare scalar values at the top level of the table are also treated as global keys.

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
TaskStatus.Cancelled = 6   -- Cancel() requested and not yet processed (see Cancellation)
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
bool task:Resume()          -- wake any suspended state (Pause/Sleep/Wait); no value delivered
bool task:Resume(value)     -- wake; value delivered only when the target was Paused — discarded for Sleep/Wait
```

`Resume` returns `true` only if the target was Paused, Sleeping or Waiting at that moment. Otherwise (still running, idle between yields, finished) it returns `false` and **the value is discarded**, not queued. So wait until the target is actually paused before each `Resume(value)`.

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

-- Dispatch work from another coroutine. Resume only delivers to a Paused task,
-- so wait for the worker to pause before each send:
local function send(task, value)
    while task:GetStatus() ~= TaskStatus.Paused do
        if task:Finished() then return false end
        Sleep(1)
    end
    return task:Resume(value)
end
send(worker, "job-1")
send(worker, "job-2")
send(worker, nil)   -- signal shutdown
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
- Raises a Lua error if called outside a scheduler-managed coroutine, or from an inline call ("task:Wait: cannot yield from an inline coroutine").

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
value  task:GetResult()       -- the coroutine's FIRST return value, or nil when it has not finished yet
value  task:ConsumeResult()   -- like GetResult, but immediately frees the result and releases the slot
```

Only the first value a task returns is kept; return a table to pass back several values. `GetResult` is non-destructive — the slot and its result stay pinned until all handles are GC'd. Use this when you need to read the result multiple times or keep the slot observable.

`ConsumeResult` frees the result data immediately after pushing it to the Lua stack and advances the slot to `RELEASED` so the scheduler can compact it on the next tick — no waiting for GC. Use this when you want to eagerly release a large result (e.g. a full database query table) as soon as it has been consumed. After calling `ConsumeResult`, `Finished()` returns `true` and `GetResult()` returns `nil`.

If the task faulted (has an error), `ConsumeResult` returns `nil` and still releases the slot — call `GetError()` before `ConsumeResult()` if you need the error message.

### Cancellation

```lua
task:Cancel()   -- signals the coroutine to be terminated before its next resume; no-op if already done
```

The coroutine is not stopped immediately; the scheduler sets `interrupted` and terminates it at the next scheduling opportunity. `TaskStatus.Cancelled` is only visible while the cancel is pending. Once the scheduler has processed it, the slot is freed straight away (even while a handle is still live): `GetStatus()` returns `TaskStatus.None`, `GetError()` returns `nil` and `Finished()` returns `true`. Neither `OnError` nor the global error handler fires for a cancel. Engine shutdown ends running tasks the same way.

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

-- Open a coroutine from its id (e.g. one obtained with Tasks.GetCurrentId() inside it)
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

A local LLM inference module backed by [llama.cpp](https://github.com/ggml-org/llama.cpp). Runs GGUF models on CPU or GPU (CUDA). Generation is dispatched to a **persistent background worker thread** per context; the calling coroutine uses non-blocking `Poll()` calls cooperatively. Exceptions: `ctx:LoadModel()` and `ctx:Embed()` block the calling OS thread until they finish (they don't yield).

> **Platform note:** llama.cpp itself supports Windows and Linux. The prebuilt vendor binaries bundled with this project (`vendor/fetch-llama.ps1`) are Windows-only (CUDA + AVX2 DLLs). To enable Llama on Linux, build llama.cpp from source and link against it — the C++ integration code is fully cross-platform and compiles cleanly on Linux when `KITSUNE_LLAMA` is defined.

> **Note:** `Llama.CreateContext` lazily initialises the llama.cpp and ggml backends on first call. The CUDA backend is loaded automatically when `ggml-cuda.dll` / `libggml-cuda.so` is present in the output directory.

### Module-level

```lua
LlamaContext  Llama.CreateContext(opt opts)
LlamaPrompt   Llama.CreatePrompt()
ToolSuite     Llama.CreateToolSuite()
table         Llama.GetLogs()
table|nil     Llama.PeekModel(string path)
```

#### Llama.PeekModel

```lua
table|nil, string  Llama.PeekModel(string path)
```

Reads model metadata from a `.gguf` file without loading any weights. Only the vocabulary and GGUF header are parsed — this takes a fraction of a second and uses negligible memory regardless of model size. Returns a table on success, or `nil, err` on failure.

**Returned table fields:**

| Field | Type | Description |
|-------|------|-------------|
| `desc` | string | Human-readable model description |
| `arch` | string | Model architecture (e.g. `"llama"`, `"qwen2"`) |
| `context_length` | integer | Maximum context length the model was trained with |
| `n_params` | integer | Total parameter count |
| `n_embd` | integer | Embedding dimension |
| `n_layer` | integer | Number of layers |
| `size_bytes` | integer | Model file size in bytes |
| `chat_template` | string | Embedded chat template string (empty if none) |
| `has_encoder` | boolean | Whether the model has an encoder |
| `has_decoder` | boolean | Whether the model has a decoder |
| `is_recurrent` | boolean | Whether the model uses recurrent state (e.g. Mamba, RWKV) |
| `capabilities` | string[] | Detected capability list. Values: `"completion"`, `"tools"`, `"reasoning"`, `"vision"`, `"embedding"`, `"recurrent"` |
| `meta` | table | All raw GGUF metadata key/value pairs as strings |

```lua
local info, err = Llama.PeekModel("/models/qwen2.5-7b-q4_k_m.gguf")
if not info then
    print("Error:", err)
else
    print(info.desc, "ctx:", info.context_length, "params:", info.n_params)
    print("recurrent:", info.is_recurrent)
    print("capabilities:", table.concat(info.capabilities, ", "))
end
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
| `n_threads` | integer | `0` | CPU inference threads. `0` = auto: half the hardware thread count |
| `n_batch` | integer | `512` | Prompt prefill batch size. Controls how many tokens are processed per decode call during prompt ingestion. Smaller values use less memory at the cost of slower prefill; larger values are faster but use more memory. Independent of `n_ctx` — the engine chunks the prompt automatically so this never needs to match or exceed `n_ctx` |
| `flash_attn` | boolean | `false` | Enable Flash Attention |
| `model_ttl_ms` | integer | `300000` | Milliseconds of idle time before the model is automatically unloaded. `0` disables auto-unload |
| `use_mmap` | boolean | `true` | Memory-map the model file. When `true` the OS pages weights from disk on demand, keeping RAM usage low but causing page faults on first access. Set to `false` to load all weights into RAM up front for more consistent inference latency |
| `use_mlock` | boolean | `false` | Lock memory-mapped pages into RAM so the OS cannot swap them out. Has no effect when `use_mmap` is `false`. May require elevated privileges on some systems |
| `offload_kqv` | boolean | `true` | Offload the KV cache (keys, queries, values) to GPU VRAM. When `true` the KV cache lives on the GPU alongside the weights, which is faster. Set to `false` to keep the KV cache in system RAM — useful when VRAM is tight and you need a long context window at the cost of some performance |

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

-- Long context on a VRAM-limited GPU: keep KV cache in RAM
local ctx = Llama.CreateContext({
    n_gpu_layers = 99,
    n_ctx        = 32768,
    offload_kqv  = false, -- KV cache in system RAM to free VRAM for weights
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
true          ctx:SetModel(path [, opts])
true          ctx:LoadModel()
true          ctx:UnloadModel()
true, path    ctx:IsModelLoaded()
false[, err]  ctx:IsModelLoaded()
bool          ctx:IsReady()
bool          ctx:Generate(prompt [, opts] [, tools])
ok, data      ctx:Poll()
bool          ctx:Stop()
bool          ctx:Reset()
float[]       ctx:Embed(text)
table         ctx:Info()
LlamaPrompt   ctx:TrimPrompt(prompt)
bool          ctx:Dispose()
```

---

#### ctx:SetModel

```lua
true         ctx:SetModel(path [, opts])
nil, errmsg  ctx:SetModel(path [, opts])
```

Sets the path to the GGUF model file. Does not load or unload anything immediately. Returns `nil, "busy"` if the worker is loading or generating.

If the new path differs from the currently loaded model, the swap happens automatically the next time `Generate` is called — the old model is unloaded, the new one is loaded, and the KV cache is reset before generation begins. Explicit `UnloadModel` + `LoadModel` calls are only needed if you want the swap to happen eagerly.

**`opts` fields (all optional):**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `n_gpu_layers` | integer | `-1` (use context default) | Number of layers to offload to GPU |

```lua
ctx:SetModel([[C:\Models\qwen3-0.6b-q8_0.gguf]])
ctx:SetModel([[C:\Models\qwen3-0.6b-q8_0.gguf]], { n_gpu_layers = 0 })  -- CPU only
```

---

#### ctx:LoadModel

```lua
true         ctx:LoadModel()
nil, errmsg  ctx:LoadModel()
```

Loads the model set by `SetModel` **synchronously**: it blocks the calling OS thread (and so the whole scheduler) until loading finishes, then returns `true`, or `nil, errmsg` (e.g. `"failed to load model: <path>"`, `"no model path set"`). Returns `true` straight away if the model is already loaded. If the worker is currently generating or still unloading it is refused with `nil, "busy"` — yield and retry. (When refused, the error string can be a stale message from an earlier failure rather than `"busy"`; check `ctx:Info().context.status` if you need to tell them apart.) Generation loads the model automatically, so an explicit `LoadModel` is only needed to front-load the cost.

```lua
_context:UnloadModel()
while _context:IsModelLoaded() do coroutine.yield() end
_context:SetModel(path)
repeat
    local ok, err = _context:LoadModel()
    if not ok and _context:Info().context.status == 'idle' then error(err) end
    if not ok then coroutine.yield() end
until ok
```

---

#### ctx:UnloadModel

```lua
true         ctx:UnloadModel()
nil, errmsg  ctx:UnloadModel()
```

Queues a model unload on the worker thread and returns immediately. Returns `nil, "busy"` if the worker is generating, loading, or already unloading. Call `Stop()` first to cancel an in-progress generation before unloading.

The unload completes asynchronously — poll `ctx:IsModelLoaded()` to confirm the model is fully released before calling `LoadModel` or `SetModel` + `LoadModel` for a swap:

```lua
_context:UnloadModel()
while _context:IsModelLoaded() do coroutine.yield() end
-- safe to load a new model now
```

---

#### ctx:IsModelLoaded

```lua
true, model_path  ctx:IsModelLoaded()
false [, err]     ctx:IsModelLoaded()
```

Returns `true` and the **actually loaded** model path when a model is currently loaded. Returns `false` with no second value when simply idle/unloaded, or `false, err` if there is an error (e.g. load failure or context disposed).

---

#### ctx:IsReady

```lua
bool  ctx:IsReady()
```

Returns `true` if the context is idle and a model path is set — i.e. ready to accept a `Generate` call. The model may not be loaded yet (`Generate` loads it automatically); use `IsModelLoaded()` to check that. Returns `false` in the `"error"` status.

---

#### ctx:Generate

```lua
true         ctx:Generate(prompt [, opts] [, tools])
nil, errmsg  ctx:Generate(prompt [, opts] [, tools])
```

Queues a generation request. `prompt` must be a `LlamaPrompt` userdata created with `Llama.CreatePrompt()`. Returns immediately; output is consumed via `Poll`.

When generation ends (including when it ends with an error, with whatever partial content was produced), the assistant reply is **automatically appended** to the prompt as a new message (via `prompt:AddAssistantMessage`). If the model produced tool calls they are stored as a structured `tool_calls` array on that message, with `content = ""` and **no** `reasoning`. There is no need to manually push a reply back into the history.

**Automatic model load / swap:** if no model is loaded yet, `Generate` loads the model set by `SetModel` automatically. If a different model is already loaded (i.e. `SetModel` was called with a new path since the last load), the old model is unloaded, the new one is loaded, and the KV cache is cleared — all transparently before generation begins.

Returns `nil, errmsg` when:
- `"already running"` — a generation is already in progress
- `"no model"` — no model has been set / loaded
- `"busy"` — the worker is occupied with another task
- `"empty prompt"` — the prompt contained no messages
- `"disposed"` — the context has been disposed

**`opts` fields (all optional):**

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `temperature` | number | `0.8` | Sampling temperature |
| `top_p` | number | `0.95` | Top-P nucleus sampling |
| `top_k` | integer | `40` | Top-K sampling |
| `min_p` | number | `0.05` | Min-P sampling |
| `seed` | integer | `-1` | RNG seed. `-1` = random |
| `max_tokens` | integer | `2048` | Maximum tokens to generate |

The optional `tools` argument accepts a **`ToolSuite` userdata**, a **JSON string**, or a **Lua table**. It is the 3rd argument when `opts` is present. A `ToolSuite` or JSON string may also be the 2nd argument, but **a Lua table in 2nd position is read as `opts`**, so when passing tools as a table always pass `opts` too (`{}` if you have none). A table is serialized automatically with empty tables encoded as `{}` so parameter schemas are preserved correctly.

```lua
local prompt = Llama.CreatePrompt()
prompt:SetSystem("You are a helpful assistant.")
prompt:AddUserMessage("What is 2 + 2?")

ctx:Generate(prompt, { temperature = 0.3, max_tokens = 512 })

local ok, data = ctx:Poll()
while ok do
    if data and data.type == 'token' then io.write(data.text) end
    Sleep(10)
    ok, data = ctx:Poll()
end
-- errors arrive on the final call, together with ok == false
if data and data.type == 'error' then error(data.text) end
-- prompt now has the assistant reply appended automatically
print(prompt:Last().content)
```

---

#### ctx:Poll

```lua
ok, data  ctx:Poll()
```

Non-blocking. Drains the next token or event from the worker queue.

When `ok` returns `false` (generation complete), the assistant reply is **automatically appended** to the `LlamaPrompt` that was passed to `Generate` — there is no need to manually construct a reply message.

**Return values:**

| Return | Type | Description |
|--------|------|-------------|
| `ok` | boolean | `true` while generation is in progress; `false` when done (successfully or with an error) |
| `data` | table or nil | `nil` when nothing is ready yet; otherwise a table with `text` and `type` fields |

**`data.type` values:**

| Value | Description |
|-------|-------------|
| `"token"` | Regular output token text (`ok = true`) |
| `"reasoning"` | Token inside a `<think>...</think>` block (Qwen3, DeepSeek-R1, QwQ) (`ok = true`). Stored in `prompt:Last().reasoning` after generation, except when the turn produced tool calls |
| `"tool_calls"` | The model called tools (`ok = true`); `data.text` is the JSON array of calls. The structured calls reach `prompt:Last().tool_calls` only once `ok` becomes `false` |
| `"error"` | Generation failed: returned **with `ok = false`**, and `data.text` contains the error message |

When `ok` is `false` generation is over; the poll loop should exit, and **then check `data` for an error**, because the error arrives on that same final call.

```lua
local ok, data = ctx:Poll()
while ok do
    if data then
        if data.type == 'token'     then io.write(data.text) end
        if data.type == 'reasoning' then --[[ discard or log ]] end
    end
    Sleep(10)
    ok, data = ctx:Poll()
end
if data and data.type == 'error' then error(data.text) end
-- reply (including tool_calls if any) is now in prompt:Last()
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

Generates an embedding vector for `text`. **Blocks the calling OS thread** (it does not yield) until the embedding is complete, and loads the model first if needed. The model must support embeddings. Returns a sequential table of floats, **L2-normalised** (unit length, so a dot product is the cosine similarity), or `nil, errmsg` on failure: `"busy"` (not idle, **or no model path set**), `"model does not support embeddings"`, `"embedding decode failed"`.

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

Returns a snapshot of the context state, or `nil, "disposed"` on a disposed context. The returned table has two sub-tables: `context` (always present) and `model` (present only when a model is loaded).

**`info.context` fields:**

| Field | Type | Description |
|-------|------|-------------|
| `status` | string | `"idle"`, `"loading"`, `"generating"`, `"unloading"`, or `"error"` |
| `n_ctx` | integer | Context window size: the configured value before a model is loaded, llama.cpp's actual value after |
| `n_gpu_layers` | integer | Configured GPU layer count |
| `n_threads` | integer | CPU thread count: the full hardware thread count before a model is loaded, the thread count actually in use (half of it when configured as 0) after |
| `n_batch` | integer | Prompt prefill batch size (configured before load, actual after). The engine feeds the prompt in chunks of this size, so it is independent of and never needs to match `n_ctx` |
| `use_mmap` | boolean | Whether the model file is memory-mapped |
| `use_mlock` | boolean | Whether mapped pages are locked in RAM |
| `model_ttl_ms` | integer | Auto-unload timeout in milliseconds |
| `model_path` | string or nil | Path set via `SetModel`, or `nil` |
| `error` | string or nil | Last error message, or `nil` |
| `last_used` | number or nil | Seconds since last generation completed, or `nil` if never used |
| `tokens_used` | integer | Tokens currently occupying the KV cache |
| `tokens_available` | integer | Remaining tokens available in the context window |
| `last_messages_used` | integer | Number of messages from the last `Generate` call that were actually included in the prompt after auto-trimming. `0` before any generation. Compare against your full messages array length to find out how many were silently dropped |

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
| `n_gpu_layers` | integer | Effective GPU layer count (same value as `gpu_layer_count`), not the configured one |
| `gpu_layer_count` | integer | Layers actually offloaded to GPU (capped at `n_layer + 1`, the output layer included) |
| `cpu_layer_count` | integer | Layers running on CPU |
| `gpu_percent` | number | Percentage of layers on GPU (of `n_layer + 1`) |
| `cpu_percent` | number | Percentage of layers on CPU (of `n_layer + 1`) |
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

#### ctx:TrimPrompt

```lua
LlamaPrompt  ctx:TrimPrompt(prompt)
nil          ctx:TrimPrompt(prompt)
nil, errmsg  ctx:TrimPrompt(prompt)
```

Returns a **new** `LlamaPrompt` with the oldest non-system messages dropped until the conversation fits the loaded model's `n_ctx`. Returns `nil` when the prompt already fits or no model is loaded, and `nil, "disposed"` / `nil, "invalid prompt"` on those errors. The original prompt is not changed.

---

#### ctx:Dispose

```lua
bool  ctx:Dispose()
```

Signals the worker thread to shut down, unloads the model, and frees all resources. Idempotent — safe to call multiple times. The GC calls this automatically, but explicit disposal is recommended to release GPU memory promptly.

---

### LlamaPrompt

`LlamaPrompt` is the conversation history object. It owns all messages in a session and is the single source of truth for what has been said. Pass it to `ctx:Generate` and `tools:Call`; the engine appends replies and tool results directly to it.

Create one with `Llama.CreatePrompt()`.

```lua
LlamaPrompt  Llama.CreatePrompt()
```

#### Summary

```lua
prompt:SetSystem(content)
prompt:GetSystem()           -> string
prompt:AddUserMessage(content)
prompt:AddAssistantMessage(content [, reasoning])
prompt:AddToolResult(tool_call_id, content)
prompt:AddMessage(message)
prompt:Export()              -> table
prompt:Import(data)
prompt:Last()                -> table or nil
prompt:Clear()
prompt:TrimmedFrom(index)    -> LlamaPrompt
#prompt                      -> integer
prompt[i]                    -> table or nil
```

#### Message table shape

All methods that return a message return a table with these fields:

| Field | Type | Present when |
|-------|------|--------------|
| `id` | integer | always — 1-based id assigned on append (stable within a prompt; a `TrimmedFrom` copy is renumbered from 1) |
| `role` | string | always — `"user"`, `"assistant"`, or `"tool"` |
| `content` | string | always |
| `reasoning` | string | assistant messages that had a `<think>` block |
| `tool_calls` | table | assistant messages that produced tool calls |
| `tool_call_id` | string | `"tool"` role result messages |

`tool_calls` entries each have `id`, `name`, and `arguments` (JSON string) fields.

---

#### Llama.CreatePrompt

```lua
LlamaPrompt  Llama.CreatePrompt()
```

Creates a new empty prompt with no system message and no history.

```lua
local prompt = Llama.CreatePrompt()
```

---

#### prompt:SetSystem

```lua
prompt:SetSystem(content)
```

Sets (or replaces) the system message. The system message is always prepended to the message list sent to the model and is preserved by `TrimmedFrom`.

```lua
prompt:SetSystem("You are a helpful assistant.")
```

---

#### prompt:GetSystem

```lua
string  prompt:GetSystem()
```

Returns the current system message content, or an empty string if none is set.

---

#### prompt:AddUserMessage

```lua
prompt:AddUserMessage(content)
```

Appends a `"user"` role message.

```lua
prompt:AddUserMessage("What is the weather in Paris?")
```

---

#### prompt:AddAssistantMessage

```lua
prompt:AddAssistantMessage(content [, reasoning])
```

Appends an `"assistant"` role message. `reasoning` is optional and stored on the message for Lua/UI use, but never sent to the model. Called automatically by `Poll` when generation completes — you do not normally need to call this yourself.

---

#### prompt:AddToolResult

```lua
prompt:AddToolResult(tool_call_id, content)
```

Appends a `"tool"` role result message. Called automatically by `tools:Call` — you do not normally need to call this yourself.

```lua
prompt:AddToolResult("call_abc123", "Sunny, 22°C")
```

---

#### prompt:AddMessage

```lua
prompt:AddMessage(message)
```

Appends a message given as a table in the same shape that `prompt[i]` returns. The `role` field is required and must be `"user"`, `"assistant"`, or `"tool"`; any other role raises `AddMessage: unknown role '<role>'`. All other fields follow the same rules as the typed helpers.

| Field | Used by role | Notes |
|---|---|---|
| `role` | all | **Required.** `"user"`, `"assistant"`, or `"tool"`. |
| `content` | all | Message body (optional, defaults to `""`). |
| `reasoning` | `"assistant"` | Optional chain-of-thought text. |
| `tool_call_id` | `"tool"` | Id of the tool call being answered (defaults to `""`). |
| `tool_calls` | `"assistant"` | Array of `{id, name, arguments}` tables. |

The `id` field from `prompt[i]` is ignored — a new stable id is always assigned.

```lua
-- Round-trip a message from one prompt into another
local msg = src[1]
dst:AddMessage(msg)

-- Manually add an assistant message with tool calls
prompt:AddMessage({
    role = "assistant",
    content = "",
    tool_calls = {
        { id = "call_1", name = "get_weather", arguments = '{"city":"Oslo"}' }
    }
})
```

---

#### prompt:Export

```lua
table  prompt:Export()
```

Returns the full prompt as a plain Lua table that can be serialized, saved, and later restored with `Import`. The returned table has two fields:

| Field | Type | Description |
|---|---|---|
| `system` | `string` | The system message, or `""` if none is set. |
| `messages` | `table` | Array of message tables in the same shape as `prompt[i]`. |

```lua
local data = prompt:Export()
-- data.system  -> "You are helpful."
-- data.messages[1].role    -> "user"
-- data.messages[1].content -> "Hello"
```

---

#### prompt:Import

```lua
prompt:Import(data)
```

Clears the prompt and loads it from a table previously produced by `Export`. The `data` argument must have the same structure: an optional `system` string and a `messages` array of message tables. Each message is passed through `AddMessage`, so the same role rules apply.

```lua
local data = prompt:Export()
local p2 = Llama.CreatePrompt()
p2:Import(data)
-- p2 is now an independent copy of prompt
```

---

#### prompt:Last

```lua
table or nil  prompt:Last()
```

Returns the last message as a table, or `nil` if the prompt is empty.

```lua
local last = prompt:Last()
if last and last.tool_calls then
    -- model wants to call a tool
end
```

---

#### prompt:Clear

```lua
prompt:Clear()
```

Removes all messages and the system message. Resets the id counter to 1.

---

#### prompt:TrimmedFrom

```lua
LlamaPrompt  prompt:TrimmedFrom(index)
```

Returns a new `LlamaPrompt` containing messages from `index` (1-based) to the end. The system message is always preserved in the copy, and the copied messages get new ids starting from 1. Useful for passing a sliding window of context to the model while keeping the full history in the original prompt.

```lua
local recent = prompt:TrimmedFrom(#prompt - 5)  -- last 6 messages
ctx:Generate(recent, opts)
```

---

#### #prompt

```lua
integer  #prompt
```

Returns the number of messages in the prompt (excluding the system message).

---

#### prompt[i]

```lua
table or nil  prompt[i]
```

Returns the message at 1-based index `i`, or `nil` if out of range.

```lua
for i = 1, #prompt do
    local msg = prompt[i]
    print(msg.role, msg.content)
end
```

---

### Tool calling

Tools are registered with a `ToolSuite`. Pass the suite to `ctx:Generate` so the model knows what tools are available. When generation finishes, if the model produced tool calls they are stored on `prompt:Last().tool_calls`. Call `tools:Call(prompt)` to dispatch them; results are appended to the prompt automatically. Then call `ctx:Generate(prompt, ...)` again to let the model continue with the tool results.

```lua
local tools = Llama.CreateToolSuite()
tools:AddTool('get_weather', 'Get the weather for a city',
    { {name='city', type='string', description='City name', required=true} },
    function(city) return 'Sunny, 22°C in ' .. city end)

local prompt = Llama.CreatePrompt()
prompt:SetSystem("You are a helpful assistant.")
prompt:AddUserMessage("What is the weather in Paris?")

-- Drain one generation; errors arrive with the final ok == false
local function drain()
    local ok, data = ctx:Poll()
    while ok do
        if data and data.type == 'token' then io.write(data.text) end
        Sleep(10)
        ok, data = ctx:Poll()
    end
    if data and data.type == 'error' then error(data.text) end
end

-- Generate with tool awareness
ctx:Generate(prompt, { temperature = 0.3 }, tools)
drain()

-- If the model called a tool, dispatch it and generate again
if prompt:Last() and prompt:Last().tool_calls then
    tools:Call(prompt)   -- results appended to prompt automatically
    ctx:Generate(prompt, { temperature = 0.3 }, tools)
    drain()
end
```

`tools:Call` also still accepts a raw Lua message table for backwards compatibility (see `suite:Call`).

---

### Reasoning models

Models with `<think>` support (Qwen3, DeepSeek-R1, QwQ) emit chain-of-thought tokens before their final answer. These are returned as `data.type == "reasoning"` by `Poll`.

```lua
local content, reasoning = '', ''
local ok, data = ctx:Poll()
while ok do
    if data then
        if data.type == 'token'     then content   = content   .. data.text end
        if data.type == 'reasoning' then reasoning = reasoning .. data.text end
    end
    Sleep(10)
    ok, data = ctx:Poll()
end
if data and data.type == 'error' then error(data.text) end
print('Reasoning:', reasoning)
print('Answer:',    content)
```

---

### Full example

```lua
local ctx = Llama.CreateContext({ n_gpu_layers = 99, n_ctx = 4096 })
ctx:SetModel([[C:\Models\qwen3-0.6b-q8_0.gguf]])
assert(ctx:LoadModel())   -- blocks until the model is loaded

local info = ctx:Info()
print('Model loaded:', info.model.desc)
print(string.format('GPU: %.0f%%  CPU: %.0f%%', info.model.gpu_percent, info.model.cpu_percent))

local function drain()
    local ok, data = ctx:Poll()
    while ok do
        if data and data.type == 'token' then io.write(data.text) end
        Sleep(10)
        ok, data = ctx:Poll()
    end
    if data and data.type == 'error' then error(data.text) end
    print()
end

-- First turn
local p = Llama.CreatePrompt()
p:SetSystem('You are a helpful assistant.')
p:AddUserMessage('What is the capital of France?')
assert(ctx:Generate(p, { temperature = 0.3 }))
drain()                       -- the reply is appended to p automatically

-- Second turn: add to the same prompt (KV cache preserved — no Reset needed)
p:AddUserMessage('And Germany?')
assert(ctx:Generate(p))
drain()

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

The callback `fn` is called with arguments in the order the parameters were declared. Each argument is the decoded value from the model's `arguments` object. **Return a string or number**: that becomes the tool reply. Other values (`nil`, booleans, tables) become an empty reply (`""`), so `Json.New():Encode(t)` a table yourself. If the callback raises an error, the reply is `"error: <message>"`.

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

-- Both forms are accepted by Generate (prompt is a LlamaPrompt):
ctx:Generate(prompt, opts, suite:GetJson())  -- JSON string
ctx:Generate(prompt, opts, suite)            -- userdata directly
```

---

#### suite:Call

```lua
number  suite:Call(prompt_or_messages)
```

Inspects the **last message**. If it is an `assistant` message with tool calls, dispatches each call to the matching registered function and appends a tool reply for each.

- **`LlamaPrompt`** (the normal case): reads `prompt:Last().tool_calls` and appends replies with `prompt:AddToolResult`.
- **Raw message table** (backwards compatibility): the last message's `tool_calls` must be a **JSON string**; a `tool_calls` Lua table (as returned by `prompt[i]` or `Export`) is ignored and `Call` returns 0. Replies are appended as `{ role='tool', content=result, tool_call_id=id }`.

Call it **after** the poll loop has finished (`ok == false`): the assistant message with the tool calls is only appended to the prompt then.

Returns the number of tool replies appended (0 if the last message is not a tool call or no calls were decoded).

**Yield-safe:** both tool callbacks and the permission gate (see `suite:Callback`) use `lua_pcallk` internally, so they can call `Sleep`, `HttpClient:Call`, or any other yieldable engine function without stalling the application. The KitsuneEngine 1000-instruction ticker that forces coroutine yields mid-execution is also handled correctly.

If a tool name is not found in the suite, a `"Tool not found: <name>"` reply is appended (`"Unknown tool"` for an empty name) and dispatch continues with the next call.

```lua
-- After the generation has finished:
local ok, data = ctx:Poll()
while ok do Sleep(10); ok, data = ctx:Poll() end
if data and data.type == 'error' then error(data.text) end

if prompt:Last().tool_calls then
    suite:Call(prompt)                  -- dispatches and appends tool replies
    ctx:Generate(prompt, opts, suite)   -- continue the conversation
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

**An error raised by the gate denies the call**, whether it happens before or after the gate yields: the tool is not run, an `"error: permission check failed: <message>"` reply is appended instead, and dispatch continues with the next call.

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
assert(ctx:LoadModel())   -- blocks until loaded

local prompt = Llama.CreatePrompt()
prompt:SetSystem('You are a helpful assistant with access to tools.')
prompt:AddUserMessage('What is the weather in Paris?')

local opts = { temperature = 0.3 }
assert(ctx:Generate(prompt, opts, suite))

while true do
    -- Drain this generation
    local ok, data = ctx:Poll()
    while ok do
        if data and data.type == 'token' then io.write(data.text) end
        Sleep(10)
        ok, data = ctx:Poll()
    end
    if data and data.type == 'error' then error(data.text) end

    -- The assistant message (with any tool calls) is in the prompt now
    if not prompt:Last().tool_calls then break end
    suite:Call(prompt)                          -- run the tools, append the results
    assert(ctx:Generate(prompt, opts, suite))   -- let the model continue
end
print()

ctx:Dispose()
```

---

## MCP

Implements a [Model Context Protocol](https://modelcontextprotocol.io) server directly in the engine — register Lua functions as MCP tools and serve them to any MCP client (Claude Code, Claude Desktop, etc.) over the process's own stdin/stdout. No networking of any kind is involved: MCP's stdio transport is exactly this — the client spawns the process and exchanges newline-delimited JSON-RPC messages over the two standard pipes every process already has. There is no HTTP/network transport in this version.

Only one `MCP` instance can exist per process (there is exactly one stdin/stdout to poll); `MCP.Create` is a singleton constructor — see below.

### Creation

```lua
Mcp  MCP.Create(opt settings, opt context)
```

| Argument | Type | Description |
|----------|------|-------------|
| `settings` | table (opt) | `Name` (string), `Version` (string), `Instructions` (string) — reported to the client in the `initialize` handshake. All fields optional; `Name` defaults to `"kitsune-lua"`, `Version` to `"1.0.0"` |
| `context` | table (opt) | Shared state passed as the first argument to every tool callback (a DB handle, counters, config, whatever the tool author needs). An empty table is created automatically if omitted |

If an `MCP` instance already exists in this process, `MCP.Create` returns that **same instance** unchanged — new `settings`/`context` arguments are ignored. This is deliberate: there is only one stdin/stdout, so only one server can meaningfully poll it.

```lua
local mcp = MCP.Create({ Name = "kitsune-lua", Version = "1.0.0" }, { logPath = "server.log" })
```

### mcp:AddTool

```lua
true  mcp:AddTool(name, description, parameters, fn)
```

Registers a tool. Normally called before `mcp:Start()`; registering later also works, since `tools/list` always reads the current list.

| Argument | Type | Description |
|----------|------|-------------|
| `name` | string | Tool name as the client will call it |
| `description` | string | Natural-language description of what the tool does |
| `parameters` | table | Sequential array of parameter descriptor tables (see below) — becomes the tool's `inputSchema` in `tools/list`, exactly as MCP requires so the calling model knows what to send |
| `fn` | function | `function(context, request) ... end` — see below |

**Parameter descriptor fields** (same shape as `Llama.CreateToolSuite():AddTool`'s parameter descriptors):

| Field | Type | Required | Description |
|-------|------|----------|--------------|
| `name` | string | yes | Parameter name |
| `type` | string | no | JSON Schema type (`"string"`, `"integer"`, `"number"`, `"boolean"`). Defaults to `"string"` |
| `description` | string | no | Human-readable description |
| `required` | boolean | no | Whether the parameter is required. Defaults to `false` |

**The callback** `fn(context, request)` receives:

- `context` — the same table passed to `MCP.Create` (or the auto-created empty one). Shared across every tool call for the life of the server.
- `request` — per-call data:
  | Field | Description |
  |-------|-------------|
  | `request.Arguments` | The decoded arguments as a named table, matching the declared parameter schema (e.g. `request.Arguments.text`). When the client sends no `arguments`, this is the `Json.EmptyObject` sentinel, **not a table**, so indexing it raises; use `request.Parameters[i]`, or check `type(request.Arguments) == "table"` first |
  | `request.Parameters` | The same values as a 1-based positional array, in declared parameter order (e.g. `request.Parameters[1]`) |
  | `request.Name` | The tool name being called (useful if one function is registered for several tools) |
  | `request.RequestId` | The JSON-RPC request id of this call, for correlation/logging |
  | `request.McpSessionId` | Identifies the MCP connection the call came from, as a string: `"0"` over stdio (one connection per process). It identifies a connection or conversation, not a user. Use it to key per-caller data on the shared `context`, e.g. `context.Cache[request.McpSessionId]` |
  | `request.CanElicit` | `true` if this caller declared the `elicitation` capability, i.e. it can show [elicitation](#elicitation) forms |
  | `request.Client` | `{ Name=, Version=, Capabilities= }` from the MCP `initialize` handshake. `Capabilities` is the client's decoded `capabilities` object (absent if the client sent none) |

The callback's return value becomes the tool's text result: **return a string or number**. Other values (`nil`, booleans, tables) yield an empty text result, so encode tables yourself (`Json.New():Encode(t)`). The callback is yield-safe (built on the same `lua_pcallk`/continuation mechanism as `ToolSuite:Call`) — it may call `Sleep()`, `HttpClient:Call()`, or any other yieldable engine function without stalling the server. A Lua error raised inside the callback is caught and reported back to the client as a normal MCP tool-execution error (`isError = true`), not a protocol-level failure. Calling a tool name that isn't registered returns a JSON-RPC error (`-32602 "Unknown tool"`) instead.

```lua
mcp:AddTool(
    "log",
    "Appends a line to the server's log file. Usage: log(text)",
    { { name = "text", type = "string", description = "Text to log", required = true } },
    function(context, request)
        local f = io.open(context.logPath, "a")
        f:write(tostring(request.Parameters[1]) .. "\n")
        f:close()
        return "OK"
    end
)
```

### mcp:Start

```lua
ok, err = mcp:Start()
```

Starts the server: spins up its own independently-scheduled task (via `Tasks.New`, the same mechanism `Tasks.New(fn)` gives any script) that polls stdin non-blockingly, decodes JSON-RPC requests, and dispatches them to registered tools. Returns immediately.

Returns `true` on success. Returns `false, "stdin/stdout not available"` if stdin/stdout are not available in this host (e.g. a GUI-subsystem process launched with no console and no redirected pipes) — there is no reasonable MCP client to serve in that case, so `Start()` fails cleanly rather than guessing. If the polling task can't be created it returns `false, errmsg`.

Calling `Start()` again while the server is running returns `true` and does nothing. After `Stop()` the server can't be restarted (`IsRunning()` stays `false`).

**`print`/`io.write` are globally redirected the moment `Start()` succeeds.** stdout is reserved for JSON-RPC responses, so once the server is running nothing may write to it directly — this holds regardless of which coroutine calls `print`/`io.write` or when. Output isn't discarded: whatever a tool callback prints while it runs is captured and added as its own entry in that call's `content` array, ahead of the callback's actual return value. Output produced outside of any tool dispatch (nothing currently listening) is dropped the next time a dispatch starts. Because `io.write` is always captured, `io.output` is disabled in MCP mode and raises an error; to write a file, use the handle from `io.open(path, "w")` (`f:write(...)`).

Only the **global** `print` and `io.write` are replaced. `io.stdout:write(...)`, and any `print` / `io.write` saved in a local variable *before* `Start()`, still write straight to stdout and corrupt the JSON-RPC stream, so never use them in MCP mode.

### mcp:IsRunning

```lua
bool  mcp:IsRunning()
```

Returns `true` while the server's polling task is still active. Becomes `false` once the client closes its end of the pipe (the normal MCP disconnect path — stdin reaches EOF) or after `mcp:Stop()` is called. Since `Start()` returns immediately, the idiomatic top-level script keeps itself (and therefore `kitsune.exe`) alive by polling this:

```lua
local ok, err = mcp:Start()
assert(ok, err)

while mcp:IsRunning() do
    Sleep(20)
end
```

### mcp:Stop

```lua
nil  mcp:Stop()
```

Signals the polling task to stop; it exits cleanly on its next poll iteration (typically within a few milliseconds).

### Elicitation

Elicitation lets a tool pause and ask the **user** (not the model) to fill in a form in the client's UI, then continue with the answers ([MCP elicitation](https://modelcontextprotocol.io/specification/2025-06-18/client/elicitation)). A form is defined as an `Elicitation` object with questions, and each question can have answers to pick from. Callbacks attached to questions and answers run when the user submits, and their return values are collected into a results table.

```lua
local function deleteLogs(context, request, value, answers)
    return 5                                  -- e.g. number of files deleted; answers.days is available
end

local function listLogs(context, request, value, answers)
    return 5                                  -- e.g. number of files that would be deleted
end

local function default(context, request, reason)
    -- reason: "unsupported" | "decline" | "cancel" | "timeout"
    if request.CanElicit then return 0 end    -- user skipped
    return -1                                 -- client can't show forms
end

local deleteElicitation = mcp:CreateElicitation("How old should logs be before they're deleted?", default)

local days = deleteElicitation:AddQuestion("days", "integer", "Older than (days)", true)
days:SetDefault(30)
days:SetRange(1, 365)

local mode = deleteElicitation:AddQuestion("mode", "string", "What should happen?", true)
mode:AddAnswer("delete",  "Delete them",             deleteLogs)
mode:AddAnswer("dry_run", "Only list, don't delete", listLogs)

mcp:AddTool("delete_old_logs", "Deletes log files older than N days", {}, function(context, request)
    if not request.CanElicit then
        return "This tool requires elicitation"
    end

    local ok, results = deleteElicitation:Elicit(context, request)
    if not ok then error(results) end         -- or: assert(deleteElicitation:Elicit(context, request))

    if type(results) ~= "table" then          -- the default callback ran
        return "Nothing done"
    end
    -- results.days == 30
    -- results.mode.delete == 5               (only the picked answer is present)
end)
```

#### mcp:CreateElicitation

```lua
Elicitation  mcp:CreateElicitation(message, defaultCallback, opt timeout)
```

| Argument | Type | Description |
|----------|------|-------------|
| `message` | string | The text shown to the user above the form |
| `defaultCallback` | function | `function(context, request, reason)`. Runs whenever the form doesn't produce answers (see [Outcomes](#elicitation-outcomes)); its return value becomes `results`. `reason` is `"unsupported"`, `"decline"`, `"cancel"` or `"timeout"` |
| `timeout` | integer (opt) | Milliseconds to wait for the user; after that the default callback runs with reason `"timeout"`. Omitted or `nil` waits as long as the dialog is open. For a different timeout, create a separate elicitation |

An elicitation can be defined once at load time and shared by every tool call, or built inside a tool call when its message or questions depend on per-call data (e.g. `"You have " .. count .. " logs"`).

**Everything is write-once and append-only.** Nothing can be changed or removed after it is set:

- `AddQuestion` with a name that already exists raises an error.
- `AddAnswer` with a value that the question already has raises an error.
- Calling the same setter twice on a question raises an error.
- New questions and answers can be added at any time. A form that is already showing is not affected, because each `Elicit` builds its request from the definition at that moment.

#### elicitation:AddQuestion

```lua
Question  elicitation:AddQuestion(name, type, title, required)
```

| Argument | Type | Description |
|----------|------|-------------|
| `name` | string | Key of this question in `results` (and the property name in the form's schema) |
| `type` | string | `"string"`, `"integer"`, `"number"` or `"boolean"`. Anything else raises an error |
| `title` | string | Label shown next to the field |
| `required` | boolean | Whether the user must answer it before submitting. Must be a boolean |

#### question:AddAnswer

```lua
nil  question:AddAnswer(value, label, opt callback)
```

Turns the question into a closed list: the user picks from the answers and can't type anything else. A string question without answers is free text. Answers are only allowed on `"string"` questions, because the protocol only allows lists of strings. There is no "pick one or type your own" field; the protocol can't express one.

| Argument | Type | Description |
|----------|------|-------------|
| `value` | string | What comes back when this answer is picked; also its key in `results[name]` |
| `label` | string | What the user sees |
| `callback` | function (opt) | `function(context, request, value, answers)`. Runs when this answer is picked |

#### Question setters

Each setter can be called **once** per question. Calling one on a question it doesn't fit raises an error.

| Setter | Valid on | Description |
|--------|----------|-------------|
| `SetDescription(text)` | all | Help text shown with the field |
| `SetDefault(value)` | all | Pre-filled value. Must match the question type; for a question with answers it must be one of them, and for multi-select it is a list of answer values |
| `SetRange(min, max)` | `integer`, `number` | Allowed range; either bound may be `nil`. Integer bounds must be integers |
| `SetLength(min, max)` | `string` without answers | Allowed text length in characters; either bound may be `nil` |
| `SetFormat(format)` | `string` without answers | `"email"`, `"uri"`, `"date"` or `"date-time"` (the client enforces it; the server doesn't re-check formats) |
| `SetMultiple(opt min, opt max)` | `string` with answers | Makes it multi-select, optionally with a min/max number of picks. Call it before `SetDefault`. Needs a client on protocol `2025-11-25` or later; older clients get the default callback with reason `"unsupported"` |
| `SetCallback(fn)` | all | `function(context, request, value, answers)`. On a question without answers, runs with the answered value. On a question with answers, runs for picked answers that have no callback of their own |

#### elicitation:Elicit

```lua
ok, results  elicitation:Elicit(context, request)
```

Sends the form to the caller that `request` belongs to and waits for the user. Pass the `context` and `request` of the tool call it belongs to; every callback receives them. It works like the database helpers: the calling code waits, but the coroutine yields cooperatively, so the rest of the engine keeps running. It also answers `ping` while waiting. Other requests from the same client (another `tools/call`, `tools/list`, …) are queued until the current tool call finishes, because tool calls are handled one at a time.

Mistakes in the definition that only show up once everything is added, such as a default that isn't one of the answers or a multi-select question with no answers, raise an error when `Elicit` is called.

`Elicit` can only be called from inside a tool callback, while that tool call is running. It raises an error if called anywhere else (load-time code, another task, or code running after the tool returned), or from a C function inside the tool that can't yield (e.g. a `table.sort` comparator). The error is raised before anything is sent to the client.

<a id="elicitation-outcomes"></a>**Outcomes:**

| What happened | `ok` | `results` |
|---|---|---|
| User submitted the form | `true` | Results table (below) |
| Caller can't show forms (`request.CanElicit` is `false`) | `true` | Return value of the default callback, reason `"unsupported"` |
| User clicked decline | `true` | Return value of the default callback, reason `"decline"` |
| User closed the dialog | `true` | Return value of the default callback, reason `"cancel"` |
| The timeout passed | `true` | Return value of the default callback, reason `"timeout"` (the client is sent `notifications/cancelled`) |
| A callback (answer, question or default) raised an error | `false` | The error message |
| Client disconnected while waiting | `false` | `"client disconnected"` |
| Server stopped while waiting | `false` | `"MCP server stopped"` |
| Client answered with a JSON-RPC error | `false` | `"elicitation failed: <message>"` |
| The client's answers don't match the form (missing required answer, unknown question, wrong type, not one of the answers, out of range) | `false` | `"invalid response from client: <detail>"` |

The default callback only covers the user skipping, or the caller not supporting forms; transport problems are errors. A callback that returns `nil` counts as having run successfully and is stored as `true`.

**Results table**: one entry per answered question, keyed by question name:

| Question kind | `results[name]` |
|---|---|
| Has answers (single or multi-select) | A table: picked answer value → its callback's return (the answer's own callback, else the question's `SetCallback`, else `true`). Unpicked answers are absent |
| No answers (text, number, yes/no) | The question's `SetCallback` return if it has one, otherwise the value itself |

A question the user left empty, or that the client sent as `null` (only possible when it isn't required), is absent. The answers are checked against the definition before any callback runs. Callbacks then run in question order, and within a multi-select question in answer order. They may yield (`Sleep`, database calls, …). If one raises an error, `Elicit` returns `false, message`, and callbacks that already ran keep their side effects. Every callback's `answers` argument is the plain submitted form (question name → value, or a list of values for multi-select), so a callback can read the other questions' answers.

**Lifetimes:** an `Elicitation` keeps its `MCP` server alive, and a `Question` keeps its `Elicitation` alive. Each releases its parent when it is garbage-collected.

### Protocol version

During `initialize`, the server echoes the client's requested protocol version if it supports it (`2025-11-25`, `2025-06-18`, `2025-03-26`, `2024-11-05`). Otherwise it offers `2025-11-25`, the newest it supports. Elicitation was added in `2025-06-18`. Clients on `2025-11-25` or later get titled answer lists (`oneOf` with `const`/`title`) and multi-select; older clients get `enum` plus `enumNames`.

### Complete example

```lua
local mcp = MCP.Create(
    { Name = "kitsune-lua", Version = VERSION or "1.0.0" },
    { logPath = "mcp_server_example.log" }
)

mcp:AddTool(
    "log",
    "Appends a line to the server's log file. Usage: log(text)",
    { { name = "text", type = "string", description = "Text to log", required = true } },
    function(context, request)
        local f = io.open(context.logPath, "a")
        f:write(tostring(request.Parameters[1]) .. "\n")
        f:close()
        return "OK"
    end
)

mcp:AddTool(
    "run_lua",
    "Executes a snippet of Lua code against this engine and returns its result. " ..
    "The full Kitsune API (Redis, HttpClient, SQLite, Stream, Json, ...) is available.",
    { { name = "code", type = "string", description = "Lua source; the value returned becomes the tool result", required = true } },
    function(context, request)
        local fn, err = load(request.Arguments.code, "=run_lua", "t", _G)
        if not fn then
            error("compile error: " .. tostring(err))
        end
        return tostring(fn())
    end
)

local ok, err = mcp:Start()
assert(ok, err)

while mcp:IsRunning() do
    Sleep(20)
end
```

Run with `kitsune mcp_server_example.lua`, then register it with an MCP client by pointing it at the built `Kitsune.exe` and this script — the client will spawn the process and talk to it over stdin/stdout for the life of the session.

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



