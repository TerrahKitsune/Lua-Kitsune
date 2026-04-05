# HTTP Client + WebSocket Implementation Plan (libcurl)

## Decision

Replace the entire existing HTTP module (`Http.cpp`, `HttpCoroutine.cpp`, `HttpMain.cpp`,
`Http.h`, `HttpMain.h`) with a single **libcurl**-based implementation that works identically
on Linux and Windows. There is no blocking request path — all I/O is non-blocking via the
libcurl multi interface, integrated cooperatively with the Kitsune coroutine scheduler.

The old files are **deleted**. The old API (`Http.Start`, `Http.CoStart`, `Http.GetResult`,
`Http.Wait`, `Http.GetRaw`, `Http.GetStatus`, `Http.SetTimeout`) is **removed**.
`Http.UrlEncode` and `Http.UrlDecode` are **kept** — they have no networking dependency.

---

## Target Lua API

All network calls must be made from within a Kitsune-managed coroutine. Functions
that perform I/O yield cooperatively to the scheduler; no OS thread is ever blocked.

---

### Client creation and configuration

```lua
local client = Http.Create()

client:SetTimeout(5000)                          -- ms; 0 = no timeout (default)
client:SetDefaultHeader("Accept", "application/json")
client:SetFollowRedirects(true)                  -- default true
client:SetVerifySSL(true)                        -- default true
```

Settings are per-client and inherited by every request, stream, and WebSocket
connection issued from that client instance.

---

### Buffered HTTP request — `client:Request()`

Returns `(coroutine, nil)` on success, or `(nil, errstr)` on immediate failure (bad
URL, OOM, etc.). The coroutine yields `nil` while in-flight and returns the complete
result table exactly once when the response has been fully received.

#### GET

```lua
local co, err = client:Request("GET", "https://api.example.com/data")
if not co then
	error(err)
end

local ok, result = coroutine.resume(co)
while ok and not result do
	ok, result = coroutine.resume(co)
end
assert(ok, result)

if not result.Code then
	error(result.Status)  -- transport failure: timeout, DNS, TLS, connection refused, …
end

print(result.Code)      -- integer HTTP status code, e.g. 200
print(result.Status)    -- HTTP reason phrase, e.g. "OK"
print(result.Contents)  -- response body as a string
for k, v in pairs(result.Headers) do
	print(k, v)
end
```

#### POST with a string body

```lua
local co, err = client:Request(
	"POST",
	"https://api.example.com/items",
	'{"name":"foo"}',
	{ ["Content-Type"] = "application/json" })
```

#### POST/PUT with a stream body (large upload)

```lua
-- curl reads from the stream via CURLOPT_READFUNCTION.
-- Content-Length is set automatically when the stream supports STREAM_CAP_SEEK.
local upload = Stream.OpenFile("payload.bin", "rb")
local co, err = client:Request("PUT", "https://example.com/resource", upload, headers)
```

#### GET with a stream sink (large download)

```lua
-- The write callback pipes data directly into 'out'.
-- result.Contents is nil; all data lands in the stream.
-- outStream must be a native-backend stream (vtbl != NULL).
-- Stream.Create(fn) Lua-function backends are rejected at call time.
local out = Stream.OpenFile("download.bin", "wb")
local co, err = client:Request("GET", "https://example.com/bigfile", nil, nil, out)
if not co then
	error(err)
end

local ok, result = coroutine.resume(co)
while ok and not result do
	ok, result = coroutine.resume(co)
end
assert(ok, result)

if not result.Code then
	error(result.Status)
end
-- result.Contents is nil when outStream was provided; data is already in 'out'.
```

#### Result table fields

| Field | Type | Notes |
|---|---|---|
| `Code` | `integer` or `nil` | HTTP status code. `nil` = transport error (see `Status`). |
| `Status` | `string` | Reason phrase (e.g. `"OK"`) or a transport error message when `Code` is `nil`. |
| `Contents` | `string` or `nil` | Full response body. `nil` when a stream sink was provided as arg 5. |
| `Headers` | `table` | Response headers as `{[key] = value}` string pairs. |

---

### Streaming HTTP request — `client:Stream()`

Returns `(LuaStream, nil)` or `(nil, errstr)`. The stream has
`STREAM_CAP_READ` and is registered under the `STREAM` metatable.
Unlike `client:Request()`, the connection stays open and data is delivered chunk by
chunk as it arrives — no full-response buffering occurs.

#### SSE / chunked streaming

```lua
local stream, err = client:Stream("GET", "https://api.example.com/events", nil, {
	Accept = "text/event-stream",
})
if not stream then
	error(err)
end

-- GetInfo yields cooperatively until response headers arrive.
-- Returns a table with Code, Status, Headers — never has a Contents field.
local info = stream:GetInfo()
if not info.Code then
	error(info.Status)
end
print(info.Code, info.Status)
for k, v in pairs(info.Headers) do
	print(k, v)
end

-- Read yields when no data is available.
-- Returns a string chunk on success; nil at EOF (server closed the connection).
local chunk = stream:Read()
while chunk do
	io.write(chunk)   -- process chunk: parse SSE events, JSON lines, etc.
	chunk = stream:Read()
end
```

`client:Stream()` accepts the same body argument as `client:Request()`: nil, a string,
or a `Stream` input — so POST/PUT streaming requests are supported identically.

#### When to use `client:Stream()` vs `client:Request()`

| Use case | Right tool | Reason |
|---|---|---|
| Full JSON / XML response | `client:Request()` | Must buffer whole body to parse anyway; buffered path is simpler |
| Large file download to disk | `client:Request()` + `streamOutput` | C write callback runs without Lua overhead; no chunk loop needed |
| SSE / EventSource | `client:Stream()` | Connection stays open; process each event as it arrives |
| JSONL / newline-delimited JSON | `client:Stream()` | Parse one line at a time; never buffer the full stream |
| Custom binary streaming protocol | `client:Stream()` | Process raw bytes chunk by chunk in Lua |
| WebSocket | `client:Connect()` | Bidirectional; frame-by-frame I/O maps naturally to stream |

#### True streaming patterns — no buffering required

```lua
-- ── SSE: process each event chunk as it arrives ───────────────────────────────
local stream, err = client:Stream("GET", "https://api.example.com/events", nil, {
	Accept = "text/event-stream",
})
if not stream then error(err) end
stream:GetInfo()

local chunk = stream:Read()
while chunk do
	-- Each chunk may contain partial or multiple SSE events.
	-- Buffer across chunks and parse per the SSE spec in Lua.
	process_sse_chunk(chunk)
	chunk = stream:Read()
end

-- ── JSONL (newline-delimited JSON): one object per line ──────────────────────
local stream, err = client:Stream("GET", "https://api.example.com/stream.jsonl")
if not stream then error(err) end
stream:GetInfo()

local buf = ""
local chunk = stream:Read()
while chunk do
	buf = buf .. chunk
	-- Extract and decode all complete lines from the accumulated buffer.
	local pos = 1
	while true do
		local nl = buf:find("\n", pos, true)
		if not nl then break end
		local line = buf:sub(pos, nl - 1)
		if #line > 0 then
			process(Json.Decode(line))  -- small individual line, no stream needed
		end
		pos = nl + 1
	end
	buf = buf:sub(pos)
	chunk = stream:Read()
end

-- ── Large download to disk: use client:Request() with a stream sink ───────────
-- The C write callback runs without any Lua overhead — no chunk loop needed.
local out = Stream.OpenFile("bigfile.bin", "wb")
local co, err = client:Request("GET", "https://example.com/bigfile.bin", nil, nil, out)
if not co then error(err) end
local ok, result = coroutine.resume(co)
while ok and not result do ok, result = coroutine.resume(co) end
assert(ok, result)
```

`stream:ReadAll([limit])` exists for **small to moderate-size responses** (kilobytes to
low megabytes) where you happen to use `client:Stream()` but need the complete data
before processing. Do NOT use it for large data — it allocates the full body in memory
and defeats the purpose of streaming entirely.

```lua
-- Acceptable: small config endpoint, response fits comfortably in memory
local stream = client:Stream("GET", "https://api.example.com/config")
stream:GetInfo()
local config = Json.Decode(stream:ReadAll())
```

---

### WebSocket — `client:Connect()`

Returns `(LuaStream, nil)` or `(nil, errstr)`. The stream has
`STREAM_CAP_READ | STREAM_CAP_WRITE` and is registered under a
`LUAWEBSOCKET` sub-metatable that inherits all standard stream methods via `__index`
chaining to the `STREAM` module table. `lua_isstream` accepts both metatables.

`client:Connect()` yields cooperatively until the WebSocket handshake completes, then
returns the live connection. From Lua's perspective it behaves like a blocking call.

#### Sending frames

```lua
local ws, err = client:Connect("wss://echo.websocket.org")
if not ws then
	error(err)
end

ws:Write("hello")           -- sends a text frame; synchronous, does not yield
ws:WriteBinary(binaryData)  -- sends a binary frame; synchronous, does not yield
```

#### Receiving frames

```lua
-- Read yields until the next complete frame (or fragment) arrives.
-- Returns the frame payload as a string, or nil when the connection is closed.
local frame = ws:Read()
while frame do
	-- GetInfo returns metadata for the frame that was just read.
	local meta = ws:GetInfo()
	-- meta.Binary    (boolean) — true for binary frames, false for text
	-- meta.Opcode    (integer) — 1=text  2=binary  8=close  9=ping  10=pong
	-- meta.BytesLeft (integer) — 0 = frame complete; >0 = fragmented, read again

	if meta.BytesLeft > 0 then
		-- Fragmented frame: accumulate frame payloads until BytesLeft == 0.
	elseif meta.Binary then
		-- Handle binary frame payload
	else
		-- Handle text frame payload
		print(frame)
	end

	frame = ws:Read()
end
-- frame == nil: connection closed by the remote end
```

#### Closing and cleanup

```lua
ws:Close()   -- sends a WebSocket close frame
			 -- __gc also calls Close automatically if it was never called
```

#### Compose with stream utilities

Because `ws` is a `LuaStream`, standard stream operations work on it:

```lua
-- Pipe all WebSocket data to a file until the server closes the connection:
local ws2, _ = client:Connect("wss://example.com/feed")
local sink    = Stream.OpenFile("feed.bin", "wb")
local f = ws2:Read()
while f do
	sink:Write(f)
	f = ws2:Read()
end
ws2:Close()
sink:Close()
```

---

### Complete method reference

**`Http` module**

| Function | Returns | Description |
|---|---|---|
| `Http.Create()` | `LuaHttpClient` | Creates a new client with default settings. |
| `Http.UrlEncode(str)` | `string` | Percent-encodes a string. |
| `Http.UrlDecode(str)` | `string` | Decodes a percent-encoded string. |

**`LuaHttpClient` methods**

| Method | Returns | Description |
|---|---|---|
| `client:Request(method, url [,body [,headers [,outStream]]])` | `(co, nil)` or `(nil, err)` | Buffered request. `body` = nil / string / `Stream`. `outStream` = optional write `Stream`. |
| `client:Stream(method, url [,body [,headers]])` | `(LuaStream, nil)` or `(nil, err)` | Streaming request. Returns an async-readable `LuaStream`. |
| `client:Connect(url [,headers])` | `(LuaStream, nil)` or `(nil, err)` | WebSocket. Yields until handshake complete, then returns async read/write `LuaStream`. |
| `client:SetTimeout(ms)` | — | Timeout in ms. 0 = none. |
| `client:SetDefaultHeader(key, value)` | — | Header sent with every request. |
| `client:SetFollowRedirects(bool)` | — | Follow `3xx` redirects (default `true`). |
| `client:SetVerifySSL(bool)` | — | Verify TLS certificates (default `true`). |

**HTTP stream** — `client:Stream()` result (`STREAM_CAP_READ`; async signalled by `vtbl->hasdata != NULL`)

| Method | Returns | Description |
|---|---|---|
| `stream:GetInfo()` | `table` | Yields until response headers arrive. Returns `{Code, Status, Headers}` (1 table). On transport error: `{Status = errstr}` with no `Code`. Note: on sync streams `GetInfo()` returns 2 values (Caps table + info table); on async streams it always returns 1. |
| `stream:Read([len])` | `string` or `nil` | Yields until a chunk is available. `nil` = EOF. `len` is advisory. **Only this method yields** — `lua_stream_read_chunk` (used by JSON/CSV/Compress) does not. |
| `stream:ReadAll([limit])` | `LuaStream` | Reads via `Read()` (yields) until EOF or `limit` bytes. Returns a seekable in-memory stream. **For small/moderate responses only** — allocates the full body. |
| `stream:HasData()` | `boolean` | Non-blocking: returns `true` if at least one chunk is ready to deliver without yielding. Calls `curl_multi_perform` internally. |
| `stream:Close()` | — | Cancels the request; also called by `__gc`. |

**WebSocket stream** — `client:Connect()` result (`STREAM_CAP_READ | STREAM_CAP_WRITE`; async signalled by `vtbl->hasdata != NULL`; inherits standard stream methods)

| Method | Returns | Description |
|---|---|---|
| `ws:Read([len])` | `string` or `nil` | Yields until next frame (or fragment). `nil` = closed. |
| `ws:HasData()` | `boolean` | Non-blocking frame availability check. Calls `curl_multi_perform` internally. |
| `ws:ReadAll([limit])` | `LuaStream` | Reads frames until closed or limit reached. Returns a memory stream. |
| `ws:Write(data)` | `boolean` | Sends a text frame. Synchronous. |
| `ws:WriteBinary(data)` | `boolean` | Sends a binary frame. Synchronous. |
| `ws:GetInfo()` | `table` | Frame metadata after the last `Read`: `{Binary, Opcode, BytesLeft}`. Does not yield (data is already in the native struct). |
| `ws:Close()` | — | Sends close frame; also called by `__gc`. |

---

## libcurl Requirements

| Minimum version | Reason |
|---|---|
| **7.86.0** | `curl_ws_recv` / `curl_ws_send` / WebSocket scheme support |
| 8.0.0 | `CURLWS_NOBLOCK` flag (non-blocking recv without manual socket manipulation) |

7.86.0 is the hard minimum for this implementation. WebSocket support must be compiled
into the libcurl binary (configure flag `--enable-websockets`; vcpkg feature `websockets`).

### Getting libcurl

**Windows (vcpkg):**
```
vcpkg install curl[websockets]:x64-windows
```

**Linux (build from source if distro package lacks WebSocket support):**
```bash
./configure --with-openssl --enable-websockets
make && sudo make install
```

---

## Architecture

### CURLM* lifetime

A single `CURLM*` multi handle is created inside `luaopen_http` and stored in the Lua
registry under a unique static-address key (`g_curlm_key`). It is shared by every
`LuaHttpRequest` and `LuaWebSocket` in the process. A sentinel userdata with a `__gc`
metamethod cleans it up when the Lua state is closed by `lua_close`.

```
luaopen_http(L)
  ├─ curl_multi_init()  →  store at registry[&g_curlm_key] as lightuserdata
  └─ register sentinel userdata at registry[&g_sentinel_key] with __gc = CleanupMulti
```

All curl operations execute on the Kitsune scheduler thread — no locking is needed.

### Coroutine model for HTTP requests

```
client:Request(method, url, body, headers)
  1.  Allocate LuaHttpRequest; configure CURL* easy handle
  2.  curl_multi_add_handle(curlm, easy)
  3.  lua_newthread → coroutine T
  4.  Push HttpRequestEntry + LuaHttpRequest userdata onto T
  5.  lua_resume(T, L, 1, &n)   ← kicks off first continuation pass
  6.  Return (T, nil)

HttpRequestContinuation(L, status, ctx):
  ├─ curl_multi_perform(curlm, &running)
  ├─ curl_multi_info_read() — is our handle done?
  │   ├─ NO  → lua_yieldk(L, 0, 0, HttpRequestContinuation)
  │   └─ YES → BuildHttpResultTable(L, req)  → return 1
  └─ on transport error → return {Status = errorBuf}  (no Code field)
```

Each scheduler resume of the coroutine advances I/O for **all** in-flight handles
(because `curl_multi_perform` is global to the multi handle), not just the current one.
This means multiple concurrent requests all make progress even when only one coroutine
is being polled.

---

## New Files

### `HttpCurl.h`

```cpp
#pragma once
#include "lua_main_incl.h"
#include <curl/curl.h>

#define LUAHTTPCLIENT  "LuaHTTPClient"
#define LUAHTTPREQUEST "LuaHTTPRequest"
#define LUAWEBSOCKET   "LuaWebSocket"

typedef struct LuaHttpClient {
	int    defaultHeadersRef;  // LUA_NOREF = none
	long   timeoutMs;          // 0 = no timeout
	bool   followRedirects;    // default true
	bool   verifySsl;          // default true
} LuaHttpClient;

typedef struct LuaHttpRequest {
	CURL*  easy;
	CURLM* multi;              // borrowed; do NOT free here
	// callbackL: Lua state stored just before curl_multi_perform so that
	// ReadBodyCallback can call vtbl->read (which requires a lua_State*).
	// Always valid during callbacks because Kitsune is single-threaded and
	// curl_multi_perform is only ever called from within a Lua continuation.
	lua_State* callbackL;
	// Response body — one of the two is active, never both
	char*  body;               // NULL when streamOutput is set
	size_t bodyLen;
	size_t bodyAlloc;
	LuaStream* streamOutput;   // optional; write callback pipes here instead of body
	int    streamOutputRef;    // registry ref keeping the stream alive during the request
	// Request body from stream
	LuaStream* streamInput;    // optional; read callback reads from here instead of string
	int    streamInputRef;     // registry ref
	// Accumulated response headers (parallel arrays)
	char** headerKeys;
	char** headerVals;
	int    headerCount;
	int    headerAlloc;
	// Status
	long   httpCode;           // 0 until curl reports completion
	char   statusText[256];    // parsed from the HTTP status line
	char   errorBuf[CURL_ERROR_SIZE];
	bool   addedToMulti;
} LuaHttpRequest;

typedef struct LuaWebSocket {
	CURL*  easy;
	CURLM* multi;              // borrowed
	char   errorBuf[CURL_ERROR_SIZE];
	bool   connected;
	bool   closed;
} LuaWebSocket;

int luaopen_http(lua_State* L);
```

### `HttpCurl.cpp` — structure

```
luaopen_http(L)
  ├─ curl_multi_init() → store in registry
  ├─ register sentinel __gc to call curl_multi_cleanup
  ├─ luaL_newmetatable(LUAHTTPCLIENT)   — functions[] + meta[] wchar pattern
  ├─ luaL_newmetatable(LUAHTTPREQUEST)  — __gc removes from multi, frees buffers
  ├─ luaL_newmetatable(LUAWEBSOCKET)    — __gc closes socket
  └─ build Http module table:
       { Create, UrlEncode, UrlDecode }
       return 1

LuaHttpClient methods (registered on functions[]):
  Request(method, url, body, headers)  → (co, nil) | (nil, errstr)
  Connect(url, headers)                → (ws, nil) | (nil, errstr)   [WebSocket]
  SetTimeout(ms)
  SetDefaultHeader(key, value)
  SetFollowRedirects(bool)
  SetVerifySSL(bool)

LuaHttpClient meta[]:
  __gc, __tostring

Static helpers:
  WriteBodyCallback     — CURLOPT_WRITEFUNCTION; appends to req->body OR writes to req->streamOutput
  ReadBodyCallback      — CURLOPT_READFUNCTION; reads from req->streamInput (stream upload path)
  WriteHeaderCallback   — CURLOPT_HEADERFUNCTION; parses "Key: Value\r\n"
  HttpRequestContinuation(L, status, ctx)
  BuildHttpResultTable(L, req)  — pushes {Code, Status, Contents, Headers}; Contents is nil when streamOutput is set
  WsConnectContinuation(L, status, ctx)
  WsRecvContinuation(L, status, ctx)
```

---

## Implementation Details

### `WriteBodyCallback`

When `req->streamOutput` is set the callback writes directly into the stream via its
vtable (sync path only — native-backend streams only; Lua fn backends are rejected at
call time). Otherwise it accumulates bytes in the heap buffer.

```cpp
static size_t WriteBodyCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
	LuaHttpRequest* req = (LuaHttpRequest*)userdata;
	size_t total = size * nmemb;
	if (req->streamOutput && req->streamOutput->vtbl && req->streamOutput->vtbl->write) {
		// Write directly into the caller-supplied stream via the C vtable.
		// curl callbacks are always invoked from curl_multi_perform, which is
		// called from within a Lua continuation — Kitsune is single-threaded.
		bool ok = req->streamOutput->vtbl->write(req->streamOutput->native,
													(const BYTE*)ptr, total);
		return ok ? total : 0;  // 0 signals an error to curl
	}
	// Heap-buffer accumulation path.
	if (req->bodyLen + total + 1 > req->bodyAlloc) {
		size_t newAlloc = req->bodyLen + total + 8192;
		char* nb = (char*)gff_realloc(req->body, newAlloc);
		if (!nb)
			return 0;
		req->body = nb;
		req->bodyAlloc = newAlloc;
	}
	memcpy(req->body + req->bodyLen, ptr, total);
	req->bodyLen += total;
	req->body[req->bodyLen] = '\0';
	return total;
}
```

### `ReadBodyCallback`

Used when `req->streamInput` is set (stream upload path). `vtbl->read` requires a
`lua_State*`; `callbackL` is set to the current state just before each
`curl_multi_perform` call so callbacks have access to it.

```cpp
static size_t ReadBodyCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
	LuaHttpRequest* req = (LuaHttpRequest*)userdata;
	if (!req->streamInput || !req->streamInput->vtbl || !req->callbackL)
		return 0;
	size_t capacity = size * nitems;
	StreamRead(req->callbackL, req->streamInput, capacity);  // pushes string or nil
	lua_State* L = req->callbackL;
	if (lua_type(L, -1) != LUA_TSTRING) {
		lua_pop(L, 1);
		return 0;  // EOF signals end of upload to curl
	}
	size_t nread = 0;
	const char* data = lua_tolstring(L, -1, &nread);
	if (nread > capacity) nread = capacity;
	memcpy(buffer, data, nread);
	lua_pop(L, 1);
	return nread;
}
```

In `HttpRequestContinuation` and all other places that call `curl_multi_perform`,
set `req->callbackL = L` first and clear it after:

```cpp
req->callbackL = L;
curl_multi_perform(req->multi, &running);
req->callbackL = NULL;  // prevent dangling use outside continuation context
```

### `WriteHeaderCallback`

Receives one header line per call (including the status line and trailing `\r\n`).

```cpp
static size_t WriteHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
	LuaHttpRequest* req = (LuaHttpRequest*)userdata;
	size_t total = size * nitems;
	// Status line: "HTTP/1.1 200 OK\r\n"
	if (strncmp(buffer, "HTTP/", 5) == 0) {
		// extract reason phrase after the status code
		char* space = strchr(buffer + 5, ' ');
		if (space) {
			space = strchr(space + 1, ' ');  // skip past the code
			if (space) {
				size_t len = total - (size_t)(space + 1 - buffer) - 2;  // strip \r\n
				if (len > 0 && len < sizeof(req->statusText))
					memcpy(req->statusText, space + 1, len);
				req->statusText[len] = '\0';
			}
		}
		return total;
	}
	// Skip blank separator line
	if (total <= 2)
		return total;
	// Parse "Key: Value\r\n"
	char* colon = (char*)memchr(buffer, ':', total);
	if (!colon)
		return total;
	// grow header arrays as needed, copy key and value
	// ... (standard dynamic array growth with gff_malloc/gff_realloc)
	return total;
}
```

### `HttpRequestContinuation`

```cpp
static int HttpRequestContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaHttpRequest* req = (LuaHttpRequest*)luaL_checkudata(L, 1, LUAHTTPREQUEST);
	int running = 0;
	curl_multi_perform(req->multi, &running);
	int msgsLeft;
	CURLMsg* msg;
	while ((msg = curl_multi_info_read(req->multi, &msgsLeft)) != NULL) {
		if (msg->msg == CURLMSG_DONE && msg->easy_handle == req->easy) {
			req->addedToMulti = false;
			curl_multi_remove_handle(req->multi, req->easy);
			curl_easy_getinfo(req->easy, CURLINFO_RESPONSE_CODE, &req->httpCode);
			return BuildHttpResultTable(L, req);
		}
	}
	return lua_yieldk(L, 0, 0, HttpRequestContinuation);
}
```

### `BuildHttpResultTable`

When `req->streamOutput` is set, `Contents` is omitted from the result table entirely
(the data is already in the stream). `luaL_unref` is called here to release the
registry anchors for both stream refs so the GC can collect them once the coroutine dies.

```cpp
static int BuildHttpResultTable(lua_State* L, LuaHttpRequest* req) {
	// Release stream registry anchors — the streams are no longer needed by the request.
	if (req->streamOutputRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, req->streamOutputRef);
		req->streamOutputRef = LUA_NOREF;
	}
	if (req->streamInputRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, req->streamInputRef);
		req->streamInputRef = LUA_NOREF;
	}
	lua_newtable(L);
	if (req->httpCode > 0) {
		lua_pushstring(L, "Code");
		lua_pushinteger(L, req->httpCode);
		lua_settable(L, -3);
		lua_pushstring(L, "Status");
		lua_pushstring(L, req->statusText);
		lua_settable(L, -3);
		if (!req->streamOutput) {
			// Body was buffered — include Contents in the result.
			lua_pushstring(L, "Contents");
			lua_pushlstring(L, req->body ? req->body : "", req->bodyLen);
			lua_settable(L, -3);
		}
		// Contents is intentionally absent when streamOutput was used;
		// the caller already has the data in the stream they passed in.
		lua_pushstring(L, "Headers");
		lua_newtable(L);
		for (int i = 0; i < req->headerCount; i++) {
			lua_pushstring(L, req->headerKeys[i]);
			lua_pushstring(L, req->headerVals[i]);
			lua_settable(L, -3);
		}
		lua_settable(L, -3);
	} else {
		// Transport failure — no Code field
		lua_pushstring(L, "Status");
		lua_pushstring(L, req->errorBuf[0] ? req->errorBuf : "request failed");
		lua_settable(L, -3);
	}
	return 1;
}
```

### `client:Request()` setup

```cpp
static int client_request(lua_State* L) {
	LuaHttpClient* client = (LuaHttpClient*)luaL_checkudata(L, 1, LUAHTTPCLIENT);
	const char* method  = luaL_checkstring(L, 2);
	const char* url     = luaL_checkstring(L, 3);
	// Arg 4: body — string, Stream userdata, or nil
	size_t bodyLen = 0;
	const char* body = NULL;
	LuaStream* streamInput  = NULL;
	LuaStream* streamOutput = NULL;

	if (lua_isstring(L, 4)) {
		body = lua_tolstring(L, 4, &bodyLen);
	} else if (lua_isuserdata(L, 4) && lua_isstream(L, 4)) {
		streamInput = (LuaStream*)lua_touserdata(L, 4);
	}

	// Arg 5 (optional): output stream for the response body
	if (lua_isuserdata(L, 5) && lua_isstream(L, 5))
		streamOutput = (LuaStream*)lua_touserdata(L, 5);

	// Retrieve the shared CURLM* from the registry
	lua_rawgetp(L, LUA_REGISTRYINDEX, &g_curlm_key);
	CURLM* multi = (CURLM*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	// Allocate LuaHttpRequest userdata
	LuaHttpRequest* req = (LuaHttpRequest*)lua_newuserdata(L, sizeof(LuaHttpRequest));
	memset(req, 0, sizeof(LuaHttpRequest));
	luaL_setmetatable(L, LUAHTTPREQUEST);
	int reqIdx = lua_gettop(L);

	req->multi = multi;
	req->easy  = curl_easy_init();
	if (!req->easy) {
		lua_pushnil(L);
		lua_pushstring(L, "curl_easy_init failed");
		return 2;
	}

	// Merge default headers from client + per-call headers (arg 5 = L[5])
	// outStream is arg 6 (L[6]) per signature: Request(method, url, body, headers, outStream)
	struct curl_slist* hdrs = NULL;
	// ... populate hdrs from client->defaultHeadersRef and stack arg 5 (L[5]) ...

	curl_easy_setopt(req->easy, CURLOPT_URL,           url);
	curl_easy_setopt(req->easy, CURLOPT_CUSTOMREQUEST, method);
	curl_easy_setopt(req->easy, CURLOPT_HTTPHEADER,    hdrs);
	curl_easy_setopt(req->easy, CURLOPT_WRITEFUNCTION, WriteBodyCallback);
	curl_easy_setopt(req->easy, CURLOPT_WRITEDATA,     req);
	curl_easy_setopt(req->easy, CURLOPT_HEADERFUNCTION,WriteHeaderCallback);
	curl_easy_setopt(req->easy, CURLOPT_HEADERDATA,    req);
	curl_easy_setopt(req->easy, CURLOPT_ERRORBUFFER,   req->errorBuf);
	curl_easy_setopt(req->easy, CURLOPT_FOLLOWLOCATION, client->followRedirects ? 1L : 0L);
	curl_easy_setopt(req->easy, CURLOPT_SSL_VERIFYPEER, client->verifySsl ? 1L : 0L);
	if (client->timeoutMs > 0)
		curl_easy_setopt(req->easy, CURLOPT_TIMEOUT_MS, (long)client->timeoutMs);
	// Anchor stream refs in the registry so the GC cannot collect them mid-request.
	if (streamInput) {
		lua_pushvalue(L, 4);
		req->streamInput    = streamInput;
		req->streamInputRef = luaL_ref(L, LUA_REGISTRYINDEX);
		curl_easy_setopt(req->easy, CURLOPT_UPLOAD,       1L);
		curl_easy_setopt(req->easy, CURLOPT_READFUNCTION, ReadBodyCallback);
		curl_easy_setopt(req->easy, CURLOPT_READDATA,     req);
		// If the stream is seekable, tell curl the exact size; otherwise -1 = chunked.
		curl_off_t uploadSize = -1;
		if (streamInput->Caps & STREAM_CAP_SEEK) {
			// getlen gives total length; curpos gives current position.
			// Bytes remaining = len - pos (upload starts from current position).
			lua_Integer pos = streamInput->vtbl->curpos
								? streamInput->vtbl->curpos(streamInput->native) : 0;
			lua_Integer len = streamInput->vtbl->getlen
								? streamInput->vtbl->getlen(streamInput->native) : 0;
			uploadSize = (curl_off_t)(len - pos);
			// getlen gives total length; curpos gives current position.
			// Bytes remaining = len - pos (upload starts from current position).
			lua_Integer pos = streamInput->vtbl->curpos
								? streamInput->vtbl->curpos(streamInput->native) : 0;
			lua_Integer len = streamInput->vtbl->getlen
								? streamInput->vtbl->getlen(streamInput->native) : 0;
			uploadSize = (curl_off_t)(len - pos);
		}
		curl_easy_setopt(req->easy, CURLOPT_INFILESIZE_LARGE, uploadSize);
	} else if (body && bodyLen > 0) {
		curl_easy_setopt(req->easy, CURLOPT_POSTFIELDS,    body);
		curl_easy_setopt(req->easy, CURLOPT_POSTFIELDSIZE, (long)bodyLen);
	}

	// Arg 6 (optional): output stream for the response body
	if (lua_isuserdata(L, 6) && lua_isstream(L, 6)) {
		streamOutput = lua_toluastream(L, 6);
		if (!streamOutput->vtbl || !streamOutput->vtbl->write) {
			lua_pushnil(L);
			lua_pushstring(L, "outStream must be a native-backend stream (vtbl->write != NULL)");
			return 2;
		}
	}
	if (streamOutput) {
		lua_pushvalue(L, 6);
		req->streamOutput    = streamOutput;
		req->streamOutputRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	curl_multi_add_handle(multi, req->easy);
	req->addedToMulti = true;

	// Create the coroutine and start the first continuation pass
	lua_State* T = lua_newthread(L);
	lua_pushvalue(L, reqIdx);
	lua_xmove(L, T, 1);          // LuaHttpRequest is arg 1 on coroutine stack
	lua_pushcfunction(T, HttpRequestEntry);
	lua_insert(T, 1);             // entry function is below the userdata
	// ... lua_resume to prime the continuation ...

	// Return the coroutine to Lua
	return 1;  // T is on top of L's stack
}
```

### `ChunkNode` — streaming response chunk queue

```cpp
typedef struct ChunkNode {
	char*       data;
	size_t      len;
	struct ChunkNode* next;
} ChunkNode;
```

`client:Stream()` uses its own write and header callbacks that populate the chunk queue
and set `headersComplete`:

```cpp
// Write callback for client:Stream() — enqueues received bytes as ChunkNodes.
static size_t WriteStreamBodyCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
	LuaHttpStreamNative* h = (LuaHttpStreamNative*)userdata;
	size_t total = size * nmemb;
	ChunkNode* node = (ChunkNode*)gff_malloc(sizeof(ChunkNode));
	if (!node) return 0;
	node->data = (char*)gff_malloc(total);
	if (!node->data) { gff_free(node); return 0; }
	memcpy(node->data, ptr, total);
	node->len  = total;
	node->next = NULL;
	if (h->chunkTail) h->chunkTail->next = node;
	else              h->chunkHead = node;
	h->chunkTail = node;
	return total;
}

// Header callback for client:Stream() — mirrors WriteHeaderCallback but targets
// LuaHttpStreamNative and sets headersComplete when the blank separator is seen.
static size_t WriteStreamHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
	LuaHttpStreamNative* h = (LuaHttpStreamNative*)userdata;
	size_t total = size * nitems;
	// Status line
	if (strncmp(buffer, "HTTP/", 5) == 0) {
		// parse httpCode and statusText into h->...
		return total;
	}
	// Blank line — headers are complete
	if (total <= 2) {
		h->headersComplete = true;
		return total;
	}
	// Parse "Key: Value\r\n" into h->headerKeys[]/headerVals[]
	// ... (same growth pattern as WriteHeaderCallback) ...
	return total;
}
```

### WebSocket connect + receive

`client:Connect(url, headers)`:
1. Allocate `LuaWebSocket`; configure easy handle with `CURLOPT_CONNECT_ONLY = 2L` and the `wss://` URL.
2. `curl_multi_add_handle`.
3. Call `lua_yieldk(L, 0, 0, WsConnectContinuation)` directly from `client_connect`.
   This works because `client:Connect()` must be called from within a Kitsune-managed
   coroutine. The ws userdata is placed at a known stack position before the yield.
4. `WsConnectContinuation` calls `curl_multi_perform` and polls `curl_multi_info_read`.
   When `CURLMSG_DONE` arrives: set `ws->connected = true`, push the ws userdata, return 1.

```cpp
static int WsConnectContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);
	int running = 0;
	curl_multi_perform(ws->multi, &running);
	int msgsLeft;
	CURLMsg* msg;
	while ((msg = curl_multi_info_read(ws->multi, &msgsLeft)) != NULL) {
		if (msg->msg == CURLMSG_DONE && msg->easy_handle == ws->easy) {
			if (msg->data.result != CURLE_OK) {
				lua_pushnil(L);
				lua_pushstring(L, ws->errorBuf[0] ? ws->errorBuf : "connect failed");
				return 2;
			}
			ws->connected = true;
			lua_pushvalue(L, 1);  // ws userdata is arg 1
			return 1;
		}
	}
	return lua_yieldk(L, 0, 0, WsConnectContinuation);
}
```

`ws:Read()` follows the identical vtable pattern as `http_stream_read` — no separate
coroutine is needed. `ws_stream_read` is registered as `g_wsStreamVtable.read`:

```cpp
static int WsReadContinuation(lua_State* L, int status, lua_KContext ctx);

static int ws_stream_read(void* native, lua_State* L, size_t len) {
	return WsReadContinuation(L, LUA_OK, 0);
}

static int WsReadContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaStream* s   = lua_toluastream(L, 1);
	LuaWebSocketNative* ws = (LuaWebSocketNative*)s->native;
	if (ws->closed) { lua_pushnil(L); return 1; }
	char buf[65536];
	size_t nrecv = 0;
	const struct curl_ws_frame* meta = NULL;
	CURLcode rc = curl_ws_recv(ws->easy, buf, sizeof(buf), &nrecv, &meta);
	if (rc == CURLE_AGAIN)
		return lua_yieldk(L, 0, 0, WsReadContinuation);
	if (rc != CURLE_OK) { lua_pushnil(L); return 1; }  // closed/error → nil
	// Cache frame metadata for the next ws:GetInfo() call.
	ws->lastFrameFlags = meta->flags;
	ws->lastBytesLeft  = meta->bytesleft;
	if (meta->flags & CURLWS_CLOSE) { ws->closed = true; lua_pushnil(L); return 1; }
	lua_pushlstring(L, buf, nrecv);
	return 1;
}

`ws:Send(data, isBinary)`:

```cpp
static int ws_send(lua_State* L) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);
	size_t len = 0;
	const char* data = luaL_checklstring(L, 2, &len);
	int isBinary = lua_toboolean(L, 3);
	size_t nsent = 0;
	unsigned int flags = isBinary ? CURLWS_BINARY : CURLWS_TEXT;
	CURLcode rc = curl_ws_send(ws->easy, data, len, &nsent, 0, flags);
	if (rc != CURLE_OK) {
		lua_pushnil(L);
		lua_pushstring(L, curl_easy_strerror(rc));
		return 2;
	}
	lua_pushboolean(L, 1);
	return 1;
}
```

`ws:Close()` sends a close frame and marks the socket closed:

```cpp
static int ws_close(lua_State* L) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);
	if (!ws->closed) {
		size_t nsent = 0;
		curl_ws_send(ws->easy, "", 0, &nsent, 0, CURLWS_CLOSE);
		ws->closed = true;
	}
	return 0;
}
```

---

## Module Registration (wchar pattern)

```cpp
static const luaL_Reg httpclient_functions[] = {
	{ "Request",            client_request },
	{ "Stream",             client_stream },   // ← streaming request; returns LuaStream
	{ "Connect",            client_connect },
	{ "SetTimeout",         client_set_timeout },
	{ "SetDefaultHeader",   client_set_default_header },
	{ "SetFollowRedirects", client_set_follow_redirects },
	{ "SetVerifySSL",       client_set_verify_ssl },
	{ NULL, NULL }
};

static const luaL_Reg httpclient_meta[] = {
	{ "__gc",       luahttpclient_gc },
	{ "__tostring", luahttpclient_tostring },
	{ NULL, NULL }
};

static const luaL_Reg http_module[] = {
	{ "Create",    http_create },
	{ "UrlEncode", UrlEncode },
	{ "UrlDecode", UrlDecode },
	{ NULL, NULL }
};

int luaopen_http(lua_State* L) {
	// CURLM* sentinel and registry setup
	CURLM* multi = curl_multi_init();
	lua_pushlightuserdata(L, multi);
	lua_rawsetp(L, LUA_REGISTRYINDEX, &g_curlm_key);
	// ... register sentinel __gc for cleanup ...

	// LuaHttpClient metatable
	luaL_newmetatable(L, LUAHTTPCLIENT);
	luaL_setfuncs(L, httpclient_meta, 0);
	lua_pushliteral(L, "__index");
	luaL_newlibtable(L, httpclient_functions);
	luaL_setfuncs(L, httpclient_functions, 0);
	lua_rawset(L, -3);
	lua_pop(L, 1);

	// LuaHttpRequest metatable (internal; no Lua-visible methods)
	luaL_newmetatable(L, LUAHTTPREQUEST);
	lua_pushcfunction(L, luahttprequest_gc);
	lua_setfield(L, -2, "__gc");
	lua_pop(L, 1);

	// LuaWebSocket metatable
	// ... same wchar pattern for ws_functions / ws_meta ...

	// Http module table
	luaL_newlibtable(L, http_module);
	luaL_setfuncs(L, http_module, 0);
	return 1;
}
```

---

## Changes to Existing Files

### `KitsuneEngine.cpp`

1. Replace the `#ifdef _WIN32` block for Http includes and registration with `#ifdef KITSUNE_HTTP`:

```cpp
#ifdef KITSUNE_HTTP
#include "HttpCurl.h"
#endif
```

```cpp
// In KitsuneInit, before lua_newstate:
#ifdef KITSUNE_HTTP
curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
```

```cpp
// Registration (no longer under #ifdef _WIN32):
#ifdef KITSUNE_HTTP
luaopen_http(L); lua_setglobal(L, "Http");
#endif
```

```cpp
// In KitsuneCleanup, after lua_close:
#ifdef KITSUNE_HTTP
curl_global_cleanup();
#endif
```

2. Remove the OpenSSL manual init/cleanup block — `curl_global_init(CURL_GLOBAL_DEFAULT)` initializes OpenSSL internally when curl is built with the OpenSSL backend:

```cpp
// DELETE these from KitsuneInit:
SSL_load_error_strings();
SSL_library_init();
OpenSSL_add_all_algorithms();

// DELETE these from KitsuneCleanup:
ERR_free_strings();
EVP_cleanup();

// DELETE this from KitsuneCleanup:
GetHttpBuffer(0);

// DELETE the openssl includes:
#include "openssl/err.h"
#include "openssl/evp.h"
#include "openssl/ssl.h"
```

   **Prerequisite audit:** Before deleting these, grep the other Win32-only modules
   (`LuaFTPMain`, `RedisMain`, `luakafkamain`, `LuaArchiveMain`) for direct OpenSSL
   API calls (`SSL_`, `EVP_`, `ERR_`). If any module calls OpenSSL directly, keep the
   init/cleanup or move responsibility into that module's own `luaopen_*`. If none do,
   the `openssl/` include directory and link libraries can be removed from the vcxproj
   entirely once `HttpCurl.cpp` is in place.

3. WSAStartup / WSACleanup: keep these — other modules (Kafka, FTP, Redis) still need WinSock on Windows.

### `CMakeLists.txt`

Add after the existing optional-module blocks:

```cmake
# ── Optional HTTP + WebSocket support (libcurl) ───────────────────────────────
option(KITSUNE_HTTP "Build with HTTP/WebSocket support (requires libcurl >= 7.86.0 with --enable-websockets)" OFF)

if(KITSUNE_HTTP)
	find_package(CURL 7.86.0 REQUIRED)
	target_sources(KitsuneEngine PRIVATE HttpCurl.cpp)
	target_compile_definitions(KitsuneEngine PRIVATE KITSUNE_HTTP)
	target_link_libraries(KitsuneEngine PRIVATE CURL::libcurl)
endif()
```

Remove the existing `Http.cpp`, `HttpCoroutine.cpp` entries from `KITSUNE_ENGINE_SOURCES`
(they are deleted).

### `Kitsune.vcxproj`

- Remove `Http.cpp`, `HttpCoroutine.cpp`, `HttpMain.cpp`, `Http.h`, `HttpMain.h` from the project.
- Add `HttpCurl.cpp` / `HttpCurl.h`.
- Add to Additional Include Directories: path to the curl `include/` directory.
- Add to Additional Dependencies: `libcurl.lib`.
- Add `KITSUNE_HTTP` to Preprocessor Definitions.
- Remove `openssl/` includes from Additional Include Directories if no other module uses them.

---

## Files to Delete

| File | Reason |
|---|---|
| `Http.h` | Replaced by `HttpCurl.h` |
| `Http.cpp` | Replaced by `HttpCurl.cpp` |
| `HttpCoroutine.cpp` | Replaced by `HttpCurl.cpp` |
| `HttpMain.h` | Replaced by `HttpCurl.h` |
| `HttpMain.cpp` | Replaced by `HttpCurl.cpp` |

---

## HTTP and WebSocket as Stream Backends

### Motivation

HTTP responses and WebSocket connections as first-class `LuaStream` objects gives:

- `ws:Read()` / `ws:Write()` — uniform, familiar interface for WebSocket bidirectional I/O
- `stream:Read()` loop — cooperative, non-blocking delivery of SSE events, JSONL lines, and raw binary protocol data as they arrive, without buffering the entire connection
- `ws:HasData()` / `stream:HasData()` — non-blocking availability check for manual polling

**What streaming does NOT provide automatically:** `lua_stream_read_chunk` (used internally by JSON/CSV/Compress) calls `StreamRead` synchronously and cannot yield. These consumers must use the `lua_callk` continuation pattern to support async sources — CSV and Compress already do this. JSON decode does not need it (see note below).

For large downloads that do not require chunk-by-chunk processing, `client:Request()` with a `streamOutput` argument is the right tool — the C write callback pipes data directly to the destination stream without any Lua overhead.

### The actual vtable — one unified `read`, no separate `readasync`/`infoasync`

The original plan called for separate `readasync` and `infoasync` vtable slots. The final implementation simplified this: the existing `read` and `info` slots are used for everything, with an updated signature that supports yielding.

```cpp
typedef struct LuaStreamVtable {
	// read: pushes a string on success or false on EOF/error at the vtable level.
	// ReadLuaStream (the Lua stream:Read() boundary) then normalises false → nil so
	// Lua code always sees nil at EOF. May call lua_yieldk for async backends.
	int         (*read)    (void* native, lua_State* L, size_t len);
	bool        (*write)   (void* native, const BYTE* data, size_t len);
	bool        (*setpos)  (void* native, lua_Integer pos);
	lua_Integer (*curpos)  (void* native);
	lua_Integer (*getlen)  (void* native);
	// L is provided so backends holding Lua registry refs can clean up.
	void        (*close)   (void* native, lua_State* L);
	// info: pushes a backend-specific info table. May call lua_yieldk for async
	// backends (e.g. HTTP waits for response headers to arrive).
	// Async vs sync detected by vtbl->hasdata != NULL; async returns 1 value,
	// sync returns 2 values (caps table + info table).
	int         (*info)    (void* native, lua_State* L);
	// hasdata: non-blocking availability check. 1 = data ready; 0 = not yet.
	// Must never yield. NULL = stream does not support this check.
	// Non-NULL presence also signals that vtbl->read and vtbl->info may yield.
	int         (*hasdata) (void* native);
} LuaStreamVtable;
```

All existing backends (`InMemoryStream`, `InFileStream`, shared-memory) are unaffected —
their `read` function signature changed but `hasdata` defaults to `NULL`.

### The C-call boundary rule — why `lua_stream_read_chunk` must NOT yield

`lua_yieldk` registers a continuation for one C frame and unwinds the stack. This means
yielding must only happen at **Lua method boundaries** where a continuation can be
registered. Internal C helpers called without a continuation would lose their local state
on yield.

Consequently:

| Call site | May yield? | Reason |
|---|---|---|
| `stream:Read()` → `ReadLuaStream` | ✅ | Lua method boundary; dispatches to `vtbl->read` which may call `lua_yieldk` |
| `lua_stream_read_chunk` (internal C helper) | ❌ | Called by JSON decode, Compress sync path — no continuation support |
| `stream:GetInfo()` → `GetStreamInfo` | ✅ | Lua method boundary; `vtbl->info` may call `lua_yieldk` |

### Changes to `stream.cpp` — dispatch at Lua method boundaries only

**`ReadLuaStream` (the `stream:Read()` Lua method)** — all streams dispatched through `vtbl->read`:

```cpp
int ReadLuaStream(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (!(s->Caps & STREAM_CAP_READ)) { lua_pushnil(L); return 1; }
	if (s->vtbl == NULL) {
		// Lua fn backend: lua_callk so Sleep works inside the READ handler.
		size_t len = (size_t)luaL_optinteger(L, 2, 0);
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->backendRef);
		lua_pushinteger(L, STREAM_OP_READ);
		lua_pushinteger(L, (lua_Integer)len);
		lua_callk(L, 2, 1, 0, fn_read_cont);
		return fn_read_cont(L, LUA_OK, 0);
	}
	StreamRead(L, s, (size_t)luaL_optinteger(L, 2, 0));
	if (lua_type(L, -1) != LUA_TSTRING) { lua_pop(L, 1); lua_pushnil(L); }
	return 1;
}
```

For async HTTP/WebSocket backends, `vtbl->read` calls `lua_yieldk` internally.
For sync backends it returns immediately. `lua_stream_read_chunk` (used by JSON, CSV sync
path, Compress sync path) is **unchanged** — it continues calling `StreamRead` → `vtbl->read`
without any yield.

**`GetStreamInfo` (the `stream:GetInfo()` Lua method)** — `vtbl->hasdata != NULL` selects return count:

```cpp
int GetStreamInfo(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (s->vtbl && s->vtbl->hasdata) {
		// Async/network streams: vtbl->info may yield (e.g. HTTP waits for headers).
		// Signalled by vtbl->hasdata being non-NULL — no separate cap flag needed.
		// Returns 1 value — the stream-specific info table.
		if (s->vtbl->info)
			return s->vtbl->info(s->native, L);
		lua_pushnil(L);
		return 1;
	}
	// Sync: returns 2 values — Caps table + backend info table. Unchanged.
	lua_createtable(L, 0, 1);
	lua_pushinteger(L, s->Caps);
	lua_setfield(L, -2, "Caps");
	if (s->vtbl) {
		s->vtbl->info(s->native, L);
	} else {
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->backendRef);
		lua_pushinteger(L, STREAM_OP_INFO);
		lua_call_nohook(L, 1, 1);
	}
	return 2;
}
```

**`HasDataLuaStream` (new `stream:HasData()` method)** — three-way dispatch:

```cpp
// 0 → false, 1 → true, n>1 → integer (byte count, e.g. network buffer size)
static void push_hasdata_result(lua_State* L, lua_Integer n) {
	if (n <= 0)       lua_pushboolean(L, 0);
	else if (n == 1)  lua_pushboolean(L, 1);
	else              lua_pushinteger(L, n);
}

int HasDataLuaStream(lua_State* L) {
	LuaStream* s = lua_toluastream(L, 1);
	if (s->vtbl) {
		if (s->vtbl->hasdata) {
			// Async/network: dedicated non-blocking check via vtbl->hasdata.
			push_hasdata_result(L, s->vtbl->hasdata(s->native));
		} else {
			// Sync streams (memory, file, shmem): bytes remaining = len - pos.
			// Returns false at EOF, positive integer when data is buffered.
			lua_Integer pos = s->vtbl->curpos ? s->vtbl->curpos(s->native) : 0;
			lua_Integer len = s->vtbl->getlen ? s->vtbl->getlen(s->native) : 0;
			push_hasdata_result(L, len > pos ? len - pos : 0);
		}
	} else {
		// Lua fn backend: dispatch STREAM_OP_HASDATA = 8; pass result through.
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->backendRef);
		lua_pushinteger(L, STREAM_OP_HASDATA);
		lua_call_nohook(L, 1, 1);
	}
	return 1;
}
```

Register: `{ "HasData", HasDataLuaStream }` in `streamfunctions[]`.

### HTTP stream `hasdata` implementation

```cpp
static int http_stream_hasdata(void* native) {
	LuaHttpStreamNative* h = (LuaHttpStreamNative*)native;
	// Advance curl I/O so newly arrived chunks are enqueued.
	int running = 0;
	curl_multi_perform(h->multi, &running);
	// Drain CURLMSG_DONE
	int msgsLeft;
	CURLMsg* msg;
	while ((msg = curl_multi_info_read(h->multi, &msgsLeft)) != NULL) {
		if (msg->msg == CURLMSG_DONE && msg->easy_handle == h->easy) {
			h->done = true;
			h->addedToMulti = false;
			curl_multi_remove_handle(h->multi, h->easy);
		}
	}
	return (h->chunkHead != NULL || h->done) ? 1 : 0;
}
```

WebSocket uses the same chunk-queue pattern, so `ws_stream_hasdata` is identical in
structure.

### Making sync consumers streaming-capable — `lua_callk` is the key

`lua_yieldk` registers a continuation for ONE C frame and unwinds the stack. But `lua_callk`
does the inverse: it calls a function and registers a continuation **in the caller** that
runs if the callee yields, without unwinding the caller's frame.

```
lua_callk(L, nargs, nresults, ctx, continuation)
  If callee completes normally → returns here, continuation is never called
  If callee yields            → continuation(L, status, ctx) is called when resumed
								 Lua stack and upvalues are fully preserved
```

Crucially: when the continuation is called, it has access to the **same upvalues** as the
C closure that originally called `lua_callk`. From the Lua 5.4 reference: *"the continuation
function is called with the same thread, with the same stack, and with the same upvalues."*

This means `CsvStreamIterator` (a C closure with `LuaCsv` as upvalue 1) can register a
continuation that accesses `lua_upvalueindex(1)` directly — no registry lookup, no heap
allocation for state, no struct pointer to manage.

### CSV: implemented — unified `streamRef` path

**What was built** (differs from the original plan; no `STREAM_CAP_ASYNC` needed):

1. `LuaCsv` struct: single `int streamRef` (no `isAsync` flag, no `asyncStreamRef`).
   All streams — sync and async — are stored identically.

2. `WrapStreamIfNeeded`: validates readability only; no closure wrapping.
   All streams go through the `streamRef` path.

3. `CsvStreamIterator` / `CsvStreamContinuation`: calls `stream:Read()` via `lua_callk`
   for all streams. Sync backends complete the call immediately (continuation fires inline);
   async backends yield. No separate `isAsync` branch required.

4. Delimiter sniffing: after each chunk the buffer is scanned for a newline. When
   auto-detecting, the partial-line fix in `SniffDelimiter` ensures a delimiter-free
   trailing fragment does not break consistency detection.

5. `LuaCsvStreamStateGc`: unrefs `streamRef`.

```cpp
// Actual CsvStreamIterator (simplified):
static int CsvStreamIterator(lua_State* L) {
	LuaCsv* csv = (LuaCsv*)lua_touserdata(L, lua_upvalueindex(1));
	csv->streamL = L;

	if (csv->streamRef != LUA_NOREF) {
		// Fetch until newline or EOF (covers sniff and row parsing for all streams).
		bool hasRow = csv->streamDone;
		for (size_t i = (size_t)csv->streamPos; i < csv->streamLen && !hasRow; i++) {
			if (csv->streamBuf[i] == L'\n' || csv->streamBuf[i] == L'\r')
				hasRow = true;
		}
		if (!hasRow) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, csv->streamRef);
			lua_getfield(L, -1, "Read");
			lua_insert(L, -2);
			lua_callk(L, 1, 1, 0, CsvStreamContinuation);
			return CsvStreamContinuation(L, LUA_OK, 0);
		}
	}

	if (!DecodeOneRow(csv, L)) return 0;
	return 1;
}
```

`CSV.Read(httpStream)` works correctly for any async stream. The `lua_call_nohook`
path in `RefillStreamBuffer` is only reached by the legacy function-backend path.

### Compress/Decompress: implemented — unified `lua_callk` path

`CompressStream` now uses a **single** `lua_callk` path for all streams — no
`STREAM_CAP_ASYNC` gate, no separate sync for-loop. Sync backends complete the
`lua_callk` immediately (the continuation fires inline); async backends may yield.
`Decompress` accumulates via `lua_callk` when `vtbl->hasdata != NULL` (async),
or uses the direct `StreamRead` loop otherwise.

```cpp
// ctx packing:
//   bits 0-3: level (clamped to 0xF — miniz treats >9 as default).
//   bit    8: ownDst flag.
// NOTE: using only 4 bits for level avoids the MZ_DEFAULT_COMPRESSION = -1 bug:
// (unsigned int)(-1) = 0xFFFFFFFF would set bit 8 and corrupt ownDst.
#define COMPRESS_CTX(level, ownDst) \
	((lua_KContext)(((lua_KContext)(level) & 0xF) | ((ownDst) ? (1 << 8) : 0)))
#define COMPRESS_LEVEL(ctx)   ((int)((ctx) & 0xF))
#define COMPRESS_OWNDST(ctx)  (((ctx) >> 8) & 1)

// CompressStream—unified path (replaces the old sync for-loop + async branch):
if (src->Caps & STREAM_CAP_SEEK)
	StreamSetPosC(L, src, 0);        // rewind seekable sources only
// Push dst, then kick off the lua_callk chain:
lua_KContext ctx = COMPRESS_CTX(level, ownDst);
lua_pushvalue(L, 1);              // src
lua_getfield(L, -1, "Read");
lua_insert(L, -2);
lua_pushinteger(L, STREAM_COMPRESS_CHUNK);
lua_callk(L, 2, 1, ctx, CompressContinuation);
return CompressContinuation(L, LUA_OK, ctx);
```

All streams (memory, file, HTTP, WebSocket) use this path.
The async signal is `src->vtbl && src->vtbl->hasdata` — not a cap flag.

### JSON: why streaming isn't needed

The JSON decoder is a recursive descent parser. Making it continuation-aware would require
rewriting it as an iterative parser with an explicit parse stack stored on the heap — a
complete rewrite of `jsondecode.cpp`.

However, streaming JSON decode isn't useful for the primary HTTP use cases:

- **Large single JSON objects**: even if decoded in chunks, the resulting Lua table
  requires the same peak memory as the full body. There is no memory saving.
- **JSONL (newline-delimited JSON)**: each line is a small independent JSON object.
  `Json.Decode(line)` on each line already works. No streaming decoder needed.

The manual JSONL loop shown in **True streaming patterns** covers every real streaming
JSON use case. The JSON decoder does not need changes.

### Updated consequence table

| Consumer | With HTTP async stream | Status |
|---|---|---|
| `Json.Decode(stream)` | ❌ EOF immediately | **By design** — streaming JSON only needed for JSONL, which already works via manual `Read()` loop |
| `CSV.Read(stream)` | ✅ | **Done** — unified `streamRef` + `lua_callk` path handles all streams (sync and async) |
| `Stream.Compress(stream, dst)` | ✅ | **Done** — unified `lua_callk` path; works for sync and async sources |
| `Stream.Decompress(stream)` | ✅ | **Done** — accumulates via `lua_callk` for async sources (`vtbl->hasdata != NULL`) |
| Manual `stream:Read()` loop | ✅ | Works for all streams; async backends yield, sync return immediately |
| `stream:ReadAll()` | ✅ | Works today; small/moderate responses only |

### `stream:ReadAll([limit])` — utility for small/moderate responses

Reads chunks via `stream:Read()` (which yields cooperatively for async streams) until
EOF or `limit` bytes have been accumulated. Returns a new seekable in-memory `LuaStream`.

**This is a convenience for small/moderate responses** — configuration endpoints,
token payloads, small JSON objects — where you happen to use `client:Stream()` but need
the complete data before calling a sync consumer. It is **not** a solution for large
data; it allocates the full body in memory and defeats the purpose of streaming.


### HTTP streaming as a `LuaStream` backend

`client:Stream()` returns a `LuaStream*` under the `STREAM` metatable. The stream
backend uses `WriteStreamBodyCallback` and `WriteStreamHeaderCallback` (described above)
to populate the `ChunkNode` queue and set `headersComplete`. The stream userdata is
created immediately and returned to Lua; `curl_multi_perform` advances the connection
when `stream:Read()` or `stream:GetInfo()` yields.

```cpp
typedef struct LuaHttpStreamNative {
	CURL*      easy;
	CURLM*     multi;           // borrowed
	ChunkNode* chunkHead;
	ChunkNode* chunkTail;
	bool       headersComplete;
	bool       headersYielded;  // true after GetInfo() has surfaced Code/Status/Headers once
	long       httpCode;
	char       statusText[256];
	char**     headerKeys;
	char**     headerVals;
	int        headerCount;
	int        headerAlloc;
	char       errorBuf[CURL_ERROR_SIZE];
	bool       addedToMulti;
	bool       done;            // CURLMSG_DONE received
} LuaHttpStreamNative;

static const LuaStreamVtable g_httpStreamVtable = {
	.read      = http_stream_read,   // may call lua_yieldk; delivers buffered chunks
	.write     = NULL,               // HTTP responses are read-only
	.setpos    = NULL,
	.curpos    = NULL,
	.getlen    = NULL,
	.close     = http_stream_close,
	.info      = http_stream_info,   // yields until response headers arrive
	.hasdata   = http_stream_hasdata,
};
```

`luaopen_http` creates the stream userdata with:
```cpp
s->Caps = STREAM_CAP_READ;   // async signalled by vtbl->hasdata != NULL, no separate flag needed
s->vtbl = &g_httpStreamVtable;
```

### `http_stream_read` — the vtable read with cooperative yielding

```cpp
static int HttpStreamReadContinuation(lua_State* L, int status, lua_KContext ctx);

static int http_stream_read(void* native, lua_State* L, size_t len) {
	(void)len;  // network streams deliver chunks as they arrive; len is advisory
	return HttpStreamReadContinuation(L, LUA_OK, 0);
}

static int HttpStreamReadContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaStream* s = lua_toluastream(L, 1);            // arg 1 on coroutine stack
	LuaHttpStreamNative* h = (LuaHttpStreamNative*)s->native;

	int running = 0;
	curl_multi_perform(h->multi, &running);

	// Drain CURLMSG_DONE
	int msgsLeft;
	CURLMsg* msg;
	while ((msg = curl_multi_info_read(h->multi, &msgsLeft)) != NULL) {
		if (msg->msg == CURLMSG_DONE && msg->easy_handle == h->easy) {
			h->done = true;
			h->addedToMulti = false;
			curl_multi_remove_handle(h->multi, h->easy);
		}
	}

	// Deliver oldest chunk
	if (h->chunkHead) {
		ChunkNode* node = h->chunkHead;
		h->chunkHead = node->next;
		if (!h->chunkHead) h->chunkTail = NULL;
		lua_pushlstring(L, node->data, node->len);
		gff_free(node->data);
		gff_free(node);
		return 1;
	}

	// EOF
	if (h->done) {
		lua_pushnil(L);  // nil = EOF, matching ReadLuaStream's normalisation contract
		return 1;
	}

	// Still waiting — yield nil and retry on the next resume
	return lua_yieldk(L, 0, 0, HttpStreamReadContinuation);
}
```

### `http_stream_info` — headers

`stream:GetInfo()` returns the response metadata. Blocks (yields) until headers arrive:

```cpp
static int HttpStreamInfoContinuation(lua_State* L, int status, lua_KContext ctx);

static int http_stream_info(void* native, lua_State* L) {
	return HttpStreamInfoContinuation(L, 0, 0);
}

static int HttpStreamInfoContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaStream* s = lua_toluastream(L, 1);
	LuaHttpStreamNative* h = (LuaHttpStreamNative*)s->native;

	int running = 0;
	curl_multi_perform(h->multi, &running);

	if (!h->headersComplete && !h->done)
		return lua_yieldk(L, 0, 0, HttpStreamInfoContinuation);

	lua_newtable(L);
	if (h->httpCode > 0) {
		lua_pushinteger(L, h->httpCode);  lua_setfield(L, -2, "Code");
		lua_pushstring(L, h->statusText); lua_setfield(L, -2, "Status");
		lua_newtable(L);
		for (int i = 0; i < h->headerCount; i++) {
			lua_pushstring(L, h->headerKeys[i]);
			lua_pushstring(L, h->headerVals[i]);
			lua_settable(L, -3);
		}
		lua_setfield(L, -2, "Headers");
	} else {
		lua_pushstring(L, h->errorBuf[0] ? h->errorBuf : "request failed");
		lua_setfield(L, -2, "Status");
	}
	return 1;
}
```

### Updated Lua API for HTTP streaming and WebSocket

See the **Target Lua API** section at the top of this document for the complete
reference with annotated examples. The relevant entry points are:

- `client:Stream(method, url, body, headers)` → `LuaStream` (`STREAM_CAP_READ`; async via `vtbl->hasdata`)
  — `stream:GetInfo()`, `stream:Read()`, `stream:Close()`
- `client:Connect(url, headers)` → `LuaStream` (`STREAM_CAP_READ | STREAM_CAP_WRITE`; async via `vtbl->hasdata`)
  — `ws:Read()`, `ws:Write()`, `ws:WriteBinary()`, `ws:GetInfo()`, `ws:Close()`

### `LuaWebSocketNative` struct

```cpp
typedef struct LuaWebSocketNative {
	CURL*  easy;
	CURLM* multi;             // borrowed; used only during the connect phase
	char   errorBuf[CURL_ERROR_SIZE];
	bool   connected;
	bool   closed;
	// Last received frame metadata (valid after each successful Read)
	unsigned int lastFrameFlags;
	size_t       lastBytesLeft;
} LuaWebSocketNative;

static const LuaStreamVtable g_wsStreamVtable = {
	.read      = ws_stream_read,     // may call lua_yieldk; delivers frame payloads
	.write     = ws_stream_write,    // curl_ws_send with CURLWS_TEXT
	.setpos    = NULL,
	.curpos    = NULL,
	.getlen    = NULL,
	.close     = ws_stream_close,    // curl_ws_send CURLWS_CLOSE + curl_easy_cleanup
	.info      = ws_stream_info,     // {Binary, Opcode, BytesLeft} — synchronous, no yield
	.hasdata   = ws_stream_hasdata,
};
```

`ws:WriteBinary(data)` is a `LUAWEBSOCKET`-specific method registered on the
`LUAWEBSOCKET` sub-metatable (not on `STREAM`). It calls `curl_ws_send` with
`CURLWS_BINARY` instead of `CURLWS_TEXT`.

### Impact on `stream.cpp` / existing code

| Change | Scope |
|---|---|
| `STREAM_CAP_ASYNC` **removed** — async signalled by `vtbl->hasdata != NULL` instead | `stream.h` — no cap flag needed; cleaner vtable |
| `read` signature changed to `int (*read)(native, L, len)` — no `outLen`, no pointer return | `stream.h` + all backends — callers use `lua_tolstring` for C pointer when needed |
| `close` gains `lua_State* L` parameter | `stream.h` + all backends — existing backends ignore it |
| `readasync` and `infoasync` removed — `read` and `info` handle async via `lua_yieldk` | `stream.h` — vtable is smaller and simpler |
| `ReadLuaStream`: dispatches through `vtbl->read` for vtable streams; `lua_callk` for fn backends | `stream.cpp` — fn backends can now use `Sleep` inside `READ` handler |
| `GetStreamInfo`: `vtbl->hasdata != NULL` selects 1-value (async) vs 2-value (sync) return | `stream.cpp` — existing 2-value sync path unchanged |
| New `HasDataLuaStream` — 3-way dispatch (vtbl→hasdata, len−pos for sync, `STREAM_OP_HASDATA=8` for fn) | `stream.cpp` + `StreamMain.cpp` — new, no side-effects |
| `lua_isstream` accepts `LUAWEBSOCKET` metatable | `stream.cpp` — one extra `rawequal` check |
| `lua_stream_read_chunk` | **unchanged** — no async dispatch; used only by sync consumers |

No existing stream backends (`InMemoryStream`, `InFileStream`, shared-memory, Lua fn)
are touched or recompiled differently.

### `client:Stream()` implementation note

`client:Stream()` creates a `LuaStream` userdata registered under `STREAM`, not a
coroutine. `WriteStreamBodyCallback` and `WriteStreamHeaderCallback` (described in the
`ChunkNode` section above) are the internal mechanism — the Lua-visible interface is
entirely the standard stream `Read` / `GetInfo` API.

`client:Request()` (buffered, returns result table via coroutine) is unchanged.
Only `client:Stream()` and `client:Connect()` return streams.

---

## Open Questions

1. **`CURLWS_NOBLOCK`** — This flag (for non-blocking `curl_ws_recv`) was added in curl 8.x.
   For curl 7.86.x the alternative is to obtain the raw socket via `CURLINFO_ACTIVESOCKET`
   and call `fcntl(fd, F_SETFL, O_NONBLOCK)` / `ioctlsocket(fd, FIONBIO, &1)` manually.
   Decision needed: require curl ≥ 8.0, or support 7.86.x with the manual path?

3. **Windows curl distribution** — vcpkg (`curl[websockets]:x64-windows`) vs a bundled
   pre-built binary in the repo (matching how `openssl/` is currently bundled).
   vcpkg is cleaner for CI; bundled is easier for contributors without vcpkg.

4. **OpenSSL audit** — Audit `LuaFTPMain`, `RedisMain`, `luakafkamain`, `LuaArchiveMain`
   for direct OpenSSL calls before removing the `openssl/` directory and init/cleanup
   from `KitsuneEngine.cpp`.
