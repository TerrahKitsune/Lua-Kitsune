# RegisterVirtualTable Implementation Plan

## Overview

Implement `SQLiteExt.RegisterVirtualTable(name, fields, readerfunction, opt indexfunction, opt insertupdatedelete)` as a new C++ source file (`registerluavtable.cpp`) following the same patterns as the existing `registerluatable.cpp`.

The key difference from `RegisterTable`:
- `RegisterTable` mirrors a **Lua table** directly.
- `RegisterVirtualTable` calls **Lua functions** to produce rows (like the old `executeluafunction.cpp` in the legacy branch), allowing any data source.

---

## Reference Files

| File | Role |
|---|---|
| `registerluatable.cpp` | Pattern to follow for vtab boilerplate |
| `luafunctions.cpp` | How to anchor Lua functions and register Kitsune callbacks |
| `kitsuneext.h` | `sqlite_val_to_kitsune`, `KitsuneExtState`, `push_kv_to_sqlite` pattern |
| `KitsuneEngine.h` | `KitsuneVariable`, `KitsuneExecuteVariable`, `KitsuneAnchorVariable` |
| legacy `executeluafunction.cpp` | Original reader/update pattern (uses raw `lua_State*`; ours uses the Kitsune API instead) |

---

## New Files

### `registerluavtable.h`
```cpp
#pragma once
#include "dllmain.h"

int register_virtual_table_cb(int argc, const KitsuneVariable* argv,
    kitsune_ResultSetter resultSetter, void* userdata);
```

### `registerluavtable.cpp`

#### Structs

```
LuaVTabModule {
    KitsuneVariable* readFunc;    // anchored read function (via lua_add_kitsune_state)
    KitsuneVariable* indexFunc;   // anchored index capability function, or NULL (full-scan only)
    KitsuneVariable* updateFunc;  // anchored update function, or NULL if read-only
    int fieldCount;
    char** fieldNames;            // sqlite3_malloc'd array of sqlite3_malloc'd strings
}

LuaVTabVTab {
    sqlite3_vtab base;   // must be first
    LuaVTabModule* mod;  // back-pointer; NOT owned by this struct
}

LuaVTabCursor {
    sqlite3_vtab_cursor base;
    KitsuneVariable* contextVar;  // per-cursor context table; anchored on xOpen, freed on xClose
    KitsuneVariable* currentRow;  // result from last reader call; NULL = eof
    KitsuneVariable* indexData;   // anchored constraints table for current index scan; NULL = full scan
    sqlite3_int64 rowid;          // 1-based call counter; reset to 1 on each xFilter
    int eof;
}
```

#### Module destructor: `lua_vtab_free_module(void* pAux)`
- Frees `mod->fieldNames` entries and array via `sqlite3_free`
- Does **not** call `KitsuneVariableFree` on `readFunc`/`indexFunc`/`updateFunc` — all three are owned by `g_extState` (anchored via `lua_add_kitsune_state`), freed in `lua_cleanup_kitsune_state`
- Frees the `mod` struct itself via `sqlite3_free`

#### `xCreate` / `xConnect` — `lua_vtab_connect`
- Allocate `LuaVTabVTab` shell
- Build DDL: `"CREATE TABLE x(col0 PRIMARY KEY, col1, col2, ...) WITHOUT ROWID;"` — same as `registerluatable.cpp`\r\n  - WITHOUT ROWID ensures `xUpdate` receives the actual PK column value in `argv[0]` for DELETE/UPDATE, not an internal rowid integer\r\n  - `xRowid` returns a dummy 0 (no-op for WITHOUT ROWID tables)\r\n  - The `rowid` field on the cursor is the internal `nth` counter only; it is never exposed as a SQLite rowid
- Call `sqlite3_declare_vtab`

#### `xBestIndex` — `lua_vtab_best_index`
- If `mod->indexFunc == NULL`: return `SQLITE_OK` immediately (full-scan only)
- Collect all **usable** constraints from `info->aConstraint[0..nConstraint-1]`, skipping unsupported op types
- **Build the constraints argument as nested `KITSUNE_TTABLECONTENTS` on the stack:**
  - For each constraint, fill two `KeyValuePairKitsuneVariableNode` nodes with string keys `"column"`/`"op"` and values `KITSUNE_TINTEGER` N and `KITSUNE_TSTRING` op-string, forming an inner `KITSUNE_TTABLECONTENTS` sub-table
  - `KITSUNE_TTABLECONTENTS` supports nested sub-tables when passed *into* the engine; `KitsuneExecuteVariable` recursively creates Lua tables from them
  - Outer array: one `KeyValuePairKitsuneVariableNode` per constraint with integer key 1..N and value = inner `KITSUNE_TTABLECONTENTS`; all nodes stack-allocated
  - Op strings (`"="`, `">"`, etc.) are string literals; set `.data` and `.length` accordingly
- Pass the outer `KITSUNE_TTABLECONTENTS` directly to `KitsuneExecuteVariable(mod->indexFunc, 1, &constraintsArg)` — no `KitsuneAnchorVariable` needed; call is synchronous and stack nodes outlive it
- If result is truthy (non-nil, non-false):
  - Mark all collected constraints: `aConstraintUsage[i].argvIndex = 1, 2, ...` (in collection order)
  - Set `idxNum = 1`, `estimatedCost = 10`
  - Encode constraint metadata into `idxStr` (sqlite3_malloc'd) as `"col:op,"` pairs (e.g. `"1:=,2:>,"`) so `xFilter` can rebuild the full constraint table from `argv`
- `KitsuneVariableFree` the result; stack nodes freed automatically

Supported op mappings:

| SQLite constant | `op` string |
|---|---|
| `SQLITE_INDEX_CONSTRAINT_EQ` | `"="` |
| `SQLITE_INDEX_CONSTRAINT_GT` | `">"` |
| `SQLITE_INDEX_CONSTRAINT_LT` | `"<"` |
| `SQLITE_INDEX_CONSTRAINT_GE` | `">="` |
| `SQLITE_INDEX_CONSTRAINT_LE` | `"<="` |

Any other constraint type is skipped (not passed to `indexFunc`).

#### `xDisconnect` / `xDestroy` — `lua_vtab_disconnect`
- `sqlite3_free(pVtab)` only (vtab shell); `mod` is owned by the destructor

#### `xOpen` — `lua_vtab_open`
- Allocate and zero-init `LuaVTabCursor`
- Create per-cursor context table:
  ```cpp
  KitsuneVariable cv = {};
  cv.type = KITSUNE_TTABLECONTENTS; // table == NULL → empty
  cursor->contextVar = KitsuneAnchorVariable(&cv);
  ```
  Per `KitsuneEngine.h`: *"pass a KITSUNE_TTABLECONTENTS variable with table == NULL; PushKitsuneVariable creates a fresh lua_newtable"* — returns a `KITSUNE_TTABLE` ref

#### `xClose` — `lua_vtab_close`
- `KitsuneVariableFree(cursor->contextVar)`
- `KitsuneVariableFree(cursor->currentRow)`
- `KitsuneVariableFree(cursor->indexData)`
- `sqlite3_free(cursor)`

#### `xFilter` — `lua_vtab_filter`
- Free and NULL-clear `cursor->currentRow` and `cursor->indexData`
- Reset `cursor->rowid = 1`, `cursor->eof = 0`
- **Re-scan behaviour (JOINs):** `xFilter` may be called multiple times on the same cursor (once per outer row in a nested-loop join). Each call resets `rowid` to 1. The context table is intentionally **not** re-created — `nth == 1` is the Lua-visible signal that a new scan is starting; reader code that caches per-scan state in context should check `nth == 1` and reset accordingly
- If `idxNum == 1` (index scan):
  - Parse `idxStr` (`"col:op,"` pairs) combined with `argv[0], argv[1], ...` to build a nested `KITSUNE_TTABLECONTENTS` list of `{column=N, op="=", value=v}` sub-table nodes — same nested structure as in `xBestIndex`, but now values are populated via `sqlite_val_to_kitsune(argv[i], ...)`
  - String values in the nodes borrow SQLite-owned memory; `KitsuneAnchorVariable` copies all data into the Lua heap before returning
  - `KitsuneAnchorVariable` the list → `cursor->indexData` (a `KITSUNE_TTABLE` ref stable for the cursor lifetime)
  - Call reader with 3 args: `{*contextVar, {KITSUNE_TINTEGER, 1}, *indexData}`
- If `idxNum == 0` (full scan):
  - Call reader with 2 args: `{*contextVar, {KITSUNE_TINTEGER, 1}}`
- On result: if `NULL`/`KITSUNE_TNIL`/`KITSUNE_TERROR` → `eof=1`; else store as `cursor->currentRow`

#### Reader call helper (shared by `xFilter` and `xNext`)
- Full scan (`indexData == NULL`): `KitsuneExecuteVariable(readFunc, 2, args)` — `args = {*contextVar, {KITSUNE_TINTEGER, rowid}}`
- Index scan (`indexData != NULL`): `KitsuneExecuteVariable(readFunc, 3, args)` — `args = {*contextVar, {KITSUNE_TINTEGER, rowid}, *indexData}`
- Passing `*contextVar` and `*indexData` (struct copy) is safe — both are anchored `KITSUNE_TTABLE` refs; their `ref` fields remain valid for the cursor lifetime

#### `xNext` — `lua_vtab_next`
- `KitsuneVariableFree(cursor->currentRow)`, set to `NULL`
- `cursor->rowid++`
- Call reader using the helper above (re-uses stored `cursor->indexData`)
- Same nil/error/table handling as `xFilter`

#### `xEof` — `lua_vtab_eof`
- Return `cursor->eof`

#### `xColumn` — `lua_vtab_column`
- `currentRow` is a `KITSUNE_TTABLE` ref — the direct result of `KitsuneExecuteVariable`; it is **not** `KITSUNE_TTABLECONTENTS` (the engine returns inner tables as live `KITSUNE_TTABLE` refs, not snapshots)
- Do **not** access `.table` directly (that field is only valid for `KITSUNE_TTABLECONTENTS`)
- Get column N: call `KitsuneGetIndex(cursor->currentRow, &intKey)` where `intKey = {KITSUNE_TINTEGER, N+1}` (Lua 1-based)
- Push the result to SQLite via `push_kv_to_sqlite`, then `KitsuneVariableFree` it

#### `xRowid` — `lua_vtab_rowid`
- `*pRowid = 0` — no-op for WITHOUT ROWID tables (matches `registerluatable.cpp`)

#### `xUpdate` — `lua_vtab_update`
- If `mod->updateFunc == NULL`: set `pVtab->zErrMsg = sqlite3_mprintf("Readonly")`, return `SQLITE_READONLY`
- Map SQLite's `argc`/`argv` to the documented Lua call `insertupdatedelete(pk, data)`:

| SQLite operation | `pk` arg | `data` arg |
|---|---|---|
| DELETE (`argc == 1`) | `argv[0]` (old PK) | `nil` |
| INSERT (`argv[0]` is NULL) | `nil` | table `{argv[2], argv[3], ...}` (all field values) |
| UPDATE (`argv[0]` not NULL) | `argv[0]` (old PK) | table `{argv[2], argv[3], ...}` (new field values) |

- Build args as a 2-element `KitsuneVariable` array and call `KitsuneExecuteVariable(mod->updateFunc, 2, args)`
- If result is `KITSUNE_TERROR`: copy error message to `pVtab->zErrMsg`, return `SQLITE_ERROR`
- Otherwise return `SQLITE_OK`

---

## Changes to Existing Files

### `luafunctions.cpp`
1. Add `#include "registerluavtable.h"`
2. In `lua_register_kitsune_functions`, add:
```cpp
KitsuneRegisterFunction("SQLiteExt.RegisterVirtualTable", register_virtual_table_cb, g_extState);
```

### `register_virtual_table_cb` argument parsing
- `argv[0]`: name (KITSUNE_TSTRING)
- `argv[1]`: fields array (KITSUNE_TTABLE), min 2 entries
- `argv[2]`: reader function (KITSUNE_TFUNCTION)
- `argv[3]`: optional — index function (KITSUNE_TFUNCTION) or nil/absent
- `argv[4]`: optional — update function (KITSUNE_TFUNCTION) or nil/absent
- All three functions anchored via `lua_add_kitsune_state`; stored as pointers in `LuaVTabModule`
- **Field name validation**: for each field name, reject if any character fails `isalpha()` — matching the validation in `register_table_cb`
- **DROP + CREATE pattern** (same as `registerluatable.cpp`):
  1. `DROP TABLE IF EXISTS "name"` via `sqlite3_exec` — destroys any stale vtab shell while the **old** module's destructor is still valid; safe because `lua_vtab_disconnect` only frees the shell, never `mod`
  2. `sqlite3_create_module_v2(db, name, &g_luaVTabModule, mod, lua_vtab_free_module)` — registers the new module; destructor takes ownership of `mod`
  3. `CREATE VIRTUAL TABLE "name" USING "name"` via `sqlite3_exec` — triggers `xConnect` with the fresh `mod`

### `SQLiteKitsuneExtension.vcxproj`
- Add `<ClCompile Include="registerluavtable.cpp" />` alongside the existing `registerluatable.cpp` entry

### `SQLiteKitsuneExtension.vcxproj.filters`
- Add matching filter entries for `registerluavtable.cpp` and `registerluavtable.h`

---

## Tests to Add (`SQLiteExtensionTests.cs`)

| Test name | Description |
|---|---|
| `SQLiteExtension_RegisterVirtualTable_ReadOnly_TwoField` | Reader returns `{key, value}` rows; SELECT returns expected results |
| `SQLiteExtension_RegisterVirtualTable_ReadOnly_ThreeField` | Reader returns `{id, f1, f2}` rows; SELECT returns expected results |
| `SQLiteExtension_RegisterVirtualTable_NthCounter` | Verify nth=1 on first call, increments correctly each call |
| `SQLiteExtension_RegisterVirtualTable_ContextPerCursor` | Two concurrent cursors receive separate context tables |
| `SQLiteExtension_RegisterVirtualTable_ReadOnly_RejectWrite` | No update function; INSERT returns error containing "Readonly" |
| `SQLiteExtension_RegisterVirtualTable_Insert` | Update function called with `pk=nil`; new row visible in reader output |
| `SQLiteExtension_RegisterVirtualTable_Delete` | Update function called with `data=nil`; row gone from reader output |
| `SQLiteExtension_RegisterVirtualTable_Update_SamePK` | `data[1] == pk`; field values updated |
| `SQLiteExtension_RegisterVirtualTable_Update_RenamePK` | `data[1] ~= pk`; PK and values change |
| `SQLiteExtension_RegisterVirtualTable_UpdateError` | Update function calls `error()`; SQLite returns the error message |
| `SQLiteExtension_RegisterVirtualTable_Index_EqLookup` | Index function returns true for EQ; reader receives indexdata with value; no full scan |
| `SQLiteExtension_RegisterVirtualTable_Index_FallbackFullScan` | Index function returns false; full scan used |
| `SQLiteExtension_RegisterVirtualTable_Index_ConstraintShape` | Verify indexdata fields: column, op, value are correct |
| `SQLiteExtension_RegisterVirtualTable_ContextPreservedOnRescan` | Context table persists across xFilter re-scans (JOIN); nth resets to 1 each time; state stored in context survives |

---

## Notes

- `lua_add_kitsune_state` (in `luafunctions.cpp`) anchors function variables and tracks them for cleanup at DLL unload. `readFunc`, `indexFunc`, and `updateFunc` must all be anchored through it — **not** freed in the module destructor.
- The context table is created fresh on `xOpen` as an empty Lua table. `xFilter` may be called multiple times on the same cursor (e.g. nested-loop join); the context is **not** re-created on subsequent `xFilter` calls. `rowid`/`nth` resets to 1 on each `xFilter` call — this is the Lua-visible re-scan signal. Reader code that caches per-scan state (e.g. a key snapshot) should check `nth == 1` and reset that state; reader code that is purely `nth`-indexed (e.g. `return data[nth]`) requires no special handling.
- `xBestIndex` is called by SQLite multiple times per query to compare cost estimates for different constraint subsets. Each call invokes the Lua index function; this is expected and fine for typical WHERE clause counts.
- For the data table passed to the update function on INSERT/UPDATE, build a `KITSUNE_TTABLECONTENTS` linked list from `argv[2..argc-1]` with integer keys `1..fieldCount` on the stack and pass it directly to `KitsuneExecuteVariable` — no `KitsuneAnchorVariable` needed since the call is synchronous and the Lua function receives the data within the call frame. (Contrast with `set_row_value` in `registerluatable.cpp` which must anchor because it stores the result persistently via `KitsuneSetIndex`.)
- Passing `*contextVar` and `*indexData` as struct copies into the args array is safe: both are anchored `KITSUNE_TTABLE` values whose `ref` field (a Lua registry index) remains valid for the full cursor lifetime.

---

## Examples

### Example 1 — Reader only (simplest)

Only a reader function. Full scan every time. SQLite applies any WHERE filtering post-scan.

```lua
local data = {{"alice", 100}, {"bob", 200}, {"carol", 300}}

SQLiteExt.RegisterVirtualTable("Scores", {"Name", "Points"}, function(context, nth)
    return data[nth]  -- returns nil when nth exceeds length; signals end of data
end)

-- SELECT * FROM Scores
-- SELECT Name FROM Scores WHERE Points > 150  (SQLite filters post-scan)
```

---

### Example 2 — Reader + index function

Index function lets SQLite skip the full scan for `WHERE Name='x'` queries.

```lua
local data = {alice=100, bob=200, carol=300}
local keys  = {"alice", "bob", "carol"}

SQLiteExt.RegisterVirtualTable("Scores", {"Name", "Points"},
    function(context, nth, indexdata)
        if indexdata then
            -- index scan: look up the specific name directly
            local name = indexdata[1].value
            if nth > 1 then return nil end  -- at most one row per PK lookup
            return {name, data[name]}
        end
        -- full scan
        local key = keys[nth]
        if not key then return nil end
        return {key, data[key]}
    end,
    function(constraints)
        -- report that we can handle EQ on column 1 (Name)
        for _, c in ipairs(constraints) do
            if c.column == 1 and c.op == "=" then return true end
        end
        return false
    end
)

-- SELECT * FROM Scores WHERE Name='alice'  -- reader called with indexdata; no full scan
-- SELECT * FROM Scores                     -- reader called without indexdata; full scan
```

---

### Example 3 — Reader + index function + insert/update/delete

All three functions. Fully read-write with indexed lookup.

```lua
local store = {}

SQLiteExt.RegisterVirtualTable("KV", {"Key", "Value"},
    function(context, nth, indexdata)
        if indexdata then
            local k = indexdata[1].value
            if nth > 1 or store[k] == nil then return nil end
            return {k, store[k]}
        end
        -- full scan: snapshot keys on first call so iteration is stable
        if not context.keys then
            context.keys = {}
            for k in pairs(store) do context.keys[#context.keys + 1] = k end
        end
        local k = context.keys[nth]
        if not k then return nil end
        return {k, store[k]}
    end,
    function(constraints)
        for _, c in ipairs(constraints) do
            if c.column == 1 and c.op == "=" then return true end
        end
        return false
    end,
    function(pk, data)
        if data == nil then
            store[pk] = nil             -- DELETE
        elseif pk == nil then
            store[data[1]] = data[2]    -- INSERT
        else
            if pk ~= data[1] then store[pk] = nil end
            store[data[1]] = data[2]    -- UPDATE (rename key if data[1] ~= pk)
        end
    end
)

-- INSERT INTO KV VALUES('x', 42)
-- UPDATE KV SET Value=99 WHERE Key='x'
-- DELETE FROM KV WHERE Key='x'
-- SELECT * FROM KV WHERE Key='x'  -- uses index, reader gets indexdata
-- SELECT * FROM KV                -- full scan
```
