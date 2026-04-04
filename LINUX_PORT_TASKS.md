# Linux Port Task List

Tracks every `[WindowsOnlyFact]` test in `KitsuneUtilTests.cs` that can realistically be made
cross-platform, with the exact files to touch, the Linux API to use, and how to validate.

Tests that are **permanently Windows-only** (Registry, ResList, Clipboard) are listed at the
bottom as out-of-scope.

---

## Task 1 — MD5 module: register on Linux

**Status:** code already compiles on Linux (in CMakeLists.txt); module just isn't registered.

**Tests unlocked:**
- `MD5_KnownVector_ReturnsCorrectHex`
- `MD5_EmptyInput_ReturnsKnownHash`

**Problem:**
`MD5Main.cpp` calls `luaopen_md5` but `LuaEngineBuiltins.cpp` only calls it inside an
`#ifdef _WIN32` block (or it is not called at all on Linux — confirm with grep).

**Fix:**
1. `LuaEngineBuiltins.cpp` — locate the `luaopen_md5` call site; remove or loosen the
   `#ifdef _WIN32` guard so it is also called on Linux.
2. `luaaes.h` — includes `<Windows.h>`; `LuaMD5.cpp` / `md5.h` do not appear to have
   the same dependency, but verify there are no hidden Windows headers.
3. Change `[WindowsOnlyFact]` → `[Fact]` for both MD5 tests.

---

## Task 2 — `GetLastError` on Linux

**Tests unlocked:**
- `GetLastError_WithCode2_ReturnsNonEmptyMessageAndCode`
- `GetLastError_NoArgs_ReturnsString`

**Problem:**
`GetLastError` in `LuaEngineBuiltins.cpp` calls the Win32 `GetLastError()` /
`FormatMessage()` APIs.

**Fix:**
`LuaEngineBuiltins.cpp` — wrap with `#ifdef _WIN32` / `#else`:

```cpp
#else  // Linux
int L_GetLastError(lua_State* L) {
    int code = (int)luaL_optinteger(L, 1, errno);
    lua_pop(L, lua_gettop(L));
    char buf[256];
    strerror_r(code, buf, sizeof(buf));
    lua_pushstring(L, buf);
    lua_pushinteger(L, code);
    return 2;
}
#endif
```

Register in `luaopen_kitsune` (or equivalent) on both branches.
Change `[WindowsOnlyFact]` → `[Fact]` for both tests.

---

## Task 3 — `GetIsAdmin` on Linux

**Tests unlocked:**
- `GetIsAdmin_ReturnsBool`

**Problem:**
`LuaEngineBuiltins.cpp` uses `IsUserAnAdmin()` (Shell32).

**Fix:**
```cpp
#else  // Linux
int L_GetIsAdmin(lua_State* L) {
    lua_pushboolean(L, geteuid() == 0);
    return 1;
}
#endif
```

Requires `#include <unistd.h>` (already present via `is_stdin_tty`).
Change `[WindowsOnlyFact]` → `[Fact]`.

---

## Task 4 — `GlobalMemoryStatus` extended variants on Linux

**Tests unlocked:**
- `GlobalMemoryStatus_TotalPhysical_ReturnsPositive`

**Problem:**
The `GlobalMemoryStatus(n)` implementation uses `GlobalMemoryStatusEx` (Win32).
The no-arg form (`GlobalMemoryStatus()` → load percentage) already returns a value on
Linux; only the numeric-argument variants (total physical, available, etc.) are gated.

**Fix:**
`LuaEngineBuiltins.cpp` Linux branch — read `/proc/meminfo`:

```
MemTotal:     total physical RAM
MemAvailable: available RAM
SwapTotal:    total swap
SwapFree:     free swap
```

Map the integer argument to the same index conventions as the Windows branch.
Change `[WindowsOnlyFact]` → `[Fact]` for `GlobalMemoryStatus_TotalPhysical_ReturnsPositive`.

---

## Task 5 — CSV streaming tests (quick win after AppendStreamBuffer fix)

**Tests unlocked:**
- `CSV_DecodeFromFunction_AutoDetect_AccumulatesChunksForSniff`
- `CSV_DecodeFromFunction_ChunkedStringInput_YieldsAllRows`
- `CSV_DecodeFromFunction_WcharChunks_ConvertedTransparently`

**Problem:**
These tests were marked `[WindowsOnlyFact]` when the Linux `AppendStreamBuffer`
path contained a data-corruption bug (writing from offset 0 into an already-filled
buffer). That bug has already been fixed in `luacsv.cpp`.

**Fix:**
No C++ changes needed — the bug is already patched.
Change `[WindowsOnlyFact]` → `[Fact]` for all three tests and verify they pass on WSL2.

---

## Task 6 — `Stream.Open` file-backend on Linux

**Tests unlocked:**
- `Stream_Open_WriteRead_RoundTrip`
- `Stream_Open_Info_ContainsNameAndType`
- `Stream_Open_Seek_UpdatesPosition`
- `Stream_Open_Len_ReturnsFileSizeWithoutMovingCursor`
- `Stream_Open_Info_LenMatchesFileSize`
- `Stream_Open_ReadMode_BlocksWrite`
- `Stream_Tostring_FileStream_ReturnsFallbackString`

**Problem:**
`streamfile.cpp` already has `#ifdef _WIN32` / `#else` guards for every platform-specific
call (`fopen_s`, `_ftelli64`, `_filelengthi64`, `_fseeki64`, `strncpy_s`) and the Linux
`posix_filelength` helper is already present.
The module is included in `CMakeLists.txt`. The actual blocker is:

1. The Lua test scripts use `os.getenv('TEMP')` (Windows env var) for the temp directory.
2. `StreamMain.cpp` may register `Stream.Open` under an `#ifdef _WIN32` guard —
   confirm with grep.

**Fix:**
1. `streamfile.cpp` — verify all `#ifdef _WIN32` else-branches are correct (they appear
   to be already — confirm no remaining `ZeroMemory`, `strncpy_s` etc. on the Linux path).
2. `StreamMain.cpp` — remove any `#ifdef _WIN32` guard around the `"Open"` entry in the
   function table.
3. `KitsuneUtilTests.cs` — replace `os.getenv('TEMP') .. '\\'` with a portable helper:
   ```csharp
   // helper at top of Lua script
   local tmpdir = os.getenv('TEMP') or os.getenv('TMPDIR') or '/tmp'
   local sep    = package.config:sub(1,1)   -- '\\' on Windows, '/' on Linux
   ```
   Or inject a `KITSUNE_TMPDIR` global from C# before the test runs.
4. Change `[WindowsOnlyFact]` → `[Fact]` for all seven tests.

---

## Task 7 — Stream Compress / Decompress on Linux (zlib)

**Tests unlocked:**
- `Stream_Compress_Decompress_RoundTrip`
- `Stream_Compress_ProducesSmallerOutput`
- `Stream_Compress_IntoProvidedStream_RoundTrip`
- `Stream_Decompress_IntoProvidedStream_RoundTrip`
- `Stream_Compress_AndDecompress_BothIntoProvidedStreams_RoundTrip`
- `Stream_Compress_ProvidedDst_PositionNotReset`
- `Stream_Decompress_ProvidedDst_PositionNotReset`
- `Stream_Compress_NonReadableSource_ReturnsNilAndError`
- `Stream_Decompress_NonReadableSource_ReturnsNilAndError`
- `Stream_Compress_NonWritableDest_ReturnsNilAndError`

**Problem:**
`stream.cpp` uses the Windows `compressapi.h` (`CreateCompressor` / `Compress` /
`CreateDecompressor` / `Decompress`). The Linux stub immediately errors:
`"Stream.Compress is not supported on this platform"`.

**Fix:**
1. `CMakeLists.txt` — find and link zlib:
   ```cmake
   find_package(ZLIB REQUIRED)
   target_link_libraries(KitsuneEngine PRIVATE ZLIB::ZLIB)
   ```
2. `stream.cpp` — in the `#else` (Linux) branch of `CompressStream` / `DecompressStream`
   replace the stub with a zlib implementation using `deflate` / `inflate`.

   Wire format: 4-byte little-endian uncompressed length header followed by the raw
   deflate stream — **exactly matching the Windows MSZIP frame** so compressed blobs
   remain interchangeable between platforms.

   Key zlib calls: `deflateInit`, `deflate`, `deflateEnd` for compress;
   `inflateInit`, `inflate`, `inflateEnd` for decompress.
3. Change `[WindowsOnlyFact]` → `[Fact]` for all ten tests.

---

## Task 8 — Timer module on Linux

**Tests unlocked:**
- `Timer_InitialState_NotRunning`
- `Timer_AfterStart_IsRunning`
- `Timer_ElapsedAfterSleep_IsPositive`
- `Timer_StopAndReset_ElapsedIsZero`

**Problem:**
`Timer.cpp` includes `<windows.h>` and uses `QueryPerformanceCounter` /
`QueryPerformanceFrequency` throughout. No `#ifdef` guards exist.

**Fix:**
1. `Timer.h` — add a platform-portable counter type:
   ```cpp
   #ifdef _WIN32
   typedef long long timer_tick_t;
   #else
   #include <time.h>
   typedef long long timer_tick_t;
   static inline timer_tick_t get_monotonic_ns() {
       struct timespec ts;
       clock_gettime(CLOCK_MONOTONIC, &ts);
       return (timer_tick_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;
   }
   #endif
   ```
2. `Timer.cpp` — wrap each `QueryPerformanceCounter` / `QueryPerformanceFrequency`
   call in `#ifdef _WIN32` / `#else` and use `get_monotonic_ns()` on Linux.
   The `PCFreq` field becomes `1000000.0` (ticks/ms = ns → ms) on Linux.
3. Remove `#include <windows.h>` from `Timer.cpp` / `Timer.h` and guard it.
4. `CMakeLists.txt` — add `Timer.cpp` and `TimerMain.cpp` to `KITSUNE_ENGINE_SOURCES`.
5. `LuaEngineBuiltins.cpp` — call `luaopen_timer` on both platforms (currently
   `#ifdef _WIN32` gated).
6. Change `[WindowsOnlyFact]` → `[Fact]` for all four tests.

---

## Task 9 — FileSystem module on Linux

**Tests unlocked:**
- `FileSystem_CurrentDirectory_ReturnsNonEmptyString`
- `FileSystem_GetTempFileName_ReturnsValidPath`
- `FileSystem_GetDrives_ReturnsList`
- `FileSystem_CreateAndDeleteDirectory_Succeeds`

**Problem:**
`LuaFileSystem.cpp` includes `<Windows.h>`, `<io.h>`, `<shlobj.h>` and uses
`GetCurrentDirectoryW`, `GetTempFileName`, `GetLogicalDrives`, `CreateDirectoryW`,
`RemoveDirectoryW`, etc.  The entire file is Windows-only.

**Fix:**
Add Linux implementations behind `#ifdef _WIN32` / `#else` guards in `LuaFileSystem.cpp`:

| Function | Windows API | Linux replacement |
|---|---|---|
| `CurrentDirectory` | `GetCurrentDirectoryW` | `getcwd()` |
| `GetTempFileName` | `GetTempFileNameA` | `mkstemp()` with `<stdlib.h>` |
| `GetDrives` | `GetLogicalDrives` bitmask | parse `/proc/mounts` or return `{"/"}` |
| `CreateDirectory` | `CreateDirectoryW` | `mkdir(path, 0755)` |
| `RemoveDirectory` | `RemoveDirectoryW` | `rmdir()` |
| `ListFiles` / `FindFirst` | `FindFirstFileW` | `opendir` / `readdir` |

The `wchar_t`-path helpers (`lua_topathw`) used in the Windows branch should be replaced
with a simple UTF-8 string path on Linux (no wide-char path needed on POSIX).

`CMakeLists.txt` — add `LuaFileSystem.cpp` and `LuaFileSystemMain.cpp` to sources.
`LuaEngineBuiltins.cpp` — call `luaopen_filesystem` on both platforms.
Change `[WindowsOnlyFact]` → `[Fact]` for all four tests.

---

## Task 10 — SQLite module on Linux

**Tests unlocked:**
- `SQLite_InMemory_CreateInsertSelect`
- `SQLite_GetRow_ByIndex_ReturnsValue`
- `SQLite_RegisterFunction_CallableFromQuery`

**Problem:**
`LuaSQLite.cpp` uses only the SQLite C API, which is fully cross-platform.
The blocker is purely build configuration: the module is not compiled or linked on Linux.

**Fix:**
1. `CMakeLists.txt`:
   ```cmake
   find_package(SQLite3)
   if(SQLite3_FOUND)
       target_sources(KitsuneEngine PRIVATE LuaSQLite.cpp LuaSQLiteMain.cpp)
       target_link_libraries(KitsuneEngine PRIVATE SQLite::SQLite3)
       target_compile_definitions(KitsuneEngine PRIVATE KITSUNE_HAS_SQLITE)
   endif()
   ```
2. `LuaSQLite.cpp` / `LuaSQLiteMain.cpp` — remove or guard any `#include <Windows.h>`.
3. `LuaEngineBuiltins.cpp` — call `luaopen_sqlite` under `#ifdef KITSUNE_HAS_SQLITE`.
4. Change `[WindowsOnlyFact]` → `[Fact]` for all three tests.

---

## Task 11 — AES module on Linux

**Tests unlocked:**
- `Aes_EncryptDecrypt_RoundTrip`
- `Aes_EncryptedData_DiffersFromPlaintext`

**Problem:**
`luaaes.h` includes `<Windows.h>` (for `DWORD` / `ZeroMemory`).
`luaaes.cpp` itself uses only the portable `aes.hpp` / `AES_*` API — the Windows
dependency is entirely the header.

**Fix:**
1. `luaaes.h` — replace `#include <Windows.h>` with:
   ```cpp
   #ifdef _WIN32
   #include <Windows.h>
   #else
   #include <stdint.h>
   typedef uint32_t DWORD;
   #endif
   ```
   Replace the single `ZeroMemory` call in `luaaes.cpp` with `memset`.
2. `CMakeLists.txt` — add `luaaes.cpp`, `LuaAesMain.cpp`, and `aes.c` (or `aes.cpp`)
   to sources. Verify `aes.hpp` / `aes.c` have no Windows includes.
3. `LuaEngineBuiltins.cpp` — call `luaopen_aes` on both platforms.
4. Change `[WindowsOnlyFact]` → `[Fact]` for both tests.

---

## Task 12 — Mutex module on Linux (POSIX named semaphores)

**Tests unlocked:**
- `Mutex_Open_LockAndUnlock_Succeeds`
- `Mutex_Info_ReturnsNameAndLockedState`

**Problem:**
`LuaMutex.cpp` uses `CreateMutex` / `OpenMutex` / `WaitForSingleObject` /
`ReleaseMutex` / `CloseHandle`. No Linux path exists.

**Fix:**
Add a POSIX named-semaphore implementation behind `#ifdef _WIN32` / `#else`:

```cpp
#else  // Linux
#include <semaphore.h>
#include <fcntl.h>
typedef struct {
    sem_t* sem;
    char   name[256];
    bool   locked;
} LuaMutex;

// Open:  sem_open(name, O_CREAT, 0666, 1)
// Lock:  sem_timedwait or sem_trywait + nanosleep loop for timeout
// Unlock: sem_post
// Close: sem_close + optionally sem_unlink
#endif
```

`CMakeLists.txt` — add `LuaMutex.cpp` and `LuaMutexMain.cpp`.
Link `-lrt` if needed (some Linux distros require it for `sem_open`):
`target_link_libraries(KitsuneEngine PRIVATE ... rt)`.
`LuaEngineBuiltins.cpp` — call `luaopen_mutex` on both platforms.
Change `[WindowsOnlyFact]` → `[Fact]` for both tests.

---

## Task 13 — Session Console / Display APIs on Linux (terminal)

**Tests unlocked:**
- `GetScreenSize_ReturnsTwoNumbers`
- `GetCursorPosition_ReturnsTwoNumbers`
- `GetCursorPointPosition_ReturnsTwoNumbers`
- `GetTextColor_ReturnsTwoValuesOrNilWhenNoConsole`
- `GetKeyState_ReturnsBoolean`

**Problem:**
All `Session.Display.*` and `Session.Console.GetColor` / `GetKeyState` are guarded
`#ifdef _WIN32` in `LuaEngineBuiltins.cpp`.

**Fix (Linux):**

| Function | Linux implementation |
|---|---|
| `GetScreenSize` | `ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws)` → `ws.ws_col`, `ws.ws_row` |
| `GetCursorPosition` | Write `\033[6n`, read back `\033[row;colR` from stdin |
| `GetCursorPoint` | Same as `GetCursorPosition` (pixel position is not meaningful in terminal; return character cell coords) |
| `GetColor` | Return `nil, nil` when not a TTY; on a TTY the foreground/background colour is not trivially readable — return `nil, nil` as the test accepts that |
| `GetKeyState` | Return `false` always (no reliable cross-platform key-state API without X11/ncurses) |

All functions require `#include <sys/ioctl.h>`, `<termios.h>`, `<unistd.h>`.
The test for `GetTextColor` already accepts `nil, nil` so returning that on Linux is valid.
The test for `GetKeyState` checks `type(...) == 'boolean'` so returning `false` is valid.
Change `[WindowsOnlyFact]` → `[Fact]` for all five tests after implementing.

---

## Out of scope — permanently Windows-only

These tests exercise Win32 concepts with no portable equivalent.
They remain `[WindowsOnlyFact]` indefinitely.

| Test | Reason |
|---|---|
| `GetRegistryValue_KnownKey_ReturnsNonEmptyString` | Windows Registry |
| `GetRegistryValue_NonExistentKey_ReturnsNilAndError` | Windows Registry |
| `ResList_IsTable` | Windows PE resource section |
| `Clipboard_SetAndGet_RoundTrip` | Windows/X11/Wayland clipboard — too fragmented and headless-unfriendly to test reliably on CI |

---

## Suggested execution order

| Priority | Task | Effort | Tests unlocked |
|---|---|---|---|
| 1 | Task 5 — CSV streaming | Trivial (attr change only) | 3 |
| 2 | Task 1 — MD5 | Very low (registration only) | 2 |
| 3 | Task 2 — GetLastError | Low (`errno` / `strerror_r`) | 2 |
| 4 | Task 3 — GetIsAdmin | Trivial (`geteuid()`) | 1 |
| 5 | Task 4 — GlobalMemoryStatus | Low (`/proc/meminfo`) | 1 |
| 6 | Task 11 — AES | Low (header fix only) | 2 |
| 7 | Task 10 — SQLite | Low (CMake + link) | 3 |
| 8 | Task 6 — Stream.Open | Medium (temp-path in tests) | 7 |
| 9 | Task 7 — Compress/Decompress | Medium (zlib implementation) | 10 |
| 10 | Task 8 — Timer | Medium (`clock_gettime` port) | 4 |
| 11 | Task 9 — FileSystem | High (full POSIX rewrite) | 4 |
| 12 | Task 13 — Session terminal | High (terminal escape sequences) | 5 |
| 13 | Task 12 — Mutex | High (POSIX semaphores) | 2 |
