# Postgres Module Rewrite — Implementation Plan

## Overview

Full rewrite of `LuaPostgres.h`, `LuaPostgres.cpp`, and `PostgresMain.cpp` to
replace the Win32-only thread-per-connection model with:

- **Both platforms**: libpq asynchronous API (`PQconnectStart`/`PQconnectPoll`,
  `PQsendQuery`/`PQsendQueryParams`, `PQflush`, `PQconsumeInput`/`PQisBusy`,
  `PQgetResult`) driven by `lua_yieldk` continuations — zero background threads.

Windows links `postgres/lib/libpq.lib` via `#pragma comment` (unchanged).
Linux discovers the library via `pkg_check_modules(libpq)`.

The result set is streamed row-by-row through a Lua coroutine — identical
protocol to the MySQL rewrite. The old `Fetch`/`GetRow`/`Finish` polling model
is removed.

**libpq nonblocking socket requirement**: after connect the socket must be put
into OS-level non-blocking mode so that `PQconsumeInput` returns immediately
when no data is available (rather than blocking in `recv`/`WSARecv`). This is
abstracted into a `static void set_fd_nonblocking(int fd)` helper defined once
in a single `#ifdef _WIN32` / `#else` block at the top of `LuaPostgres.cpp`
(identical pattern to `StartCounter`/`GetCounter` in `KitsuneEngine.cpp`).
All logic below that helper is platform-agnostic — zero additional `#ifdef`
anywhere in `LuaPostgres.cpp`.

```c
// Windows branch  (requires <WinSock2.h> included before platform.h)
static void set_fd_nonblocking(int fd) {
    u_long one = 1;
    ioctlsocket((SOCKET)(uintptr_t)fd, FIONBIO, &one);
}
// Linux branch
static void set_fd_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
```

---

## Final Lua API

Identical coroutine protocol to MySQL. Connection string replaces individual
host/user/pass arguments.

```lua
-- Connect (yields cooperatively during TCP + auth)
local conn, err = Postgres.Connect("host=127.0.0.1 user=postgres password=secret dbname=mydb")
if not conn then error(err) end

-- Query returns a coroutine immediately
local co, err = conn:Query("SELECT id, name FROM users WHERE active = $1", {true})
if not co then error(err) end

-- Phase 1: drive the async state machine
local ok, val = coroutine.resume(co)
while ok and val == nil and coroutine.status(co) == "suspended" do
    ok, val = coroutine.resume(co)
end
if not ok then error(val) end
if type(val) == "string" then error(val) end
local rowcount = val  -- integer

-- Phase 2: stream integer-keyed row arrays
ok, val = coroutine.resume(co)
while ok and val ~= nil do
    print(val[1], val[2])
    ok, val = coroutine.resume(co)
end

-- Stop early at any phase
coroutine.resume(co, true)
```

### Helper methods (same as MySQL)

```lua
local ok, affected = conn:NonQuery("UPDATE users SET name = $1 WHERE id = $2", {newName, id})
local ok, name     = conn:Scalar("SELECT name FROM users WHERE id = $1", {id})
local ok, rows     = conn:QueryAll("SELECT id, name FROM users")
```

### Coroutine yield protocol reference

| `coroutine.resume` returns | Meaning |
|---|---|
| `true, nil` + status `"suspended"` | Query still in progress |
| `true, <integer>` | Done — affected/row count |
| `true, <string>` | Done — query-level error message |
| `true, {col1, col2, …}` | One data row (integer-keyed array) |
| `true, nil` + status `"dead"` | All rows consumed |
| `false, <string>` | Coroutine raised an error |

---

## Data Structures

### `LuaPostgres` (connection object — simplified)

```c
typedef struct LuaPostgres {
    PGconn*  connection;   // libpq connection handle
    int      queryRef;     // Lua registry ref to active query coroutine; LUA_NOREF = idle
    void*    activeQuery;  // LuaPostgresQuery* of the running query, or NULL
    char*    error;        // last connection-level error
} LuaPostgres;
```

### `LuaPostgresQuery` (query object — upvalue of the coroutine body)

```c
#define PG_HELPER_RAW      0
#define PG_HELPER_NONQUERY 1
#define PG_HELPER_SCALAR   2
#define PG_HELPER_QUERYALL 3

typedef struct LuaPostgresQuery {
    LuaPostgres*  conn;           // back-pointer, NOT owned
    int           connRef;        // registry ref to conn userdata
    char**        paramValues;    // already-allocated param strings for PQsendQueryParams
    int*          paramLengths;
    int           nParams;
    char*         sql;            // query string (gff_malloc'd copy)
    PGresult*     result;         // populated after PQgetResult
    char*         error;          // query-level error, or NULL
    int           cancelFnRef;    // registry ref to cancelFn, LUA_NOREF if none
    int           helperMode;     // PG_HELPER_* constant
    int           accumTableIdx;  // QueryAll: absolute L stack index of row array
    int           accumRowIdx;    // QueryAll: next 1-based row index to write
} LuaPostgresQuery;
```

---

## C Continuation Chain

### Connect

```
PostgresConnect(L)
  │  PQconnectStart(conninfo) → conn
  │  set_fd_nonblocking(PQsocket(conn))   ← unified call, no #ifdef
  │  PQconnectPoll(conn)
  │    PGRES_POLLING_READING / _WRITING → lua_yieldk(nil, ConnectCont)
  │    PGRES_POLLING_FAILED → PQfinish, luaL_error
  ↓  PGRES_POLLING_OK
     PQsetClientEncoding(conn, "UTF8")
     return connection userdata
```

### Query body

```
PostgresQueryBody(L)           ← initial C function on the query coroutine
  │  PQsendQuery / PQsendQueryParams
  │  if returns 0 → error
  ↓
QueryFlushCont(L, status, ctx)
  │  PQflush(conn)
  │    1 (incomplete) → lua_yieldk(nil, QueryFlushCont)
  │   -1 (error)     → FreeQuery, yield errmsg
  ↓  0 (done)
QueryPollCont(L, status, ctx)
  │  PQconsumeInput(conn)
  │  PQisBusy(conn)
  │    1 (busy) → lua_yieldk(nil, QueryPollCont)
  ↓  0 (result ready)
  │  PQgetResult(conn) → result
  │  Drain remaining results (PQgetResult until NULL)
  │  Check ExecStatusType:
  │    PGRES_COMMAND_OK → yield integer (PQcmdTuples affected count)
  │    PGRES_TUPLES_OK  → yield integer (PQntuples rowcount)
  │    other            → yield errmsg string
  ↓
QueryStreamCont(L, status, ctx)   ← called for every subsequent resume
  │  check stop flag (lua_toboolean(L,1))
  │    true → PQclear, clear conn->queryRef, free query, return nil
  │  fetch next row from result buffer (integer index, synchronous)
  │    row  → build integer-keyed Lua table, lua_yieldk(row, QueryStreamCont)
  ↓  all rows consumed
     PQclear(result), clear conn->queryRef, free query, return nil
```

`FreeQuery(lua_State* L, LuaPostgresQuery* q)`:
- `PQclear(q->result)` if set
- `luaL_unref` for `q->connRef` and `q->cancelFnRef`
- Free `q->paramValues[i]`, `q->paramValues`, `q->paramLengths`
- `gff_free(q->sql)`, `gff_free(q->error)`
- Clear `q->conn->activeQuery = NULL`, `q->conn->queryRef = LUA_NOREF`
- `gff_free(q)`

Helper continuations (`Helper_WaitCont`, `Helper_StreamCont`) are identical to
the MySQL implementation — see MySQL Implementation Plan for the full chain.

---

## `PushPostgresValue` (row column → Lua value)

Keep the existing OID switch unchanged. Rows are yielded as integer-keyed arrays
(same as MySQL); the old named-key hash table from `GetRow` is removed.

---

## Files to Modify

### 1. `LuaPostgres.h`

- Remove `#include <Windows.h>` — use `"platform.h"` guard in the `.cpp` instead
- Remove fields: `busy`, `alive`, `result`, `currentRow`, `query`, `querylen`,
  `paramValues`, `paramLengths`, `nParams`, `isParamQuery`, `thread`, `interrupt`
- Add fields: `int queryRef`, `void* activeQuery`
- Remove declarations: `PostgresFetch`, `PostgresGetRow`, `PostgresFinish`
- Add declarations: `PostgresNonQuery`, `PostgresScalar`, `PostgresQueryAll`
- Add `#include <inttypes.h>` for `PRIx64` in `luapostgres_tostring`

### 2. `LuaPostgres.cpp`

Complete rewrite. **Unified code path — no `#ifdef` in any logic.**

File header (the only `#ifdef _WIN32` block in the entire file):

```cpp
#ifdef _WIN32
#include <WinSock2.h>           // ioctlsocket — must precede platform.h / Windows.h
#pragma comment(lib, "postgres/lib/libpq.lib")
#endif
#include "platform.h"
#include "LuaPostgres.h"
#include "luawchar.h"
```

Immediately after the includes, one `#ifdef _WIN32` / `#else` block defines
`set_fd_nonblocking(int fd)` (Windows: `ioctlsocket`; Linux: `fcntl` +
`O_NONBLOCK`). This is the **only** platform split in the whole file —
identical in structure to `StartCounter`/`GetCounter` in `KitsuneEngine.cpp`.
Every function below calls `set_fd_nonblocking` directly with no `#ifdef`.

Sections:

- `PushAsParamString` — move parameter serialisation into `BuildQueryParams`
  which allocates `q->paramValues` / `q->paramLengths` for `PQsendQueryParams`
- `PushPostgresValue` — unchanged, still OID-switch based
- `lua_pushpostgres` / `lua_topostgres` — simplified init, `queryRef = LUA_NOREF`
- `PostgresConnect` — `PQconnectStart` + `PQconnectPoll` continuation
- `PostgresQuery` — allocate `LuaPostgresQuery`, `lua_newthread`, store `queryRef`,
  return coroutine (same pattern as `MySqlQuery`)
- `PostgresQueryBody` + `QueryFlushCont` + `QueryPollCont` + `QueryStreamCont`
- `FreeQuery`
- `PostgresIsBusy` — `lua_status(T) == LUA_YIELD` check (same as MySQL)
- `luapostgres_gc` — unref `queryRef`, `PQfinish`, no thread join
- `luapostgres_tostring` — `uint64_t` / `PRIx64`
- `PostgresNonQuery`, `PostgresScalar`, `PostgresQueryAll` — C helpers with
  `lua_yieldk`, identical structure to MySQL helpers

### 3. `PostgresMain.cpp`

Remove `Fetch`, `GetRow`, `Finish` from the function table. Add `NonQuery`,
`Scalar`, `QueryAll`. Follow the same wchar pattern as `MySQLMain.cpp`:

```cpp
static const luaL_Reg postgresfunctions[] = {
    { "Connect",      PostgresConnect      },
    { "IsBusy",       PostgresIsBusy       },
    { "EscapeValue",  PostgresEscapeValue  },
    { "Query",        PostgresQuery        },
    { "NonQuery",     PostgresNonQuery     },
    { "Scalar",       PostgresScalar       },
    { "QueryAll",     PostgresQueryAll     },
    { "Close",        luapostgres_gc       },
    { NULL, NULL }
};

static const luaL_Reg postgresmeta[] = {
    { "__gc",         luapostgres_gc       },
    { "__tostring",   luapostgres_tostring },
    { NULL, NULL }
};
```

### 4. `CMakeLists.txt`

Add a Linux opt-in option mirroring the MySQL one:

```cmake
option(KITSUNE_POSTGRES "Build with PostgreSQL support (requires libpq)" OFF)

if(KITSUNE_POSTGRES)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBPQ REQUIRED libpq)

    target_sources(KitsuneEngine PRIVATE LuaPostgres.cpp PostgresMain.cpp)

    target_compile_definitions(KitsuneEngine PRIVATE KITSUNE_POSTGRES)
    target_include_directories(KitsuneEngine PRIVATE ${LIBPQ_INCLUDE_DIRS})
    target_link_libraries(KitsuneEngine PRIVATE ${LIBPQ_LINK_LIBRARIES})
endif()
```

### 5. `KitsuneEngine.cpp`

Change the existing `#ifdef _WIN32` gate to `#ifdef KITSUNE_POSTGRES`:

```cpp
// Before:
#ifdef _WIN32
    luaopen_postgres(L);  lua_setglobal(L, "Postgres");
    ...
#endif

// After:
#ifdef KITSUNE_POSTGRES
    luaopen_postgres(L);  lua_setglobal(L, "Postgres");
#endif
```

`KITSUNE_POSTGRES` is defined by `platform.h` under `KITSUNE_ALL` on Windows
and by CMake on Linux.

### 6. `platform.h`

Add under `#ifdef KITSUNE_ALL`:

```c
#   define KITSUNE_POSTGRES
```

No other change needed. The `set_fd_nonblocking` helper lives in
`LuaPostgres.cpp` (private to that translation unit), keeping `platform.h`
free of any Postgres-specific includes like `<fcntl.h>`.

### 7. `postgres/include/libpq-fe.h` (Windows stub only)

The stub currently declares only the synchronous API. Add every symbol needed
by the async rewrite so the Windows build compiles without the real libpq
header tree:

```c
typedef enum {
    PGRES_POLLING_FAILED = 0,
    PGRES_POLLING_READING,
    PGRES_POLLING_WRITING,
    PGRES_POLLING_OK,
    PGRES_POLLING_ACTIVE
} PostgresPollingStatusType;

// Nonblocking connect
extern PGconn*                   PQconnectStart(const char* conninfo);
extern PostgresPollingStatusType PQconnectPoll(PGconn* conn);

// Nonblocking query dispatch
extern int  PQsendQuery(PGconn* conn, const char* query);
extern int  PQsendQueryParams(PGconn* conn, const char* command,
                int nParams, const Oid* paramTypes,
                const char* const* paramValues,
                const int* paramLengths,
                const int* paramFormats, int resultFormat);

// Flush / poll / fetch
extern int        PQflush(PGconn* conn);
extern int        PQconsumeInput(PGconn* conn);
extern int        PQisBusy(PGconn* conn);
extern PGresult*  PQgetResult(PGconn* conn);

// Socket fd (for set_fd_nonblocking)
extern int   PQsocket(const PGconn* conn);

// Affected-row count (PGRES_COMMAND_OK path)
extern char* PQcmdTuples(PGresult* res);
```

On Linux the real system header (found via `pkg_check_modules(LIBPQ REQUIRED libpq)`)
already declares all of the above.

### 8. `KitsuneNet.Tests/PostgresTests.cs`

New file. Mirrors `MySqlTests.cs`. Gate with a `PostgresFactAttribute` that
skips unless `KITSUNE_POSTGRES_TEST=conninfo` is set.

```csharp
public sealed class PostgresFactAttribute : FactAttribute
{
    public PostgresFactAttribute()
    {
        if (string.IsNullOrEmpty(
                Environment.GetEnvironmentVariable("KITSUNE_POSTGRES_TEST")))
            Skip = "Set KITSUNE_POSTGRES_TEST=<conninfo> to run Postgres tests";
    }
}
```

Test groups mirror `MySqlTests.cs`:
- Coroutine protocol (connect, raw SELECT, INSERT, error, stop-flag, IsBusy)
- CRUD helpers (NonQuery, Scalar, QueryAll)
- Type mapping (integer OIDs → integer, float OIDs → number, BOOL → boolean, others → string)

### 9. `KitsuneNet.Tests/kitsune.runsettings`

Add `KITSUNE_POSTGRES_TEST` alongside the existing `KITSUNE_MYSQL_TEST`.

---

## Install on Ubuntu / WSL2

```sh
sudo apt-get install libpq-dev
cmake -DKITSUNE_MYSQL=ON -DKITSUNE_POSTGRES=ON -B build-linux .
cmake --build build-linux
```

---

## Task Checklist

- [x] **T0** — Update `postgres/include/libpq-fe.h` (Windows stub): add `PostgresPollingStatusType` enum and async function declarations (`PQconnectStart`, `PQconnectPoll`, `PQsendQueryParams`, `PQflush`, `PQconsumeInput`, `PQisBusy`, `PQgetResult`, `PQsocket`, `PQcmdTuples`). Linux uses the real system header; no change needed there.
- [x] **T1** — Rewrite `LuaPostgres.h`
- [x] **T2** — Write the `LuaPostgres.cpp` file header: `#ifdef _WIN32` / `#include <WinSock2.h>` / `#pragma comment` / `#endif`, then `platform.h`, then the `set_fd_nonblocking` helper in one `#ifdef _WIN32` / `#else` block. All logic below is platform-agnostic. Define `LuaPostgresQuery` struct.
- [x] **T3** — Rewrite `lua_pushpostgres` (simplified init, `queryRef = LUA_NOREF`)
- [x] **T4** — Write `BuildQueryParams`: allocate `paramValues`/`paramLengths` arrays from Lua table for `PQsendQueryParams`
- [x] **T5** — Rewrite `PostgresConnect`: `PQconnectStart` + `ConnectCont` (`PQconnectPoll` loop) via `lua_yieldk`; call `set_fd_nonblocking(PQsocket(conn))` after `PGRES_POLLING_OK` — single unified call, no `#ifdef` at the call site
- [x] **T6** — Write `PostgresQuery`: allocate `LuaPostgresQuery`, `lua_newthread`, store `queryRef`, return coroutine
- [x] **T7** — Write continuation chain: `PostgresQueryBody`, `QueryFlushCont`, `QueryPollCont`, `QueryStreamCont`
- [x] **T8** — Write `FreeQuery` cleanup helper
- [x] **T9** — Rewrite `PostgresIsBusy` (`lua_status` check)
- [x] **T10** — Rewrite `luapostgres_gc` (no thread join, `PQfinish`, unref `queryRef`)
- [x] **T11** — Fix `luapostgres_tostring` (`uint64_t` / `PRIx64`)
- [x] **T12** — Write `PostgresNonQuery`, `PostgresScalar`, `PostgresQueryAll` C helpers with `lua_yieldk` (`Helper_WaitCont`, `Helper_StreamCont`)
- [x] **T13** — Rewrite `PostgresMain.cpp`: remove `Fetch`/`GetRow`/`Finish`, add `NonQuery`/`Scalar`/`QueryAll`, `__index` pattern
- [x] **T14** — Update `platform.h`: add `KITSUNE_POSTGRES` under `KITSUNE_ALL`. No other change — the `set_fd_nonblocking` helper is private to `LuaPostgres.cpp`
- [x] **T15** — Update `KitsuneEngine.cpp`: replace `#ifdef _WIN32` gate with `#ifdef KITSUNE_POSTGRES`
- [x] **T16** — Update `CMakeLists.txt`: add `KITSUNE_POSTGRES` option + `pkg_check_modules(libpq)`
- [x] **T17** — Add `PostgresFactAttribute.cs`; add `KITSUNE_POSTGRES_TEST` to `kitsune.runsettings` (section 9)
- [x] **T18** — Write `PostgresTests.cs` (section 8): coroutine protocol, CRUD helpers, type mapping, cancel, error, IsBusy, double-Close
- [x] **T19** — Windows build regression: existing `.vcxproj` must compile without changes
- [x] **T20** — Linux build smoke test: `cmake -DKITSUNE_POSTGRES=ON .. && make`; all Postgres tests pass on both platforms

---

## Files Changed Summary

| File | Action |
|---|---|
| `postgres/include/libpq-fe.h` | Add async API declarations (Windows stub) |
| `LuaPostgres.h` | Rewrite |
| `LuaPostgres.cpp` | Rewrite — one `#ifdef` block at top (`WinSock2.h` + `set_fd_nonblocking`), rest unified |
| `PostgresMain.cpp` | Rewrite |
| `platform.h` | Add `KITSUNE_POSTGRES` under `KITSUNE_ALL` |
| `CMakeLists.txt` | Add `KITSUNE_POSTGRES` option block |
| `KitsuneEngine.cpp` | Replace `#ifdef _WIN32` gate with `#ifdef KITSUNE_POSTGRES` |
| `KitsuneNet.Tests/PostgresFactAttribute.cs` | New |
| `KitsuneNet.Tests/PostgresTests.cs` | New |
| `KitsuneNet.Tests/kitsune.runsettings` | Add `KITSUNE_POSTGRES_TEST` env var |
