# Lua-Kitsune

A Lua 5.4 scripting engine with a rich set of built-in modules for systems programming, networked applications, and data processing. The engine is designed around cooperative coroutines — multiple Lua scripts run concurrently on a single thread by yielding to each other, with no OS threads involved in normal operation.

## C API

`KitsuneEngine.h` is the primary integration surface. It exposes a flat C API so the engine can be embedded in any native application regardless of language or runtime.

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
// Async — returns a coroutine ID; result consumed later
int id = KitsuneExecuteStringAsync("return 1 + 1", 0, NULL, false);

// Sync — blocks the caller, returns the result directly
KitsuneVariable* result = KitsuneExecuteString("return 1 + 1", 0, NULL);
KitsuneVariableFree(result);

// Query / control a running coroutine
KitsuneGetStatus(id);   // KITSUNE_STATUS_RUNNING, _DONE, _FAULTED, ...
KitsuneHasResult(id, NULL);
KitsuneVariable* r = KitsuneGetResult(id);  // consumes the slot
KitsuneCancel(id);
KitsuneWait();          // block until all coroutines finish
```

All four execution targets are supported: `File`, `String`, `Function`, `Variable` — each in both sync and async flavours.

### Variables (`KitsuneVariable`)

A tagged union that represents any Lua value crossing the host/Lua boundary.

```c
// String → Lua
KitsuneVariable v = { .type = KITSUNE_TSTRING, .data = (unsigned char*)"hello", .length = 5 };
KitsuneSetVariable("greeting", &v);

// Lua → host
KitsuneVariable* got = KitsuneGetVariable("greeting");
// got->type == KITSUNE_TSTRING
KitsuneVariableFree(got);
```

**Type constants:**

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
| `KITSUNE_TCHAR16` | −4 | UTF-16 string |
| `KITSUNE_TJSON` | −5 | pre-encoded JSON string |
| `KITSUNE_TCFUNCTION` | −6 | anonymous C function closure |
| `KITSUNE_TITERATOR` | −7 | custom iterator |
| `KITSUNE_TTABLECONTENTS` | −8 | table snapshot (linked list of key/value pairs) |
| `KITSUNE_TERROR` | −2 | error message |

Coercion helpers are provided for numeric types (`KitsuneAsFloat`, `KitsuneAsDouble`, `KitsuneAsInt`) that follow Lua's coercion order — integer beats number beats string. `KitsuneAsBool` follows **strict Lua truthiness**: only `nil` and `false` are falsy; `0`, `0.0`, and `""` are all truthy.

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

Dot-separated names create intermediate tables automatically. An optional `finalizer` is called when the Lua closure is GC'd.

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

Both `__gc` and `__tostring` are required. All methods are reachable as `MyType.Greet()` or `instance:Greet()`.

### Table and index operations

```c
// obj[key]  — fires __index
KitsuneVariable* val = KitsuneGetIndex(obj, key);

// obj[key] = value  — fires __newindex
KitsuneSetIndex(obj, key, value);

// #obj  — fires __len
KitsuneVariable* len = KitsuneGetLength(obj);

// obj:Method(args...)
KitsuneVariable* ret = KitsuneCallMethod(obj, "Save", argc, argv);

// getmetatable(obj).__name(obj, args...)
KitsuneVariable* ret = KitsuneCallMetamethod(obj, "__tostring", 0, NULL);

// Snapshot table contents to a linked list
KitsuneVariable* snap = KitsuneGetTableContents(tableVar);

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

## Projects

### KitsuneEngine (C++ · DLL)

The core native library. Embeds Lua 5.4 and exposes the C API above. All built-in Lua modules are compiled into this DLL.

**Built-in modules:**

| Module | What it does |
|--------|-------------|
| `HttpClient` | Async HTTP/1.1 and WebSocket client (libcurl) |
| `HttpServer` | Embedded HTTP/1.1 server (libevent, poll-based, coroutine-driven) |
| `Redis` | Full Redis client including Pub/Sub, Streams, and RedisJSON |
| `MySQL` | Async MySQL/MariaDB client (nonblocking API, no threads) |
| `Postgres` | Async PostgreSQL client (libpq async API, no threads) |
| `MongoDB` | MongoDB CRUD via libmongoc (background worker, coroutine yield) |
| `Kafka` | Kafka producer and consumer (librdkafka) |
| `SQLite` | Embedded SQLite database |
| `Archive` | Read zip/tar/etc. archives (libarchive) |
| `Stream` | In-memory, file, and custom-backend byte streams with compression |
| `Json` | Fast JSON encode/decode with stream and chunked-function support |
| `CSV` | CSV encode/decode with auto-delimiter sniffing and streaming |
| `Aes` | AES-256 encryption (CBC, ECB, CTR) |
| `Base64` | Base64 encode/decode with swappable alphabet |
| `Wchar` | UTF-16 wide string type with full Unicode operations |
| `DateTime` | Timezone-aware date/time arithmetic (100ns tick precision) |
| `Decimal` | Exact 128-bit base-10 arithmetic |
| `Identifier` | RFC 4122 UUID v4 and MongoDB ObjectID generation |
| `Timer` | High-resolution stopwatch |
| `Mutex` | Named cross-process mutex |
| `Process` | Spawn and communicate with child processes |
| `FileSystem` | File and directory operations with wide-path support |
| `MsgPack` | MessagePack encode/decode |
| `SHA256 / MD5 / SHA1` | Incremental hash functions |

---

### KitsuneNet (C# · .NET 10)

Managed .NET bindings for `KitsuneEngine`. Lets you embed and drive the Lua engine from any .NET application.

**Key features:**
- Create and dispose engine instances (`KitsuneEngine`)
- Run scripts as async coroutines with `ExecuteStringAsync` / `ExecuteFileAsync`
- Exchange typed variables with the Lua state (`GetVariable`, `SetVariable`)
- Register .NET delegates as Lua global functions
- Register .NET objects as Lua userdata with methods and metamethods
- Drive coroutines step-by-step or iterate them as `IAsyncEnumerable<LuaValue>`
- Cancel running scripts via `CancellationToken`

---

### SQLiteKitsuneExtension (C++ · DLL)

A SQLite loadable extension that embeds a `KitsuneEngine` instance inside a SQLite database connection. Exposes Lua functions as SQL scalar functions, aggregate functions, and virtual tables, letting you write SQL logic in Lua directly from any SQLite client.

---

### Kitsune (C++ · Executable)

A standalone REPL and script runner for `KitsuneEngine`. Includes optional [Dear ImGui](https://github.com/ocornut/imgui) and SDL2 bindings for building immediate-mode GUI applications in Lua.

---

### KitsuneNet.Tests (C# · xUnit)

The full test suite for `KitsuneNet` and `KitsuneEngine`. Covers all engine features, all Lua modules, and integration tests against real services (Redis, Kafka, MySQL, Postgres, MongoDB).

---

## Building

### Windows

Open `Kitsune.sln` in Visual Studio 2022 or later and build. vcpkg is used for some dependencies — run `vcpkg install` from the repo root before the first build if prompted.

### Linux

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## Documentation

Full Lua API reference: [`kitsuneengine-lua-functions.md`](kitsuneengine-lua-functions.md)

---

## License

MIT — see [`LICENSE`](LICENSE).

Third-party dependency licenses are listed in the [Third-Party Notices](kitsuneengine-lua-functions.md#third-party-notices) section of the API reference.


### KitsuneEngine (C++ · DLL)

The core native library. Embeds Lua 5.4 and exposes a C API that hosts can call to create engine instances, run scripts, exchange variables, and register callbacks. All built-in Lua modules are compiled into this DLL.

**Built-in modules:**

| Module | What it does |
|--------|-------------|
| `HttpClient` | Async HTTP/1.1 and WebSocket client (libcurl) |
| `HttpServer` | Embedded HTTP/1.1 server (libevent, poll-based, coroutine-driven) |
| `Redis` | Full Redis client including Pub/Sub, Streams, and RedisJSON |
| `MySQL` | Async MySQL/MariaDB client (nonblocking API, no threads) |
| `Postgres` | Async PostgreSQL client (libpq async API, no threads) |
| `MongoDB` | MongoDB CRUD via libmongoc (background worker, coroutine yield) |
| `Kafka` | Kafka producer and consumer (librdkafka) |
| `SQLite` | Embedded SQLite database |
| `Archive` | Read zip/tar/etc. archives (libarchive) |
| `Stream` | In-memory, file, and custom-backend byte streams with compression |
| `Json` | Fast JSON encode/decode with stream and chunked-function support |
| `CSV` | CSV encode/decode with auto-delimiter sniffing and streaming |
| `Aes` | AES-256 encryption (CBC, ECB, CTR) |
| `Base64` | Base64 encode/decode with swappable alphabet |
| `Wchar` | UTF-16 wide string type with full Unicode operations |
| `DateTime` | Timezone-aware date/time arithmetic (100ns tick precision) |
| `Decimal` | Exact 128-bit base-10 arithmetic |
| `Identifier` | RFC 4122 UUID v4 and MongoDB ObjectID generation |
| `Timer` | High-resolution stopwatch |
| `Mutex` | Named cross-process mutex |
| `Process` | Spawn and communicate with child processes |
| `FileSystem` | File and directory operations with wide-path support |
| `MsgPack` | MessagePack encode/decode |
| `SHA256 / MD5 / SHA1` | Incremental hash functions |

---

### KitsuneNet (C# · .NET 10)

Managed .NET bindings for `KitsuneEngine`. Lets you embed and drive the Lua engine from any .NET application.

**Key features:**
- Create and dispose engine instances (`KitsuneEngine`)
- Run scripts as async coroutines with `ExecuteStringAsync` / `ExecuteFileAsync`
- Exchange typed variables with the Lua state (`GetVariable`, `SetVariable`)
- Register .NET delegates as Lua global functions
- Register .NET objects as Lua userdata with methods and metamethods
- Drive coroutines step-by-step or iterate them as `IAsyncEnumerable<LuaValue>`
- Cancel running scripts via `CancellationToken`

---

### SQLiteKitsuneExtension (C++ · DLL)

A SQLite loadable extension that embeds a `KitsuneEngine` instance inside a SQLite database connection. Exposes Lua functions as SQL scalar functions, aggregate functions, and virtual tables, letting you write SQL logic in Lua directly from any SQLite client.

---

### Kitsune (C++ · Executable)

A standalone REPL and script runner for `KitsuneEngine`. Includes optional [Dear ImGui](https://github.com/ocornut/imgui) and SDL2 bindings for building immediate-mode GUI applications in Lua.

---

### KitsuneNet.Tests (C# · xUnit)

The full test suite for `KitsuneNet` and `KitsuneEngine`. Covers all engine features, all Lua modules, and integration tests against real services (Redis, Kafka, MySQL, Postgres, MongoDB).

---

## Building

### Windows

Open `Kitsune.sln` in Visual Studio 2022 or later and build. vcpkg is used for some dependencies — run `vcpkg install` from the repo root before the first build if prompted.

### Linux

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

---

## Documentation

Full Lua API reference: [`kitsuneengine-lua-functions.md`](kitsuneengine-lua-functions.md)

---

## License

MIT — see [`LICENSE`](LICENSE).

Third-party dependency licenses are listed in the [Third-Party Notices](kitsuneengine-lua-functions.md#third-party-notices) section of the API reference.
