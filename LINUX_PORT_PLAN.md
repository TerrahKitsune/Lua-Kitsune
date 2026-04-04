# Linux Port Plan

Goal: get a barebones KitsuneEngine + Program.cpp building and running on Linux
(Docker / WSL2) without any custom modules (MySQL, PostgreSQL, ImGui, etc.).

All tests are marked `[WindowsOnlyFact]` and skip on Linux automatically.
Once the native `.so` builds and runs a bare `main.lua`, tests can be re-enabled
one module at a time.

Branch: `linuxport`

---

## Task checklist

### Task 1 - platform.h
**~1 hour | Foundation -- everything else depends on this file**

Create `platform.h` to replace `<Windows.h>` across the codebase.

- [ ] Type aliases: `DWORD`->`uint32_t`, `BOOL`->`int`, `LONG`->`long`, `BYTE`->`uint8_t`, `LPVOID`->`void*`, `LPSTR`->`char*`
- [ ] `ZeroMemory(p,n)` -> `memset(p,0,n)`, `CopyMemory` -> `memcpy`
- [ ] `Sleep(ms)` -> `usleep(ms * 1000)`
- [ ] `min`/`max` -> `std::min` / `std::max`
- [ ] `KITSUNE_API`: `__declspec(dllexport/dllimport)` -> `__attribute__((visibility("default")))` on Linux
- [ ] Keep `#ifdef _WIN32` guard so Windows path is unchanged

**Files:** `platform.h` (new)

---

### Task 2 - CMakeLists.txt + fix xp_lua_incl.h
**~2 hours | Gets cmake running so there is a build loop to iterate against**

- [ ] `CMakeLists.txt`: Lua static lib + KitsuneEngine shared lib (`-DKITSUNE_BAREBONES`) + kitsune exe
- [ ] `xp_lua_incl.h`: guard `#pragma warning` with `#ifdef _MSC_VER`
- [ ] `xp_lua_incl.h`: guard `LUA_BUILD_AS_DLL` / `LUA_CORE` with `#ifdef _WIN32`
- [ ] `xp_lua_incl.h`: guard `ImTextureID ImU64` with `#ifdef _WIN32`

**Files:** `CMakeLists.txt` (new), `xp_lua_incl.h`

---

### Task 3 - KITSUNE_BAREBONES guards in KitsuneEngine.cpp + LuaEngineBuiltins.cpp
**~2 hours | Strips compile scope to just the core scheduler**

`KitsuneEngine.cpp`:
- [ ] Wrap all module `#include` headers in `#ifndef KITSUNE_BAREBONES`
- [ ] Wrap all `luaopen_*` / module registration calls in `#ifndef KITSUNE_BAREBONES`

`LuaEngineBuiltins.cpp`:
- [ ] Replace `#include <Windows.h>` with `#include "platform.h"`
- [ ] Guard `<conio.h>` / `<io.h>` / `"Shellapi.h"` with `#ifdef _WIN32`
- [ ] Guard `_kbhit()` / `_getch()` with `#ifdef _WIN32`; stubs in `#else`
- [ ] Guard `GetConsoleScreenBufferInfo` with `#ifdef _WIN32`; push `nil, nil` in `#else`
- [ ] Guard `GlobalMemoryStatus`, `GetComputerName`, `GetIsAdmin`, clipboard, registry with `#ifdef _WIN32`

**Files:** `KitsuneEngine.cpp`, `LuaEngineBuiltins.cpp`

---

### Task 4 - Fix KitsuneEngine.h + guard DllMain
**~1 hour | Removes `<Windows.h>` from the public header**

- [ ] Replace `#include <Windows.h>` with `#include "platform.h"` in `KitsuneEngine.h`
- [ ] `BYTE flags` in `SharedMemoryBlock` -> `uint8_t flags`
- [ ] Guard `DllMain` in `KitsuneEngine.cpp` with `#ifdef _WIN32`
- [ ] Verify `KITSUNE_API` resolves via `platform.h`

**Files:** `KitsuneEngine.h`, `KitsuneEngine.cpp`

---

### Task 5 - volatile LONG -> std::atomic in KitsuneEngine.cpp
**~2 hours | Mechanical -- ~15 call sites, do all in one pass**

| Replace | With |
|---|---|
| `volatile LONG foo` | `std::atomic<long> foo{0}` |
| `volatile DWORD schedulerThreadId` | `std::atomic<uint32_t> schedulerThreadId{0}` |
| `InterlockedExchange(&foo, val)` | `foo.store(val)` |
| `InterlockedAdd(&foo, 0)` | `foo.load()` |
| `InterlockedIncrement(&foo)` | `++foo` |
| `InterlockedDecrement(&foo)` | `--foo` |
| `GetCurrentThreadId()` | guard with `#ifdef _WIN32` |

- [ ] Add `#include <atomic>` and `#include <thread>`

**Files:** `KitsuneEngine.cpp`

---

### Task 6 - WinEvent -> std::condition_variable
**~2 hours | Most algorithmically significant change -- self-contained within one struct**

Replace `WinEvent` (`CreateEvent`/`SetEvent`/`WaitForSingleObject`) with:

```cpp
struct PlatformEvent {
	std::mutex              mtx;
	std::condition_variable cv;
	bool                    signaled = false;

	void Set() {
		{ std::lock_guard<std::mutex> lk(mtx); signaled = true; }
		cv.notify_one();
	}
	void Wait() {
		std::unique_lock<std::mutex> lk(mtx);
		cv.wait(lk, [this] { return signaled; });
		signaled = false;
	}
	bool WaitFor(uint32_t ms) {
		std::unique_lock<std::mutex> lk(mtx);
		bool r = cv.wait_for(lk, std::chrono::milliseconds(ms), [this] { return signaled; });
		if (r)
			signaled = false;
		return r;
	}
};
```

- [ ] Replace `WinEvent` struct with `PlatformEvent`
- [ ] Update three `KitsuneState` members: `pausedEvent`, `resumeEvent`, `workEvent`
- [ ] Add `#include <mutex>`, `<condition_variable>`, `<chrono>`
- [ ] Remove `Create()` call from `KitsuneInit` (default ctor handles it)

**Files:** `KitsuneEngine.cpp`

---

### Task 7 - CreateThread -> std::thread
**~1 hour | Mechanical once Task 6 is done**

- [ ] `HANDLE schedulerThread` -> `std::thread schedulerThread`
- [ ] `DWORD WINAPI SchedulerProc(LPVOID param)` -> `static void SchedulerProc(KitsuneState* state)`
- [ ] `CreateThread(...)` -> `state->schedulerThread = std::thread(SchedulerProc, state)`
- [ ] `WaitForSingleObject(...) + CloseHandle` -> `state->schedulerThread.join()`
- [ ] Remove / guard `GetCurrentThreadId()` usage

**Files:** `KitsuneEngine.cpp`

---

### Task 8 - Port Program.cpp
**~1 hour | Small file, isolated changes**

| Windows code | Linux replacement |
|---|---|
| `volatile LONG g_exitSignaled` | `std::atomic<long> g_exitSignaled{0}` |
| `InterlockedExchange` / `InterlockedAdd` | `.store()` / `.load()` |
| `SetConsoleOutputCP(65001)` | `#ifdef _WIN32` guard |
| `BOOL WINAPI ConsoleCtrlHandler` + setter | `#ifdef _WIN32`; `#else signal(SIGINT, ...)` |
| `Sleep(1)` | `std::this_thread::sleep_for(std::chrono::milliseconds(1))` |
| `OutputDebugString` / `DebugBreak` | `#if defined(_WIN32) && defined(_DEBUG)` |
| `_CrtMemState` / `_CrtDumpMemoryLeaks` | `#if defined(_WIN32) && defined(_DEBUG)` |

- [ ] Add `#include "platform.h"`, `<atomic>`, `<thread>`, `<chrono>`, `<csignal>`

**Files:** `Program.cpp`

---

### Task 9 - Port luawchar.cpp / luawchar.h
**~2 hours | Most of this file is already cross-platform**

`luawchar.h`:
- [ ] Replace `#include <Windows.h>` with `#include "platform.h"`

`luawchar.cpp`:
- [ ] Replace `#include <windows.h>` with `#include "platform.h"`
- [ ] Replace remaining `WCHAR` with `wchar_t`
- [ ] `ZeroMemory(wchar, sizeof(LuaWChar))` -> `memset(wchar, 0, sizeof(LuaWChar))`
- [ ] `FromUtf8`: `MultiByteToWideChar` -> `#ifdef _WIN32` / `#else iconv` block
- [ ] `ToUtf8`: `WideCharToMultiByte` -> `#ifdef _WIN32` / `#else iconv` block
- [ ] `wchar_concat`: same for both `MultiByteToWideChar` call sites
- [ ] Add portable `utf8_to_wchar` / `wchar_to_utf8` helpers using `iconv` in `#else` branches

Note: `wchar_alloc_as_char16` / `char16_alloc_as_wchar` already have `#ifdef _WIN32` guards.

**Files:** `luawchar.h`, `luawchar.cpp`

---

### Task 10 - First clean Linux build
**~1-2 hours | Fix the residual cascade of GCC errors**

- [ ] `wsl --install -d Ubuntu-24.04` (requires reboot)
- [ ] `sudo apt update && sudo apt install -y build-essential cmake libiconv-hook-dev libssl-dev`
- [ ] `cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Debug -DKITSUNE_BAREBONES=1`
- [ ] `cmake --build build-linux --parallel` -- fix each error
- [ ] Common stragglers: transitive `<windows.h>`, `__cdecl`/`__stdcall`, `__pragma`, escaped `HANDLE`/`HWND`
- [ ] **Deliverable:** `./build-linux/kitsune main.lua` runs without crashing

**Files:** Various

---

## Progress tracker

| Task | Status | Notes |
|------|--------|-------|
| 1 - `platform.h` | Done | |
| 2 - `CMakeLists.txt` + `xp_lua_incl.h` | Done | |
| 3 - `KITSUNE_BAREBONES` guards | Done | |
| 4 - `KitsuneEngine.h` + `DllMain` | Done | |
| 5 - `Interlocked*` -> `std::atomic` | Done | |
| 6 - `WinEvent` -> `condition_variable` | Done | |
| 7 - `CreateThread` -> `std::thread` | Done | |
| 8 - `Program.cpp` | Done | |
| 9 - `luawchar.cpp` / `luawchar.h` | Done | |
| 10 - First clean Linux build | In progress | Pre-flight fixes applied; CMake run needed |

---

## Out of scope (barebones build) -- re-enable per module in later phases

| Module | Headers |
|---|---|
| MySQL | `MySQLMain`, `LuaMySQL` |
| PostgreSQL | `PostgresMain`, `LuaPostgres` |
| Redis | `Redis`, `RedisMain` |
| HTTP | `HttpMain` |
| Server / Client | `LuaServer`, `LuaClientMain` |
| Named pipes | `NamedPipeMain` |
| SQLite | `LuaSQLiteMain` |
| File async | `FileAsyncMain` |
| ImGui | `LuaImguiMain`, `LuaImgui`, `LuaImguiInterface` |
| Windows Services | `WinServicesMain` |
| ODBC | `ODBCMain` |
| Kafka | `luakafkamain` |
| FTP | `LuaFTPMain` |
| TTS | `LuaTTSMain` |
| Process | `ProcessMain` |
| Image | `LuaImageMain` |
| Game formats | ERF, TLK, 2DA, GFF modules |
| MainLoop hook | `MainLoop.cpp` (uses `VirtualProtect`) |
