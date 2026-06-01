#ifdef KITSUNE_HTTP

#include "LuaWebSocket.h"
#include "HttpCurl.h"
#include "LuaHttpRequest.h"
#include "LuaHttpServer.h"
#include "luaalivetoken.h"
#include "kitsune_internal.h"
#include "mem.h"

extern "C" {
#include <event2/ws.h>
#include <event2/bufferevent.h>
}
#include <curl/curl.h>
#include <curl/websockets.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
static uint64_t ws_now_ms() { return (uint64_t)GetTickCount64(); }
#else
#include <time.h>
static uint64_t ws_now_ms() {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}
#endif

// After WS_PING_AFTER_MS of silence from the client, send a ping frame to
// keep the TCP connection alive during long LLM generations.
// Pong frames are consumed internally by libevent and never reach evws_msg_cb,
// so we cannot use them as a liveness signal; dead-connection detection is
// left to the TCP stack (which will fire evws_close_cb when the connection drops).
#define WS_PING_AFTER_MS 30000u   // 30 s idle before sending a keepalive ping

static void set_did_work(lua_State* L) {
	void* ud;
	lua_getallocf(L, &ud);
	KitsuneState* state = (KitsuneState*)ud;
	if (!state)
		return;
	int id = (int)state->currentCoroutineId.load();
	KitsuneCoroutine* slot = FindSlot(state, id);
	if (slot)
		slot->didWork = true;
}

// Shared CURLM* registry key defined in HttpCurl.cpp
extern const char g_curlm_key;

// ---- global next-ID counter -------------------------------------------------
static lua_Integer g_next_ws_id = 1;

// ---- forward declarations ---------------------------------------------------
static void ws_dispose_native(lua_State* L, LuaWebSocket* ws);

// ---- message queue helpers --------------------------------------------------

static WsMsgNode* ws_msg_alloc(const char* data, size_t len, int type) {
	WsMsgNode* node = (WsMsgNode*)kitsune_malloc(sizeof(WsMsgNode));
	if (!node)
		return NULL;
	memset(node, 0, sizeof(WsMsgNode));
	node->type = type;
	if (data && len > 0) {
		node->data = (char*)kitsune_malloc(len);
		if (!node->data) {
			kitsune_free(node);
			return NULL;
		}
		memcpy(node->data, data, len);
		node->len = len;
	}
	return node;
}

static void ws_msg_free(WsMsgNode* node) {
	if (!node)
		return;
	kitsune_free(node->data);
	kitsune_free(node);
}

static void ws_enqueue(LuaWebSocket* ws, WsMsgNode* node) {
	if (!node)
		return;
	if (ws->msg_tail) {
		ws->msg_tail->next = node;
		ws->msg_tail = node;
	}
	else {
		ws->msg_head = node;
		ws->msg_tail = node;
	}
}

static WsMsgNode* ws_dequeue(LuaWebSocket* ws) {
	WsMsgNode* node = ws->msg_head;
	if (!node)
		return NULL;
	ws->msg_head = node->next;
	if (!ws->msg_head)
		ws->msg_tail = NULL;
	node->next = NULL;
	return node;
}

static void ws_drain_queue(LuaWebSocket* ws) {
	WsMsgNode* node = ws->msg_head;
	while (node) {
		WsMsgNode* next = node->next;
		ws_msg_free(node);
		node = next;
	}
	ws->msg_head = NULL;
	ws->msg_tail = NULL;
}

// ---- curl fragment reassembly helpers ---------------------------------------

static bool ws_frag_append(LuaWebSocket* ws, const char* data, size_t len) {
	if (ws->fragLen + len + 1 > ws->fragAlloc) {
		size_t newAlloc = ws->fragLen + len + 8192;
		char* nb = (char*)kitsune_realloc(ws->fragBuf, newAlloc);
		if (!nb)
			return false;
		ws->fragBuf = nb;
		ws->fragAlloc = newAlloc;
	}
	memcpy(ws->fragBuf + ws->fragLen, data, len);
	ws->fragLen += len;
	return true;
}

static void ws_frag_reset(LuaWebSocket* ws) {
	kitsune_free(ws->fragBuf);
	ws->fragBuf = NULL;
	ws->fragLen = 0;
	ws->fragAlloc = 0;
}

// ---- drain_curlm / curlm_step (duplicated minimally; extern in HttpCurl.cpp)
// These are file-static copies so LuaWebSocket.cpp does not need to expose them.

static void ws_drain_curlm(CURLM* multi) {
	int msgsLeft;
	CURLMsg* msg;
	while ((msg = curl_multi_info_read(multi, &msgsLeft)) != NULL) {
		if (msg->msg != CURLMSG_DONE)
			continue;
		CurlMsg* cm = NULL;
		curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, &cm);
		if (cm) {
			cm->done = true;
			cm->result = msg->data.result;
		}
	}
}

static void ws_curlm_step(CURLM* multi) {
	int running = 0;
	curl_multi_wait(multi, NULL, 0, 0, NULL);
	curl_multi_perform(multi, &running);
	ws_drain_curlm(multi);
}

// Send a text or binary WebSocket frame over the bufferevent (RFC 6455).
// evws_send() has a bug: for payloads > 65535 bytes it writes the 64-bit
// length indicator (127) but leaves the 8 length bytes as zeros, producing
// a malformed frame that browsers reject with a protocol error.
// We always build the frame header manually to avoid this.
static void ws_server_send_frame(struct evws_connection* evws,
	const char* data, size_t len, bool binary)
{
	struct bufferevent* bev = evws_connection_get_bufferevent(evws);
	if (!bev)
		return;
	unsigned char hdr[10];
	size_t hdrLen;
	// FIN bit (0x80) + opcode: 0x2 = binary, 0x1 = text
	hdr[0] = binary ? 0x82 : 0x81;
	if (len <= 125) {
		hdr[1] = (unsigned char)len;
		hdrLen = 2;
	}
	else if (len <= 0xFFFF) {
		hdr[1] = 126;
		hdr[2] = (unsigned char)(len >> 8);
		hdr[3] = (unsigned char)(len & 0xFF);
		hdrLen = 4;
	}
	else {
		hdr[1] = 127;
		uint64_t len64 = (uint64_t)len;
		hdr[2] = (unsigned char)(len64 >> 56);
		hdr[3] = (unsigned char)((len64 >> 48) & 0xFF);
		hdr[4] = (unsigned char)((len64 >> 40) & 0xFF);
		hdr[5] = (unsigned char)((len64 >> 32) & 0xFF);
		hdr[6] = (unsigned char)((len64 >> 24) & 0xFF);
		hdr[7] = (unsigned char)((len64 >> 16) & 0xFF);
		hdr[8] = (unsigned char)((len64 >> 8) & 0xFF);
		hdr[9] = (unsigned char)(len64 & 0xFF);
		hdrLen = 10;
	}
	bufferevent_write(bev, hdr, hdrLen);
	if (data && len > 0)
		bufferevent_write(bev, data, len);
}

// Send a WebSocket ping frame (opcode 0x9, no payload) over the bufferevent.
// The browser will reply with a pong; if it doesn't, the TCP stack will
// eventually time out the connection naturally.
static void ws_server_send_ping(struct evws_connection* evws) {
	struct bufferevent* bev = evws_connection_get_bufferevent(evws);
	if (!bev)
		return;
	unsigned char hdr[2];
	hdr[0] = 0x89; // FIN + opcode 0x9 (ping)
	hdr[1] = 0x00; // no payload, not masked (server→client is unmasked)
	bufferevent_write(bev, hdr, 2);
}

// ---- libevent evws callbacks ------------------------------------------------

static void evws_msg_cb(struct evws_connection* conn, int type,
	const unsigned char* data, size_t len, void* arg)
{
	LuaWebSocket* ws = (LuaWebSocket*)arg;
	if (!ws || ws->closed)
		return;
	if (ws->maxMsgSize > 0 && len > ws->maxMsgSize)
		return; // silently drop oversized messages
	// Any frame from the client resets the keepalive clock.
	ws->lastDataMs = ws_now_ms();
	int wstype = WS_TYPE_TEXT;
	if (type == WS_BINARY_FRAME)
		wstype = WS_TYPE_BINARY;
	WsMsgNode* node = ws_msg_alloc((const char*)data, len, wstype);
	ws_enqueue(ws, node);
}

static void evws_close_cb(struct evws_connection* conn, void* arg) {
	LuaWebSocket* ws = (LuaWebSocket*)arg;
	if (!ws)
		return;
	fprintf(stderr, "[WS] evws_close_cb: ws id=%lld already_closed=%d\n",
		(long long)ws->id, ws->closed ? 1 : 0);
	// Null the pointer immediately — libevent calls evws_connection_free
	// after this callback returns, so the pointer will be invalid.
	ws->evws = NULL;
	ws->connected = false;
	// Enqueue a close message so Poll/Read can return it to Lua, but only if
	// the connection was not already torn down by an explicit Dispose().
	if (!ws->closed) {
		WsMsgNode* node = ws_msg_alloc(NULL, 0, WS_TYPE_CLOSE);
		ws_enqueue(ws, node);
	}
	// IMPORTANT: Do NOT call any Lua API here.
	// This callback fires from within event_base_loop, which can be called
	// from inside ws_dispose_native, which is itself called from a Lua C
	// function (Poll/Read/GC). Calling luaL_unref here would re-enter the
	// Lua API while the stack is already in use, causing an access violation.
	// self_ref is released by the caller (ws_dispose_native) after
	// event_base_loop returns, or by ws_release_self_ref in Poll/Read.
}

// Release self_ref from a safe Lua context (NOT from within event_base_loop).
// self_ref keeps the WEBSOCKET userdata alive while libevent holds the arg
// pointer.  Once evws_close_cb has fired (ws->evws == NULL && ws->closed or
// ws->connected == false), libevent is done with the pointer and we can
// safely unref from a normal Lua C function call frame.
static void ws_release_self_ref(lua_State* L, LuaWebSocket* ws) {
	if (ws->self_ref > 0) {
		luaL_unref(L, LUA_REGISTRYINDEX, ws->self_ref);
		ws->self_ref = LUA_NOREF;
	}
}

// ---- curl build_headers (local helper; mirrors HttpCurl.cpp) ----------------

static struct curl_slist* ws_build_headers(lua_State* L, LuaHttpClient* client, int headersIdx) {
	struct curl_slist* hdrs = NULL;
	char buf[2048];
	if (client->defaultHeadersRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, client->defaultHeadersRef);
		lua_pushnil(L);
		while (lua_next(L, -2)) {
			const char* k = lua_tostring(L, -2);
			const char* v = lua_tostring(L, -1);
			if (k && v) {
				snprintf(buf, sizeof(buf), "%s: %s", k, v);
				hdrs = curl_slist_append(hdrs, buf);
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
	}
	if (headersIdx > 0 && lua_istable(L, headersIdx)) {
		lua_pushnil(L);
		while (lua_next(L, headersIdx)) {
			const char* k = lua_tostring(L, -2);
			const char* v = lua_tostring(L, -1);
			if (k && v) {
				snprintf(buf, sizeof(buf), "%s: %s", k, v);
				hdrs = curl_slist_append(hdrs, buf);
			}
			lua_pop(L, 1);
		}
	}
	return hdrs;
}

// ---- push helpers -----------------------------------------------------------

// Push a new WSMESSAGE userdata owning the given WsMsgNode's payload.
// Takes ownership: the node is freed after data is transferred.
static void ws_push_message(lua_State* L, WsMsgNode* node) {
	LuaWsMessage* msg = (LuaWsMessage*)lua_newuserdata(L, sizeof(LuaWsMessage));
	luaL_getmetatable(L, LUAWSMESSAGE);
	lua_setmetatable(L, -2);
	msg->data = node->data;
	msg->len = node->len;
	msg->type = node->type;
	// The WsMsgNode wrapper is no longer needed (message userdata owns data now).
	node->data = NULL;
	ws_msg_free(node);
}

// Push a new WEBSOCKET userdata initialised to defaults.
static LuaWebSocket* ws_push_userdata(lua_State* L) {
	LuaWebSocket* ws = (LuaWebSocket*)lua_newuserdata(L, sizeof(LuaWebSocket));
	luaL_getmetatable(L, LUAWEBSOCKET);
	lua_setmetatable(L, -2);
	memset(ws, 0, sizeof(LuaWebSocket));
	ws->client_ref = LUA_NOREF;
	ws->context_ref = LUA_NOREF;
	ws->self_ref = LUA_NOREF;
	ws->aliveTokenRef = LUA_NOREF;
	ws->id = g_next_ws_id++;
	ws->L = L;
	return ws;
}

// ---- poll curl for a new message (called by ws_client_poll) -----------------

// Returns true if a message was appended to the queue.
static bool ws_client_recv_one(LuaWebSocket* ws) {
	// Grow the reusable receive buffer on demand (initial 64 KB).
	if (!ws->recvBuf) {
		ws->recvAlloc = 65536;
		ws->recvBuf = (char*)kitsune_malloc(ws->recvAlloc);
		if (!ws->recvBuf)
			return false;
	}
	char* buf = ws->recvBuf;
	size_t bufSize = ws->recvAlloc;
	size_t nrecv = 0;
	const struct curl_ws_frame* meta = NULL;
	CURLcode rc = curl_ws_recv(ws->easy, buf, bufSize, &nrecv, &meta);
	if (rc == CURLE_AGAIN)
		return false;
	if (rc != CURLE_OK) {
		ws->connected = false;
		WsMsgNode* node = ws_msg_alloc(NULL, 0, WS_TYPE_CLOSE);
		ws_enqueue(ws, node);
		return true;
	}
	if (meta->flags & CURLWS_CLOSE) {
		ws->connected = false;
		// Echo the close frame back per RFC 6455.
		size_t nsent = 0;
		curl_ws_send(ws->easy, buf, nrecv, &nsent, 0, CURLWS_CLOSE);
		WsMsgNode* node = ws_msg_alloc(NULL, 0, WS_TYPE_CLOSE);
		ws_enqueue(ws, node);
		return true;
	}
	if (meta->flags & CURLWS_PING) {
		size_t nsent = 0;
		curl_ws_send(ws->easy, buf, nrecv, &nsent, 0, CURLWS_PONG);
		WsMsgNode* node = ws_msg_alloc(buf, nrecv, WS_TYPE_PING);
		ws_enqueue(ws, node);
		return true;
	}
	if (meta->flags & CURLWS_PONG) {
		WsMsgNode* node = ws_msg_alloc(buf, nrecv, WS_TYPE_PONG);
		ws_enqueue(ws, node);
		return true;
	}
	// Data frame (text or binary) — may arrive as fragments.
	int wstype = (meta->flags & CURLWS_BINARY) ? WS_TYPE_BINARY : WS_TYPE_TEXT;
	if (nrecv > 0) {
		if (!ws_frag_append(ws, buf, nrecv)) {
			ws_frag_reset(ws);
			return false;
		}
	}
	if (meta->bytesleft > 0)
		return false; // more fragments coming; caller should loop
	// Complete message assembled.
	const char* payload = ws->fragLen > 0 ? ws->fragBuf : buf;
	size_t payloadLen = ws->fragLen > 0 ? ws->fragLen : nrecv;
	if (ws->maxMsgSize == 0 || payloadLen <= ws->maxMsgSize) {
		WsMsgNode* node = ws_msg_alloc(payload, payloadLen, wstype);
		ws_enqueue(ws, node);
	}
	ws_frag_reset(ws);
	return true;
}

// ---- dispose ----------------------------------------------------------------

static void ws_dispose_native(lua_State* L, LuaWebSocket* ws) {
	if (ws->closed)
		return;
	fprintf(stderr, "[WS] ws_dispose_native called for ws id=%lld (connected=%d, evws=%p)\n",
		(long long)ws->id, ws->connected ? 1 : 0, (void*)ws->evws);
	ws->closed = true;
	ws->connected = false;

	// --- server side ---------------------------------------------------------
	if (ws->evws) {
		// evws_close() queues a close frame; libevent then calls
		// evws_connection_free() automatically via close_after_write_cb /
		// close_event_cb once the frame drains.  Our evws_close_cb nulls
		// ws->evws before the free, so we must NOT call evws_connection_free
		// here — doing so causes a double-free crash.
		evws_close(ws->evws, WS_CR_NORMAL);
		// Pump the event base so the close frame drains and libevent can call
		// evws_connection_free itself (avoiding a leak when we shut down first).
		if (ws->evbase) {
			for (int i = 0; i < 10 && ws->evws; i++)
				event_base_loop(ws->evbase, EVLOOP_NONBLOCK);
		}
		// ws->evws is nulled by evws_close_cb when the frame drains.
		// If it's still set the write didn't drain in time; null defensively
		// to avoid a dangling pointer (the event_base will eventually free it).
		ws->evws = NULL;
	}
	ws->evbase = NULL;
	// Now that event_base_loop has returned we are back in a safe Lua C call
	// frame and can release self_ref.  evws_close_cb intentionally skips this
	// because it fires from inside event_base_loop.
	ws_release_self_ref(L, ws);

	// --- client side
	if (ws->easy) {
		if (ws->multi)
			curl_multi_remove_handle(ws->multi, ws->easy);
		{
			size_t nsent = 0;
			curl_ws_send(ws->easy, "", 0, &nsent, 0, CURLWS_CLOSE);
		}
		curl_easy_cleanup(ws->easy);
		ws->easy = NULL;
		ws->multi = NULL;
	}
	if (ws->requestHdrs) {
		curl_slist_free_all(ws->requestHdrs);
		ws->requestHdrs = NULL;
	}
	kitsune_free(ws->curlMsg);
	ws->curlMsg = NULL;
	ws_frag_reset(ws);
	kitsune_free(ws->recvBuf);
	ws->recvBuf = NULL;
	ws->recvAlloc = 0;

	if (ws->client_ref > 0) {
		luaL_unref(L, LUA_REGISTRYINDEX, ws->client_ref);
		ws->client_ref = LUA_NOREF;
	}
	if (ws->aliveTokenRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ws->aliveTokenRef);
		ws->aliveTokenRef = LUA_NOREF;
	}

	// --- shared ---------------------------------------------------------------
	ws_drain_queue(ws);
	if (ws->context_ref > 0) {
		luaL_unref(L, LUA_REGISTRYINDEX, ws->context_ref);
		ws->context_ref = LUA_NOREF;
	}
}

// =============================================================================
// Shared WEBSOCKET methods
// =============================================================================

int WebSocket_IsConnected(lua_State* L) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);
	lua_pushboolean(L, ws->connected && !ws->closed ? 1 : 0);
	return 1;
}

int WebSocket_Dispose(lua_State* L) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);
	ws_dispose_native(L, ws);
	return 0;
}

int WebSocket_GC(lua_State* L) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);
	if (!ws->closed)
		fprintf(stderr, "[WS] __gc fired on LIVE ws id=%lld\n", (long long)ws->id);
	ws_dispose_native(L, ws);
	return 0;
}

int WebSocket_ToString(lua_State* L) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);
	lua_pushfstring(L, "WebSocket(%I, %s)", ws->id,
		ws->connected ? "connected" : "closed");
	return 1;
}

int WebSocket_GetId(lua_State* L) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);
	lua_pushinteger(L, ws->id);
	return 1;
}

int WebSocket_GetContext(lua_State* L) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);
	if (ws->context_ref == LUA_NOREF) {
		lua_newtable(L);
		lua_pushvalue(L, -1);
		ws->context_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	else {
		lua_rawgeti(L, LUA_REGISTRYINDEX, ws->context_ref);
	}
	return 1;
}

int WebSocket_SetMaxMessageSize(lua_State* L) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);
	lua_Integer sz = luaL_checkinteger(L, 2);
	ws->maxMsgSize = (sz > 0) ? (size_t)sz : 0;
	return 0;
}

// WebSocket:Poll() — non-blocking; drives network, returns next WsMessage or nil.
// Server side: libevent callbacks already enqueue into msg_head; just dequeue.
// Also handles server-initiated ping keepalive: sends a ping after 30 s of
// client silence and closes the connection if no response arrives within 30 s.
// Client side: calls curl_ws_recv until no more data, then dequeues.
int WebSocket_Poll(lua_State* L) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);
	if (ws->closed) {
		lua_pushnil(L);
		return 1;
	}

	if (ws->easy) {
		// Client: advance curl multi and drain all available fragments.
		ws_curlm_step(ws->multi);
		while (ws_client_recv_one(ws))
			;
	}
	else if (ws->evws && ws->connected) {
		// Server: send a ping after WS_PING_AFTER_MS of client silence to keep
		// the TCP connection alive during long LLM generations.
		// NOTE: libevent's evws_msg_cb only fires for TEXT/BINARY frames; pong
		// frames are consumed internally and never reach our callback, so we
		// cannot use pong receipt as a liveness signal.  We rely on TCP itself
		// to detect genuinely dead connections (evws_close_cb will fire).
		uint64_t now = ws_now_ms();
		if (now - ws->lastDataMs >= WS_PING_AFTER_MS) {
			ws_server_send_ping(ws->evws);
			// Reset so we don't spam pings every Poll() call.
			ws->lastDataMs = now;
		}
	}

	WsMsgNode* node = ws_dequeue(ws);
	if (!node) {
		lua_pushnil(L);
		return 1;
	}
	ws_push_message(L, node);
	return 1;
}

// WebSocket:Read() — yields until a message arrives or connection closes.
static int WebSocket_ReadContinuation(lua_State* L, int status, lua_KContext ctx);

static int WebSocket_ReadContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);

	// AliveToken check — treat a disposed token as connection closed
	if (ws->aliveTokenRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, ws->aliveTokenRef);
		int alive = lua_alivetoken_isalive(L, -1);
		lua_pop(L, 1);
		if (alive == 0) {
			ws_dispose_native(L, ws);
			lua_pushnil(L);
			return 1;
		}
	}

	if (ws->easy) {
		// Client: drive multi and drain all available curl fragments so a large
		// message that arrives in multiple curl_ws_recv chunks is fully assembled
		// before we check the queue.
		ws_curlm_step(ws->multi);
		while (ws_client_recv_one(ws))
			;
	}
	else if (ws->evws && ws->connected) {
		// Server: same periodic ping as Poll().
		uint64_t now = ws_now_ms();
		if (now - ws->lastDataMs >= WS_PING_AFTER_MS) {
			ws_server_send_ping(ws->evws);
			ws->lastDataMs = now;
		}
	}

	WsMsgNode* node = ws_dequeue(ws);
	if (node) {
		set_did_work(L);  // dequeued a message
		ws_push_message(L, node);
		return 1;
	}
	if (ws->closed || !ws->connected) {
		lua_pushnil(L);
		return 1;
	}
	return lua_yieldk(L, 0, 0, WebSocket_ReadContinuation);  // no message — no work done
}

int WebSocket_Read(lua_State* L) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);
	if (ws->closed || !ws->connected) {
		lua_pushnil(L);
		return 1;
	}
	return WebSocket_ReadContinuation(L, LUA_OK, 0);
}

// WebSocket:Ping() — sends a WebSocket ping frame to the client.
// Returns true if the ping was sent, false if the connection is closed.
// The browser will respond with a pong automatically; we don't track it
// here — TCP will detect the dead connection if no response ever comes.
int WebSocket_Ping(lua_State* L) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);
	if (ws->closed || !ws->connected) {
		lua_pushboolean(L, 0);
		return 1;
	}
	if (ws->evws) {
		ws_server_send_ping(ws->evws);
		lua_pushboolean(L, 1);
	}
	else {
		// Client-side: use curl to send a ping frame
		size_t nsent = 0;
		CURLcode rc = curl_ws_send(ws->easy, "", 0, &nsent, 0, CURLWS_PING);
		lua_pushboolean(L, rc == CURLE_OK ? 1 : 0);
	}
	return 1;
}

// WebSocket:Send(data [, binary]) — returns true on success, false on failure/overflow.
int WebSocket_Send(lua_State* L) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, 1, LUAWEBSOCKET);
	if (ws->closed || !ws->connected) {
		lua_pushboolean(L, 0);
		return 1;
	}
	size_t len;
	const char* data = luaL_checklstring(L, 2, &len);
	bool binary = lua_toboolean(L, 3) != 0;

	if (ws->maxMsgSize > 0 && len > ws->maxMsgSize) {
		fprintf(stderr, "[WS] Send: oversized payload len=%zu (max=%zu) id=%lld\n",
			len, ws->maxMsgSize, (long long)ws->id);
		// Oversized send: return false without closing.
		lua_pushboolean(L, 0);
		return 1;
	}

	bool ok = false;
	if (ws->evws) {
		// Server side: always use manual framing — evws_send has a bug with
		// payloads > 65535 bytes (writes the 8-byte length field as zeros).
		ws_server_send_frame(ws->evws, data, len, binary);
		ok = true;
	}
	else if (ws->easy) {
		// Client side: honour binary flag.
		size_t nsent = 0;
		unsigned int flags = binary ? CURLWS_BINARY : CURLWS_TEXT;
		CURLcode rc = curl_ws_send(ws->easy, data, len, &nsent, 0, flags);
		ok = (rc == CURLE_OK);
	}
	lua_pushboolean(L, ok ? 1 : 0);
	return 1;
}

// =============================================================================
// WSMESSAGE methods
// =============================================================================

int WsMessage_GetData(lua_State* L) {
	LuaWsMessage* msg = (LuaWsMessage*)luaL_checkudata(L, 1, LUAWSMESSAGE);
	if (msg->data && msg->len > 0)
		lua_pushlstring(L, msg->data, msg->len);
	else
		lua_pushliteral(L, "");
	return 1;
}

int WsMessage_GetType(lua_State* L) {
	LuaWsMessage* msg = (LuaWsMessage*)luaL_checkudata(L, 1, LUAWSMESSAGE);
	lua_pushinteger(L, msg->type);
	return 1;
}

int WsMessage_GC(lua_State* L) {
	LuaWsMessage* msg = (LuaWsMessage*)luaL_checkudata(L, 1, LUAWSMESSAGE);
	kitsune_free(msg->data);
	msg->data = NULL;
	return 0;
}

int WsMessage_ToString(lua_State* L) {
	LuaWsMessage* msg = (LuaWsMessage*)luaL_checkudata(L, 1, LUAWSMESSAGE);
	lua_pushfstring(L, "WsMessage(type=%d, len=%d)", msg->type, (int)msg->len);
	return 1;
}

// =============================================================================
// Client entry point: ws_client_connect
// Called as: client:Connect(url [, headers])
// Yields until the WebSocket handshake completes.
// On success returns the WEBSOCKET userdata.
// On failure returns nil, errmsg.
// =============================================================================

static int WsConnectContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaWebSocket* ws = (LuaWebSocket*)luaL_checkudata(L, -1, LUAWEBSOCKET);
	// AliveToken check
	if (ws->aliveTokenRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, ws->aliveTokenRef);
		int alive = lua_alivetoken_isalive(L, -1);
		lua_pop(L, 1);
		if (alive == 0) {
			ws_dispose_native(L, ws);
			lua_pop(L, 1);
			lua_pushnil(L);
			lua_pushstring(L, "aborted");
			return 2;
		}
	}
	ws_curlm_step(ws->multi);
	if (ws->curlMsg && ws->curlMsg->done) {
		if (ws->curlMsg->result != CURLE_OK) {
			const char* err = ws->errorBuf[0] ? ws->errorBuf : "WebSocket connect failed";
			ws_dispose_native(L, ws);
			lua_pop(L, 1);
			lua_pushnil(L);
			lua_pushstring(L, err);
			return 2;
		}
		ws->connected = true;
		return 1;
	}
	return lua_yieldk(L, 0, 0, WsConnectContinuation);
}

int ws_client_connect(lua_State* L) {
	LuaHttpClient* client = (LuaHttpClient*)luaL_checkudata(L, 1, LUAHTTPCLIENT);
	const char* url = luaL_checkstring(L, 2);
	int headersIdx = lua_istable(L, 3) ? 3 : 0;

	// Retrieve the shared CURLM* from the Lua registry (same key as HttpCurl.cpp).
	lua_rawgetp(L, LUA_REGISTRYINDEX, &g_curlm_key);
	CURLM* multi = (CURLM*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (!multi) {
		lua_pushnil(L);
		lua_pushstring(L, "HTTP module not initialised");
		return 2;
	}

	LuaWebSocket* ws = ws_push_userdata(L);
	// Stack: [client, url, (headers?), ws]

	ws->curlMsg = (CurlMsg*)kitsune_malloc(sizeof(CurlMsg));
	if (!ws->curlMsg) {
		lua_pushnil(L);
		lua_pushstring(L, "out of memory");
		return 2;
	}
	memset(ws->curlMsg, 0, sizeof(CurlMsg));

	ws->easy = curl_easy_init();
	if (!ws->easy) {
		lua_pushnil(L);
		lua_pushstring(L, "curl_easy_init failed");
		return 2;
	}
	ws->multi = multi;

	struct curl_slist* hdrs = ws_build_headers(L, client, headersIdx);
	ws->requestHdrs = hdrs;

	curl_easy_setopt(ws->easy, CURLOPT_URL, url);
	curl_easy_setopt(ws->easy, CURLOPT_CONNECT_ONLY, 2L);
	curl_easy_setopt(ws->easy, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
	curl_easy_setopt(ws->easy, CURLOPT_HTTPHEADER, hdrs);
	curl_easy_setopt(ws->easy, CURLOPT_ERRORBUFFER, ws->errorBuf);
	curl_easy_setopt(ws->easy, CURLOPT_PROTOCOLS_STR, "https,http,wss,ws");
	curl_easy_setopt(ws->easy, CURLOPT_SSL_VERIFYPEER, client->verifySsl ? 1L : 0L);
	curl_easy_setopt(ws->easy, CURLOPT_SSL_VERIFYHOST, client->verifySsl ? 2L : 0L);
	curl_easy_setopt(ws->easy, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(ws->easy, CURLOPT_PRIVATE, ws->curlMsg);
	// Use CONNECTTIMEOUT, not TIMEOUT_MS. CURLOPT_TIMEOUT_MS is a total-transfer
	// deadline that fires on the multi handle even during the live data phase,
	// killing the socket while the coroutine is still waiting for echo frames.
	// CURLOPT_CONNECTTIMEOUT_MS only covers the TCP+TLS+HTTP-101 handshake.
	if (client->timeoutMs > 0)
		curl_easy_setopt(ws->easy, CURLOPT_CONNECTTIMEOUT_MS, (long)client->timeoutMs);

	curl_multi_add_handle(multi, ws->easy);

	// Hold a ref to the client to prevent GC while the WebSocket is alive.
	lua_pushvalue(L, 1);
	ws->client_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	// Snapshot the client's AliveToken (if any) so Read/Poll can check it
	// without needing the client userdata to remain reachable.
	if (client->aliveTokenRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, client->aliveTokenRef);
		ws->aliveTokenRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	// The WEBSOCKET userdata is at the top of the stack; yield until connected.
	ws_curlm_step(multi);
	return lua_yieldk(L, 0, 0, WsConnectContinuation);
}

// =============================================================================
// Server entry point: ws_server_upgrade
// Called as: resp:UpgradeToWebSocket()
// Synchronous: evws_new_session handles the HTTP 101 upgrade immediately.
// Returns the WEBSOCKET userdata or nil, errmsg.
// =============================================================================

int ws_server_upgrade(lua_State* L) {
	LuaHttpResponse* resp = (LuaHttpResponse*)luaL_checkudata(L, 1, LUAHTTPRESPONSE);
	if (!resp->connection || !resp->connection->req) {
		lua_pushnil(L);
		lua_pushstring(L, "response is not connected to a live request");
		return 2;
	}
	if (resp->finalized) {
		lua_pushnil(L);
		lua_pushstring(L, "response is already finalized");
		return 2;
	}

	LuaHttpRequest* req = resp->connection;

	LuaWebSocket* ws = ws_push_userdata(L);
	// Stack: [resp, ws]

	// evhttp_start_ws_() (called inside evws_new_session) calls
	// evhttp_connection_free() which synchronously fires conn_close_cb on the
	// LuaHttpServer.  conn_close_cb uses r->req to look up what to clean up.
	// If r->req is still set when that callback fires it will call
	// HttpRequest_Cleanup on a request that libevent is in the middle of
	// freeing — corrupting state and aborting the TCP connection.
	// Null these out NOW, before evws_new_session, so conn_close_cb sees
	// an already-handled connection and returns early.
	struct evhttp_request* raw_req = req->req;
	struct evhttp_connection* evcon = evhttp_request_get_connection(raw_req);

	// Pre-clear the evhttp_connection timeout. Best-effort: evws_new_session
	// steals the bufferevent from evcon, so we also clear it on the bev below.
	if (evcon)
		evhttp_connection_set_timeout_tv(evcon, NULL);

	// Mark the request as upgraded before evws_new_session fires conn_close_cb.
	req->req = NULL;
	req->upgraded = true;
	resp->finalized = true;
	// Also clear the registry entries keyed by req* and con* so conn_close_cb
	// finds nothing and returns immediately.
	if (evcon) {
		lua_pushlightuserdata(L, (void*)evcon);
		lua_pushnil(L);
		lua_rawset(L, LUA_REGISTRYINDEX);
	}
	lua_pushlightuserdata(L, (void*)raw_req);
	lua_pushnil(L);
	lua_rawset(L, LUA_REGISTRYINDEX);

	struct evws_connection* evws = evws_new_session(raw_req,
		evws_msg_cb, ws, 0);
	if (!evws) {
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushstring(L, "evws_new_session failed");
		return 2;
	}

	evws_connection_set_closecb(evws, evws_close_cb, ws);

	// Replace libevent's broken ws_evhttp_error_cb with our own.
	// The stock callback only handles BEV_EVENT_EOF — any other bufferevent
	// error (BEV_EVENT_ERROR, BEV_EVENT_TIMEOUT, etc.) is silently swallowed:
	// reads stop, the connection appears alive on our side, but the browser
	// gets no more data and eventually closes from its end via TCP timeout.
	// We store a pointer to our error handler in the ws struct and install it
	// by reaching into the bufferevent after evws_new_session has set its own
	// read callback (which we must preserve).
	{
		struct bufferevent* bev = evws_connection_get_bufferevent(evws);
		if (bev) {
			bufferevent_set_timeouts(bev, NULL, NULL);
			// evws_new_session already called bufferevent_setcb with its own
			// read+error callbacks. We want to keep the read callback intact
			// and only replace the error callback. bufferevent_setcb with NULL
			// for readcb would zero it, so we use the event callback field only
			// via bufferevent_setcb — but we need the existing readcb pointer.
			// libevent exposes bufferevent_getcb for exactly this purpose.
			bufferevent_data_cb  readcb  = NULL;
			bufferevent_data_cb  writecb = NULL;
			bufferevent_event_cb eventcb = NULL;
			void*                cbarg   = NULL;
			bufferevent_getcb(bev, &readcb, &writecb, &eventcb, &cbarg);
			// Install our fixed error handler, preserving the read callback.
			bufferevent_setcb(bev, readcb, writecb,
				[](struct bufferevent*, short what, void* arg) {
					// Close the WS connection on any bufferevent error or EOF.
					// libevent's stock ws_evhttp_error_cb only handles EOF,
					// leaving BEV_EVENT_ERROR/BEV_EVENT_TIMEOUT silently broken.
					fprintf(stderr, "[WS] bev error event: what=0x%04x (EOF=%d ERR=%d TIMEOUT=%d)\n",
						(unsigned)what,
						(what & BEV_EVENT_EOF)     ? 1 : 0,
						(what & BEV_EVENT_ERROR)   ? 1 : 0,
						(what & BEV_EVENT_TIMEOUT) ? 1 : 0);
					struct evws_connection* evws = (struct evws_connection*)arg;
					evws_close(evws, 1001 /* going away */);
				},
				cbarg);
		}
	}

	ws->evws = evws;
	ws->evbase = req->server ? req->server->base : NULL;
	ws->connected = true;
	ws->L = L;
	ws->lastDataMs = ws_now_ms();

	// Hold a registry ref to the WEBSOCKET userdata so evws_close_cb can
	// safely access ws-> even if Lua has dropped its own reference.
	lua_pushvalue(L, -1);
	ws->self_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	return 1;
}

// =============================================================================
// Module init — called by luaopen_http (in HttpCurlMain.cpp or equivalent)
// =============================================================================

int luaopen_websocket(lua_State* L) {
	// WSMESSAGE metatable
	luaL_newmetatable(L, LUAWSMESSAGE);
	lua_pushcfunction(L, WsMessage_GC);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, WsMessage_ToString);
	lua_setfield(L, -2, "__tostring");
	lua_newtable(L);
	lua_pushcfunction(L, WsMessage_GetData);
	lua_setfield(L, -2, "GetData");
	lua_pushcfunction(L, WsMessage_GetType);
	lua_setfield(L, -2, "GetType");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	// WEBSOCKET metatable
	luaL_newmetatable(L, LUAWEBSOCKET);
	lua_pushcfunction(L, WebSocket_GC);
	lua_setfield(L, -2, "__gc");
	lua_pushcfunction(L, WebSocket_ToString);
	lua_setfield(L, -2, "__tostring");
	lua_newtable(L);
	lua_pushcfunction(L, WebSocket_IsConnected);
	lua_setfield(L, -2, "IsConnected");
	lua_pushcfunction(L, WebSocket_Poll);
	lua_setfield(L, -2, "Poll");
	lua_pushcfunction(L, WebSocket_Read);
	lua_setfield(L, -2, "Read");
	lua_pushcfunction(L, WebSocket_Send);
	lua_setfield(L, -2, "Send");
	lua_pushcfunction(L, WebSocket_Dispose);
	lua_setfield(L, -2, "Dispose");
	lua_pushcfunction(L, WebSocket_GetId);
	lua_setfield(L, -2, "GetId");
	lua_pushcfunction(L, WebSocket_GetContext);
	lua_setfield(L, -2, "GetContext");
	lua_pushcfunction(L, WebSocket_SetMaxMessageSize);
	lua_setfield(L, -2, "SetMaxMessageSize");
	lua_pushcfunction(L, WebSocket_Ping);
	lua_setfield(L, -2, "Ping");
	lua_setfield(L, -2, "__index");
	lua_pop(L, 1);

	return 0;
}

#endif // KITSUNE_HTTP
