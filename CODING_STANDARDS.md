# Kitsune coding standard

Applies to all our own code in this repository. Follow it when writing new code and when changing existing code.

## Third-party code

Never edit, move, rename or add files in third-party code; updating a library replaces its folder wholesale. That covers `Lua/`, `SQLite/`, `DuckDB/`, `OpenSSL/`, `curl/`, `hiredis/`, `libarchive/`, `libevent/`, `libogg/`, `libvorbis/`, `libyaml/`, `mongodb/`, `msgpack/`, `mysql/`, `postgres/`, `pugixml/`, `stb/`, `tomlc99/`, `dr_libs/`, `Kitsune/SDL2`, `Kitsune/SDL2_mixer`, `Kitsune/litehtml`, `Kitsune/Imgui`, `Kitsune/stb_image.h`, generated `Kitsune/*_generated.cpp`, and the root `aes.*`, `md5.*`, `sha1.*`, `sha256.*`, `miniz.*`.

Conflicts with third-party code (include resolution, name clashes, behaviour) are fixed on our side.

## Layout

- `src/engine/`: engine core and loose globals (`KitsuneEngine`, memory, userdata, debug, stdlib/file wrappers, `LuaMisc`, `LuaEngineBuiltins`).
- `src/modules/<name>/`: one folder per Lua module. `<name>` is the lowercase Lua global the module registers (`Json` → `json`, `HttpClient` → `httpclient`, `MongoDB` → `mongodb`).
- Third-party code stays outside `src/`.

## Files

- PascalCase file names.
- In a module folder every file is prefixed `Lua<Global>` with the global's exact casing:
  - `LuaJson.cpp/.h`: implementation.
  - `LuaJsonMain.cpp/.h`: only `luaopen_json`, declared in the `.h`.
  - Extra files extend the prefix: `LuaRedisString.cpp`, `LuaKafkaProducer.cpp`.
- Include dirs are the repo root, `src/engine` and `src/modules`:
  - engine headers by bare name: `"KitsuneEngine.h"`
  - module headers by folder: `"json/LuaJson.h"`
  - third-party by its folder: `"SQLite/sqlite3.h"`

## Functions

| What | Pattern | Example |
|---|---|---|
| Module registration | `luaopen_<folder>` | `luaopen_json` |
| Userdata helpers | `lua_push<type>`, `lua_to<type>`, `luaL_check<type>`, `lua_is<type>`; a helper pushing the type's string form is `lua_push<type>string` | `lua_pushtoml`, `luaL_checktoml`, `lua_pushdatetimestring` |
| Lua-callable functions | `<Prefix>_<Key>` | `Json_Decode`, `TCP_Connect`, `RedisKey_Delete` |
| Metamethods | `<Prefix>_GC`, `<Prefix>_ToString`, `<Prefix>_Index`, … | `Toml_GC` |
| Loose Lua globals | `Global_<LuaName>` | `Global_Sleep`, `Global_getenv` |
| Internal helpers | PascalCase; `static` when file-local | `BsonToLua`, `PushSqliteValue` |

For Lua-callable functions:
- `<Prefix>` is spelled exactly as Lua sees it: the module's global for functions on the global table (`TCP`, `MCP`, `UInt`, `MongoDB`), or the object type's name (its C struct without the leading `Lua`) for methods of a distinct object type (`TcpClient`, `McpQuestion`). Functions added to Lua's own libraries use that table's name (`string_equal`, `io_lines`).
- `<Key>` is the Lua key exactly as registered (`"Decode"` gives `Json_Decode`). A function registered under several keys takes its non-metamethod key (`"Dispose"` + `__gc` gives `Tasks_Dispose`).
- `<type>` in helper names is the lowercase prefix (`lua_pushmongodb`).
- Internal names must not collide with Windows, CRT or POSIX names; prefix with the module if they would (`StdlibOpenFile`, `FileStreamGetInfo`).

Never rename:
- the exported `Kitsune*` API (C# P/Invoke binds by name)
- the `kitsune_*` C helpers (used by `sqliteextension` and the app)
- any name visible to Lua scripts: globals, methods, metatable name strings

## Types, variables, constants

- Types: PascalCase (`LuaToml`, `KitsuneState`).
- Locals, parameters, struct/class fields: camelCase (`timeoutMs`, `rowCount`).
- Mutable static storage, file-scope or function-local `static`: `g_` + camelCase (`g_nvml`, `g_prevIdle`).
- Macros and scalar constants: `UPPER_SNAKE` (`LUATOML`, `SENTINEL_KEY`).
- Constant tables (`luaL_Reg` arrays, vtables, lookup arrays): camelCase without prefix (`jsonFunctions`, `daysInMonth`).
- Structs mirroring a third-party ABI keep that ABI's names (e.g. the NVML declarations).

## Lua registry references

A `luaL_ref` value is valid only when it is `> 0`; `0` or less means no ref. Test refs with `> 0` / `<= 0`, never `== LUA_NOREF` / `!= LUA_NOREF`. Initialising or resetting a field to `LUA_NOREF` is fine.

Why: zeroed memory (`calloc`, `memset`, `{}`) leaves ref fields at `0`, which a `!= LUA_NOREF` test treats as valid, and `luaL_unref(L, t, 0)` corrupts the reference free list. `LUA_REFNIL` (-1) is also no ref.

## Comments

Comment only where the code can't explain itself: the why, contracts in headers, platform quirks, non-obvious constraints, license headers.

Not allowed:
- comments restating the code (`// Create a new table`, `// Advance cursor`)
- references to plans, tasks, phases, stages or review rounds (`see Plans/...`, task IDs like `T5a`)
- change history (`replaces old client_connect`, `no longer needed`); that belongs in git
- commented-out code
- decorative separator lines (`// =====`, `// -----`)

Allowed: a one-line section label in a long file (`// Instance management`), and algorithm step labels that other code or comments refer to (the scheduler's Step 1–5 in `KitsuneEngine.cpp`).

## Formatting

Tabs (width 4), opening brace on the same line, `else`/`catch` on their own line, `case` at `switch` level, `Type* p`. `.editorconfig` sets the indentation.
