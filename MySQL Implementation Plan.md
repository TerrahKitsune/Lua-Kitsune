# MySQL Module Rewrite — Implementation Plan

## Overview

Full rewrite of `LuaMySQL.h`, `LuaMySQL.cpp`, and `MySQLMain.cpp` to replace the
Win32-only thread-per-connection model with:

- **Both platforms**: MySQL 8.0 nonblocking API (`mysql_real_query_nonblocking`,
  `mysql_store_result_nonblocking`) driven by `lua_yieldk` continuations — zero
  background threads on Windows and Linux alike.

Windows links `mysql/libmysql.lib` (MySQL 8.0 client) via `#pragma comment`;
Linux discovers the library via `pkg_check_modules(mysqlclient>=8.0)`.

The result set is streamed row-by-row through a Lua coroutine so only one row
exists as a Lua table at a time, eliminating the previous double-copy of all rows.

---

## Final Lua API

```lua
-- Connect (yields on Linux while TCP+auth handshake runs)
local conn, err = MySQL.Connect(host, user, password, database [, port [, timeout]])
if not conn then error(err) end

-- Query returns a real Lua coroutine immediately (does NOT block or yield the caller)
local co, err = conn:Query("SELECT name, age FROM users WHERE id = ?", {id})
if not co then error(err) end

-- Phase 1: drive the async state machine
-- coroutine.resume → true, nil       while still waiting for the server
local ok, val = coroutine.resume(co)
while ok and val == nil and coroutine.status(co) == "suspended" do
    ok, val = coroutine.resume(co)
end

-- Phase 2: outcome
-- ok=false → coroutine errored (val = error message)
-- val=integer → affected/row count (query succeeded)
-- val=string  → query-level error message

if not ok then error(val) end
if type(val) == "string" then error(val) end
local rowcount = val  -- integer

-- Phase 3: stream rows one at a time
-- coroutine.resume → true, {col1, col2, ...}  one row per resume (integer-keyed)
-- coroutine.resume → true, nil                 no more rows (coroutine dies here)
ok, val = coroutine.resume(co)
while ok and val ~= nil do
    -- val[1], val[2], val[3] ...
    ok, val = coroutine.resume(co)
end

-- Stop early by passing a truthy stop flag
coroutine.resume(co, true)   -- frees C buffer, clears conn busy state, coroutine dies

-- Check whether a query coroutine is still alive on a connection
if conn:IsBusy() then ... end

-- Escape a string value for safe inline use
local safe = conn:EscapeValue(rawStr)

-- Close the connection (also safe to let GC collect it)
conn:Close()
```

### Coroutine yield protocol reference

| `coroutine.resume` returns | Meaning |
|---|---|
| `true, nil` + status `"suspended"` | Query/store still in progress |
| `true, <integer>` | Done — integer is affected/row count |
| `true, <string>` | Done — string is a query-level error |
| `true, {col1,col2,...}` | One data row (integer-keyed array) |
| `true, nil` + status `"dead"` | All rows consumed, C buffer freed |
| `false, <string>` | Coroutine raised an error |

### Stop flag

Every `coroutine.resume` call on a query coroutine accepts an optional boolean
first argument. When it is truthy the coroutine immediately frees the result
buffer, clears `conn->queryRef`, and returns `nil` so the coroutine dies cleanly.
This is checked at the **top of every continuation function** before any other
work is done, including during the in-progress polling phase.

```lua
coroutine.resume(co, true)   -- cancel at any point; safe to call even when already dead
```

---

## Helper Methods

Three convenience methods are added to the connection metatable. They wrap
`conn:Query()` internally and share a single `_driveQuery` closure so the
polling logic is written once.

All three accept an optional `cancelFn` argument — a zero-argument Lua function
that is called between each `nil`-yielding resume during the async wait phase and
between each row during the streaming phase. If it returns a truthy value the
helper stops the query coroutine early and returns `false, "cancelled"`.

Because these helpers are pure Lua wrappers over the coroutine protocol they are
embedded as a literal string inside `MySQLMain.cpp` and loaded once at module
initialisation with `luaL_loadbuffer` + `lua_call`. All shared state lives in the
`_driveQuery` upvalue captured by the three closures.

### `conn:NonQuery(sql [, params [, cancelFn]])` → `ok, rowcount | errmsg`

For INSERT / UPDATE / DELETE or any statement that returns no rows. Drives the
coroutine to completion, discards any rows that appear (should be none), and
returns the affected-row count as an integer.

```lua
local ok, affected = conn:NonQuery(
    "UPDATE users SET name = ? WHERE id = ?",
    {newName, userId}
)
if not ok then error(affected) end
print(affected .. " row(s) updated")
```

### `conn:Scalar(sql [, params [, cancelFn]])` → `ok, value | errmsg`

Returns the first column of the first row and immediately stops the coroutine
(stop flag is sent after the first row is received so the C buffer is freed
without iterating remaining rows).

```lua
local ok, name = conn:Scalar("SELECT name FROM users WHERE id = ?", {id})
if not ok then error(name) end
print(name)   -- nil if no rows matched
```

### `conn:QueryAll(sql [, params [, cancelFn]])` → `ok, rows | errmsg`

Collects every row into a Lua array of integer-keyed row arrays and returns it.
The `cancelFn` is checked after each row is appended so very large result sets
can be aborted mid-stream.

```lua
local ok, rows = conn:QueryAll("SELECT id, name FROM users", nil,
    function() return ShouldAbort() end)
if not ok then error(rows) end
for i = 1, #rows do
    print(rows[i][1], rows[i][2])
end
```

### C-side implementation

All three helpers are implemented as C functions in `LuaMySQL.cpp` using
`lua_yieldk` continuations, exactly as `MySqlConnect` and the query coroutine
body do. This means:

- The **outer** Kitsune-managed coroutine (the one calling `NonQuery` /
  `Scalar` / `QueryAll`) yields cooperatively between each poll of the inner
  query coroutine. Other Kitsune coroutines can run during the wait.
- The `cancelFn` is called inside the continuation before each poll. If it
  returns truthy the query coroutine receives the stop flag and the helper
  returns `false, "cancelled"`.
- Because the helpers use `lua_yieldk`, the outer coroutine's stack is stable
  across all continuation re-entries. `QueryAll` pushes an accumulator table
  onto the outer `L` stack before the first yield and reads back rows into it
  via `lua_xmove` + `lua_rawseti` on each continuation entry.

#### Continuation chain (all three helpers)

```
MySqlNonQuery / MySqlScalar / MySqlQueryAll
  │  Allocate and populate LuaMySQLQuery (same as MySqlQuery)
  │  Set q->helperMode, q->cancelFnRef
  │  For QueryAll: lua_newtable(L), record accumTableIdx = lua_gettop(L)
  │  Drive query coroutine first step (lua_resume on T)
  │    T yields nil  →  lua_yieldk(L, 0, ctx, Helper_WaitCont)
  │    T yields rowcount  →  fall through to mode-specific finish
  │    T yields string (error)  →  return false, errmsg
  ↓
Helper_WaitCont(L, status, ctx)          ← re-entered each outer resume
  │  Check cancelFn; if cancel → stop T, return false,"cancelled"
  │  lua_resume(T, L, 0, &nr)
  │    T yields nil  →  lua_yieldk(L, 0, ctx, Helper_WaitCont)  (loop)
  │    T yields rowcount  →  see below per mode
  │    T yields string  →  return false, errmsg
  │
  │  NonQuery mode (HELPER_NONQUERY):
  │    Send stop flag to T (drain any rows without building tables)
  │    return true, rowcount
  │
  │  Scalar mode (HELPER_SCALAR):
  │    lua_resume(T, L, 0, &nr)  ← fetch exactly one row
  │    If T yields row table: xmove col[1] to L, send stop flag to T
  │    If T yields nil: result = nil, no stop needed
  │    return true, result
  │
  │  QueryAll mode (HELPER_QUERYALL):
  │    lua_yieldk(L, 0, ctx, Helper_StreamCont)  ← start streaming phase
  ↓
Helper_StreamCont(L, status, ctx)        ← QueryAll only
  │  Check cancelFn; if cancel → stop T, return false,"cancelled"
  │  lua_resume(T, L, 0, &nr)
  │    T yields row table:
  │      lua_xmove(T, L, 1)  (move row from T to L)
  │      lua_rawseti(L, q->accumTableIdx, ++q->accumRowIdx)
  │      lua_yieldk(L, 0, ctx, Helper_StreamCont)  (loop)
  │    T yields nil (exhausted):
  │      push accumulator table (already on L at accumTableIdx)
  ↓  return true, rows_table
```

`lua_KContext ctx` stores the `LuaMySQLQuery*` pointer cast to `intptr_t`.
On all supported platforms `sizeof(lua_KContext) >= sizeof(void*)`.

---

## Data Structures

### `LuaMySQL` (connection object — simplified)

```c
typedef struct LuaMySQL {
    MYSQL*  connection;   // libmysql connection handle
    int     queryRef;     // Lua registry ref to active query coroutine; LUA_NOREF = idle
    void*   activeQuery;  // LuaMySQLQuery* of the running query, or NULL
    char*   error;        // last connection-level error (EscapeValue etc.)
} LuaMySQL;
```

`activeQuery` is set when any query starts (raw `Query`, `NonQuery`, `Scalar`,
or `QueryAll`) and cleared by `FreeQuery`. If the outer Kitsune coroutine is
interrupted mid-helper, `luamysql_gc` uses `activeQuery` to call `FreeQuery`
directly rather than waiting for GC, preventing `mysql_free_result` leaks.

All async state (query string, result buffer, thread handle) moves to `LuaMySQLQuery`.

### `LuaMySQLQuery` (query object — upvalue of the coroutine body)

```c
// helperMode values
#define MYSQL_HELPER_RAW      0  // raw conn:Query() — helpers not involved
#define MYSQL_HELPER_NONQUERY 1
#define MYSQL_HELPER_SCALAR   2
#define MYSQL_HELPER_QUERYALL 3

typedef struct LuaMySQLQuery {
    LuaMySQL*   conn;           // back-pointer, NOT owned; kept alive via connRef
    int         connRef;        // registry ref to conn userdata (prevents GC mid-query)
    char*       sql;            // built SQL string with params already substituted
    size_t      sqllen;
    MYSQL_RES*  result;         // populated after store_result completes
    char*       error;          // query-level error message, or NULL
    int         cancelFnRef;    // registry ref to cancelFn, LUA_NOREF if none
    int         helperMode;     // MYSQL_HELPER_* constant
    int         accumTableIdx;  // QueryAll: absolute L stack index of row array
    int         accumRowIdx;    // QueryAll: next 1-based row index to write

// no platform-specific fields — both Windows and Linux use the nonblocking API
} LuaMySQLQuery;
```

`LuaMySQLQuery` is allocated with `gff_malloc`, stored as a light userdata
upvalue on the query coroutine's C body function, and freed inside the coroutine
body when the final `nil` is yielded (exhausted) or when the stop flag is
received, and also by `luamysql_gc` via `conn->activeQuery` if the outer
coroutine is interrupted before the query completes.

---

## C Continuation Chain

### Linux — nonblocking path

```
MySqlQueryBody(L)           ← initial C function on the query coroutine thread
  │  mysql_real_connect_nonblocking (if CONNECT phase, see Connect section)
  │  mysql_real_query_nonblocking
  │    NET_ASYNC_NOT_READY  →  lua_yieldk(nil, QueryRunCont)
  │    NET_ASYNC_ERROR      →  cleanup + luaL_error
  ↓  NET_ASYNC_COMPLETE
QueryStoreCont(L, status, ctx)
  │  mysql_store_result_nonblocking
  │    NET_ASYNC_NOT_READY  →  lua_yieldk(nil, QueryStoreCont)
  │    NET_ASYNC_ERROR      →  cleanup + yield error string
  ↓  NET_ASYNC_COMPLETE
  │  yield integer rowcount → lua_yieldk(rowcount, QueryStreamCont)
  ↓
QueryStreamCont(L, status, ctx)   ← called for every subsequent resume
  │  check stop flag (lua_toboolean(L,1))
  │    true  →  mysql_free_result, clear conn->queryRef, free query, return nil
  │  mysql_fetch_row (synchronous — data is already in C heap)
  │    row   →  build integer-keyed Lua table, lua_yieldk(row, QueryStreamCont)
  ↓  NULL (exhausted)
     mysql_free_result, clear conn->queryRef, free query, return nil (coroutine dies)
```

Each continuation checks the stop flag from `lua_toboolean(L, 1)` before doing
any other work.

### Windows — nonblocking path

Identical to the Linux path above. Windows uses the same
`mysql_real_query_nonblocking` / `mysql_store_result_nonblocking` API
available in the MySQL 8.0 client (`libmysql.lib`). No background threads.

---

## Files to Modify

### 1. `LuaMySQL.h`

- **Remove** `#include <Windows.h>` (use `"platform.h"` instead via the
  `.cpp` file)
- **Remove** fields: `busy`, `alive`, `result`, `currentRow`,
  `currentRowLengths`, `query`, `querylen`, `paramValues`, `paramLengths`,
  `nParams`, `isParamQuery`, `thread`, `interrupt`
- **Add** field: `int queryRef`
- **Remove** declarations: `MySqlFetch`, `MySqlGetRow`, `MySqlFinish`
- **Keep** declarations: `MySqlConnect`, `MySqlQuery`, `MySqlIsBusy`,
  `MySqlEscapeValue`, `luamysql_gc`, `luamysql_tostring`,
  `lua_tomysql`, `lua_pushmysql`
- **Add** `#include <inttypes.h>` for `PRIx64` used in `luamysql_tostring`

### 2. `LuaMySQL.cpp`

Complete rewrite. Sections:

#### 2a. Includes and `LuaMySQLQuery` definition

```cpp
#include "LuaMySQL.h"
#include "stream.h"
#include "luawchar.h"
#ifdef _WIN32
#pragma comment(lib, "mysql/libmysql.lib")
#endif
```

Define `LuaMySQLQuery` struct (as above). Define the
`static const char* LUAMYSQLQUERY = "LuaMySQLQuery"` metatable name.

#### 2b. `PushMySQLValue` (unchanged from original)

Converts a MySQL field value + type to a Lua value on the stack.
BLOB fields push via `lua_pushluastream`. Use `uint8_t*` instead of `BYTE*`.

#### 2c. Parameter substitution helper

`BuildQueryWithParams(LuaMySQLQuery* q, const char* sql, size_t sqllen, lua_State* L, int paramTableIdx)`
— same logic as the original `QueryThread` parameter loop, returns a
`gff_malloc`'d `q->sql`.

#### 2d. `lua_tomysql` / `lua_pushmysql`

`lua_pushmysql`: `memset` the struct, `queryRef = LUA_NOREF`, no Win32 handles.

#### 2e. `MySqlEscapeValue` (unchanged)

#### 2f. `MySqlIsBusy`

```cpp
int MySqlIsBusy(lua_State* L) {
    LuaMySQL* m = lua_tomysql(L, 1);
    if (m->queryRef == LUA_NOREF) {
        lua_pushboolean(L, false);
        return 1;
    }
    lua_rawgeti(L, LUA_REGISTRYINDEX, m->queryRef);
    lua_State* T = lua_tothread(L, -1);
    lua_pop(L, 1);
    lua_pushboolean(L, T != NULL && lua_status(T) == LUA_YIELD);
    return 1;
}
```

#### 2g. `MySqlConnect`

- **Both platforms**: `mysql_real_connect_nonblocking` loop via `lua_yieldk`
  - First call before yield; on each continuation re-read args from the Lua
    stack (still valid — original args pinned by the coroutine's own stack)
  - `NET_ASYNC_ERROR` → `mysql_close`, `luaL_error`
  - `NET_ASYNC_COMPLETE` → return userdata (top of stack)

#### 2h. `MySqlQuery`

```
1. lua_tomysql(L, 1) — validate conn
2. Check m->queryRef != LUA_NOREF → luaL_error("connection already has an active query")
3. Read SQL string from arg 2, params table from arg 3
4. Allocate LuaMySQLQuery* q via gff_malloc, memset to zero
5. q->connRef = luaL_ref(L, ...) anchoring the connection userdata
6. BuildQueryWithParams(q, sql, sqllen, L, 3) → q->sql / q->sqllen
7. lua_newthread(L) → T  (the query coroutine)
8. Push MySqlQueryBody as a C closure with q as light-userdata upvalue onto T
9. m->queryRef = luaL_ref(L, LUA_REGISTRYINDEX) anchoring T
10. Return T (the coroutine) to Lua — do NOT add Kitsune scheduler hook
```

#### 2i. Linux continuation functions

`MySqlQueryBody`, `QueryRunCont`, `QueryStoreCont`, `QueryStreamCont`
as described in the continuation chain above.

Each function:
1. Gets `LuaMySQLQuery* q = lua_touserdata(L, lua_upvalueindex(1))`
2. Checks `lua_toboolean(L, 1)` for stop flag
3. If stop: call `FreeQuery(q)`, clear `conn->queryRef`, return `0`
4. Advances state machine step, yields `nil` or the appropriate value

`FreeQuery(LuaMySQLQuery* q)`:
- `mysql_free_result` if `q->result`
- `gff_free(q->sql)`, `gff_free(q->error)`
- `luaL_unref(L, LUA_REGISTRYINDEX, q->connRef)`
- clear `q->conn->queryRef = LUA_NOREF`
- `gff_free(q)`

#### 2j. Windows continuation functions

Identical to section 2i (Linux). Windows uses the same `mysql_real_query_nonblocking`
and `mysql_store_result_nonblocking` path. The `#ifdef _WIN32` / `#else` / `#endif`
blocks are removed from the source; a single cross-platform implementation remains.

#### 2k. `luamysql_gc`

```cpp
int luamysql_gc(lua_State* L) {
    LuaMySQL* m = lua_tomysql(L, 1);
    if (!m) return 0;
    // activeQuery is non-NULL when a query was running and the outer coroutine
    // was interrupted before FreeQuery was called normally. Free it directly to
    // ensure mysql_free_result is not skipped on abnormal teardown.
    if (m->activeQuery) {
        FreeQuery(L, (LuaMySQLQuery*)m->activeQuery);
        // FreeQuery clears m->activeQuery and m->queryRef
    }
    if (m->queryRef != LUA_NOREF) {
        luaL_unref(L, LUA_REGISTRYINDEX, m->queryRef);
        m->queryRef = LUA_NOREF;
    }
    if (m->connection) {
        mysql_close(m->connection);
        m->connection = NULL;
    }
    if (m->error) {
        gff_free(m->error);
        m->error = NULL;
    }
    return 0;
}
```

#### 2l. `luamysql_tostring`

Replace `DWORD64` with `uint64_t` and use `PRIx64`:
```cpp
snprintf(buf, sizeof(buf), "MySQL: 0x%016" PRIx64, (uint64_t)(uintptr_t)m);
```

#### 2m. C helper functions (`MySqlNonQuery`, `MySqlScalar`, `MySqlQueryAll`)

All three follow the same pattern: allocate a `LuaMySQLQuery`, start the query
coroutine (internal call to the same logic as `MySqlQuery`), set
`q->helperMode`, optionally anchor `cancelFn` as `q->cancelFnRef`, drive the
query coroutine one step, then yield the **outer** `L` with `lua_yieldk` if
not yet complete.

**`FreeQuery(lua_State* L, LuaMySQLQuery* q)`**

Always clears `q->conn->activeQuery = NULL` and `q->conn->queryRef = LUA_NOREF`
before freeing any fields, so `luamysql_gc` never double-frees:
- `mysql_free_result(q->result)` if set
- `luaL_unref` for `q->connRef` and `q->cancelFnRef`
- `gff_free(q->sql)`, `gff_free(q->error)`
- `gff_free(q)`

**`Helper_WaitCont(L, status, ctx)` — shared wait-phase continuation**

Used by all three helpers. `ctx` = `(lua_KContext)q`. Steps:
1. If `q->cancelFnRef != LUA_NOREF`: call the cancel function; if it returns
   truthy, send stop flag to T, call `FreeQuery`, return `false, "cancelled"`
2. `lua_resume(T, L, 0, &nr)` on the query coroutine
3. Inspect T's first yielded value:
   - `nil` → still waiting: `lua_yieldk(L, 0, ctx, Helper_WaitCont)` (loop)
   - `string` → query error: `FreeQuery`, return `false, errmsg`
   - `integer` (rowcount) → Phase 2 complete; branch per mode:
     - `HELPER_NONQUERY`: send stop flag to T, `FreeQuery`, return `true, rowcount`
     - `HELPER_SCALAR`: get one row (`lua_resume(T, L, 0, &nr)`), send stop
       flag, `FreeQuery`, return `true, row[1]` (or `true, nil` if no rows)
     - `HELPER_QUERYALL`: record `q->accumTableIdx = lua_gettop(L)` after
       pushing the accum table; `lua_yieldk(L, 0, ctx, Helper_StreamCont)`
4. On coroutine error (`rc != LUA_YIELD && rc != LUA_OK`):
   `FreeQuery`, return `false, errmsg`

**`Helper_StreamCont(L, status, ctx)` — `QueryAll` streaming continuation**

1. Check `cancelFn`; cancel if requested
2. `lua_resume(T, L, 0, &nr)`
3. T yields row table:
   - `lua_xmove(T, L, 1)` (move row table from T to L)
   - `lua_rawseti(L, q->accumTableIdx, ++q->accumRowIdx)` (append to accum)
   - `lua_yieldk(L, 0, ctx, Helper_StreamCont)` (loop)
4. T yields `nil` (exhausted): `FreeQuery`; push the accum table
   (`lua_pushvalue(L, q->accumTableIdx)`), return `true, rows`
5. T error: `FreeQuery`, return `false, errmsg`

**Why `lua_yieldk` is required here (not just a tight C loop):**

When a helper C function calls `lua_resume(T, L, 0, &nr)` in a tight loop, the
Kitsune scheduler ticker cannot fire (we are in C, not executing Lua
instructions). Other Kitsune coroutines would be starved for the entire duration
of the query. By using `lua_yieldk` between each poll, the outer coroutine
gives up control after every single query coroutine step, allowing the scheduler
to run other coroutines in between.

### 3. `MySQLMain.cpp`

Register two metatables:
- `LuaMySQL` (connection): `Connect`, `IsBusy`, `EscapeValue`, `Query`, `Close`
- `__index = self` so methods work as `conn:Query(...)`

Remove from the function table:
- `IsBusy` (as a module-level function; keep as connection method)
- `Fetch`, `GetRow`, `Finish`

The query coroutine is a plain `lua_State*` (LUA_TTHREAD) — no separate
metatable needed.

```cpp
static const luaL_Reg connfunctions[] = {
    { "Connect",      MySqlConnect      },
    { "IsBusy",       MySqlIsBusy       },
    { "EscapeValue",  MySqlEscapeValue  },
    { "Query",        MySqlQuery        },
    { "Close",        luamysql_gc       },
    { NULL, NULL }
};

static const luaL_Reg connmeta[] = {
    { "__gc",         luamysql_gc       },
    { "__tostring",   luamysql_tostring },
    { NULL, NULL }
};
```

#### Helper C function registrations

`NonQuery`, `Scalar`, and `QueryAll` are C functions defined in `LuaMySQL.cpp`
(see section 2m) and registered in the same connection function table as `Query`:

```cpp
static const luaL_Reg connfunctions[] = {
    { "Connect",    MySqlConnect    },
    { "IsBusy",     MySqlIsBusy     },
    { "EscapeValue",MySqlEscapeValue},
    { "Query",      MySqlQuery      },
    { "NonQuery",   MySqlNonQuery   },
    { "Scalar",     MySqlScalar     },
    { "QueryAll",   MySqlQueryAll   },
    { "Close",      luamysql_gc     },
    { NULL, NULL }
};
```

No embedded Lua string is used. All logic is in C.

### 4. `CMakeLists.txt`

Add a new opt-in build option for Linux:

```cmake
option(KITSUNE_MYSQL "Build with MySQL support (requires libmysqlclient >= 8.0)" OFF)

if(KITSUNE_MYSQL)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MYSQLCLIENT REQUIRED mysqlclient>=8.0)

    target_sources(KitsuneEngine PRIVATE LuaMySQL.cpp MySQLMain.cpp)

    target_compile_definitions(KitsuneEngine PRIVATE KITSUNE_MYSQL)
    target_include_directories(KitsuneEngine PRIVATE ${MYSQLCLIENT_INCLUDE_DIRS})
    target_link_libraries(KitsuneEngine PRIVATE ${MYSQLCLIENT_LINK_LIBRARIES})
endif()
```

`KITSUNE_MYSQL` is the single define used everywhere: on Windows it is pulled in
via `platform.h` (`#ifdef KITSUNE_ALL → #define KITSUNE_MYSQL`); on Linux CMake
passes it directly. `KitsuneEngine.cpp` gates the registration with:

```cpp
#ifdef KITSUNE_MYSQL
    luaopen_mysql(L);  lua_setglobal(L, "MySQL");
#endif
```

### 5. `KitsuneNet.Tests/MySqlTests.cs`

All MySQL tests are C# xUnit tests in the existing `KitsuneNet.Tests` project,
following the same patterns used by `KitsuneEngineTests.cs` and
`KitsuneUtilTests.cs`. MySQL tests require a live server, so they are gated
behind a `MySqlFactAttribute` (modelled on `WindowsOnlyFactAttribute`) that
skips unless `KITSUNE_MYSQL_TEST` is set in the environment.

#### `MySqlFactAttribute.cs`

```csharp
public sealed class MySqlFactAttribute : FactAttribute
{
    public MySqlFactAttribute()
    {
        if (string.IsNullOrEmpty(
                Environment.GetEnvironmentVariable("KITSUNE_MYSQL_TEST")))
            Skip = "Set KITSUNE_MYSQL_TEST=host:port:user:pass:db to run MySQL tests";
    }
}
```

#### `MySqlTests.cs` structure

```csharp
[Collection("KitsuneSequential")]
public sealed class MySqlTests
{
    // Parses KITSUNE_MYSQL_TEST=host:port:user:pass:db
    private static string ConnectLua()
    {
        var parts = Environment.GetEnvironmentVariable("KITSUNE_MYSQL_TEST")!.Split(':');
        return $"MySQL.Connect('{parts[0]}','{parts[2]}','{parts[3]}','{parts[4]}',{parts[1]})";
    }

    private async Task<string?> Run(string lua)
    {
        using KitsuneEngine engine = new();
        return await engine.ExecuteStringAsync(lua);
    }

    // ── coroutine protocol ───────────────────────────────────────────────────

    // test: Connect with valid credentials returns connection userdata
    // test: SELECT via raw coroutine — Phase 1 yields nil, Phase 2 yields
    //       integer rowcount, Phase 3 yields integer-keyed row arrays then nil
    // test: INSERT/UPDATE returns integer rowcount, row stream is empty
    // test: query error — Phase 2 yields string; type(val)=="string" check holds
    // test: stop flag mid-wait — conn:IsBusy()==false after resume(co,true)
    // test: stop flag mid-stream — conn:IsBusy()==false after resume(co,true)
    // test: conn:IsBusy() is true while coroutine is suspended, false once dead
    // test: concurrent queries on two separate connections from same Lua script
    // test: conn:Query() while busy returns nil + error string
    // test: GC of conn with active query coroutine does not crash or leak

    // ── helper methods ───────────────────────────────────────────────────────

    // test: NonQuery returns correct affected-row count as integer
    // test: Scalar returns first column of first row; nil when no rows matched
    // test: QueryAll returns complete Lua array of row arrays
    // test: cancelFn that always returns false — query runs to full completion
    // test: cancelFn returning true mid-wait cancels; result is false+"cancelled"
    // test: cancelFn returning true mid-stream cancels; result is false+"cancelled"
    // test: NonQuery / Scalar / QueryAll surface query-level errors correctly
    // test: EscapeValue escapes single quotes and backslashes
    // test: Close is idempotent (double-close does not crash)
}
```

Connection details are read from `KITSUNE_MYSQL_TEST` so no credentials are
hardcoded. The CI pipeline sets this variable when a MySQL service container is
available; all tests are skipped on machines where it is absent.

---

## Task Checklist

- [x] **T1** — Rewrite `LuaMySQL.h`: new `LuaMySQL` struct, remove old method declarations, add `queryRef`
- [x] **T2** — Define `LuaMySQLQuery` struct in `LuaMySQL.cpp` (Windows + Linux fields)
- [x] **T3** — Rewrite `lua_pushmysql` (simplified init, `queryRef = LUA_NOREF`)
- [x] **T4** — Rewrite `MySqlConnect`: Linux nonblocking `lua_yieldk` path; Windows blocking path unchanged
- [x] **T5** — Write `BuildQueryWithParams` helper (extracted from old `QueryThread`)
- [x] **T6** — Write `MySqlQuery`: allocates `LuaMySQLQuery`, creates `lua_newthread`, stores `queryRef`, returns coroutine
- [x] **T7** — Write Linux continuation chain: `MySqlQueryBody`, `QueryRunCont`, `QueryStoreCont`, `QueryStreamCont`
- [x] **T8** — ~~Write Windows thread chain~~ Removed — Windows now uses the identical MySQL 8.0 nonblocking path as Linux; no background threads
- [x] **T9** — Write `FreeQuery` cleanup helper
- [x] **T10** — Rewrite `MySqlIsBusy` (lua_status check)
- [x] **T11** — Rewrite `luamysql_gc` (no thread join, unref queryRef)
- [x] **T12** — Fix `luamysql_tostring` (`uint64_t` / `PRIx64`)
- [x] **T13** — Fix `PushMySQLValue` BLOB case (`uint8_t*` instead of `BYTE*`)
- [x] **T14** — Rewrite `MySQLMain.cpp`: remove `Fetch`/`GetRow`/`Finish` entries, add connection method `__index`
- [x] **T15** — Update `CMakeLists.txt`: add `KITSUNE_MYSQL` option, pkg-config for mysqlclient >= 8.0
- [x] **T16** — Update `KitsuneEngine.cpp`: gate `luaopen_mysql` with `#ifdef KITSUNE_MYSQL`. On Windows the define is emitted by `platform.h` when `KITSUNE_ALL` is set; on Linux CMake passes `-DKITSUNE_MYSQL` directly. No `KITSUNE_BAREBONES` removal needed.
- [x] **T17** — Add `MySqlFactAttribute.cs` to `KitsuneNet.Tests`; add `KITSUNE_MYSQL_TEST` env-var parsing helper
- [x] **T17b** — Write coroutine-protocol tests in `MySqlTests.cs` (connect, raw SELECT/INSERT, error, stop-flag, IsBusy, concurrent, busy-guard, GC)
- [x] **T18** — Linux build smoke test: `cmake -DKITSUNE_MYSQL=ON .. && make`. All 32 MySQL tests pass on both Windows and Linux (WSL Ubuntu 24.04).
- [x] **T19** — Windows build regression: existing `.vcxproj` must compile without changes
- [x] **T20** — Implement `MySqlNonQuery`, `MySqlScalar`, `MySqlQueryAll` as C functions in `LuaMySQL.cpp` with `lua_yieldk` continuations (`Helper_WaitCont`, `Helper_StreamCont`); add `cancelFnRef`, `helperMode`, `accumTableIdx`, `accumRowIdx` fields to `LuaMySQLQuery`; add `activeQuery` to `LuaMySQL`; update `FreeQuery` to clear `activeQuery`; register all three in `MySQLMain.cpp` function table
- [x] **T21** — Write helper-method and `cancelFn` tests in `MySqlTests.cs` (`NonQuery`, `Scalar`, `QueryAll`, cancel mid-wait, cancel mid-stream, error propagation, `EscapeValue`, double-`Close`)

---

## Files Changed Summary

| File | Action |
|---|---|
| `LuaMySQL.h` | Rewrite |
| `LuaMySQL.cpp` | Rewrite |
| `MySQLMain.cpp` | Rewrite |
| `CMakeLists.txt` | Add `KITSUNE_MYSQL` option block |
| `KitsuneEngine.cpp` | `#ifdef KITSUNE_MYSQL` guard around `luaopen_mysql` |
| `KitsuneNet.Tests/MySqlFactAttribute.cs` | New — skips tests when `KITSUNE_MYSQL_TEST` not set |
| `KitsuneNet.Tests/MySqlTests.cs` | New — xUnit tests for MySQL coroutine protocol and helpers |
| `KitsuneNet.Tests/kitsune.runsettings` | New — provides `KITSUNE_MYSQL_TEST` env var for local VS test runs |
| `KitsuneEngine.vcxproj` | `<LinkIncremental>false</LinkIncremental>` added to Debug `<Link>` — MSVC incremental linker was silently preserving a stale `.rdata` layout for `connfunctions[]` after entries were added, causing new methods to resolve via ILT stubs that bypassed the updated array |
| `.gitignore` | `build-*/` pattern added; all three build directories (`build-linux/`, `build-barebones/`, `build-asan/`) fully untracked — binaries are rebuilt locally, not committed |

---

## Install on Ubuntu / WSL2

```sh
sudo apt-get install libmysqlclient-dev   # provides MySQL 8.0 headers + libmysqlclient.so
cmake -DKITSUNE_MYSQL=ON -B build-mysql .
cmake --build build-mysql
```
