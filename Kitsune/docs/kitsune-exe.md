# kitsune.exe

`kitsune.exe` is a console-first Lua script runner built on top of [KitsuneEngine](../../kitsuneengine-lua-functions.md). It provides everything KitsuneEngine exposes (Redis, HTTP, databases, streams, etc.) plus an optional immediate-mode GUI layer powered by SDL2, OpenGL, and Dear ImGui. The GUI layer is compiled in when the `KITSUNE_IMGUI` preprocessor flag is set; without it the binary is a plain headless console program.

---

## How It Works

```
kitsune.exe [script.lua] [arg1 arg2 ...]
```

1. Initialises KitsuneEngine and registers all built-in functions.
2. Executes the supplied Lua script file (defaults to `main.lua` in the working directory) as a coroutine.
3. Waits for the coroutine to finish or an OS signal (`CTRL+C`, `SIGINT`, `SIGTERM`). While waiting, any functions enqueued with `Schedule(fn)` are polled each millisecond tick.
4. If the script called `Imgui.Start(...)` before returning, the render loop is entered and blocks until the window is closed.
5. Prints the script's return value to stdout (string, number, or boolean) then exits.

On Windows the console output code page is set to UTF-8 (CP 65001) automatically.

Exit code `0` = success; `1` = the script raised an uncaught error (message printed to stderr).

---

## Command-Line Usage

| Position | Value |
|---|---|
| `argv[1]` | Path to the Lua script. Defaults to `main.lua`. Accessible inside Lua as `ARGS[1]` |
| `argv[2..n]` | Extra arguments forwarded to the script as `ARGS[2]`, `ARGS[3]`, … |

```
kitsune.exe server.lua 8080 --verbose
```

Inside the script:
```lua
print(ARGS[1])  -- "server.lua"
print(ARGS[2])  -- "8080"
print(ARGS[3])  -- "--verbose"
```

---

## Modes of Operation

### Console / Headless Mode

The default. The script runs as a single cooperative coroutine. All KitsuneEngine APIs are available. No window is created.

```lua
-- hello.lua
print("Hello, world!")
```

```
> kitsune.exe hello.lua
Hello, world!
```

### GUI Mode (`KITSUNE_IMGUI`)

Triggered by calling `Imgui.Start(...)` from anywhere in the startup script. After the startup coroutine returns, kitsune automatically enters the SDL2+OpenGL render loop. The render callback then drives the UI every frame.

```lua
-- gui.lua
Imgui.Start("My Window", 1280, 720, function(renderer, ctx)
    renderer:Text("Hello from ImGui!")
    return true  -- keep running
end)
```

All SDL, OpenGL, and ImGui APIs documented in [imgui-renderer-api.md](imgui-renderer-api.md) become available once inside the render callback.

---

## Lua APIs Exclusive to kitsune.exe

These functions are registered by kitsune.exe itself on top of what KitsuneEngine provides. They are **not** available when embedding KitsuneEngine in another host.

---

### Session.Console

Cross-platform console I/O. Available in both headless and GUI builds.

| Function | Parameters | Returns | Platform | Notes |
|---|---|---|---|---|
| `Session.Console.Put(str)` | `string` | — | All | Print to stdout with `\r` → newline and `\b` → backspace-space-backspace handling |
| `Session.Console.GetKey()` | — | `integer` | All | Block and read one byte from stdin. Returns `-1` on EOF. On a TTY uses `_getch` (Windows) or blocking read (Linux) |
| `Session.Console.HasKeyDown()` | — | `boolean` | All | Non-blocking check: `true` if a key is waiting in stdin (TTY) or stdin is not at EOF (pipe) |
| `Session.Console.Write(str)` | `string` | `integer` | Windows | `WriteConsole` to stdout; returns bytes written |
| `Session.Console.ReadKey()` | — | `integer or nil` | Windows | Non-blocking: returns key code if one is ready, `nil` otherwise |
| `Session.Console.GetKeyState(vkey)` | `integer` | `boolean` | Windows | `GetAsyncKeyState` — `true` while the given virtual-key code is physically held |
| `Session.Console.SetColor(bg, fg)` | `integer, integer` | — | Windows | Set console text attributes. Each value is a 4-bit colour index (0–15) |
| `Session.Console.GetColor()` | — | `integer bg, integer fg` | Windows | Returns current background and foreground colour index nibbles |
| `Session.Console.SetVisible(bool)` | `boolean` | — | Windows | Show (`true`) or hide (`false`) the console window |
| `Session.Console.SetTitle(str)` | `string` | — | Windows | Set the console window title bar text |
| `Session.Console.Create()` | — | `boolean` | Windows | `AllocConsole` — creates a new console for this process |
| `Session.Console.Destroy()` | — | `boolean` | Windows | `FreeConsole` — detaches the console |
| `Session.Console.Attach(opt pid)` | `integer?` | `boolean` | Windows | `AttachConsole(pid)`. Omit `pid` to attach to the parent process |
| `Session.Console.Clear()` | — | — | Windows | Fill the screen buffer with spaces and reset cursor to origin |
| `Session.Console.GetInfo()` | — | `x, y, width, height, maxW, maxH` | Windows | Returns six integers from `CONSOLE_SCREEN_BUFFER_INFO`: cursor column, cursor row, buffer width, buffer height, max window width, max window height |
| `Session.Console.SetCursorPosition(x, y)` | `integer, integer` | — | Windows | Move the cursor to the given column and row |

**Windows colour index values** (standard 16-colour palette):

| Value | Colour |
|---|---|
| 0 | Black |
| 1 | Dark Blue |
| 2 | Dark Green |
| 3 | Dark Cyan |
| 4 | Dark Red |
| 5 | Dark Magenta |
| 6 | Dark Yellow |
| 7 | Gray |
| 8 | Dark Gray |
| 9 | Blue |
| 10 | Green |
| 11 | Cyan |
| 12 | Red |
| 13 | Magenta |
| 14 | Yellow |
| 15 | White |

```lua
-- Simple coloured banner
Session.Console.SetColor(0, 14)   -- black bg, yellow fg
Session.Console.Put("WARNING: something happened\r\n")
Session.Console.SetColor(0, 7)    -- reset to gray

-- Move cursor and overwrite a line in-place
local info = {Session.Console.GetInfo()}
Session.Console.SetCursorPosition(0, info[2])
Session.Console.Put(string.rep(" ", info[3]))   -- erase line
Session.Console.SetCursorPosition(0, info[2])
Session.Console.Put("Updated status line")
```

---

### Session.Display

Screen and cursor geometry. Always available; most values are `0` on non-Windows.

| Function | Parameters | Returns | Platform | Notes |
|---|---|---|---|---|
| `Session.Display.GetScreenSize()` | — | `integer width, integer height` | All | Primary screen resolution in pixels (Windows: `GetSystemMetrics`). On Linux returns terminal columns × rows via `TIOCGWINSZ` |
| `Session.Display.GetCursorPosition()` | — | `integer x, integer y` | All | Mouse cursor position **relative to the monitor** it is currently on (Windows). Returns `0, 0` on Linux |
| `Session.Display.GetCursorPoint()` | — | `integer x, integer y` | All | Raw screen-space mouse cursor pixel coordinates (Windows `GetCursorPos`). Returns `0, 0` on Linux |

---

### Session.Clipboard

| Function | Parameters | Returns | Platform | Notes |
|---|---|---|---|---|
| `Session.Clipboard.Set(str)` | `string or nil` | `boolean` | Windows | Copy a string to the clipboard. Pass `nil` or an empty string to clear the clipboard. Returns `true` on success |
| `Session.Clipboard.Get()` | — | `Wchar or nil` | Windows | Read the clipboard as a `Wchar` (Unicode text). Returns `nil` if empty or not available |

```lua
Session.Clipboard.Set("hello world")
local text = Session.Clipboard.Get()
if text then
    print(tostring(text))
end
```

---

### `Schedule`

Enqueues a Lua function to run as an independent coroutine. The coroutine is submitted and polled in the background — in headless mode once per millisecond tick, in GUI mode once per rendered frame — without blocking the caller.

```lua
Schedule(fn, arg1, arg2, ...)
```

| Parameter | Type | Description |
|---|---|---|
| `fn` | `function` | The coroutine to execute |
| `arg1..n` | any | Optional arguments forwarded to `fn` |

Errors raised inside `fn` are forwarded to the handler set by `Scheduler.SetOnError`, or printed to stderr if no handler is set.

`Schedule` works identically in both headless and GUI mode. If `Imgui.Start` is called, the same scheduler continues running inside the render loop so any in-flight coroutines from the startup script keep executing.

```lua
-- Fire-and-forget background work
Schedule(function()
    local result = Http.Get("https://example.com")
    print(result)
end)

-- With arguments
Schedule(function(url, timeout)
    local result = Http.Get(url, timeout)
    print(result)
end, "https://example.com", 5000)
```

---

### `Scheduler.SetOnError`

Sets a global error handler for all coroutines launched via `Schedule`. Replaces any previously set handler. Pass `nil` (or omit the argument) to clear the handler and fall back to stderr output.

```lua
Scheduler.SetOnError(fn)
```

| Parameter | Type | Description |
|---|---|---|
| `fn` | `function` or `nil` | Called with the error value when a scheduled coroutine faults. Pass `nil` to clear |

If `Imgui.Start` is called with an `onError` argument, that handler overwrites the one set here for the duration of the render loop.

```lua
Scheduler.SetOnError(function(err)
    print("Scheduled task failed:", err)
end)

Schedule(function()
    error("something went wrong")
end)
```

---

### `Imgui.Start`

See [imgui-renderer-api.md](imgui-renderer-api.md) — Global API section.

### `Imgui.Schedule`

Alias for `Schedule` that is only valid after `Imgui.Start` has been called. Enqueues a coroutine into the same shared scheduler. Prefer `Schedule` for code that must work in both headless and GUI modes.

See [`Schedule`](#schedule) above.

### SDL Audio

Available after `Imgui.Start` is called (requires `KITSUNE_IMGUI`).

```lua
SDL.Audio.Load(source)
SDL.Audio.LoadRaw(stream, source)
SDL.Audio.LoadMusic(source)
SDL.Audio.Unload(id)
SDL.Audio.Destroy(id)
SDL.Audio.DestroyMusic(id)
SDL.Audio.DestroyAll()
SDL.Audio.Play(id, opt loops, opt channel)
SDL.Audio.PlayMusic(id, opt loops)
SDL.Audio.Stop(opt channel)
SDL.Audio.StopMusic()
SDL.Audio.SetVolume(volume, opt channel)
SDL.Audio.SetMusicVolume(volume)
SDL.Audio.IsPlaying(opt channel)
SDL.Audio.IsMusicPlaying()
SDL.Audio.IsMusicPaused()
SDL.Audio.FadeIn(id, loops, ms, opt channel)
SDL.Audio.FadeOut(ms, opt channel)
SDL.Audio.PauseMusic()
SDL.Audio.ResumeMusic()
SDL.Audio.GetCurrentMusic()
SDL.Audio.GetId(source)
SDL.Audio.GetData(id)
```

Sound effects (`Load`) are decoded fully into memory as `Mix_Chunk`. Music (`LoadMusic`) is streamed. Both use the same resource-cache state machine as textures: **Live** → **Sentinel** (after `Unload`) → **Tombstone** (after `Destroy`).

`volume` is in the range `0–128` (SDL_mixer convention). `loops` is the number of additional repeats (`0` = play once, `-1` = loop forever).

### SDL Window, Input, and Timing

Documented in full in [imgui-renderer-api.md](imgui-renderer-api.md) — SDL API section.

### OpenGL Texture Cache

Documented in full in [imgui-renderer-api.md](imgui-renderer-api.md) — OpenGL API section.

### Win32 Console Helpers

Documented in [imgui-renderer-api.md](imgui-renderer-api.md) — Win32 API section.

---

## Signal Handling

| Platform | Signal | Effect |
|---|---|---|
| Windows | `CTRL+C`, `CTRL+BREAK`, window close | Sets exit flag, calls `KitsuneInterrupt()` |
| Windows | Logoff / shutdown | Same as above |
| Linux/macOS | `SIGINT` | Sets exit flag, calls `KitsuneInterrupt()` |
| Linux/macOS | `SIGTERM` | Same as above |

The engine interrupt causes any currently blocking coroutine operation to unblock and propagate an error, allowing the script to exit cleanly.

---

## Build Flags

| Flag | Effect |
|---|---|
| `KITSUNE_IMGUI` | Enables SDL2, OpenGL, Dear ImGui, SDL_mixer, and all `SDL.*` / `OpenGL.*` / `Imgui.*` Lua APIs |
| `KITSUNE_ALL` | Enables all optional KitsuneEngine modules (Kafka, MongoDB, etc.) |
| `_DEBUG` | Enables CRT heap leak detection and registers a `Test()` built-in function |

---

## Third-Party Notices

kitsune.exe incorporates the following libraries in addition to those listed in [KitsuneEngine Third-Party Notices](../../kitsuneengine-lua-functions.md#third-party-notices).

---

### Dear ImGui (v1.91.9b)

**Copyright © 2014–2025 Omar Cornut**

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

*License: [MIT](https://opensource.org/licenses/MIT) — <https://github.com/ocornut/imgui>*

---

### dear_bindings (dcimgui — auto-generated C bindings for Dear ImGui)

**Copyright © 2014–2025 Omar Cornut** (generated output derives from Dear ImGui)

Generated by the [dear_bindings](https://github.com/dearimgui/dear_bindings) tool. The generated output is subject to the same MIT license as Dear ImGui above.

*License: [MIT](https://opensource.org/licenses/MIT) — <https://github.com/dearimgui/dear_bindings>*

---

### SDL2 (Simple DirectMedia Layer)

**Copyright © 1997–2024 Sam Lantinga**

This software is provided 'as-is', without any express or implied warranty. In no event will the authors be held liable for any damages arising from the use of this software.

Permission is granted to anyone to use this software for any purpose, including commercial applications, and to alter it and redistribute it freely, subject to the following restrictions:

1. The origin of this software must not be misrepresented; you must not claim that you wrote the original software. If you use this software in a product, an acknowledgment in the product documentation would be appreciated but is not required.
2. Altered source versions must be plainly marked as such, and must not be misrepresented as being the original software.
3. This notice may not be removed or altered from any source distribution.

*License: [zlib License](https://www.zlib.net/zlib_license.html) — <https://www.libsdl.org>*

---

### SDL_mixer

**Copyright © 1997–2024 Sam Lantinga**

Licensed under the same zlib license as SDL2 above.

*License: [zlib License](https://www.zlib.net/zlib_license.html) — <https://github.com/libsdl-org/SDL_mixer>*

---

### librdkafka

**Copyright © 2012–2022 Magnus Edenhill**  
**Copyright © 2023 Confluent Inc.**

Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*License: [BSD 2-Clause](https://opensource.org/licenses/BSD-2-Clause) — <https://github.com/confluentinc/librdkafka>*

---

### OpenGL

OpenGL is an open standard maintained by [The Khronos Group](https://www.khronos.org/opengl/). No library is linked — the OS-provided OpenGL implementation is used at runtime via the system driver. No attribution is required for OpenGL itself.

The `imgui_impl_opengl3.cpp` backend is part of Dear ImGui and is covered by the Dear ImGui MIT license above.
