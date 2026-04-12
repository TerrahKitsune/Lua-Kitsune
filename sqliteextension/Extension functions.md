# Extension Functions

The plugin will automatically attempt executing a file called "extension.lua" in the same directory as the database file.

---

## SQLite Functions

### LuaFunction

Executes a function found in the global lua table.

```lua
array LuaFunction(function, args...)
```

**Parameters:**
- `function`: string name of function to execute (e.g., `"string.format"`, `"CRC64"`)
- `args`: arguments passed to the lua function

**Global Variables:**
- `ARGS`: Set on the lua global containing all parameters that was pushed to this function as an array (table)
  - Only valid during this function call and is set to nil after

**Example:**
```lua
LuaFunction("dostring", "return ARGS")
```
Would execute lua script that returns the pushed args.

---

### RegisterAggregate

Registers an aggregate function.

```lua
nil RegisterAggregate(name, function)
```

**Function Definition:**
```lua
function(isFinished, context, arg1, arg2, ...)
```

**Parameters:**
- `isFinished`: boolean indicating if this is the final call
  - If true, the function should return the aggregated value
  - Additional parameters will be nil on the final call
- `context`: table passed to each call of the function
  - Destroyed when aggregation is finished
  - Used to store state between calls

**Example:**
```lua
RegisterAggregate("Test", function(isFinished, context) 
    context.cnt = context.cnt or 0
    if isFinished then return context.cnt end
    context.cnt = context.cnt + 1
end)
```

This registers a simple count function which could be called as:
```sql
select Test("Id") from Table
```

The function is called once for each row (with `isFinished = false`), and then once more with `isFinished = true` to get the final result.

---

### RegisterFunction

Register a lua function as a SQL function.

```lua
nil RegisterFunction(name, function)
```

**Function Definition:**
```lua
function(context, arg1, arg2, ...)
```

**Parameters:**
- `context`: table that is always the same each time the function is called (persistent across calls)
- `arg1, arg2, ...`: arguments passed from SQL

---

## Lua Functions

### Global Variables

- `FILE`: the sqlite file or empty if it is a memory database

---

### dostring

Executes a string as a lua function and returns 1 result.

```lua
any dostring(str)
```

---

### RegisterVirtualTable

Registers a virtual table by name that contains fields.

```lua
nil RegisterVirtualTable(string name, array fields, readerfunction, opt insertupdatedelete)
```

**Parameters:**
- `name`: Name of the virtual table
- `fields`: Array of field names. The first field will be considered the primary key
  - Example: `{"Id", "Value", "Data"}`
- `readerfunction`: Function that reads data
- `insertupdatedelete`: (optional) Function that handles modifications (delete, update, insert)

**Reader Function:**

```lua
function(context)
```

Where:
- `context`: table for user data, same context is pushed to each call of read for the same cursor
  - Different cursors reading the table will have different contexts
- Should return an array containing the field values in order
  - For example `{1, "abc", 12.3}` where the first value is the primary key "Id"

**Insert/Update/Delete Function:**

```lua
function(pk, data)
```

Where:
- `pk`: the primary key being modified
- `data`: table representing the new data to be inserted/updated/deleted
  - Contains a field for each column in order
  - Example for `{"Id", "Value", "Data"}`: `data[1] = Id, data[2] = Value, data[3] = Data`

**Modification Logic:**
- If `pk == nil`: a completely new row is being inserted (data contains the full new row)
- If `data == nil`: the row with primary key `pk` is being deleted
- If `data[1] ≠ pk`: the primary key is being changed from `pk` to `data[1]` (update key and other fields)
- If `data[1] == pk`: the row is being updated (only the fields in data are changed)

**Error Handling:**
If the modification should not be allowed, call `error()`. Example:
```lua
error("Duplicate key")
```

**Note:** If no `insertupdatedelete` function is given, the virtual table is considered read-only.

---

### RegisterTable

Registers a lua table to mirror a virtual table.

```lua
nil RegisterTable(string name, array fields, table)
```

**Parameters:**
- `name`: Name of the virtual table to mirror
- `fields`: Array of field names (must match the structure of the virtual table)
  - The first value is considered the primary key and will be used as the key in the lua table
  - Example: `{"Id", "Value", "Data"}`
- `table`: Lua table that will store and mirror the virtual table data

**Data Storage Format:**
- If more than two fields: `table[pk] = {field2, field3, ...}` (array of remaining fields)
- If exactly two fields: `table[pk] = value` (just the second field's value)

---

### query

Executes a query against the database.

```lua
table query(sql, prepared)
```

**Parameters:**
- `sql`: SQL query string with optional parameter placeholders using `@` notation
  - Example: `"select * from test where Id=@id"`
- `prepared`: (optional) table containing values for prepared statement parameters
  - Fields should match parameter names from the SQL query
  - Example: for `@id` parameter, prepared table should have `{id = value}`

**Returns:**
Array containing the entire resultset as key-value pairs.

---

### scalar

Executes a query and returns a single scalar value from the result.

```lua
singlevalue scalar(sql, prepared)
```

**Parameters:**
- `sql`: SQL query string with optional parameter placeholders using `@` notation
- `prepared`: (optional) table containing values for prepared statement parameters
  - Fields should match parameter names from the SQL query

**Returns:**
A single scalar value from the first row/column of the query result.
