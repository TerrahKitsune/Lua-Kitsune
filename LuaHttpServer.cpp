#include "LuaHttpServer.h"
#include "LuaHttpRequest.h"
#include "stream.h"
#include <event2/event.h>
#include <event2/http.h>
#include <event2/buffer.h>
#include <event2/keyvalq_struct.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* ── defensive helpers ────────────────────────────────────────────────────── */

static void safe_free_s(char** p) {
	if (*p) {
		free(*p);
		*p = NULL;
	}
}

/* ── node release ─────────────────────────────────────────────────────────── */

/* Tears down one HttpOpenConnection node. Idempotent. */
static void release_node(lua_State* L, HttpOpenConnection* node) {
	if (node->request_ref != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, node->request_ref);
		LuaHttpRequest* req = (LuaHttpRequest*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		if (req)
			HttpRequest_Cleanup(L, req);
		luaL_unref(L, LUA_REGISTRYINDEX, node->request_ref);
		node->request_ref = LUA_NOREF;
	}
	free(node);
}

/* ── push / check ─────────────────────────────────────────────────────────── */

LuaHttpServer* lua_pushhttpserver(lua_State* L) {
	LuaHttpServer* s = (LuaHttpServer*)lua_newuserdata(L, sizeof(LuaHttpServer));
	luaL_getmetatable(L, LUAHTTPSERVER);
	lua_setmetatable(L, -2);
	memset(s, 0, sizeof(LuaHttpServer));
	s->coroutine_ref = LUA_NOREF;
	s->disconnect_ref = LUA_NOREF;
	return s;
}

LuaHttpServer* lua_checkhttpserver(lua_State* L, int idx) {
	LuaHttpServer* s = (LuaHttpServer*)luaL_checkudata(L, idx, LUAHTTPSERVER);
	if (!s)
		luaL_error(L, "parameter is not a %s", LUAHTTPSERVER);
	return s;
}

/* ── per-request queue / sender cleanup ───────────────────────────────────── */

static void queue_remove_req(LuaHttpServer* s, lua_State* L, struct evhttp_request* req) {
	HttpOpenConnection* prev = NULL;
	HttpOpenConnection* cur = s->queue_head;
	while (cur) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, cur->request_ref);
		LuaHttpRequest* r = (LuaHttpRequest*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		HttpOpenConnection* next = cur->next;
		if (r && r->req == req) {
			if (prev)
				prev->next = next;
			else
				s->queue_head = next;
			if (s->queue_tail == cur)
				s->queue_tail = prev;
			release_node(L, cur);
		}
		else {
			prev = cur;
		}
		cur = next;
	}
}

static void senders_remove_req(LuaHttpServer* s, lua_State* L, struct evhttp_request* req,
	LuaHttpResponse* resp) {
	HttpOpenConnection* prev = NULL;
	HttpOpenConnection* cur = s->senders;
	while (cur) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, cur->request_ref);
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
			release_node(L, cur);
			break;
		}
		else {
			prev = cur;
		}
		cur = next;
	}
}

/* Full teardown for one request on close / error. */
static void conn_close_cleanup(LuaHttpServer* s, lua_State* L,
	struct evhttp_request* req, const char* errmsg) {
	/* 1. Remove stale queue and sender nodes */
	queue_remove_req(s, L, req);

	lua_pushlightuserdata(L, (void*)req);
	lua_rawget(L, LUA_REGISTRYINDEX);
	LuaHttpRequest* r = (LuaHttpRequest*)lua_touserdata(L, -1);
	lua_pop(L, 1);

	LuaHttpResponse* resp = NULL;
	if (r && r->response_ref != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, r->response_ref);
		resp = (LuaHttpResponse*)lua_touserdata(L, -1);
		lua_pop(L, 1);
	}

	senders_remove_req(s, L, req, resp);

	/* 2. Mark finalized */
	if (resp)
		resp->finalized = true;

	/* 3. Disconnect handler or error event */
	if (r) {
		if (errmsg) {
			safe_free_s(&r->error_msg);
			r->error_msg = _strdup(errmsg);
			r->is_error = true;
		}

		if (s->disconnect_ref != LUA_NOREF) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, s->disconnect_ref);
			lua_pushlightuserdata(L, (void*)req);
			lua_rawget(L, LUA_REGISTRYINDEX);
			lua_pcall_nohook(L, 1, 0, 0);
		}
		else if (r->is_error) {
			/* Enqueue a final event so GetError() surfaces it */
			lua_pushlightuserdata(L, (void*)req);
			lua_rawget(L, LUA_REGISTRYINDEX);
			int ref = luaL_ref(L, LUA_REGISTRYINDEX);
			HttpOpenConnection* node = (HttpOpenConnection*)malloc(sizeof(HttpOpenConnection));
			if (node) {
				node->request_ref = ref;
				node->next = NULL;
				if (s->queue_tail)
					s->queue_tail->next = node;
				else
					s->queue_head = node;
				s->queue_tail = node;
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
   was registered under Registry[con*] in http_request_cb.
   - If r->req is still set, the connection closed before we responded:
	 run full cleanup (removes from queue, fires disconnect/error handler).
   - If r->req is NULL, we already sent a response: just fire the disconnect
	 handler so Lua code that called SetOnDisconnect can react to the close. */
static void conn_close_cb(struct evhttp_connection* con, void* arg) {
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
		conn_close_cleanup(s, L, r->req, NULL);
	}
	else {
		/* Response already sent — fire the disconnect callback if registered. */
		if (s->disconnect_ref != LUA_NOREF) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, s->disconnect_ref);
			lua_pushlightuserdata(L, (void*)con);
			lua_rawget(L, LUA_REGISTRYINDEX); /* push LuaHttpRequest userdata as arg */
			lua_pcall_nohook(L, 1, 0, 0);
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
   real conn_close_cb (TCP close) does not double-fire the handler. */
static void response_complete_cb(struct evhttp_request* req, void* arg) {
	LuaHttpServer* s = (LuaHttpServer*)arg;
	if (!s || !s->L)
		return;
	lua_State* L = s->L;

	struct evhttp_connection* con = evhttp_request_get_connection(req);
	if (!con)
		return;

	if (s->disconnect_ref != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->disconnect_ref);
		lua_pushlightuserdata(L, (void*)con);
		lua_rawget(L, LUA_REGISTRYINDEX); /* LuaHttpRequest userdata */
		if (lua_isuserdata(L, -1)) {
			lua_pcall_nohook(L, 1, 0, 0);
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

static const char* cmd_to_str(enum evhttp_cmd_type t) {
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
	case EVHTTP_REQ_PROPFIND: return "PROPFIND";
	case EVHTTP_REQ_PROPPATCH:return "PROPPATCH";
	case EVHTTP_REQ_MKCOL:    return "MKCOL";
	case EVHTTP_REQ_LOCK:     return "LOCK";
	case EVHTTP_REQ_UNLOCK:   return "UNLOCK";
	case EVHTTP_REQ_COPY:     return "COPY";
	case EVHTTP_REQ_MOVE:     return "MOVE";
	default:                  return "UNKNOWN";
	}
}

/* ── address parser ───────────────────────────────────────────────────────── */

/* Accepts: "host:port", "http://host:port", "https://host:port", ":port" */
static bool parse_listen_addr(const char* addr, char* host_out, size_t host_sz,
	ev_uint16_t* port_out) {
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
		host_out[0] = '\0';
	}
	else {
		if (hlen >= host_sz)
			return false;
		memcpy(host_out, p, hlen);
		host_out[hlen] = '\0';
	}

	int port_val = atoi(colon + 1);
	if (port_val <= 0 || port_val > 65535)
		return false;
	*port_out = (ev_uint16_t)port_val;
	return true;
}

/* ── libevent request callback ────────────────────────────────────────────── */

static void http_request_cb(struct evhttp_request* req, void* arg) {
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
		evhttp_connection_set_closecb(con, conn_close_cb, s);

	/* ── Build LuaHttpRequest ── */
	lua_pushhttprequest(L);                                        /* [req_ud] */
	LuaHttpRequest* r = (LuaHttpRequest*)lua_touserdata(L, -1);
	r->req = req;
	r->con = con;
	r->server = s;

	const char* uri = evhttp_request_get_uri(req);
	r->url = _strdup(uri ? uri : "");
	r->method = _strdup(cmd_to_str(evhttp_request_get_command(req)));

	struct evbuffer* in_buf = evhttp_request_get_input_buffer(req);
	size_t           body_len = in_buf ? evbuffer_get_length(in_buf) : 0;
	if (body_len > 0) {
		r->body = (char*)malloc(body_len);
		if (r->body) {
			evbuffer_copyout(in_buf, r->body, body_len);
			r->body_len = body_len;
		}
	}

	r->is_finished = true;

	/* Parse headers into Lua table */
	lua_newtable(L);                                               /* [req_ud, tbl] */
	struct evkeyvalq* in_hdrs = evhttp_request_get_input_headers(req);
	if (in_hdrs) {
		struct evkeyval* kv;
		for (kv = in_hdrs->tqh_first; kv != NULL; kv = kv->next.tqe_next) {
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
	r->headers_ref = luaL_ref(L, LUA_REGISTRYINDEX);              /* [req_ud] */

	/* Create and link response */
	lua_pushhttpresponse(L);                                       /* [req_ud, resp_ud] */
	LuaHttpResponse* resp = (LuaHttpResponse*)lua_touserdata(L, -1);
	resp->connection = r;
	r->response_ref = luaL_ref(L, LUA_REGISTRYINDEX);            /* [req_ud] */

	/* Register on_complete_cb to fire the disconnect handler and clean up
	   Registry[con*] when the response write buffer is fully drained.
	   This fires before evhttp_send_done calls evhttp_request_free. */
	evhttp_request_set_on_complete_cb(req, response_complete_cb, s);

	/* Store request in registry keyed by req* */
	lua_pushlightuserdata(L, (void*)req);                          /* [req_ud, light] */
	lua_pushvalue(L, -2);                                          /* [req_ud, light, req_ud] */
	lua_rawset(L, LUA_REGISTRYINDEX);                              /* [req_ud] */

	/* Also key by con* so conn_close_cb can fire the disconnect handler even
	   after the request has been dequeued and responded to. */
	lua_pushlightuserdata(L, (void*)con);                          /* [req_ud, light] */
	lua_pushvalue(L, -2);                                          /* [req_ud, light, req_ud] */
	lua_rawset(L, LUA_REGISTRYINDEX);                              /* [req_ud] */
	lua_pop(L, 1);                                                 /* [] */

	/* Ref for queue node */
	lua_pushlightuserdata(L, (void*)req);                          /* [light] */
	lua_rawget(L, LUA_REGISTRYINDEX);                              /* [req_ud] */
	int ref = luaL_ref(L, LUA_REGISTRYINDEX);                      /* [] */

	HttpOpenConnection* node = (HttpOpenConnection*)malloc(sizeof(HttpOpenConnection));
	if (node) {
		node->request_ref = ref;
		node->next = NULL;
		if (s->queue_tail)
			s->queue_tail->next = node;
		else
			s->queue_head = node;
		s->queue_tail = node;
	}
	else {
		luaL_unref(L, LUA_REGISTRYINDEX, ref);
	}
}

/* ── server teardown — idempotent, shared by Close/__gc/stop-flag ─────────── */

static void server_teardown(LuaHttpServer* s, lua_State* L) {
	if (!s->http_init)
		return;
	s->http_init = false;

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
	for (HttpOpenConnection* cur = s->queue_head; cur; ) {
		HttpOpenConnection* next = cur->next;
		lua_rawgeti(L, LUA_REGISTRYINDEX, cur->request_ref);
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
		release_node(L, cur);
		cur = next;
	}
	s->queue_head = NULL;
	s->queue_tail = NULL;

	for (HttpOpenConnection* cur = s->senders; cur; ) {
		HttpOpenConnection* next = cur->next;
		lua_rawgeti(L, LUA_REGISTRYINDEX, cur->request_ref);
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
		release_node(L, cur);
		cur = next;
	}
	s->senders = NULL;

	/* Clear coroutine + server anchor */
	if (s->coroutine_ref != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->coroutine_ref);
		lua_State* co = lua_tothread(L, -1);
		lua_pop(L, 1);
		if (co) {
			lua_pushlightuserdata(L, (void*)co);
			lua_pushnil(L);
			lua_rawset(L, LUA_REGISTRYINDEX);
		}
		luaL_unref(L, LUA_REGISTRYINDEX, s->coroutine_ref);
		s->coroutine_ref = LUA_NOREF;
	}

	if (s->disconnect_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, s->disconnect_ref);
		s->disconnect_ref = LUA_NOREF;
	}
}

/* ── coroutine continuation ───────────────────────────────────────────────── */

static int accept_cont(lua_State* L, int status, lua_KContext ctx) {
	LuaHttpServer* s = (LuaHttpServer*)ctx;

	/* Stop flag passed as first resume arg */
	if (lua_toboolean(L, 1)) {
		server_teardown(s, L);
		return 0;
	}

	/* Step 1 — advance active stream senders */
	HttpOpenConnection* prev = NULL;
	HttpOpenConnection* sender = s->senders;
	while (sender) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, sender->request_ref);
		LuaHttpRequest* req = (LuaHttpRequest*)lua_touserdata(L, -1);
		lua_pop(L, 1);

		HttpOpenConnection* next = sender->next;
		bool                remove = false;

		if (req && req->response_ref != LUA_NOREF && req->req) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, req->response_ref);
			LuaHttpResponse* resp = (LuaHttpResponse*)lua_touserdata(L, -1);
			lua_pop(L, 1);

			if (resp && resp->stream) {
				lua_stream_read_chunk(L, resp->stream, 65536);
				const char* chunk = lua_tostring(L, -1);
				size_t      clen = chunk ? lua_rawlen(L, -1) : 0;
				lua_pop(L, 1);

				if (chunk && clen > 0) {
					struct evbuffer* chunk_buf = evbuffer_new();
					if (chunk_buf) {
						evbuffer_add(chunk_buf, chunk, clen);
						evhttp_send_reply_chunk(req->req, chunk_buf);
						evbuffer_free(chunk_buf);
					}
				}
				else {
					/* EOF */
					evhttp_send_reply_end(req->req);
					/* evhttp_send_done will free req when the write drains.
					   Null now so HttpRequest_Cleanup does not double-free. */
					req->req = NULL;
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
			release_node(L, sender);
		}
		else {
			prev = sender;
		}
		sender = next;
	}

	/* Step 2 — poll libevent (non-blocking, drains all pending I/O).
	   Update s->L to the current coroutine state so any callbacks that fire
	   during the poll (response_complete_cb, conn_close_cb) use the right L. */
	if (s->base) {
		s->L = L;
		event_base_loop(s->base, EVLOOP_NONBLOCK);
	}

	/* Step 3 — dispatch one queued request to Lua */
	if (s->queue_head) {
		HttpOpenConnection* node = s->queue_head;
		s->queue_head = node->next;
		if (!s->queue_head)
			s->queue_tail = NULL;
		lua_rawgeti(L, LUA_REGISTRYINDEX, node->request_ref);
		luaL_unref(L, LUA_REGISTRYINDEX, node->request_ref);
		node->request_ref = LUA_NOREF;
		free(node);
		return lua_yieldk(L, 1, ctx, accept_cont);
	}

	return lua_yieldk(L, 0, ctx, accept_cont);
}

static int accept_body(lua_State* L) {
	LuaHttpServer* s = lua_checkhttpserver(L, lua_upvalueindex(1));
	return accept_cont(L, LUA_OK, (lua_KContext)s);
}

/* ── HttpServer.Listen ────────────────────────────────────────────────────── */

int HttpServer_Listen(lua_State* L) {
	const char* addr = luaL_checkstring(L, 1);

	char        host[256];
	ev_uint16_t port;
	if (!parse_listen_addr(addr, host, sizeof(host), &port)) {
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
	s->http_init = true;

	/* Allow all standard HTTP methods */
	evhttp_set_allowed_methods(s->http,
		EVHTTP_REQ_GET | EVHTTP_REQ_POST | EVHTTP_REQ_PUT | EVHTTP_REQ_DELETE |
		EVHTTP_REQ_HEAD | EVHTTP_REQ_OPTIONS | EVHTTP_REQ_PATCH | EVHTTP_REQ_TRACE |
		EVHTTP_REQ_CONNECT | EVHTTP_REQ_PROPFIND | EVHTTP_REQ_PROPPATCH |
		EVHTTP_REQ_MKCOL | EVHTTP_REQ_LOCK | EVHTTP_REQ_UNLOCK |
		EVHTTP_REQ_COPY | EVHTTP_REQ_MOVE);

	evhttp_set_gencb(s->http, http_request_cb, s);

	/* TLS is not supported without the OpenSSL libevent feature */
	if (lua_istable(L, 2)) {
		lua_getfield(L, 2, "cert");
		bool has_cert = !lua_isnil(L, -1);
		lua_pop(L, 1);
		if (has_cert) {
			server_teardown(s, L);
			lua_pop(L, 1);
			lua_pushnil(L);
			lua_pushstring(L, "HttpServer: TLS not supported (libevent built without OpenSSL feature)");
			return 2;
		}
	}

	const char* bind_host = (host[0] != '\0') ? host : "0.0.0.0";
	if (evhttp_bind_socket(s->http, bind_host, port) != 0) {
		server_teardown(s, L);
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushfstring(L, "HttpServer: failed to bind '%s'", addr);
		return 2;
	}

	return 1;
}

/* ── server:Accept ────────────────────────────────────────────────────────── */

int HttpServer_Accept(lua_State* L) {
	LuaHttpServer* s = lua_checkhttpserver(L, 1);

	if (s->coroutine_ref != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, s->coroutine_ref);
		return 1;
	}

	/* Create coroutine wrapping accept_body (closure over server userdata) */
	lua_pushvalue(L, 1);
	lua_pushcclosure(L, accept_body, 1);
	lua_State* co = lua_newthread(L);
	lua_insert(L, -2);
	lua_xmove(L, co, 1);

	lua_pushvalue(L, -1);
	s->coroutine_ref = luaL_ref(L, LUA_REGISTRYINDEX);

	lua_pushlightuserdata(L, (void*)co);
	lua_pushvalue(L, 1);
	lua_rawset(L, LUA_REGISTRYINDEX);

	return 1;
}

/* ── server:SetOnDisconnect ───────────────────────────────────────────────── */

int HttpServer_SetOnDisconnect(lua_State* L) {
	LuaHttpServer* s = lua_checkhttpserver(L, 1);
	luaL_checktype(L, 2, LUA_TFUNCTION);
	if (s->disconnect_ref != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, s->disconnect_ref);
		s->disconnect_ref = LUA_NOREF;
	}
	lua_pushvalue(L, 2);
	s->disconnect_ref = luaL_ref(L, LUA_REGISTRYINDEX);
	return 0;
}

/* ── server:Close / __gc / __tostring ────────────────────────────────────── */

int HttpServer_Close(lua_State* L) {
	LuaHttpServer* s = lua_checkhttpserver(L, 1);
	server_teardown(s, L);
	return 0;
}

int HttpServer_GC(lua_State* L) {
	LuaHttpServer* s = lua_checkhttpserver(L, 1);
	server_teardown(s, L);
	return 0;
}

int HttpServer_ToString(lua_State* L) {
	LuaHttpServer* s = lua_checkhttpserver(L, 1);
	if (s->http_init)
		lua_pushfstring(L, "HttpServer(listening)");
	else
		lua_pushfstring(L, "HttpServer(closed)");
	return 1;
}
