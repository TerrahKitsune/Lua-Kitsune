# SQLiteKitsune Extension Functions

> **Windows only.** `SQLiteKitsune.dll` is a Windows DLL. It uses `DllMain`, delay-loads `KitsuneEngine.dll`, and has no Linux build at this time.

The SQLiteKitsune extension bridges SQLite and the Kitsune Lua engine in both directions.  Load it with `load_extension` from SQL, or from Lua using the `SQLite` module:

```lua
local db = SQLite.Open()
db:Query("SELECT load_extension('/path/to/SQLiteKitsune')")
db:Fetch()
```

Once loaded the extension registers:

- **SQLite scalar/aggregate functions** callable directly from SQL.
- **Lua globals** under the `SQLiteExt` namespace callable from Lua code running inside the same engine session.

---

## Auto-execution of `extension.lua`

When the extension is loaded against a file-backed database (not `:memory:`), it automatically looks for a file named `extension.lua` in the same directory as the database file and executes it.  This is the standard place to define custom functions and virtual tables that should always be present for that database.

```
/my/app/
  mydb.sqlite       ← database
  extension.lua     ← executed automatically on load_extension
```

If `extension.lua` raises a Lua error, `load_extension` fails and propagates the error message.  If the file does not exist it is silently ignored.

---

## SQLite Functions

These functions are registered directly in SQLite and can be called from any SQL statement after `load_extension`.

---

### KitsuneVersion

Returns the version string of the loaded SQLiteKitsune extension.

```sql
string KitsuneVersion()
```

**Example:**

```sql
SELECT KitsuneVersion();  -- e.g. "1.0.0.0"
```

---

### LuaString

Compiles and executes a Lua script string.  Returns the first value returned by the script.

```sql
any LuaString(script, arg1, arg2, ...)
```

**Parameters:**

- `script`: Lua source code to execute.
- `arg1, arg2, ...`: Additional arguments made available inside the script as the global `ARGS` table (`ARGS[1]`, `ARGS[2]`, …).

**Notes:**

- `ARGS` is set for the duration of this call and cleared afterwards.
- Multiple return values are not supported; only the first is returned to SQLite.
- A Lua error propagates as a SQLite error.

**Example:**

```sql
SELECT LuaString('return ARGS[1] .. ARGS[2]', 'hello', 'world');
-- returns "helloworld"

SELECT LuaString('return string.upper(ARGS[1])', 'kitsune');
-- returns "KITSUNE"
```

---

### LuaFile

Loads and executes a Lua file.  Returns the first value returned by the file.

```sql
any LuaFile(path, arg1, arg2, ...)
```

**Parameters:**

- `path`: Absolute or relative path to the Lua file to execute.
- `arg1, arg2, ...`: Additional arguments.

**ARGS layout inside the file:**

| Index | Value |
|---|---|
| `ARGS[1]` | The file path (set automatically by the engine). |
| `ARGS[2]` | First extra argument. |
| `ARGS[3]` | Second extra argument. |
| … | … |

**Notes:**

- A Lua error or a missing file propagates as a SQLite error.

**Example:**

```lua
-- /tmp/concat.lua
return ARGS[2] .. ARGS[3]
```

```sql
SELECT LuaFile('/tmp/concat.lua', 'foo', 'bar');
-- returns "foobar"
```

---

### LuaFunction

Calls a named Lua global function by name.  Returns the first value returned by the function.

```sql
any LuaFunction(name, arg1, arg2, ...)
```

**Parameters:**

- `name`: Name of a global Lua function (e.g. `"string.format"`, `"myFunc"`).
- `arg1, arg2, ...`: Arguments passed directly as function parameters.

**Notes:**

- Unlike `LuaString` / `LuaFile`, arguments are passed as direct function parameters, **not** via the `ARGS` global.
- The function must already exist as a Lua global when `LuaFunction` is called.

**Example:**

```lua
-- define in Lua
function greet(name) return "Hello, " .. name end
```

```sql
SELECT LuaFunction('greet', 'world');
-- returns "Hello, world"
```

---

### User-Registered Scalar Functions

Functions registered with `SQLiteExt.RegisterFunction` (see below) become callable from SQL under the name given at registration time.

```sql
SELECT MyFunc(arg1, arg2, ...);
```

---

### User-Registered Aggregate Functions

Functions registered with `SQLiteExt.RegisterAggregate` become callable as SQL aggregate functions.

```sql
SELECT MyAgg(column) FROM table;
```

---

## Lua Functions (`SQLiteExt.*`)

These functions are available as Lua globals once `load_extension` has been called.  They operate on the same SQLite connection that loaded the extension.

---

### SQLiteExt.Query

Executes a SQL query and returns all rows as a Lua table.

```lua
table SQLiteExt.Query(sql [, params])
```

**Parameters:**

- `sql`: SQL query string.  Use `@name` placeholders for parameters.
- `params` *(optional)*: Table of parameter bindings.  Keys correspond to the placeholder names without the `@` prefix.

**Returns:**

A 1-based array table where each entry is a key-value table of column names to values:

```lua
{
    [1] = { col1 = val, col2 = val, ... },
    [2] = { col1 = val, col2 = val, ... },
    ...
}
```

Returns an empty table when no rows are found.

**Examples:**

```lua
-- Simple query
local rows = SQLiteExt.Query("SELECT id, name FROM users ORDER BY id")
print(rows[1]["name"])

-- Parameterised query
local rows = SQLiteExt.Query(
    "SELECT name FROM users WHERE id = @id",
    { id = 42 }
)
print(rows[1]["name"])
```

---

### SQLiteExt.Scalar

Executes a SQL query and returns the first column of the first row as a single value.

```lua
any SQLiteExt.Scalar(sql [, params])
```

**Parameters:**

- `sql`: SQL query string.  Supports `@name` placeholders.
- `params` *(optional)*: Table of parameter bindings (same convention as `Query`).

**Returns:**

A single scalar value, or `nil` if the query produces no rows.

**Examples:**

```lua
local count = SQLiteExt.Scalar("SELECT COUNT(*) FROM users")

local name = SQLiteExt.Scalar(
    "SELECT name FROM users WHERE id = @id",
    { id = 1 }
)
```

---

### SQLiteExt.RegisterFunction

Registers a Lua function as a SQL scalar function on the current connection.

```lua
SQLiteExt.RegisterFunction(name, fn)
```

**Parameters:**

- `name`: The SQL function name to register.
- `fn`: Lua function to call.  Its signature matches the SQL arguments directly:

```lua
function(arg1, arg2, ...)
```

- Arguments are the values passed from SQL.
- The return value becomes the SQL result.
- The function reference is kept alive for the lifetime of the extension (until the process exits or the extension DLL is unloaded).

**Notes:**

- Each call is independent; no state is shared between invocations.
- Calling `RegisterFunction` again with the same name replaces the previous registration.

**Example:**

```lua
SQLiteExt.RegisterFunction("DoubleIt", function(n) return n * 2 end)
-- later in SQL:
-- SELECT DoubleIt(21);  → 42
```

---

### SQLiteExt.RegisterAggregate

Registers a Lua function as a SQL aggregate function on the current connection.

```lua
SQLiteExt.RegisterAggregate(name, fn)
```

**Parameters:**

- `name`: The SQL aggregate function name to register.
- `fn`: Lua function called once per row and once more to retrieve the final result:

```lua
function(ctx, isFinished, arg1, arg2, ...)
```

| Parameter | Per-row call | Final call |
|---|---|---|
| `ctx` | Fresh Lua table, same instance for all rows in this group | Same table as per-row calls (`nil` if no rows were processed) |
| `isFinished` | `false` | `true` |
| `arg1, arg2, …` | Column values for this row | Not present |
| Return value | Ignored | Returned to SQL as the aggregate result |

**Notes:**

- `ctx` is a fresh empty Lua table created at the start of each aggregation group and freed after the final call.  Use it to accumulate state instead of upvalues or globals — this makes concurrent aggregations (e.g. `GROUP BY` with multiple groups) independent.
- The function reference is kept alive for the lifetime of the extension.
- Calling `RegisterAggregate` again with the same name replaces the previous registration.

**Example:**

```lua
SQLiteExt.RegisterAggregate("LuaSum", function(ctx, isFinished, val)
    if isFinished then
        return ctx.total or 0
    end
    ctx.total = (ctx.total or 0) + (val or 0)
end)

-- SQL:
-- SELECT LuaSum(amount) FROM orders;
-- SELECT category, LuaSum(amount) FROM orders GROUP BY category;
```

---

### SQLiteExt.RegisterTable

Registers a Lua table as a readable and writable SQLite virtual table.  SQLite can `SELECT`, `INSERT`, `UPDATE`, and `DELETE` from this table; all changes are reflected immediately in the backing Lua table.

```lua
SQLiteExt.RegisterTable(name, fields, table)
```

**Parameters:**

- `name`: Name of the virtual table as it will appear in SQL.
- `fields`: Array of field name strings.  The **first field is the primary key** and becomes the Lua table key.
  - Example: `{"Id", "Value", "Data"}`
  - Must contain at least 2 entries; maximum 64.
  - Names must start with a letter or underscore and contain only letters, digits, or underscores (max 63 characters).
- `table`: The Lua table that backs the virtual table.

**Data Storage Format:**

| Number of fields | Lua table layout |
|---|---|
| 2 (PK + one value) | `table[pk] = scalar_value` |
| 3+ | `table[pk] = { field2_val, field3_val, ... }` (1-based array of non-PK values) |

If any non-PK value is itself a Lua table, it is serialized to JSON when read from SQL.

**Supported Operations:**

| SQL | Behaviour |
|---|---|
| `SELECT` | Full table scan or fast single-row PK equality lookup. |
| `INSERT` | Adds a new entry.  A duplicate PK raises a constraint error containing "Duplicate key". |
| `UPDATE` | Updates an existing entry.  Renaming the PK moves the entry to the new key and removes the old one. |
| `DELETE` | Removes the entry; a no-op if the key does not exist. |

**Notes:**

- Calling `RegisterTable` again with the same name replaces the previous registration.
- The Lua table is anchored by the extension; it will not be garbage-collected until the extension unloads.

**Examples:**

```lua
-- 2-field table
local scores = {}
scores["alice"] = 100
scores["bob"]   = 200
SQLiteExt.RegisterTable("Scores", {"Player", "Points"}, scores)

-- SQL:
-- SELECT Player, Points FROM Scores ORDER BY Points DESC;
-- INSERT INTO Scores VALUES('carol', 150);  -- scores["carol"] = 150

-- 3-field table
local people = {}
people[1] = {"John", "Doe"}
people[2] = {"Jane", "Smith"}
SQLiteExt.RegisterTable("People", {"Id", "FirstName", "LastName"}, people)

-- SQL:
-- SELECT FirstName, LastName FROM People WHERE Id = 1;
-- UPDATE People SET LastName = 'Jones' WHERE Id = 2;  -- people[2] = {"Jane","Jones"}
-- DELETE FROM People WHERE Id = 1;                   -- people[1] = nil
```

---

### SQLiteExt.RegisterVirtualTable

Registers a function-driven read-only or read-write SQLite virtual table.  Unlike `RegisterTable`, the data source does not need to be a Lua table — any data source can be exposed by implementing a reader function.

```lua
SQLiteExt.RegisterVirtualTable(name, fields, reader [, indexfn] [, updater])
```

**Parameters:**

- `name`: Name of the virtual table as it will appear in SQL.
- `fields`: Array of field name strings (same constraints as `RegisterTable`; first field is the primary key).
- `reader`: Function that produces rows one at a time (see below).
- `indexfn` *(optional)*: Function consulted by SQLite's query planner to determine if an index can satisfy a constraint (see below).  If omitted, every query does a full scan.
- `updater` *(optional)*: Function called for `INSERT`, `UPDATE`, and `DELETE`.  If omitted the table is read-only; write attempts return a "Readonly" error.

---

#### Reader Function

```lua
function reader(ctx, nth [, index])
```

| Parameter | Description |
|---|---|
| `ctx` | A shared Lua table, same instance for all cursors on this virtual table.  Use it to store cross-cursor state. |
| `nth` | 1-based row counter for the current scan.  Starts at `1` on every new scan (including re-scans in a join). |
| `index` | `nil` for a full scan.  For an index scan, an array table of active constraints (see Index Scans below). |

**Return value:**

- Return a 1-based array table of field values for the current row, in the same order as `fields`:
  ```lua
  return { pk_value, field2_value, field3_value, ... }
  ```
- Return `nil` (or nothing) to signal the end of data.

**Notes:**

- The reader is called once per row, then once more expecting `nil` to signal EOF.
- `ctx` persists across re-scans (e.g. a nested-loop join rescans the inner table multiple times); `nth` resets to `1` on each re-scan.

**Example (static data source):**

```lua
local data = {
    {1, "apple",  0.99},
    {2, "banana", 0.49},
    {3, "cherry", 1.29},
}
SQLiteExt.RegisterVirtualTable("Fruit", {"Id", "Name", "Price"},
    function(ctx, nth)
        return data[nth]   -- nil when nth > #data
    end
)
-- SELECT Name, Price FROM Fruit WHERE Id > 1;
```

---

#### Index Function (optional)

When provided, SQLite calls this function during query planning for each candidate `WHERE` clause constraint to determine whether the reader can satisfy it directly (avoiding a full scan).

```lua
function indexfn(ctx, op, colName)
```

| Parameter | Description |
|---|---|
| `ctx` | Shared vtable context table (same as passed to the reader). |
| `op` | Constraint operator string: `"="`, `">"`, `"<"`, `">="`, `"<="`. |
| `colName` | Name of the column being constrained (e.g. `"Id"`). |

**Return value convention:**

| Return value | Meaning |
|---|---|
| `true` | Accept constraint; **unique** result (SQLite skips post-filtering). |
| Positive integer or float | Accept constraint; estimated row count / cost (lower is better). |
| `nil`, `false`, `0`, or negative | Decline constraint; SQLite uses a full scan for this constraint. |

When SQLite chooses an index scan, the accepted constraints are bundled into the `index` argument passed to the reader (see Index Scans below).

**Example:**

```lua
local lookupById = { [1]="alice", [2]="bob", [3]="carol" }
SQLiteExt.RegisterVirtualTable("Users", {"Id", "Name"},
    function(ctx, nth, index)
        if index then
            -- fast path: only return the one row matching Id
            if nth > 1 then return nil end
            local id = index[1].Value
            return { id, lookupById[id] }
        end
        -- full scan
        local keys = {1, 2, 3}
        if not keys[nth] then return nil end
        return { keys[nth], lookupById[keys[nth]] }
    end,
    function(ctx, op, col)
        if col == "Id" and op == "=" then return true end  -- unique lookup
        return nil
    end
)
```

---

#### Index Scan Constraint Table

When the index function accepts one or more constraints, they are passed to the reader as the `index` argument — a 1-based array table.  Each entry describes one active constraint:

```lua
index[1] = {
    Column = "Id",       -- column name (string)
    Op     = "=",        -- operator (string)
    Value  = 42,         -- bound value from SQL (number, string, etc.)
}
index[2] = { ... }      -- if multiple constraints were accepted
```

The reader is responsible for filtering its data based on these constraints; SQLite may additionally post-filter rows that do not satisfy non-unique constraints.

---

#### Updater Function (optional)

When provided, the table accepts `INSERT`, `UPDATE`, and `DELETE` statements.  The updater is called once per modified row.

```lua
function updater(ctx, pk, data)
```

| Parameter | `INSERT` | `UPDATE` | `DELETE` |
|---|---|---|---|
| `ctx` | Shared vtable context table | Shared vtable context table | Shared vtable context table |
| `pk` | `nil` | Old primary key value | Old primary key value |
| `data` | `{col1, col2, ...}` (all column values, PK first) | `{newcol1, newcol2, ...}` (all new column values, new PK first) | `nil` |

**Detecting the operation:**

```lua
function updater(ctx, pk, data)
    if pk == nil then
        -- INSERT: data[1] = new PK, data[2..n] = other columns
    elseif data == nil then
        -- DELETE: pk = PK of the row to remove
    elseif data[1] ~= pk then
        -- UPDATE with PK rename: old PK = pk, new PK = data[1]
    else
        -- UPDATE (same PK): pk == data[1]
    end
end
```

Call `error("message")` inside the updater to reject the operation and propagate the message as a SQLite error.

**Example:**

```lua
local store = {}
SQLiteExt.RegisterVirtualTable("KV", {"Key", "Val"},
    function(ctx, nth)
        if nth == 1 then
            ctx.keys = {}
            for k in pairs(store) do ctx.keys[#ctx.keys + 1] = k end
            table.sort(ctx.keys)
        end
        local k = ctx.keys[nth]
        if not k then return nil end
        return { k, store[k] }
    end,
    nil,  -- no index function: full scan only
    function(ctx, pk, data)
        if pk == nil then
            -- INSERT
            if store[data[1]] then error("Duplicate key") end
            store[data[1]] = data[2]
        elseif data == nil then
            -- DELETE
            store[pk] = nil
        else
            -- UPDATE (handles PK rename automatically)
            store[pk] = nil
            store[data[1]] = data[2]
        end
    end
)
```

**Notes:**

- Calling `RegisterVirtualTable` again with the same name replaces the previous registration.
- All function references (`reader`, `indexfn`, `updater`) are kept alive for the lifetime of the extension (freed on DLL unload / process exit).
- The shared `ctx` table is created fresh when the vtable is registered and persists until the extension unloads; it is not reset between queries.
- Without an `updater`, any `INSERT`, `UPDATE`, or `DELETE` returns a SQLite error containing "Readonly".

---

## Type Mapping

**SQLite → Lua** (arguments received by all Lua functions):

| SQLite type | Lua type |
|---|---|
| `INTEGER` | integer (number) |
| `REAL` | float (number) |
| `TEXT` | string |
| `BLOB` | string (raw bytes) |
| `NULL` | `nil` |

**Lua → SQLite** (return values from all functions):

| Lua type | SQLite type |
|---|---|
| integer | `INTEGER` |
| float | `REAL` |
| string | `TEXT` |
| boolean | `INTEGER` (0 or 1) |
| table | `TEXT` (serialized as JSON) |
| nil / other | `NULL` |

---

## Lifecycle Notes

- The extension DLL is loaded once per process.  If the host (e.g. the Kitsune engine) has already initialised a Lua state before `load_extension` is called, the extension attaches to that existing state rather than creating a new one.
- Function references registered with `RegisterFunction`, `RegisterAggregate`, and `RegisterVirtualTable` are freed on `DLL_PROCESS_DETACH` (graceful unload only; OS-exit skips cleanup).
- The extension stores a single `sqlite3*` handle (the first database that called `load_extension`).  All `SQLiteExt.*` Lua functions operate against that handle.

---

## Third-Party Notices

SQLiteKitsune links against two external components:

- **KitsuneEngine** — delay-loaded at runtime.  All copyright notices for KitsuneEngine and its own dependencies are in the [KitsuneEngine Third-Party Notices](../kitsuneengine-lua-functions.md#third-party-notices).

- **SQLite** — compiled directly into `SQLiteKitsune.dll` as `sqlite3.c` / `sqlite3.h`.

### SQLite

The author disclaims copyright to the SQLite source code.  In place of a legal notice:

> May you do good and not evil.  
> May you find forgiveness for yourself and forgive others.  
> May you share freely, never taking more than you give.

*License: [Public Domain](https://www.sqlite.org/copyright.html)*
