#include "httpserver/LuaHttpServer.h"
#include "httpserver/LuaHttpServerRequest.h"
#include "stream/LuaStream.h"
#include "Mem.h"
#include "alivetoken/LuaAliveToken.h"
#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#ifndef _WIN32
#define _strdup strdup
#endif

/* ── defensive helpers ────────────────────────────────────────────────────── */

static void SafeFreeS(char** p) {
	if (*p) {
		free(*p);
		*p = NULL;
	}
}

/* ── node release ─────────────────────────────────────────────────────────── */

/* Tears down one HttpOpenConnection node. Idempotent. */
static void ReleaseNode(lua_State* L, HttpOpenConnection* node) {
	if (node->requestRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, node->requestRef);
		LuaHttpRequest* req = (LuaHttpRequest*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		if (req)
			HttpRequestCleanup(L, req);
		luaL_unref(L, LUA_REGISTRYINDEX, node->requestRef);
		node->requestRef = LUA_NOREF;
	}
	kitsune_free(node);
}

/* ── push / check ─────────────────────────────────────────────────────────── */

LuaHttpServer* lua_pushhttpserver(lua_State* L) {
	LuaHttpServer* s = (LuaHttpServer*)lua_newuserdata(L, sizeof(LuaHttpServer));
	luaL_getmetatable(L, LUAHTTPSERVER);
	lua_setmetatable(L, -2);
	memset(s, 0, sizeof(LuaHttpServer));
	s->coroutineRef = LUA_NOREF;
	s->disconnectRef = LUA_NOREF;
	s->aliveTokenRef = LUA_NOREF;
	return s;
}

LuaHttpServer* luaL_checkhttpserver(lua_State* L, int idx) {
	LuaHttpServer* s = (LuaHttpServer*)luaL_checkudata(L, idx, LUAHTTPSERVER);
	if (!s)
		luaL_error(L, "parameter is not a %s", LUAHTTPSERVER);
	return s;
}

/* ── per-request queue / sender cleanup ───────────────────────────────────── */

static void QueueRemoveReq(LuaHttpServer* s, lua_State* L, struct evhttp_request* req) {
	HttpOpenConnection* prev = NULL;
	HttpOpenConnection* cur = s->queueHead;
	while (cur) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, cur->requestRef);
		LuaHttpRequest* r = (LuaHttpRequest*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		HttpOpenConnection* next = cur->next;
		if (r && r->req == req) {
			if (prev)
				prev->next = next;
			else
				s->queueHead = next;
			if (s->queueTail == cur)
				s->queueTail = prev;
			ReleaseNode(L, cur);
		}
		else {
			prev = cur;
		}
		cur = next;
	}
}

static void SendersRemoveReq(LuaHttpServer* s, lua_State* L, struct evhttp_request* req,
	LuaHttpResponse* resp) {
	HttpOpenConnection* prev = NULL;
	HttpOpenConnection* cur = s->senders;
	while (cur) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, cur->requestRef);
		LuaHttpRequest* r = (LuaHttpRequest*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		HttpOpenConnection* next = cur->next;
		if (r && r->req == req) {
			if (prev)
				prev->next = next;
			else
				s->senders = next;
			if (resp)
				resp->stream = NULL;
			ReleaseNode(L, cur);
			break;
		}
		else {
			prev = cur;
		}
		cur = next;
	}
}

/* Call the SetOnDisconnect handler. Stack on entry: [handler, request]. These
   callbacks run inside event_base_loop on the Accept coroutine's stack, so an
   error message left behind would sit at index 1, where AcceptCont reads its
   stop flag on the next resume and tears the server down. Report and drop it. */
static void CallDisconnectHandler(lua_State* L) {
	if (lua_pcall_nohook(L, 1, 0, 0) != LUA_OK) {
		const char* err = lua_tostring(L, -1);
		fprintf(stderr, "HttpServer: SetOnDisconnect handler failed: %s\n", err ? err : "(error object is not a string)");
		lua_pop(L, 1);
	}
}

/* Full teardown for one request on close / error. */
static void ConnCloseCleanup(LuaHttpServer* s, lua_State* L,
	struct evhttp_request* req, const char* errmsg) {
	/* 1. Remove stale queue and sender nodes */
	QueueRemoveReq(s, L, req);

	lua_pushlightuserdata(L, (void*)req);
	lua_rawget(L, LUA_REGISTRYINDEX);
	LuaHttpRequest* r = (LuaHttpRequest*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	LuaHttpResponse* resp = NULL;
	if (r && r->responseRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, r->responseRef);
		resp = (LuaHttpResponse*)lua_touserdata(L, -1);
		lua_pop(L, 1);
	}

	SendersRemoveReq(s, L, req, resp);

	/* 2. Mark finalized */
	if (resp)
		resp->finalized = true;

	/* 3. Disconnect handler or error event */
	if (r) {
		if (errmsg) {
			SafeFreeS(&r->errorMsg);
			r->errorMsg = _strdup(errmsg);
			r->is_error = true;
		}

		if (s->disconnectRef != LUA_NOREF) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, s->disconnectRef);
			lua_pushlightuserdata(L, (void*)req);
			lua_rawget(L, LUA_REGISTRYINDEX);
			CallDisconnectHandler(L);
		}
		else if (r->is_error) {
			/* Enqueue a final event so GetError() surfaces it */
			lua_pushlightuserdata(L, (void*)req);
			lua_rawget(L, LUA_REGISTRYINDEX);
			int ref = luaL_ref(L, LUA_REGISTRYINDEX);
			HttpOpenConnection* node = (HttpOpenConnection*)kitsune_malloc(sizeof(HttpOpenConnection));
			if (node) {
				node->requestRef = ref;
				node->next = NULL;
				if (s->queueTail)
					s->queueTail->next = node;
				else
					s->queueHead = node;
				s->queueTail = node;
			}
			else {
				luaL_unref(L, LUA_REGISTRYINDEX, ref);
			}
		}
	}

	/* 4. Release Registry[con*] and Registry[req*] */
	if (r && r->con) {
		lua_pushlightuserdata(L, (void*)r->con);
		lua_pushnil(L);
		lua_rawset(L, LUA_REGISTRYINDEX);
		r->con = NULL;
	}
	lua_pushlightuserdata(L, (void*)req);
	lua_pushnil(L);
	lua_rawset(L, LUA_REGISTRYINDEX);
}

/* ── connection close callback ────────────────────────────────────────────── */

/* Called by libevent when a TCP connection closes.  Looks up the request that
   was registered under Registry[con*] in HttpRequestCb.
   - If r->req is still set, the connection closed before we responded:
	 run full cleanup (removes from queue, fires disconnect/error handler).
   - If r->req is NULL, we already sent a response: just fire the disconnect
	 handler so Lua code that called SetOnDisconnect can react to the close. */
static void ConnCloseCb(struct evhttp_connection* con, void* arg) {
	LuaHttpServer* s = (LuaHttpServer*)arg;
	if (!s || !s->L)
		return;
	lua_State* L = s->L;

	/* Look up the LuaHttpRequest for this connection via Registry[con*] */
	lua_pushlightuserdata(L, (void*)con);
	lua_rawget(L, LUA_REGISTRYINDEX);
	LuaHttpRequest* r = (LuaHttpRequest*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	if (!r)
		return;

	if (r->req) {
		/* Request still pending — run full teardown (handles queue removal,
		   disconnect / error event, and cleans up Registry[con*]+Registry[req*]). */
		ConnCloseCleanup(s, L, r->req, NULL);
	}
	else {
		/* Response already sent — fire the disconnect callback if registered. */
		if (s->disconnectRef != LUA_NOREF) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, s->disconnectRef);
			lua_pushlightuserdata(L, (void*)con);
			lua_rawget(L, LUA_REGISTRYINDEX); /* push LuaHttpRequest userdata as arg */
			CallDisconnectHandler(L);
		}
		/* Clean up Registry[con*] and null the back-pointer. */
		r->con = NULL;
		lua_pushlightuserdata(L, (void*)con);
		lua_pushnil(L);
		lua_rawset(L, LUA_REGISTRYINDEX);
	}
}

/* ── response complete callback ───────────────────────────────────────────── */

/* Fires when evhttp has fully written the response to the socket (just before
   evhttp_request_free is called by evhttp_send_done).  This is the evhttp
   equivalent of mongoose's MG_EV_CLOSE for server-side requests: it signals
   that the request/response exchange is complete and the connection is done
   from Lua's perspective (TCP keep-alive notwithstanding).

   Fires the SetOnDisconnect handler and cleans up Registry[con*] so that the
   real ConnCloseCb (TCP close) does not double-fire the handler. */
static void ResponseCompleteCb(struct evhttp_request* req, void* arg) {
	LuaHttpServer* s = (LuaHttpServer*)arg;
	if (!s || !s->L)
		return;
	lua_State* L = s->L;

	struct evhttp_connection* con = evhttp_request_get_connection(req);
	if (!con)
		return;

	if (s->disconnectRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->disconnectRef);
		lua_pushlightuserdata(L, (void*)con);
		lua_rawget(L, LUA_REGISTRYINDEX); /* LuaHttpRequest userdata */
		if (lua_isuserdata(L, -1)) {
			CallDisconnectHandler(L);
		}
		else {
			lua_pop(L, 2);
		}
	}

	/* Clean up Registry[con*] and Registry[req*] so the LuaHttpRequest
	   userdata becomes eligible for GC once Lua drops its own stack reference.
	   Both keys are raw pointer values — no dereference hazard even though
	   evhttp_request_free(req) is called by evhttp_send_done after we return. */
	lua_pushlightuserdata(L, (void*)con);
	lua_rawget(L, LUA_REGISTRYINDEX);
	LuaHttpRequest* r = (LuaHttpRequest*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (r)
		r->con = NULL;
	lua_pushlightuserdata(L, (void*)con);
	lua_pushnil(L);
	lua_rawset(L, LUA_REGISTRYINDEX);
	lua_pushlightuserdata(L, (void*)req);
	lua_pushnil(L);
	lua_rawset(L, LUA_REGISTRYINDEX);
}

/* ── method enum → string ─────────────────────────────────────────────────── */

static const char* CmdToStr(enum evhttp_cmd_type t) {
	switch (t) {
	case EVHTTP_REQ_GET:      return "GET";
	case EVHTTP_REQ_POST:     return "POST";
	case EVHTTP_REQ_HEAD:     return "HEAD";
	case EVHTTP_REQ_PUT:      return "PUT";
	case EVHTTP_REQ_DELETE:   return "DELETE";
	case EVHTTP_REQ_OPTIONS:  return "OPTIONS";
	case EVHTTP_REQ_TRACE:    return "TRACE";
	case EVHTTP_REQ_CONNECT:  return "CONNECT";
	case EVHTTP_REQ_PATCH:    return "PATCH";
	#ifdef EVHTTP_REQ_PROPFIND
	case EVHTTP_REQ_PROPFIND: return "PROPFIND";
	case EVHTTP_REQ_PROPPATCH:return "PROPPATCH";
	case EVHTTP_REQ_MKCOL:    return "MKCOL";
	case EVHTTP_REQ_LOCK:     return "LOCK";
	case EVHTTP_REQ_UNLOCK:   return "UNLOCK";
	case EVHTTP_REQ_COPY:     return "COPY";
	case EVHTTP_REQ_MOVE:     return "MOVE";
#endif
	default:                  return "UNKNOWN";
	}
}

/* ── address parser ───────────────────────────────────────────────────────── */

/* Accepts: "host:port", "http://host:port", "https://host:port", ":port" */
static bool ParseListenAddr(const char* addr, char* hostOut, size_t hostSz,
	ev_uint16_t* portOut) {
	const char* p = addr;
	if (strncmp(p, "http://", 7) == 0)
		p += 7;
	else if (strncmp(p, "https://", 8) == 0)
		p += 8;

	const char* colon = strrchr(p, ':');
	if (!colon)
		return false;

	size_t hlen = (size_t)(colon - p);
	if (hlen == 0) {
		/* ":port" — bind all interfaces */
		hostOut[0] = '\0';
	}
	else {
		if (hlen >= hostSz)
			return false;
		memcpy(hostOut, p, hlen);
		hostOut[hlen] = '\0';
	}

	int portVal = atoi(colon + 1);
	if (portVal <= 0 || portVal > 65535)
		return false;
	*portOut = (ev_uint16_t)portVal;
	return true;
}

/* ── libevent request callback ────────────────────────────────────────────── */

static void HttpRequestCb(struct evhttp_request* req, void* arg) {
	LuaHttpServer* s = (LuaHttpServer*)arg;
	if (!s || !s->L) {
		evhttp_send_error(req, HTTP_SERVUNAVAIL, "Server unavailable");
		return;
	}
	lua_State* L = s->L;

	/* Take ownership — prevents evhttp from freeing req when we return */
	evhttp_request_own(req);

	/* Register connection close callback */
	struct evhttp_connection* con = evhttp_request_get_connection(req);
	if (con)
		evhttp_connection_set_closecb(con, ConnCloseCb, s);

	/* ── Build LuaHttpRequest ── */
	lua_pushhttprequest(L);                                        /* [req_ud] */
	LuaHttpRequest* r = (LuaHttpRequest*)lua_touserdata(L, -1);
	r->req = req;
	r->con = con;
	r->server = s;

	const char* uri = evhttp_request_get_uri(req);
	r->url = _strdup(uri ? uri : "");
	r->method = _strdup(CmdToStr(evhttp_request_get_command(req)));

	struct evbuffer* in_buf = evhttp_request_get_input_buffer(req);
	size_t           body_len = in_buf ? evbuffer_get_length(in_buf) : 0;
	if (body_len > 0) {
		r->body = (char*)kitsune_malloc(body_len);
		if (r->body) {
			evbuffer_copyout(in_buf, r->body, body_len);
			r->body_len = body_len;
		}
	}

	r->isFinished = true;

	/* Parse headers into Lua table */
	lua_newtable(L);                                               /* [req_ud, tbl] */
	struct evkeyvalq* inHdrs = evhttp_request_get_input_headers(req);
	if (inHdrs) {
		struct evkeyval* kv;
		for (kv = inHdrs->tqh_first; kv != NULL; kv = kv->next.tqe_next) {
			if (!kv->key || !kv->value)
				continue;
			char   name[256];
			size_t nl = strlen(kv->key);
			if (nl >= sizeof(name))
				nl = sizeof(name) - 1;
			for (size_t i = 0; i < nl; i++)
				name[i] = (char)tolower((unsigned char)kv->key[i]);
			name[nl] = '\0';
			lua_pushstring(L, name);
			lua_pushstring(L, kv->value);
			lua_rawset(L, -3);
		}
	}
	r->headersRef = luaL_ref(L, LUA_REGISTRYINDEX);              /* [req_ud] */

	/* Create and link response */
	lua_pushhttpresponse(L);                                       /* [req_ud, resp_ud] */
	LuaHttpResponse* resp = (LuaHttpResponse*)lua_touserdata(L, -1);
	resp->connection = r;
	r->responseRef = luaL_ref(L, LUA_REGISTRYINDEX);            /* [req_ud] */

	/* Register on_complete_cb to fire the disconnect handler and clean up
	   Registry[con*] when the response write buffer is fully drained.
	   This fires before evhttp_send_done calls evhttp_request_free. */
	evhttp_request_set_on_complete_cb(req, ResponseCompleteCb, s);

	/* Store request in registry keyed by req* */
	lua_pushlightuserdata(L, (void*)req);                          /* [req_ud, light] */
	lua_pushvalue(L, -2);                                          /* [req_ud, light, req_ud] */
	lua_rawset(L, LUA_REGISTRYINDEX);                              /* [req_ud] */

	/* Also key by con* so ConnCloseCb can fire the disconnect handler even
	   after the request has been dequeued and responded to. */
	lua_pushlightuserdata(L, (void*)con);                          /* [req_ud, light] */
	lua_pushvalue(L, -2);                                          /* [req_ud, light, req_ud] */
	lua_rawset(L, LUA_REGISTRYINDEX);                              /* [req_ud] */
	lua_pop(L, 1);                                                 /* [] */

	/* Ref for queue node */
	lua_pushlightuserdata(L, (void*)req);                          /* [light] */
	lua_rawget(L, LUA_REGISTRYINDEX);                              /* [req_ud] */
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);                      /* [] */

	HttpOpenConnection* node = (HttpOpenConnection*)kitsune_malloc(sizeof(HttpOpenConnection));
	if (node) {
		node->requestRef = ref;
		node->next = NULL;
		if (s->queueTail)
			s->queueTail->next = node;
		else
			s->queueHead = node;
		s->queueTail = node;
	}
	else {
		luaL_unref(L, LUA_REGISTRYINDEX, ref);
	}
}

/* ── server teardown — idempotent, shared by Close/__gc/stop-flag ─────────── */

static void ServerTeardown(LuaHttpServer* s, lua_State* L) {
	if (!s->httpInit)
		return;
	s->httpInit = false;

	/* Disable close callbacks before evhttp_free: the close callback cannot
	   safely free evhttp_request* while evhttp_connection_free is in progress.
	   Drain loops below free orphaned user-owned requests after TAILQ_REMOVE. */
	s->L = NULL;

	/* Free evhttp — closes listen socket and connections.  For each connection
	   evhttp_connection_free does TAILQ_REMOVE on pending requests but does NOT
	   free user-owned ones (evhttp_request_free_auto checks EVHTTP_USER_OWNED). */
	if (s->http) {
		evhttp_free(s->http);
		s->http = NULL;
	}
	if (s->base) {
		event_base_free(s->base);
		s->base = NULL;
	}

	/* Drain any nodes not already cleaned up by close callbacks */
	for (HttpOpenConnection* cur = s->queueHead; cur; ) {
		HttpOpenConnection* next = cur->next;
		lua_rawgeti(L, LUA_REGISTRYINDEX, cur->requestRef);
		LuaHttpRequest* r = (LuaHttpRequest*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		if (r) {
			if (r->req) {
				/* Orphaned user-owned request: evhttp did TAILQ_REMOVE but not free.
				   Call the public evhttp_request_free which only frees memory. */
				evhttp_request_free(r->req);
				r->req = NULL;
			}
			if (r->con) {
				lua_pushlightuserdata(L, (void*)r->con);
				lua_pushnil(L);
				lua_rawset(L, LUA_REGISTRYINDEX);
				r->con = NULL;
			}
		}
		ReleaseNode(L, cur);
		cur = next;
	}
	s->queueHead = NULL;
	s->queueTail = NULL;

	for (HttpOpenConnection* cur = s->senders; cur; ) {
		HttpOpenConnection* next = cur->next;
		lua_rawgeti(L, LUA_REGISTRYINDEX, cur->requestRef);
		LuaHttpRequest* r = (LuaHttpRequest*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		if (r) {
			if (r->req) {
				/* Streaming sender that never completed: free orphaned request. */
				evhttp_request_free(r->req);
				r->req = NULL;
			}
			if (r->con) {
				lua_pushlightuserdata(L, (void*)r->con);
				lua_pushnil(L);
				lua_rawset(L, LUA_REGISTRYINDEX);
				r->con = NULL;
			}
		}
		ReleaseNode(L, cur);
		cur = next;
	}
	s->senders = NULL;

	/* Clear coroutine + server anchor */
	if (s->coroutineRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->coroutineRef);
		lua_State* co = lua_tothread(L, -1);
		lua_pop(L, 1);
		if (co) {
			lua_pushlightuserdata(L, (void*)co);
			lua_pushnil(L);
			lua_rawset(L, LUA_REGISTRYINDEX);
		}
		luaL_unref(L, LUA_REGISTRYINDEX, s->coroutineRef);
		s->coroutineRef = LUA_NOREF;
	}

	if (s->disconnectRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, s->disconnectRef);
		s->disconnectRef = LUA_NOREF;
	}

	if (s->aliveTokenRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, s->aliveTokenRef);
		s->aliveTokenRef = LUA_NOREF;
	}
}

/* ── coroutine continuation ───────────────────────────────────────────────── */

static int AcceptCont(lua_State* L, int status, lua_KContext ctx) {
	LuaHttpServer* s = (LuaHttpServer*)ctx;

	/* Stop flag passed as first resume arg */
	if (lua_toboolean(L, 1)) {
		ServerTeardown(s, L);
		return 0;
	}

	/* AliveToken check — treat a disposed token as a stop flag */
	if (s->aliveTokenRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->aliveTokenRef);
		int alive = AliveTokenIsAlive(L, -1);
		lua_pop(L, 1);
		if (alive == 0) {
			ServerTeardown(s, L);
			return 0;
		}
	}

	/* Step 1 — advance active stream senders */
	HttpOpenConnection* prev = NULL;
	HttpOpenConnection* sender = s->senders;
	while (sender) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, sender->requestRef);
		LuaHttpRequest* req = (LuaHttpRequest*)lua_touserdata(L, -1);
		lua_pop(L, 1);

		HttpOpenConnection* next = sender->next;
		bool                remove = false;

		if (req && req->responseRef != LUA_NOREF && req->req) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, req->responseRef);
			LuaHttpResponse* resp = (LuaHttpResponse*)lua_touserdata(L, -1);
			lua_pop(L, 1);

			if (resp && resp->stream) {
				LuaStreamReadChunk(L, resp->stream, 65536);
				const char* chunk = lua_tostring(L, -1);
				size_t      clen = chunk ? lua_rawlen(L, -1) : 0;
				lua_pop(L, 1);

				if (chunk && clen > 0) {
					struct evbuffer* chunkBuf = evbuffer_new();
					if (chunkBuf) {
						evbuffer_add(chunkBuf, chunk, clen);
						evhttp_send_reply_chunk(req->req, chunkBuf);
						evbuffer_free(chunkBuf);
					}
				}
				else {
					/* EOF */
					evhttp_send_reply_end(req->req);
					/* evhttp_send_done will free req when the write drains.
					   Null now so HttpRequestCleanup does not double-free. */
					req->req = NULL;
					/* Release the stream ref now that streaming is complete */
					if (resp->streamRef != LUA_NOREF) {
						luaL_unref(L, LUA_REGISTRYINDEX, resp->streamRef);
						resp->streamRef = LUA_NOREF;
					}
					resp->stream = NULL;
					remove = true;
				}
			}
			else {
				remove = true;
			}
		}
		else {
			remove = true;
		}

		if (remove) {
			if (prev)
				prev->next = next;
			else
				s->senders = next;
			ReleaseNode(L, sender);
		}
		else {
			prev = sender;
		}
		sender = next;
	}

	/* Step 2 — poll libevent (non-blocking, drains all pending I/O).
	   Update s->L to the current coroutine state so any callbacks that fire
	   during the poll (ResponseCompleteCb, ConnCloseCb) use the right L. */
	if (s->base) {
		s->L = L;
		event_base_loop(s->base, EVLOOP_NONBLOCK);
	}

	/* Step 3 — dispatch one queued request to Lua */
	if (s->queueHead) {
		HttpOpenConnection* node = s->queueHead;
		s->queueHead = node->next;
		if (!s->queueHead)
			s->queueTail = NULL;
		lua_rawgeti(L, LUA_REGISTRYINDEX, node->requestRef);
		luaL_unref(L, LUA_REGISTRYINDEX, node->requestRef);
		node->requestRef = LUA_NOREF;
		kitsune_free(node);
		return lua_yieldk(L, 1, ctx, AcceptCont);
	}

	return lua_yieldk(L, 0, ctx, AcceptCont);
}

static int AcceptBody(lua_State* L) {
	LuaHttpServer* s = luaL_checkhttpserver(L, lua_upvalueindex(1));
	return AcceptCont(L, LUA_OK, (lua_KContext)s);
}

/* ── HttpServer.Listen ────────────────────────────────────────────────────── */

int HttpServer_Listen(lua_State* L) {
	const char* addr = luaL_checkstring(L, 1);

	char        host[256];
	ev_uint16_t port;
	if (!ParseListenAddr(addr, host, sizeof(host), &port)) {
		lua_pushnil(L);
		lua_pushfstring(L, "HttpServer: invalid address '%s' (expected [host]:port)", addr);
		return 2;
	}

	LuaHttpServer* s = lua_pushhttpserver(L);
	s->L = L;

	s->base = event_base_new();
	if (!s->base) {
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushstring(L, "HttpServer: failed to create event base");
		return 2;
	}

	s->http = evhttp_new(s->base);
	if (!s->http) {
		event_base_free(s->base);
		s->base = NULL;
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushstring(L, "HttpServer: failed to create evhttp");
		return 2;
	}
	s->httpInit = true;

	/* Allow all standard HTTP methods */
	evhttp_set_allowed_methods(s->http,
		EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_PUT | EVHTTP_REQ_DELETE |
		EVHTTP_REQ_HEAD | EVHTTP_REQ_OPTIONS | EVHTTP_REQ_PATCH | EVHTTP_REQ_TRACE |
		EVHTTP_REQ_CONNECT
#ifdef EVHTTP_REQ_PROPFIND
		| EVHTTP_REQ_PROPFIND | EVHTTP_REQ_PROPPATCH
		| EVHTTP_REQ_MKCOL | EVHTTP_REQ_LOCK | EVHTTP_REQ_UNLOCK
		| EVHTTP_REQ_COPY | EVHTTP_REQ_MOVE
#endif
	);

	evhttp_set_gencb(s->http, HttpRequestCb, s);

	/* TLS is not supported without the OpenSSL libevent feature */
	if (lua_istable(L, 2)) {
		lua_getfield(L, 2, "cert");
		bool hasCert = !lua_isnil(L, -1);
		lua_pop(L, 1);
		if (hasCert) {
			ServerTeardown(s, L);
			lua_pop(L, 1);
			lua_pushnil(L);
			lua_pushstring(L, "HttpServer: TLS not supported (libevent built without OpenSSL feature)");
			return 2;
		}
	}

	const char* bindHost = (host[0] != '\0') ? host : "0.0.0.0";
	if (evhttp_bind_socket(s->http, bindHost, port) != 0) {
		ServerTeardown(s, L);
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushfstring(L, "HttpServer: failed to bind '%s'", addr);
		return 2;
	}

	return 1;
}

/* ── server:Accept ────────────────────────────────────────────────────────── */

int HttpServer_Accept(lua_State* L) {
	LuaHttpServer* s = luaL_checkhttpserver(L, 1);

	if (s->coroutineRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->coroutineRef);
		return 1;
	}

	/* Create coroutine wrapping AcceptBody (closure over server userdata) */
	lua_pushvalue(L, 1);
	lua_pushcclosure(L, AcceptBody, 1);
	lua_State* co = lua_newthread(L);
	lua_insert(L, -2);
	lua_xmove(L, co, 1);

	lua_pushvalue(L, -1);
	s->coroutineRef = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_pushlightuserdata(L, (void*)co);
	lua_pushvalue(L, 1);
	lua_rawset(L, LUA_REGISTRYINDEX);

	return 1;
}

/* ── server:SetOnDisconnect ───────────────────────────────────────────────── */

int HttpServer_SetOnDisconnect(lua_State* L) {
	LuaHttpServer* s = luaL_checkhttpserver(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);
	if (s->disconnectRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, s->disconnectRef);
		s->disconnectRef = LUA_NOREF;
	}
	lua_pushvalue(L, 2);
	s->disconnectRef = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

/* ── server:SetAliveToken ─────────────────────────────────────────────────── */

int HttpServer_SetAliveToken(lua_State* L) {
	LuaHttpServer* s = luaL_checkhttpserver(L, 1);
	if (s->aliveTokenRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, s->aliveTokenRef);
		s->aliveTokenRef = LUA_NOREF;
	}
	if (!lua_isnil(L, 2) && !lua_isnone(L, 2)) {
		luaL_checkudata(L, 2, LUAALIVETOKEN);
		lua_pushvalue(L, 2);
		s->aliveTokenRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	return 0;
}

/* ── server:Close / __gc / __tostring ────────────────────────────────────── */

int HttpServer_Close(lua_State* L) {
	LuaHttpServer* s = luaL_checkhttpserver(L, 1);
	ServerTeardown(s, L);
	return 0;
}

int HttpServer_GC(lua_State* L) {
	LuaHttpServer* s = luaL_checkhttpserver(L, 1);
	ServerTeardown(s, L);
	return 0;
}

int HttpServer_ToString(lua_State* L) {
	LuaHttpServer* s = luaL_checkhttpserver(L, 1);
	if (s->httpInit)
		lua_pushfstring(L, "HttpServer(listening)");
	else
		lua_pushfstring(L, "HttpServer(closed)");
	return 1;
}
