# Lua Environment API Reference

A comprehensive reference for all available functions in the Lua environment.

---

## Table of Contents

- [Global Functions](#global-functions)
- [Session](#session)
- [Mutex](#mutex)
- [Macro](#macro)
- [Redis](#redis)
- [CSV](#csv)
- [Kafka](#kafka)
- [FileAsync](#fileasync)
- [FTP](#ftp)
- [Sound](#sound)
- [ODBC](#odbc)
- [Archive](#archive)
- [Stream](#stream)
- [TTS](#tts)
- [Env](#env)
- [Zip](#zip)
- [Server](#server)
- [Client](#client)
- [Pipe](#pipe)
- [Base64](#base64)
- [Services](#services)
- [Aes](#aes)
- [Process](#process)
- [Imgui](#imgui)
- [HTTP](#http)
- [Hashing (SHA256, MD5, SHA1)](#hashing)
- [MySQL](#mysql)
- [Postgres](#postgres)
- [Timer](#timer)
- [SQLite](#sqlite)
- [Image](#image)
- [Json](#json)
- [Wchar](#wchar)
- [FileSystem](#filesystem)
- [TWODA (2da)](#twoda)
- [TLK](#tlk)
- [ERF](#erf)
- [GFF](#gff)
---

## Global Functions

### UUID Generation

```lua
string, raw16bytestring UUID()
```
Returns a UUID from Windows `CoCreateGuid`. Returns `nil` if uniqueness cannot be guaranteed.

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
int Time()
ms Runtime()
```
- `Sleep`: Sleep for 1 millisecond (default) or specified ms (max 1000)
- `Time`: Get current Unix epoch in milliseconds
- `Runtime`: Get runtime in milliseconds

### Error Handling

```lua
string, code GetLastError(opt lasterrorcode)
```
Retrieves the last error code as a message and code.

### Shell

```lua
bool ShellExecute(file, parameter)
```

### Ticker Function

```lua
nil SetTicker(function, opt ms)
```
Sets a function as a ticker. If the parameter isn't a function, the ticker is disabled.

### Memory

```lua
int GetMemory()
```
Returns memory in bytes used by Lua.

### Application Control

```lua
Exit(opt code)
```
Called when GFF.exe shuts down; can also be called to shut down prematurely.

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
| `ResList` | Key-value table for ERF container extensions |
| `LAST_TEMP_FILE` | Set by `io.tmpfile()` with created filename |
| `ARGS[1]` | The script file being run (or `"cmd"` in REPL mode) |
| `ARGS[2..n]` | Additional command-line parameters passed after the script name |

---

## Session

The `Session` global table groups interactive environment functions into three subtables.

### Session.Console

```lua
bool   Session.Console.Create()
bool   Session.Console.Destroy()
bool   Session.Console.Attach(opt processId)
cursorx, cursory, sizex, sizey, maxsizex, maxsizey Session.Console.GetInfo()
nil    Session.Console.SetCursorPosition(x, y)
nil    Session.Console.Clear()
nil    Session.Console.Put(text)
characterswritten Session.Console.Write(data)
charactersprinted Session.Console.Print(...)
key    Session.Console.ReadKey()
int    Session.Console.GetKey()
bool   Session.Console.HasKeyDown()
bool   Session.Console.GetKeyState(key)
nil    Session.Console.SetColor(Background, Foreground)
Background, Foreground Session.Console.GetColor()
nil    Session.Console.SetVisible(toggle)
nil    Session.Console.SetTitle(newtitle)
```

| Function | Description |
|----------|-------------|
| `Create` | Creates a new console if none exists |
| `Destroy` | Deallocates the console |
| `Attach` | Attaches to existing console (parent process if no `processId` given) |
| `GetInfo` | Get console cursor position and window/buffer sizes |
| `SetCursorPosition` | Move cursor to new location |
| `Clear` | Empty the console |
| `Put` | Write text character-by-character, translating CR to newline and handling backspace |
| `Write` | Write data directly to the console via `WriteConsole`; returns characters written |
| `Print` | Print tab-separated values followed by a newline (like `print` but to the console handle) |
| `ReadKey` | Returns the next key code, or `nil` if no key is currently pressed |
| `GetKey` | Block until a key is pressed and return its character code |
| `HasKeyDown` | Returns `true` if a key is currently pressed |
| `GetKeyState` | Check an async key state — [Virtual Key Codes](https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes) |
| `SetColor` | Set background and foreground colors |
| `GetColor` | Get current background and foreground colors |
| `SetVisible` | Show or hide the console window |
| `SetTitle` | Set the console window title |

### Session.Display

```lua
x, y          Session.Display.GetScreenSize()
x, y          Session.Display.GetCursorPoint()
x, y, monitor Session.Display.GetCursorPosition()
```

| Function | Description |
|----------|-------------|
| `GetScreenSize` | Returns the width and height of the primary screen in pixels |
| `GetCursorPoint` | Returns the raw cursor position as absolute screen coordinates |
| `GetCursorPosition` | Returns cursor coordinates relative to the monitor it is on, plus a 1-based monitor index |

### Session.Clipboard

```lua
bool   Session.Clipboard.Set(data)
string Session.Clipboard.Get()
```

| Function | Description |
|----------|-------------|
| `Set` | Write `data` to the system clipboard. Pass an empty string or `nil` to clear it. Returns `true` on success |
| `Get` | Read the current clipboard content as a UTF-8 string, or `nil` if empty or unavailable |

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

## Macro

```lua
x, y Macro.ScreenToMousePoint(x, y)
Macro Macro.Create(inputs)
int Macro:Send()
inputs Macro:GetInputs()
```

### Input Types

**Type 0 - Mouse:**
- `ExtraInfo`, `Flags`, `Time`, `Data`, `X` (0-65536), `Y` (0-65536)
- [Mouse Event Flags](https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-mouse_event)

**Type 1 - Keyboard:**
- `ExtraInfo`, `Flags` (0=down, 2=up), `Time`, `Scan`, `Key`
- [Virtual Key Codes](https://docs.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes)

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
channel, message Redis:Poll()
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
RedisString Redis:GetString(key)
RedisValue Redis:GetHashset(key)
RedisValue Redis:GetList(key)
RedisValue Redis:GetSet(key)
RedisValue Redis:GetSortedSet(key)
RedisStream Redis:GetStream(key)
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
int RedisString:GetTTL()
bool RedisString:SetTTL(ms)
string RedisString:Set(newValue)
string RedisString:GetOrSet(newValue)
string RedisString:Delete()
length RedisString:len()
```

### Iterator Example

```lua
for key in redis do
    print(key)
end
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

### Consumer/Producer

```lua
Kafka Kafka.NewConsumer(opt conf)
Kafka Kafka.NewProducer(opt conf)
```

Default group IDs: `LUAC` (consumer), `LUAP` (producer)

### Operations

```lua
bool, errormsg Kafka:Send(topic, data, opt partition, opt key, opt timeout, opt headers)
event Kafka:Events()
bool Kafka:AddBroker(address)
Groups Kafka:GetGroups(opt group, opt timeout)
Metadata Kafka:GetMetadata(opt timeout)
string Kafka.Logs(opt filename)
bool, errormsg Kafka:Subscribe(topic, opt partition)
bool, errormsg Kafka:Assign(topic, partition)
kafkamessage Kafka:Poll()
kafkamessage Kafka:Consume(topicobject)
int Kafka:GetCommitted(topic, partition)
bool, err Kafka:Seek(topic, partition, offset, opt timeout)
bool, low, high Kafka:GetOffsets(topic, partition, opt timeout)
```

### Topic Management

```lua
kafkatopic, errormsg Kafka:OpenTopic(topicname, opt partition, offset, opt conf)
table, errormsg Kafka:CreateTopic(topicname, opt partitions, opt replicationfactor, opt brokerid, opt requesttimeout, opt operationtimeout)
table, errormsg Kafka:DeleteTopic(topicname)
table, errormsg Kafka:AlterConfig(resourcetype, resourcename, configname, configvalue, opt timeout)
table Kafka:GetConfig(resourcetype, resourcename, opt timeout)
table, errormsg Kafka:SetPartitions(topicname, newpartitioncount, opt timeout)
bool, errormsg Kafka:PauseTopic(Kafkatopic)
bool, errormsg Kafka:ResumeTopic(Kafkatopic)
int Kafka:GetId()
```

**Resource types:** 1=any, 2=topic, 3=group, 4=broker

### Kafkatopic

```lua
int Kafkatopic:GetOwnerId()
name, partition Kafkatopic:GetInfo()
nil Kafkatopic:Dispose()
bool Kafkatopic:IsPaused()
```

### Kafkamessage

```lua
table Kafkamessage:GetData()
int, int Kafkamessage:GetTimestamp()
int Kafkamessage:GetLatency()
int Kafkamessage:GetOwnerId()
nil Kafkamessage:Dispose()
```

**GetData returns:** `Error`, `ErrorCode`, `Key`, `Offset`, `Partition`, `Payload`, `Topic`, `Headers`

---

## FileAsync

```lua
FileAsync FileAsync.Open(file, mode, opt buffersize)
bool FileAsync:Busy(opt wait, opt cancel)
int FileAsync:Tell()
int FileAsync:Seek(pos, opt type)
nil FileAsync:Rewind()
bool FileAsync:EndOfFile()
nil FileAsync:Read(opt bytestoread, opt readbuffersize)
currentlength, maxlength FileAsync:BufferStatus()
string, bool FileAsync:EmptyBuffer()
nil FileAsync:Close()
```

**Seek types:** 0=start, 1=current, 2=end

---

## FTP

### Connection

```lua
ftp, welcomemessages FTP.Open(address, opt port)
nil FTP:SetEndline(endline)
bool, error FTP:Login(user, password)
bool, error FTP:Command(command)
ip, port FTP:Passive()
nil FTP.SetTimeout(timeinseconds)
msgs FTP:GetMessages(opt timeout)
nil FTP:Close()
isconnected, wsaerror FTP:GetConnectionStatus()
```

### Data Channel

```lua
ftpchannel FTP.OpenDataChannel(address, port)
bool ftpchannel:Send(data)
isalive, data ftpchannel:Recv(opt buffersize)
isalive, hasdata ftpchannel:GetConnectionStatus()
nil ftpchannel:Close()
```

---

## Sound

```lua
bool Sound.Play(wavefile, opt async)
bool Sound.Beep(freq, duration)
int, string Sound.SendMCS(command)
```

---

## ODBC

### Connection

```lua
array ODBC.GetAllDrivers()
ODBC ODBC.DriverConnect(connectionstring)
```

### Query Operations

```lua
bool, error ODBC:Prepare(sql)
bool, error ODBC:Bind(data, opt asbinary)
bool, error ODBC:Execute()
bool, error ODBC:Fetch()
table, error ODBC:GetRow()
table ODBC:GetRowColumnTypes()
```

### Transaction Control

```lua
bool, error ODBC:ToggleAutoCommit(bool)
bool, error ODBC:Begin()
bool, error ODBC:Commit()
bool, error ODBC:Rollback()
```

### Schema Queries

```lua
bool, error ODBC:Tables(schema)
bool, error ODBC:Columns(table, schema)
bool, error ODBC:SpecialColumns(table, schema)
bool, error ODBC:PrimaryKeys(table, schema)
bool, error ODBC:ForeignKeys(table, schema)
bool, error ODBC:Procedures(schema)
bool, error ODBC:ProcedureColumns(procedure, schema)
nil ODBC:Disconnect()
```

---

## Archive

```lua
Archive Archive.OpenRead(filename, opt usewchar)
array Archive:Entries()
file, size Archive:SetEntry(index)
data Archive:Read(opt buffer)
```

**Entries returns:** Array of tables with `Name` and `Size`

---

## Stream

### Creation

```lua
Stream Stream.Create(opt string)
Stream Stream.Create(backendfunction)
Stream Stream.Open(filename, mode)
Stream Stream.OpenSharedMemory(size)
Stream Stream.CreateChunked(chunks)
```

- **No argument** — creates a new empty in-memory stream.
- **String argument** — creates an in-memory stream pre-loaded with the string contents, with the position reset to 0.
- **Function argument** — creates a stream backed by the provided Lua function. The function is called with an opcode as its first argument and must handle all `STREAM_OP_*` operations it wishes to support. It must return the capability bitmask when called with `STREAM_OP_OPEN` (0).
- **`Open(filename, mode)`** — opens a file as a stream. `mode` follows standard C `fopen` conventions: `"rb"`, `"wb"`, `"r"`, `"w"`, `"ab"`, etc. Raises an error if the file cannot be opened.
- **`OpenSharedMemory(size)`** — creates a new outbound shared-memory stream of the given byte size. The block is owned by Lua's GC; pass it to a C# host via `KITSUNE_TSTREAM`.
- **`CreateChunked(chunks)`** — creates a read-only async stream from an array of strings. Each `Read()` call yields the coroutine once before delivering the next chunk, simulating asynchronous data arrival (e.g. a network socket). Returns `nil` at EOF. Requires a coroutine context — `pos()` always returns `nil` (no seek capability) and `len()` returns the size of the currently-buffered chunk (0 before the first yield, non-zero after).

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

### File Operations

```lua
nil Stream:Save(filename)
nil Stream:WriteToFile(filename, pos, len)
nil Stream:ReadFromFile(filename, pos, len)
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
int         Stream:Buffer(str)
void        Stream:Shrink()
string      Stream:Read(opt length)
string      Stream:ReadUntil(opt tofind)
pos         Stream:IndexOf(string or byte)
bool/int    Stream:HasData()
Stream      Stream:ToSharedMemory(opt dispose)
nil         Stream:Close()
```

- **`Write`** accepts a `string`, `Wchar`, `number`, or `boolean`. A `Wchar` is written as raw UTF-16 LE bytes (2 bytes per code unit); use `WriteUtf8` instead to write its UTF-8 encoding. The optional `size` argument limits the number of bytes written. Returns the number of bytes written, or `0` on failure.
- **`WriteUtf8`** converts a Lua string from Latin-1/byte values to proper UTF-8 before writing.
- **`ReadWchar`** reads `n` UTF-16 LE code units (each 2 bytes) from the current position and returns a `Wchar`. If `n` is omitted or `nil`, reads all remaining bytes. Returns `nil` if the stream is not readable or there are no complete code units available.
- **`HasData`** — non-blocking availability check. For sync (seekable) streams returns the number of bytes remaining as an integer, or `false` at EOF. For async streams (vtable with `hasdata`) returns `true` if data is ready in the buffer, `false` if nothing is available yet (more may arrive later — `false` is **not** EOF for async streams). For fn backends dispatches `STREAM_OP_HASDATA`; returns `nil`/`false` if the backend has no handler. **Never yields.**
- **`ToSharedMemory(opt dispose)`** — snapshots the stream contents into a new outbound shared-memory block and returns it as a new `Stream`. The source must be both readable and seekable. If `dispose` is `true`, the source stream's caps are zeroed after the snapshot so it can no longer be used. Raises an error if the source is not readable+seekable.
- **`Close`** — explicitly frees the stream's resources and marks it unusable. Called automatically by the GC; safe to call early when resources should be released promptly.

### Stream Info

```lua
capsTable, backendInfo Stream:GetInfo()
void Stream:SetLength(newlength)
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
- The static module form (`Stream.Compress(source)`) accepts any readable stream — including async streams created with `Stream.CreateChunked` or a custom function backend.
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

## TTS

```lua
TTS TTS.Create()
array TTS.GetVoices()
bool TTS:Speak(text, opt flags)
bool TTS:GetIsSpeaking()
nil TTS:PlayPause()
bool TTS:GetIsPaused()
int TTS:GetVolume()
bool TTS:SetVolume(volume)
int TTS:GetRate()
bool TTS:SetRate(rate)
bool TTS:SetVoice(voicename)
bool TTS:Skip()
nil TTS:Dispose()
```

---

## Env

```lua
table Env.Create(name)
table Env.Get(name)
table Env.GetOrCreate(name)
table Env.Meta()
```

---

## Zip

```lua
Zip Zip.Open(zipfile)
int Zip:AddFile(key, file)
int Zip:AddData(key, data)
true/data Zip:Extract(key/index, opt targetfile)
table Zip:GetInfo(key/index)
int Zip:Delete(key/index)
array Zip:GetFiles()
nil Zip:Close()
```

**GetInfo returns:** `comp_method`, `comp_size`, `crc`, `encryption_method`, `flags`, `index`, `mtime`, `name`, `size`, `valid`

---

## Server

```lua
Server Server.Start(port)
nil Server:Stop()
event/nil Server:GetEvent()
bool Server:Disconnect(socket)
bool Server:Send(socket, data)
nil Server.SetStartFunc(function)
table Server:GetClients()
```

**Event types:** `socket`, `type`, `data`

---

## Client

```lua
Client Client.Connect(address, port)
bool/error Client:Status()
nil Client:Disconnect()
event/nil Client:GetEvent()
bool Client:Send(data)
```

**Network event types:**
| Value | Type |
|-------|------|
| 1 | NETEVENT_CONNECTED |
| 2 | NETEVENT_DISCONNECTED |
| 3 | NETEVENT_SEND |
| 4 | NETEVENT_RECEIVE |

---

## Pipe

```lua
Pipe Pipe.Create(name, opt maxinstances, opt buffersize, opt timeout, opt write, opt read)
Pipe Pipe.Open(name, opt write, opt read)
int Pipe:Write(string)
string Pipe:Read(opt buffersize)
int Pipe:ReadByte()
bool Pipe:WriteByte(byte)
avail Pipe:Available()
nil Pipe:Close()
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

## Services

```lua
array Services.All()
Service Services.Open(servicename, opt readonly)
status Service:Status()
config Service:Config()
bool Service:Start()
bool Service:Stop()
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
number Process:GetCPU()
number Process:GetRAM()
int/bool Process:Priority(opt prio)
int, int Process:Affinity(opt newmask)
array Process:Threads()
```

---

## Imgui

### Creation

```lua
Imgui Imgui.Create(title, tag, width, height, renderfunction)
float Imgui.GetFontSize()
nil Imgui.SetStyle(table)
table Imgui.GetStyle()
```

### Value Types

| Type | Description |
|------|-------------|
| 1 | bool |
| 2 | float |
| 3 | vec4 {x,y,z,w} |
| 4 | int |
| 5 | string |
| 6 | double |

### Operations

```lua
value Imgui:GetValue(tag, opt type)
nil Imgui:SetValue(tag, type, value)
array Imgui:GetAllValues()
bool Imgui:Tick()
nil Imgui:Close()
nil Imgui:Quit(opt code)
info Imgui:Info()
nil Imgui:Clear()
```

### Helpers

```lua
table Imgui.GetEnums()
nil Imgui.SetClipboardText(text)
string Imgui.GetClipboardText()
r, g, b Imgui.Vec4ToRGB(vector4)
vec4 Imgui.RGBToVec4(r, g, b)
```

### ImguiDraw Functions

**Layout:**
```lua
nil ImguiDraw:SameLine(opt offsetstartx, opt spacing)
vec2 ImguiDraw:GetCursorPos()
vec2 ImguiDraw:GetCursorStartPos()
nil ImguiDraw:Indent(opt w)
nil ImguiDraw:Unindent(opt w)
nil ImguiDraw:BeginGroup() / nil ImguiDraw:EndGroup()
nil ImguiDraw:PushStyleVar(flag, value)
nil ImguiDraw:PopStyleVar(opt count)
vec2 ImguiDraw:GetWindowSize()
nil ImguiDraw:SetNextItemWidth(width)
```

**Text:**
```lua
nil ImguiDraw:Text(text)
nil ImguiDraw:TextWrapped(text)
nil ImguiDraw:TextColored(color, text)
vec2 ImguiDraw:CalcTextSize(string)
```

**Input:**
```lua
bool ImguiDraw:InputText(label, tag, opt hint)
bool ImguiDraw:InputTextMultiline(label, tag, size, flags)
bool ImguiDraw:InputInt(label, tag, opt step, opt faststep, opt flags)
bool ImguiDraw:InputFloat(label, tag, opt step, opt faststep, opt format, opt flags)
bool ImguiDraw:InputDouble(label, tag, opt step, opt faststep, opt format, opt flags)
```

**Controls:**
```lua
bool ImguiDraw:Button(title)
bool ImguiDraw:Checkbox(title, tag)
bool ImguiDraw:RadioButton(title, tag, id)
bool ImguiDraw:Combo(label, tag, stringarray, opt maxitemsshown)
bool ImguiDraw:SliderFloat(title, tag, min, max, opt format, opt flags)
bool ImguiDraw:SliderInt(title, tag, min, max, opt format, opt flags)
bool ImguiDraw:ColorEdit3(title, tag)
bool ImguiDraw:Selectable(text, selected)
```

**Windows/Menus:**
```lua
bool ImguiDraw:Begin(title, opt tag, flags)
nil ImguiDraw:End()
bool ImguiDraw:BeginChild(title, width, height, noborder)
nil ImguiDraw:EndChild()
bool ImguiDraw:BeginMainMenuBar() / nil ImguiDraw:EndMainMenuBar()
bool ImguiDraw:BeginMenu(title, disabled) / nil ImguiDraw:EndMenu()
bool ImguiDraw:MenuItem(title)
bool ImguiDraw:BeginTabBar(id, flags) / nil ImguiDraw:EndTabBar()
bool ImguiDraw:BeginTabItem(label, opt tag, flags) / nil ImguiDraw:EndTabItem()
```

**Tables:**
```lua
bool ImguiDraw:BeginTable(labelid, columns, opt flags)
nil ImguiDraw:EndTable()
nil ImguiDraw:TableSetupColumn(columnname, flags, widthorweight)
bool ImguiDraw:TableNextColumn()
bool ImguiDraw:TableSetColumnIndex(idx)
nil ImguiDraw:TableNextRow(opt flags, opt row_min_height)
```

**Utilities:**
```lua
nil ImguiDraw:Separator()
nil ImguiDraw:ShowDemoWindow(opt tag)
bool ImguiDraw:IsItemHovered(opt flags)
-- Current implementation does not return a Lua boolean:
nil ImguiDraw:IsItemClicked(opt mousebutton)
nil ImguiDraw:IsMouseDoubleClicked(opt mousebutton)
```

---

## HTTP

```lua
Http Http.Start(method, url, content, headers, opt usehttp1.0)
nil Http:SetTimeout(timeout)
code, ok, contents, header Http:GetResult()
IsRunning, status, runtime, recv, send Http:GetStatus()
file Http:GetRaw()
bool Http:Wait(opt timeout)
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

Pass an array table as the second argument to `Query`, `NonQuery`, `Scalar`, or `QueryAll`. `?` placeholders are substituted in order. Missing or `nil` entries become SQL `NULL`. `table` values are JSON-encoded. `Wchar` values are UTF-8 encoded.

```lua
conn:NonQuery("INSERT INTO t (a, b, c) VALUES (?, ?, ?)", {"hello", nil, 3.14})
conn:Scalar("SELECT name FROM users WHERE id = ?", {42})
```

### MySQL type mapping

| MySQL type | Lua type |
|------------|----------|
| TINYINT, SMALLINT, MEDIUMINT, INT, BIGINT | integer |
| FLOAT, DOUBLE, DECIMAL | number |
| TINYBLOB, BLOB, MEDIUMBLOB, LONGBLOB | LuaStream (userdata) |
| all others (VARCHAR, TEXT, DATE, JSON, …) | string |

> **Note:** MySQL has no native boolean type. `TINYINT(1)` columns return integer `1` or `0`.

---

## Postgres

Connects to a PostgreSQL database using libpq. Queries are dispatched asynchronously on a background thread and results are iterated with the same `Fetch` / `GetRow` pattern as [SQLite](#sqlite). The connection is always configured with `UTF8` client encoding automatically.

```lua
Postgres  Postgres.Connect(conninfo)
bool, txt  Postgres:Query(query, opt params)
bool, txt  Postgres:Fetch()
table|value  Postgres:GetRow(opt index_or_field)
nil  Postgres:Finish()
bool  Postgres:IsBusy()
string  Postgres:EscapeValue(value)
Postgres:Close()
```

| Function | Description |
|----------|-------------|
| `Connect` | Connect using a libpq connection string (e.g. `"host=localhost user=postgres password=secret dbname=mydb connect_timeout=5"`) |
| `Query` | Dispatch an async SQL query. Returns `true` on dispatch, or `false, "Busy"` if a query is already running. Pass an optional array table as `params` for parameterized queries |
| `Fetch` | Block until the query completes on the first call, then advance to the next row. Returns `true` if a row is available, `false` when done, or `false, errorMessage` on execution error |
| `GetRow` | Return the current row as a hash table keyed by column name, a single column value when `index` (1-based integer) is given, or a single column value when `field` (string column name) is given. `NULL` columns are `nil` |
| `Finish` | Discard the current result and reset the cursor |
| `IsBusy` | Returns `true` while a query is in progress |
| `EscapeValue` | Escape a string using `PQescapeLiteral`. The result **includes** surrounding single quotes (e.g. `'O''Reilly'`) |
| `Close` | Close the connection and free all resources |

### Connection String

```
"host=127.0.0.1 port=5432 user=postgres password=secret dbname=mydb connect_timeout=5"
```

### Parameterized Queries

Pass an array table as the second argument to `Query`. The parameter count is determined automatically by scanning the SQL for the highest `$N` placeholder (`$1`, `$2`, ...). Missing or `nil` entries in the table are sent as SQL `NULL`. `table` values are JSON-encoded. `Wchar` values are UTF-8 encoded.

```lua
pg:Query("SELECT * FROM users WHERE id = $1", {42})
pg:Query("INSERT INTO t (a, b, c) VALUES ($1, $2, $3)", {"hello", nil, 3.14})
```

### Usage Pattern

```lua
local pg = Postgres.Connect("host=localhost user=postgres password=secret dbname=mydb")

pg:Query("SELECT id, name FROM users WHERE active = $1", {true})
while pg:Fetch() do
    local row = pg:GetRow()        -- {id=1, name="Alice"}
    local id  = pg:GetRow(1)       -- first column value only
    local name = pg:GetRow("name") -- column by name
end

local ok = pg:Query("DELETE FROM sessions WHERE expired = true")
local more, err = pg:Fetch()
assert(not err, err)
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
| 1700 | NUMERIC | number |
| all others | TEXT, VARCHAR, DATE, JSON, etc. | string |
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
nil SQLite:Finish()
bool SQLite:Fetch()
table SQLite:GetRow(opt index)
nil SQLite:RegisterFunction(function, name, args)
nil SQLite:RegisterAggregateFunction(function, name, args)
nil SQLite:ToggleWidechar(bool)
nil SQLite:Close()
```

**Mode:** 0=single thread, 1=multithreaded, 2=serialized

---

## Image

*All coordinates are 1-indexed*

```lua
Image Image.Screenshot(width, height, startx, starty, monitor)
bool Image:Save(filename)
Image Image.Load(filename)
Image Image.Create(Width, Height)
Image Image:Crop(Width, Height, StartX, StartY)
array Image:GetPixels()
bool Image:SetPixels(pixel)
matrix Image:GetPixelMatrix()
bool Image:SetPixelMatrix(pixelmatrix)
pixel Image:GetPixel(y, x)
void Image:SetPixel(y, x, pixel)
width, height Image:GetSize()
void Image:Close()
```

---

## Json

```lua
Json     Json.Create(opt pretty)
value    Json:SetNullValue(opt value)
string   Json:Encode(table_or_coroutine)
table    Json:Decode(jsonstring)
nil      Json:EncodeToFile(filename, table_or_coroutine)
table    Json:DecodeFromFile(filename)
nil      Json:EncodeToFunction(func, table_or_coroutine)
table    Json:DecodeFromFunction(func)
nil      Json:Dispose()
coroutine Json:Iterator(function)
```

| Function | Description |
|----------|-------------|
| `Create` | Create a new Json instance. Pass `true` for pretty-printed output |
| `SetNullValue` | Get/set the null sentinel. When a sentinel is set, encoding a value that equals the sentinel produces JSON `null`, and decoding JSON `null` returns the sentinel instead of `nil`. Calling with no argument or `nil` clears the sentinel. **Always returns the previous value** (or `nil` if none was set) |
| `Encode` | Encode a Lua table or coroutine to a JSON string |
| `Decode` | Decode a JSON string to a Lua table |
| `EncodeToFile` | Write JSON directly to a file |
| `DecodeFromFile` | Read and decode JSON from a file |
| `EncodeToFunction` | Stream JSON output to a callback function |
| `DecodeFromFunction` | Read JSON input from a callback function |
| `Dispose` | Explicitly free the Json context |
| `Iterator` | Iterate a large JSON document incrementally via a coroutine |

### Null Sentinel

By default JSON `null` decodes to Lua `nil` (which cannot be stored in a table). Use `SetNullValue` to map `null` to a distinguishable sentinel:

```lua
local json = Json.Create()
json:SetNullValue("__NULL__")

local encoded = json:Encode({value = "__NULL__"})  -- {"value":null}
local decoded = json:Decode(encoded)
print(decoded.value)  -- "__NULL__"

-- Clear the sentinel; returns the old value
local old = json:SetNullValue(nil)  -- old == "__NULL__"
```

### Type Mapping

| Lua type | JSON type |
|----------|-----------|
| `nil` | `null` (omitted from tables) |
| sentinel value | `null` |
| `boolean` | `true` / `false` |
| integer | number (no decimal point) |
| float | number (trailing zeros trimmed, e.g. `3.5` not `3.500…`) |
| `string` | string |
| `Wchar` | string (UTF-8 encoded via `ToUtf8`) |
| `LuaStream` | string (all bytes read from offset 0; stream position is preserved after encoding; `null` if not both readable and seekable) |
| `table` | object `{}` or array `[]` depending on keys |
| `NaN` | `null` |
| `±Infinity` | `1e+9999` / `-1e+9999` |

### Notes

- **Circular references** are detected automatically. Encoding a table that directly or indirectly references itself raises an error: `Recursion detected`
- **Table iteration** uses Lua's `pairs()`, so `__pairs` metamethods **are** respected during encoding. Custom iterators set via `__pairs` control what gets serialized
- **UTF-8 strings** pass through the encoder unescaped. Only control characters (U+0000–U+001F) are hex-escaped as `\uXXXX`; all other bytes including multi-byte UTF-8 sequences appear literally in the output
- **`Wchar` values** are converted to UTF-8 via `ToUtf8` before encoding; the resulting bytes are then JSON-escaped the same way as regular strings
- **`LuaStream` values** are rewound to offset 0 and all bytes are read into a JSON string; the stream's original position is restored afterwards. A stream that is not both readable and seekable encodes as `null`

### Coroutine Example

```lua
local json = Json.Create()
local s = json:Encode(coroutine.create(function()
    coroutine.yield(nil, {})        -- root object
    coroutine.yield("MyTable", {})  -- nested array
    for n = 1, 10 do
        coroutine.yield(n, "Hello")
    end
    coroutine.yield(nil, nil)       -- close array
    coroutine.yield("Cake", "Is good")
    coroutine.yield(nil, nil)       -- finish
end))
-- Result: {"MyTable":["Hello","Hello",...], "Cake":"Is good"}
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
string Wchar:ToWide()
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

## TWODA

```lua
TWODA TWODA.Open(file)
string TWODA:Get2DAString(row, index or columnname)
table TWODA:Get2DARow(row)
array, numrows, version TWODA:GetInfo()
```

---

## TLK

```lua
TLK TLK.Create(filename, array, opt languageid, opt version)
TLK TLK.Open(filename)
table TLK:GetAll()
table TLK:Get(strref)
bool TLK:SetSoundInfo(strref, soundresref, opt soundlength)
bool TLK:Set(strref, newstring)
bool TLK:Defragment(opt extra)
count, languageid, version TLK:GetInfo()
```

---

## KeyBif

```lua
KeyBif KeyBif.Create()
```

---

## ERF

```lua
ERF, entries, filetype, version ERF.Open(filename)
ERF ERF.Create(filename, typeheader, filelist, opt version, opt desc)
header ERF:GetHeader()
array ERF:GetStrings()
array ERF:GetKeys()
binary ERF:GetResource(ResID)
nil ERF:Extract(ResID, targetfile)
```

---

## GFF

```lua
struct GFF.OpenFile(file)
struct GFF.OpenString(string)
nil GFF.SaveToFile(gff, file)
string GFF.SaveToString(gff)
```

### GFF Types

| Value | Type |
|-------|------|
| 0 | byte |
| 1 | char |
| 2 | word |
| 3 | short |
| 4 | dword |
| 5 | int |
| 6 | dword64 |
| 7 | int64 |
| 8 | float |
| 9 | double |
| 10 | CExoString |
| 11 | ResRef |
| 12 | CExoLocString |
| 13 | binary string |
| 14 | struct |
| 15 | list |

