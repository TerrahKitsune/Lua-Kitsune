# Lua-Kitsune

A Lua 5.4 scripting engine with a large set of built-in modules for systems programming, networked services, data processing, media and local LLM inference. The engine is built around cooperative coroutines: many Lua scripts run concurrently on a single thread by yielding to each other, with no OS threads involved in normal operation.

It can be used in four ways:

- **As a script runner**: `kitsune.exe script.lua`
- **As an MCP server**: expose Lua functions as tools to Claude Code, Claude Desktop or any other [Model Context Protocol](https://modelcontextprotocol.io) client ([see below](#using-kitsune-as-an-mcp-server))
- **Embedded in a native application** through the flat C API in `KitsuneEngine.h`
- **Embedded in a .NET application** through `KitsuneNet`

---

## Projects

| Project | Kind | Description |
|---|---|---|
| **KitsuneEngine** | C++ DLL / shared library | The core. Embeds Lua 5.4, contains every built-in module and exposes the C API |
| **Kitsune** | C++ executable | Script runner for the engine, with optional [Dear ImGui](https://github.com/ocornut/imgui) + SDL2 bindings for immediate-mode GUI apps in Lua |
| **KitsuneNet** | C# · .NET 10 | Managed bindings for embedding and driving the engine from .NET |
| **SQLiteKitsuneExtension** | C++ DLL | SQLite loadable extension that runs Lua inside a SQLite connection, as scalar functions, aggregates and virtual tables |
| **KitsuneNet.Tests** | C# · xUnit | Test suite covering the engine, every module, and integration tests against real services (Redis, Kafka, MySQL, Postgres, MongoDB) |

---

## Built-in modules

All modules are compiled into `KitsuneEngine` and available as globals. Some of them depend on optional libraries and can be switched off at build time (see [Building](#building)).

**Networking & services**

| Module | What it does |
|---|---|
| `HttpClient` | Async HTTP/1.1 client (libcurl) |
| `HttpServer` | Embedded HTTP/1.1 server (libevent, coroutine-driven) |
| `WebSocket` | Client and server WebSocket connections (from `HttpClient` / `HttpServer` upgrades) |
| `TCP` | Raw non-blocking TCP listener and client (libevent) |
| `Redis` | Redis client including Pub/Sub, Streams and RedisJSON (hiredis) |
| `Kafka` | Kafka producer and consumer (librdkafka) |
| `MCP` | Model Context Protocol server over stdin/stdout |
| `Process` | Spawn and talk to child processes |

**Databases**

| Module | What it does |
|---|---|
| `SQLite` | Embedded SQLite |
| `DuckDB` | Embedded DuckDB analytics database |
| `MySQL` | Async MySQL/MariaDB client (nonblocking API, no threads) |
| `Postgres` | Async PostgreSQL client (libpq async API, no threads) |
| `MongoDB` | MongoDB CRUD (libmongoc, background worker + coroutine yield) |

**Data formats**

| Module | What it does |
|---|---|
| `Json` | Fast JSON encode/decode with stream and chunked-function support |
| `CSV` | CSV encode/decode with delimiter sniffing and streaming |
| `Xml` | XML encode/decode (pugixml) |
| `Yaml` | YAML 1.1 encode/decode (libyaml) |
| `Toml` | TOML 1.0 decode (tomlc99) and encode |
| `Ini` | INI encode/decode, no dependencies |
| `MsgPack` | MessagePack encode/decode |
| `Archive` | Read zip, tar, 7z and other archives (libarchive) |
| `Stream` | In-memory, file and custom-backend byte streams with compression |

**Media & AI**

| Module | What it does |
|---|---|
| `Image` | PNG load/edit/save at pixel level: crop, resize, composite and more |
| `Sound` | PCM audio creation/editing, WAV and OGG Vorbis encode/decode |
| `Llama` | Local LLM inference on GGUF models via llama.cpp (CPU or CUDA), with tool calling and embeddings |

**Crypto & encoding**

| Module | What it does |
|---|---|
| `Aes` | AES-256 (CBC, ECB, CTR) |
| `Base64` | Base64 encode/decode with swappable alphabet |
| `SHA256` / `SHA1` / `MD5` | Incremental hash functions |
| `Text` | Unicode case mapping, UTF-16 and legacy code page conversion |

**Types, time & concurrency**

| Module | What it does |
|---|---|
| `DateTime` | Timezone-aware date/time arithmetic (100 ns ticks) |
| `TimeSpan` | Signed durations in 100 ns ticks (.NET-compatible) |
| `Decimal` | Exact 128-bit base-10 arithmetic |
| `UInt` | Unsigned 64-bit integers with overloaded operators |
| `Identifier` | UUID v4 and MongoDB ObjectID generation |
| `Tasks` | Spawn and track coroutines as tasks |
| `AliveToken` | Shareable cancellation tokens with optional timeout |
| `Timer` | High-resolution stopwatch |
| `Mutex` | Named cross-process mutex |

**System**

| Module | What it does |
|---|---|
| `FileSystem` | File and directory operations with wide-path support |
| `Hardware` | Read-only sensor and system info (temperatures, CPU load, battery) |

The full Lua API reference, including global functions such as `Sleep`, is in [`kitsuneengine-lua-functions.md`](kitsuneengine-lua-functions.md). Read its *Userdata Return Values* section first.

---

## Running scripts

```text
kitsune [-e "expr"]... [script.lua] [args...]
```

- Every `-e` expression is run in order, before the script.
- With no script and no `-e`, `main.lua` in the current directory is run.
- Arguments after the script are available both as `...` and in the standard `arg` table (`arg[-1]` = executable, `arg[0]` = script, `arg[1..n]` = arguments).

```bash
kitsune -e "require('debugger').setup()" main.lua arg1 arg2
```

The process exits once the script and every coroutine it started have finished.

---

## Using Kitsune as an MCP server

The `MCP` module turns a Lua script into a [Model Context Protocol](https://modelcontextprotocol.io) server. The MCP client (Claude Code, Claude Desktop, …) starts `kitsune.exe` with your script and talks to it over stdin/stdout using newline-delimited JSON-RPC. There is no network port. Each Lua function you register shows up as a tool the model can call, and it has the whole engine available: databases, HTTP, file system, images, local LLMs and so on.

### 1. Write a server script

A minimal server with one tool (a fuller version is in [`mcp_server_example.lua`](mcp_server_example.lua)):

```lua
local mcp = MCP.Create(
    { Name = "kitsune-lua", Version = "1.0.0",
      Instructions = "Tools backed by the Kitsune Lua engine." },  -- sent to the client at initialize
    { db = SQLite.Open("notes.db") }                                  -- context: shared state for every call
)

mcp:AddTool(
    "sqlite_query",
    "Runs a read-only SQL query against notes.db and returns the rows as JSON.",
    { { name = "sql", type = "string", description = "SQL SELECT statement", required = true } },
    function(context, request)
        local db = context.db
        local ok, err = db:Query(request.Arguments.sql)
        if not ok then error(err) end              -- reported to the client as a tool error
        local rows = {}
        while db:Fetch() do rows[#rows + 1] = db:GetRow() end
        return Json.New():Encode(rows)
    end
)

local ok, err = mcp:Start()   -- returns immediately; the server runs as its own task
assert(ok, err)

while mcp:IsRunning() do      -- keep the process alive until the client disconnects
    Sleep(20)
end
```

### 2. Register it with a client

Use **absolute paths**. The client starts the process from its own working directory, so relative paths in the command, and relative paths used inside the script, would resolve against the wrong folder.

**Claude Code** (user scope, available in every project):

```bash
claude mcp add kitsune-lua --scope user -- "C:\path\to\x64\Release\Kitsune.exe" "C:\path\to\server.lua"
```

Or share it with a project by committing a `.mcp.json` at the repo root:

```json
{
  "mcpServers": {
    "kitsune-lua": {
      "type": "stdio",
      "command": "C:\\path\\to\\x64\\Release\\Kitsune.exe",
      "args": ["C:\\path\\to\\server.lua"],
      "env": {}
    }
  }
}
```

**Claude Desktop**: add the same `command` / `args` entry under `mcpServers` in `claude_desktop_config.json` (Settings → Developer → Edit Config), then restart the app.

Run `claude mcp list` (or `/mcp` inside a Claude Code session) to check that the server connects and to see its tools.

### Rules and tips for tool authors

- **stdout belongs to the protocol.** Once `Start()` succeeds, the global `print` and `io.write` are redirected. Anything a tool prints while it runs is returned to the client as an extra content entry of that call. Never use `io.stdout:write`, or a `print` saved in a local before `Start()`: they bypass the redirect and corrupt the JSON-RPC stream. `io.output` raises an error in MCP mode. To write files, use `io.open(path, "w")`.
- **Return a string or a number.** Any other value gives an empty result, so encode tables yourself with `Json.New():Encode(t)`.
- **Errors are safe.** A Lua error inside a tool is sent back as a normal tool error (`isError = true`) and the server keeps running.
- **Tools can yield.** Callbacks may call `Sleep`, `HttpClient`, database queries or any other yieldable engine function without blocking the server.
- **Arguments:** use `request.Arguments.name` (named) or `request.Parameters[i]` (positional, in declared order). If the client sends no arguments, `request.Arguments` is the `Json.EmptyObject` sentinel and not a table, so prefer `Parameters` or check `type(...) == "table"` first.
- **Put shared state in `context`** (DB handles, config, caches). It is the second argument to `MCP.Create` and is passed to every call.
- **One server per process.** `MCP.Create` is a singleton; later calls return the existing instance.
- **Debug with a log file**, not stdout. Something like the `log` tool in the example script works, or write to stderr with `io.stderr:write`.
- **Ask the user with an elicitation.** A tool can pause and show the user a form in the client (MCP elicitation), then continue with the answers. Define the form once, with a callback per answer and a default for when the user skips it or the client can't show forms:
  ```lua
  local deploy = mcp:CreateElicitation("Where should this be deployed?", function(context, request, reason)
      return "skipped (" .. reason .. ")"            -- "unsupported", "decline", "cancel" or "timeout"
  end)
  local env = deploy:AddQuestion("env", "string", "Environment", true)
  env:AddAnswer("staging",    "Staging",    function(context, request, value, answers) return deployTo(value) end)
  env:AddAnswer("production", "Production", function(context, request, value, answers) return deployTo(value) end)

  mcp:AddTool("deploy", "Deploys the current build", {}, function(context, request)
      local ok, results = assert(deploy:Elicit(context, request))
      if type(results) ~= "table" then return results end  -- the default ran
      return results.env.staging or results.env.production
  end)
  ```
  `Elicit` returns `true, results` (a table keyed by question name, or the default callback's return), or `false, errmsg` when a callback fails or the client disconnects. `request.CanElicit` tells a tool whether the caller can show forms at all.
- A catch-all `run_lua` tool (see the example script) lets the model run arbitrary Lua against the engine. That is very useful, but it means the model can do anything the process can, so only register it for trusted local use.

Full reference: the [MCP section](kitsuneengine-lua-functions.md#mcp) of the API docs.

---

## C API

`KitsuneEngine.h` is the main integration surface. It exposes a flat C API, so the engine can be embedded from any language that can call C.

### Lifecycle

```c
KitsuneInit();         // create the Lua state
// ... run scripts, exchange variables ...
KitsuneCleanup();      // destroy everything
KitsuneGC(mode);       // collect / query memory usage
```

### Running scripts

Scripts run as coroutines managed by an internal scheduler. The async functions return an integer ID immediately; the sync variants block until done.

```c
// Async: returns a coroutine ID; the result is consumed later
int id = KitsuneExecuteStringAsync("return 1 + 1", 0, NULL, false);

// Sync: blocks the caller, returns the result directly
KitsuneVariable* result = KitsuneExecuteString("return 1 + 1", 0, NULL);
KitsuneVariableFree(result);

// Query / control a running coroutine
KitsuneGetStatus(id);   // KITSUNE_STATUS_RUNNING, _DONE, _FAULTED, ...
KitsuneHasResult(id, NULL);
KitsuneVariable* r = KitsuneGetResult(id);  // consumes the slot
KitsuneCancel(id);
KitsuneWait();          // block until all coroutines finish
```

All four execution targets are supported (`File`, `String`, `Function`, `Variable`), each in sync and async flavours.

### Variables (`KitsuneVariable`)

A tagged union representing any Lua value that crosses the host/Lua boundary.

```c
// String → Lua
KitsuneVariable v = { .type = KITSUNE_TSTRING, .data = (unsigned char*)"hello", .length = 5 };
KitsuneSetVariable("greeting", &v);

// Lua → host
KitsuneVariable* got = KitsuneGetVariable("greeting");
// got->type == KITSUNE_TSTRING
KitsuneVariableFree(got);
```

| Constant | Value | Description |
|---|---|---|
| `KITSUNE_TNONE` | −1 | No value |
| `KITSUNE_TNIL` | 0 | Lua nil |
| `KITSUNE_TBOOLEAN` | 1 | bool |
| `KITSUNE_TNUMBER` | 3 | double |
| `KITSUNE_TINTEGER` | −3 | lua_Integer (int64) |
| `KITSUNE_TSTRING` | 4 | UTF-8 byte string |
| `KITSUNE_TTABLE` | 5 | live Lua table (registry ref) |
| `KITSUNE_TFUNCTION` | 6 | live Lua function (registry ref) |
| `KITSUNE_TUSERDATA` | 7 | registered userdata |
| `KITSUNE_TTHREAD` | 8 | Lua coroutine |
| `KITSUNE_TJSON` | −5 | pre-encoded JSON string |
| `KITSUNE_TCFUNCTION` | −6 | anonymous C function closure |
| `KITSUNE_TITERATOR` | −7 | custom iterator |
| `KITSUNE_TTABLECONTENTS` | −8 | table snapshot (linked list of key/value pairs) |
| `KITSUNE_TERROR` | −2 | error message |

The numeric coercion helpers (`KitsuneAsFloat`, `KitsuneAsDouble`, `KitsuneAsInt`) follow Lua's coercion order: integer first, then number, then string. `KitsuneAsBool` follows **strict Lua truthiness**: only `nil` and `false` are falsy, while `0`, `0.0` and `""` are all truthy.

### Registering C functions

```c
int my_add(int argc, const KitsuneVariable* argv,
           const kitsune_ResultSetter set, void* ud) {
    KitsuneVariable r = { .type = KITSUNE_TINTEGER,
                          .integer = argv[0].integer + argv[1].integer };
    set(&r);
    return 1;
}

KitsuneRegisterFunction("Math.Add", my_add, NULL, NULL);
// Lua: local n = Math.Add(1, 2)  --> 3
```

Dot-separated names create the intermediate tables automatically. An optional `finalizer` is called when the Lua closure is garbage-collected.

### Registering userdata types

```c
KitsuneNamedFunction gc   = { "__gc",       my_gc,       NULL, NULL, NULL };
KitsuneNamedFunction str  = { "__tostring", my_tostring, NULL, NULL, NULL };
KitsuneNamedFunction greet= { "Greet",      my_greet,    NULL, NULL, &str  };
gc.Next = &str;

KitsuneUserDataRegistration reg = {
    .MetaTableFunctions = &gc,
    .Functions          = &greet,
};
KitsuneRegisterUserdata("MyType", &reg);
```

Both `__gc` and `__tostring` are required. Every method can be called as `MyType.Greet()` or `instance:Greet()`.

### Table and index operations

```c
KitsuneVariable* val = KitsuneGetIndex(obj, key);                      // obj[key]        (__index)
KitsuneSetIndex(obj, key, value);                                      // obj[key] = value (__newindex)
KitsuneVariable* len = KitsuneGetLength(obj);                          // #obj            (__len)
KitsuneVariable* ret = KitsuneCallMethod(obj, "Save", argc, argv);     // obj:Save(...)
KitsuneVariable* s   = KitsuneCallMetamethod(obj, "__tostring", 0, NULL);
KitsuneVariable* snap = KitsuneGetTableContents(tableVar);            // snapshot as linked list

// Iterate a table one step at a time (mirrors lua_next)
KitsuneVariable* cursor = NULL;
while (true) {
    cursor = KitsuneNext(tableVar, cursor);
    if (cursor->type != KITSUNE_TTABLECONTENTS) break;
    // cursor->table->key / cursor->table->value
}
KitsuneVariableFree(cursor);
```

### Coroutine status codes

| Constant | Value | Meaning |
|---|---|---|
| `KITSUNE_STATUS_NONE` | 0 | ID not found |
| `KITSUNE_STATUS_IDLE` | 1 | Queued, waiting to be resumed |
| `KITSUNE_STATUS_SLEEPING` | 2 | Waiting out a `Sleep()` deadline |
| `KITSUNE_STATUS_RUNNING` | 3 | Currently executing |
| `KITSUNE_STATUS_DONE` | 4 | Finished, result not yet consumed |
| `KITSUNE_STATUS_FAULTED` | 5 | Finished with a Lua error |
| `KITSUNE_STATUS_CANCELLED` | 6 | Cancelled |
| `KITSUNE_STATUS_INLINE` | 7 | Paused inside a cooperative yield window |

---

## KitsuneNet (.NET)

Managed bindings for `KitsuneEngine`:

- Create and dispose engine instances (`KitsuneEngine`)
- Run scripts as async coroutines with `ExecuteStringAsync` / `ExecuteFileAsync`
- Exchange typed variables with the Lua state (`GetVariable`, `SetVariable`)
- Register .NET delegates as Lua global functions
- Register .NET objects as Lua userdata with methods and metamethods
- Drive coroutines step by step, or iterate them as `IAsyncEnumerable<LuaValue>`
- Cancel running scripts with a `CancellationToken`

---

## Building

### Windows

Open `Kitsune.sln` in Visual Studio 2022 or later and build `x64` `Release` (or `Debug`). Some dependencies come from vcpkg (`vcpkg.json`); run `vcpkg install` from the repo root before the first build if prompted. The runner ends up in `x64\<Configuration>\Kitsune.exe`, next to `KitsuneEngine.dll` and the dependency DLLs.

To build from the command line with MSBuild, pass `SolutionDir` explicitly, because the project include paths depend on it:

```bash
msbuild KitsuneEngine.vcxproj -p:Configuration=Release -p:Platform=x64 -p:SolutionDir=C:\path\to\Lua-Kitsune\
```

For the `Llama` module, run `fetch-llama-binaries.ps1` first to download the llama.cpp binaries.

Tests:

```bash
dotnet test KitsuneNet.Tests/KitsuneNet.Tests.csproj -c Debug -p:BuildProjectReferences=false
```

Build the native engine first. `dotnet` cannot build the referenced `.vcxproj` itself. Debug builds track every native allocation and fail a test on `Dispose` when memory leaks.

### Linux

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

Optional modules are off by default and can be switched on one at a time, or all together with `KITSUNE_ALL`:

| Option | Requires |
|---|---|
| `KITSUNE_HTTP` | libcurl ≥ 7.86 with WebSocket support |
| `KITSUNE_REDIS` | hiredis ≥ 1.0 with SSL |
| `KITSUNE_MYSQL` | libmysqlclient ≥ 8.0 |
| `KITSUNE_POSTGRES` | libpq |
| `KITSUNE_KAFKA` | librdkafka ≥ 1.9 |
| `KITSUNE_ARCHIVE` | libarchive ≥ 3.2 |
| `KITSUNE_MONGO` | libmongoc 2.x (or 1.x ≥ 1.17) |
| `KITSUNE_LLAMA` | llama.cpp binaries (`fetch-llama-binaries.ps1`) |
| `KITSUNE_IMGUI` | SDL2 and OpenGL |
| `KITSUNE_AUDIO` | SDL_mixer (needs `KITSUNE_IMGUI`) |

`build-linux-and-test.ps1` builds and tests the Linux target.

---

## Documentation

- Lua API reference: [`kitsuneengine-lua-functions.md`](kitsuneengine-lua-functions.md)
- MCP example server: [`mcp_server_example.lua`](mcp_server_example.lua)

---

## License

MIT, see [`LICENSE`](LICENSE).

Licenses for third-party dependencies are listed in the [Third-Party Notices](kitsuneengine-lua-functions.md#third-party-notices) section of the API reference.
