#ifdef KITSUNE_HTTP

#include "HttpCurl.h"
#include "stream.h"
#include "mem.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// -- Registry anchor keys (static-address trick) -------------------------------
const char g_curlm_key    = 0;
const char g_sentinel_key = 0;

// -- CURLM* sentinel __gc -----------------------------------------------------
int http_sentinel_gc(lua_State* L) {
	lua_rawgetp(L, LUA_REGISTRYINDEX, &g_curlm_key);
	CURLM* multi = (CURLM*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (multi) {
		curl_multi_cleanup(multi);
		lua_pushnil(L);
		lua_rawsetp(L, LUA_REGISTRYINDEX, &g_curlm_key);
	}
	return 0;
}

// -----------------------------------------------------------------------------
// T4b ? Http.UrlEncode / Http.UrlDecode
// -----------------------------------------------------------------------------

int UrlEncode(lua_State* L) {
	size_t len;
	const char* data = luaL_checklstring(L, 1, &len);
	size_t allocSize = len * 3 + 1;
	char* buf = (char*)kitsune_malloc(allocSize);
	if (!buf)
		return luaL_error(L, "out of memory");
	static const char hex[] = "0123456789abcdef";
	int pos = 0;
	for (size_t i = 0; i < len; i++) {
		unsigned char c = (unsigned char)data[i];
		if (('a' <= c && c <= 'z') || ('A' <= c && c <= 'Z') ||
			('0' <= c && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
			buf[pos++] = (char)c;
		} else {
			buf[pos++] = '%';
			buf[pos++] = hex[c >> 4];
			buf[pos++] = hex[c & 15];
		}
	}
	buf[pos] = '\0';
	lua_pushlstring(L, buf, (size_t)pos);
	kitsune_free(buf);
	return 1;
}

static inline int ishex(int x) {
	return (x >= '0' && x <= '9') || (x >= 'a' && x <= 'f') || (x >= 'A' && x <= 'F');
}

int UrlDecode(lua_State* L) {
	size_t len;
	const char* s = luaL_checklstring(L, 1, &len);
	char* buf = (char*)kitsune_malloc(len + 1);
	if (!buf)
		return luaL_error(L, "out of memory");
	char* o = buf;
	const char* end = s + len;
	while (s < end) {
		int c = (unsigned char)*s++;
		if (c == '+') {
			c = ' ';
		} else if (c == '%' && s + 1 < end && ishex((unsigned char)s[0]) && ishex((unsigned char)s[1])) {
			unsigned int v;
			sscanf(s, "%2x", &v);
			c = (int)v;
			s += 2;
		}
		*o++ = (char)c;
	}
	lua_pushlstring(L, buf, (size_t)(o - buf));
	kitsune_free(buf);
	return 1;
}

// -----------------------------------------------------------------------------
// T4c-f ? LuaHttpClient methods
// -----------------------------------------------------------------------------

int http_create(lua_State* L) {
	LuaHttpClient* client = (LuaHttpClient*)lua_newuserdata(L, sizeof(LuaHttpClient));
	memset(client, 0, sizeof(LuaHttpClient));
	luaL_setmetatable(L, LUAHTTPCLIENT);
	client->defaultHeadersRef = LUA_NOREF;
	client->followRedirects   = true;
	client->verifySsl         = true;
	client->timeoutMs         = 0;
	return 1;
}

int client_set_timeout(lua_State* L) {
	LuaHttpClient* client = (LuaHttpClient*)luaL_checkudata(L, 1, LUAHTTPCLIENT);
	client->timeoutMs = (long)luaL_checkinteger(L, 2);
	return 0;
}

int client_set_default_header(lua_State* L) {
	LuaHttpClient* client = (LuaHttpClient*)luaL_checkudata(L, 1, LUAHTTPCLIENT);
	luaL_checkstring(L, 2);
	luaL_checkstring(L, 3);
	// Get-or-create the default headers table in the registry
	if (client->defaultHeadersRef == LUA_NOREF) {
		lua_newtable(L);
		client->defaultHeadersRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, client->defaultHeadersRef);
	lua_pushvalue(L, 2);
	lua_pushvalue(L, 3);
	lua_rawset(L, -3);
	lua_pop(L, 1);
	return 0;
}

int client_set_follow_redirects(lua_State* L) {
	LuaHttpClient* client = (LuaHttpClient*)luaL_checkudata(L, 1, LUAHTTPCLIENT);
	client->followRedirects = lua_toboolean(L, 2) != 0;
	return 0;
}

int client_set_verify_ssl(lua_State* L) {
	LuaHttpClient* client = (LuaHttpClient*)luaL_checkudata(L, 1, LUAHTTPCLIENT);
	client->verifySsl = lua_toboolean(L, 2) != 0;
	return 0;
}

int luahttpclient_gc(lua_State* L) {
	LuaHttpClient* client = (LuaHttpClient*)luaL_checkudata(L, 1, LUAHTTPCLIENT);
	if (client->defaultHeadersRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, client->defaultHeadersRef);
		client->defaultHeadersRef = LUA_NOREF;
	}
	return 0;
}

int luahttpclient_tostring(lua_State* L) {
	lua_pushfstring(L, "HttpClient(%p)", lua_touserdata(L, 1));
	return 1;
}

// -----------------------------------------------------------------------------
// Header array helpers shared by buffered and streaming paths
// -----------------------------------------------------------------------------

static bool append_header(char*** keys, char*** vals, int* count, int* alloc,
	const char* key, size_t keyLen, const char* val, size_t valLen) {
	if (*count >= *alloc) {
		int newAlloc = *alloc ? *alloc * 2 : 8;
		char** newK = (char**)kitsune_realloc(*keys, (size_t)newAlloc * sizeof(char*));
		if (!newK)
			return false;
		*keys = newK;
		char** newV = (char**)kitsune_realloc(*vals, (size_t)newAlloc * sizeof(char*));
		if (!newV)
			return false;
		*vals  = newV;
		*alloc = newAlloc;
	}
	char* k = (char*)kitsune_malloc(keyLen + 1);
	char* v = (char*)kitsune_malloc(valLen + 1);
	if (!k || !v) {
		kitsune_free(k);
		kitsune_free(v);
		return false;
	}
	memcpy(k, key, keyLen); k[keyLen] = '\0';
	memcpy(v, val, valLen); v[valLen] = '\0';
	(*keys)[*count] = k;
	(*vals)[*count] = v;
	(*count)++;
	return true;
}

static void free_header_arrays(char** keys, char** vals, int count) {
	for (int i = 0; i < count; i++) {
		kitsune_free(keys[i]);
		kitsune_free(vals[i]);
	}
	kitsune_free(keys);
	kitsune_free(vals);
}

// Build a curl_slist from client defaults + per-call headers table (at headersIdx, 0 = absent).
static struct curl_slist* build_headers(lua_State* L, LuaHttpClient* client, int headersIdx) {
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

// -----------------------------------------------------------------------------
// T5a ? WriteBodyCallback
// -----------------------------------------------------------------------------

static size_t WriteBodyCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
	LuaHttpRequest* req = (LuaHttpRequest*)userdata;
	size_t total = size * nmemb;
	if (req->streamOutput && req->streamOutput->vtbl && req->streamOutput->vtbl->write) {
		bool ok = req->streamOutput->vtbl->write(req->streamOutput->native,
			(const BYTE*)ptr, total);
		return ok ? total : 0;
	}
	// Heap-buffer accumulation path
	if (req->bodyLen + total + 1 > req->bodyAlloc) {
		size_t newAlloc = req->bodyLen + total + 8192;
		char* nb = (char*)kitsune_realloc(req->body, newAlloc);
		if (!nb)
			return 0;
		req->body      = nb;
		req->bodyAlloc = newAlloc;
	}
	memcpy(req->body + req->bodyLen, ptr, total);
	req->bodyLen += total;
	req->body[req->bodyLen] = '\0';
	return total;
}

// -----------------------------------------------------------------------------
// T5b ? ReadBodyCallback
// -----------------------------------------------------------------------------

static size_t ReadBodyCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
	LuaHttpRequest* req = (LuaHttpRequest*)userdata;
	if (!req->streamInput || !req->streamInput->vtbl || !req->callbackL)
		return 0;
	size_t capacity = size * nitems;
	lua_State* L = req->callbackL;
	req->streamInput->vtbl->read(req->streamInput->native, L, capacity);
	if (lua_type(L, -1) != LUA_TSTRING) {
		lua_pop(L, 1);
		return 0;
	}
	size_t nread = 0;
	const char* data = lua_tolstring(L, -1, &nread);
	if (nread > capacity)
		nread = capacity;
	memcpy(buffer, data, nread);
	lua_pop(L, 1);
	return nread;
}

// -----------------------------------------------------------------------------
// T5c ? WriteHeaderCallback (buffered request path)
// -----------------------------------------------------------------------------

static size_t WriteHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
	LuaHttpRequest* req = (LuaHttpRequest*)userdata;
	size_t total = size * nitems;
	// Status line: "HTTP/X.Y CODE REASON\r\n"
	if (total >= 5 && strncmp(buffer, "HTTP/", 5) == 0) {
		char* sp1 = (char*)memchr(buffer + 5, ' ', total - 5);
		if (sp1) {
			char* sp2 = (char*)memchr(sp1 + 1, ' ', total - (size_t)(sp1 + 1 - buffer));
			if (sp2) {
				size_t tlen = total - (size_t)(sp2 + 1 - buffer);
				if (tlen >= 2) tlen -= 2; // strip \r\n
				if (tlen > 0 && tlen < sizeof(req->statusText)) {
					memcpy(req->statusText, sp2 + 1, tlen);
					req->statusText[tlen] = '\0';
				}
			}
		}
		return total;
	}
	if (total <= 2)
		return total; // blank separator line
	char* colon = (char*)memchr(buffer, ':', total);
	if (!colon)
		return total;
	const char* valStart = colon + 1;
	while (valStart < buffer + total && *valStart == ' ')
		valStart++;
	size_t keyLen = (size_t)(colon - buffer);
	size_t valLen = total - (size_t)(valStart - buffer);
	if (valLen >= 2) valLen -= 2; // strip \r\n
	append_header(&req->headerKeys, &req->headerVals,
		&req->headerCount, &req->headerAlloc,
		buffer, keyLen, valStart, valLen);
	return total;
}

// -----------------------------------------------------------------------------
// T5e ? BuildHttpResultTable
// -----------------------------------------------------------------------------

static int BuildHttpResultTable(lua_State* L, LuaHttpRequest* req) {
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
		lua_pushinteger(L, req->httpCode);
		lua_setfield(L, -2, "Code");
		lua_pushstring(L, req->statusText);
		lua_setfield(L, -2, "Status");
		if (!req->streamOutput) {
			lua_pushlstring(L, req->body ? req->body : "", req->bodyLen);
			lua_setfield(L, -2, "Contents");
		}
		lua_newtable(L);
		for (int i = 0; i < req->headerCount; i++) {
			lua_pushstring(L, req->headerKeys[i]);
			lua_pushstring(L, req->headerVals[i]);
			lua_rawset(L, -3);
		}
		lua_setfield(L, -2, "Headers");
	} else {
		lua_pushstring(L, req->errorBuf[0] ? req->errorBuf : "request failed");
		lua_setfield(L, -2, "Status");
	}
	return 1;
}

// -----------------------------------------------------------------------------
// T5d ? HttpRequestContinuation
// -----------------------------------------------------------------------------

static int HttpRequestContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaHttpRequest* req = (LuaHttpRequest*)luaL_checkudata(L, 1, LUAHTTPREQUEST);
	int running = 0;
	req->callbackL = L;
	curl_multi_perform(req->multi, &running);
	req->callbackL = NULL;
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

static int HttpRequestEntry(lua_State* L) {
	return HttpRequestContinuation(L, LUA_OK, 0);
}

// -----------------------------------------------------------------------------
// T5f ? client_request
// -----------------------------------------------------------------------------

int client_request(lua_State* L) {
	LuaHttpClient* client = (LuaHttpClient*)luaL_checkudata(L, 1, LUAHTTPCLIENT);
	const char* method = luaL_checkstring(L, 2);
	const char* url    = luaL_checkstring(L, 3);

	// Arg 4: body ? string, native Stream, or nil
	size_t      bodyLen    = 0;
	const char* body       = NULL;
	LuaStream*  streamInput  = NULL;
	LuaStream*  streamOutput = NULL;

	if (lua_type(L, 4) == LUA_TSTRING) {
		body = lua_tolstring(L, 4, &bodyLen);
	} else if (lua_isuserdata(L, 4) && lua_isstream(L, 4)) {
		streamInput = lua_toluastream(L, 4);
		if (!streamInput->vtbl) {
			lua_pushnil(L);
			lua_pushstring(L, "stream body must be a native-backend stream (vtbl != NULL)");
			return 2;
		}
	}

	// Arg 5: per-call headers table (optional)
	int headersIdx = lua_istable(L, 5) ? 5 : 0;

	// Arg 6: outStream (optional)
	if (lua_isuserdata(L, 6) && lua_isstream(L, 6)) {
		streamOutput = lua_toluastream(L, 6);
		if (!streamOutput->vtbl || !streamOutput->vtbl->write) {
			lua_pushnil(L);
			lua_pushstring(L, "outStream must be a native-backend stream (vtbl->write != NULL)");
			return 2;
		}
	}

	// Retrieve the shared CURLM* from the registry
	lua_rawgetp(L, LUA_REGISTRYINDEX, &g_curlm_key);
	CURLM* multi = (CURLM*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (!multi) {
		lua_pushnil(L);
		lua_pushstring(L, "HTTP module not initialised");
		return 2;
	}

	// Allocate LuaHttpRequest userdata
	LuaHttpRequest* req = (LuaHttpRequest*)lua_newuserdata(L, sizeof(LuaHttpRequest));
	memset(req, 0, sizeof(LuaHttpRequest));
	luaL_setmetatable(L, LUAHTTPREQUEST);
	req->streamOutputRef = LUA_NOREF;
	req->streamInputRef  = LUA_NOREF;
	int reqIdx = lua_gettop(L);

	req->multi = multi;
	req->easy  = curl_easy_init();
	if (!req->easy) {
		lua_pushnil(L);
		lua_pushstring(L, "curl_easy_init failed");
		return 2;
	}

	// Build and store headers slist
	req->requestHdrs = build_headers(L, client, headersIdx);

	curl_easy_setopt(req->easy, CURLOPT_URL,            url);
	curl_easy_setopt(req->easy, CURLOPT_CUSTOMREQUEST,  method);
	curl_easy_setopt(req->easy, CURLOPT_HTTPHEADER,     req->requestHdrs);
	curl_easy_setopt(req->easy, CURLOPT_WRITEFUNCTION,  WriteBodyCallback);
	curl_easy_setopt(req->easy, CURLOPT_WRITEDATA,      req);
	curl_easy_setopt(req->easy, CURLOPT_HEADERFUNCTION, WriteHeaderCallback);
	curl_easy_setopt(req->easy, CURLOPT_HEADERDATA,     req);
	curl_easy_setopt(req->easy, CURLOPT_ERRORBUFFER,    req->errorBuf);
	curl_easy_setopt(req->easy, CURLOPT_FOLLOWLOCATION, client->followRedirects ? 1L : 0L);
	curl_easy_setopt(req->easy, CURLOPT_SSL_VERIFYPEER, client->verifySsl ? 1L : 0L);
	curl_easy_setopt(req->easy, CURLOPT_SSL_VERIFYHOST, client->verifySsl ? 2L : 0L);
	curl_easy_setopt(req->easy, CURLOPT_NOSIGNAL,       1L);
	if (client->timeoutMs > 0)
		curl_easy_setopt(req->easy, CURLOPT_TIMEOUT_MS, (long)client->timeoutMs);

	if (streamInput) {
		lua_pushvalue(L, 4);
		req->streamInput    = streamInput;
		req->streamInputRef = luaL_ref(L, LUA_REGISTRYINDEX);
		curl_easy_setopt(req->easy, CURLOPT_UPLOAD,       1L);
		curl_easy_setopt(req->easy, CURLOPT_READFUNCTION, ReadBodyCallback);
		curl_easy_setopt(req->easy, CURLOPT_READDATA,     req);
		curl_off_t uploadSize = -1;
		if (streamInput->Caps & STREAM_CAP_SEEK) {
			lua_Integer pos = streamInput->vtbl->curpos(streamInput->native);
			lua_Integer len = streamInput->vtbl->getlen(streamInput->native);
			uploadSize = (curl_off_t)(len - pos);
		}
		curl_easy_setopt(req->easy, CURLOPT_INFILESIZE_LARGE, uploadSize);
	} else if (body && bodyLen > 0) {
		curl_easy_setopt(req->easy, CURLOPT_POSTFIELDS,    body);
		curl_easy_setopt(req->easy, CURLOPT_POSTFIELDSIZE, (long)bodyLen);
	}

	if (streamOutput) {
		lua_pushvalue(L, 6);
		req->streamOutput    = streamOutput;
		req->streamOutputRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	curl_multi_add_handle(multi, req->easy);
	req->addedToMulti = true;

	// Create the coroutine; prime it so the first user coroutine.resume resumes
	// from the yieldk point rather than trying to call req as a function.
	lua_State* T = lua_newthread(L);    // T pushed on L's stack
	lua_pushcfunction(T, HttpRequestEntry);
	lua_pushvalue(L, reqIdx);
	lua_xmove(L, T, 1);                 // T: [HttpRequestEntry, req]
	int nres = 0;
	int rc = lua_resume(T, L, 1, &nres);
	if (rc != LUA_YIELD) {
		// Immediate failure ? req's __gc will remove it from multi
		lua_pushnil(L);
		if (nres > 0)
			lua_xmove(T, L, 1);
		else
			lua_pushstring(L, "request failed to start");
		return 2;
	}
	return 1;  // T is on top of L's stack
}

// -----------------------------------------------------------------------------
// T5g ? luahttprequest_gc
// -----------------------------------------------------------------------------

int luahttprequest_gc(lua_State* L) {
	LuaHttpRequest* req = (LuaHttpRequest*)luaL_checkudata(L, 1, LUAHTTPREQUEST);
	if (req->addedToMulti && req->multi && req->easy) {
		curl_multi_remove_handle(req->multi, req->easy);
		req->addedToMulti = false;
	}
	if (req->easy) {
		curl_easy_cleanup(req->easy);
		req->easy = NULL;
	}
	if (req->requestHdrs) {
		curl_slist_free_all(req->requestHdrs);
		req->requestHdrs = NULL;
	}
	kitsune_free(req->body);
	req->body = NULL;
	if (req->streamOutputRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, req->streamOutputRef);
		req->streamOutputRef = LUA_NOREF;
	}
	if (req->streamInputRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, req->streamInputRef);
		req->streamInputRef = LUA_NOREF;
	}
	free_header_arrays(req->headerKeys, req->headerVals, req->headerCount);
	req->headerKeys  = NULL;
	req->headerVals  = NULL;
	req->headerCount = 0;
	return 0;
}

// -----------------------------------------------------------------------------
// T6a ? WriteStreamBodyCallback
// -----------------------------------------------------------------------------

static size_t WriteStreamBodyCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
	LuaHttpStreamNative* h = (LuaHttpStreamNative*)userdata;
	size_t total = size * nmemb;
	ChunkNode* node = (ChunkNode*)kitsune_malloc(sizeof(ChunkNode));
	if (!node)
		return 0;
	node->data = (char*)kitsune_malloc(total);
	if (!node->data) {
		kitsune_free(node);
		return 0;
	}
	memcpy(node->data, ptr, total);
	node->len  = total;
	node->next = NULL;
	if (h->chunkTail)
		h->chunkTail->next = node;
	else
		h->chunkHead = node;
	h->chunkTail = node;
	return total;
}

// -----------------------------------------------------------------------------
// T6b ? WriteStreamHeaderCallback
// -----------------------------------------------------------------------------

static size_t WriteStreamHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
	LuaHttpStreamNative* h = (LuaHttpStreamNative*)userdata;
	size_t total = size * nitems;
	if (total >= 5 && strncmp(buffer, "HTTP/", 5) == 0) {
		char* sp1 = (char*)memchr(buffer + 5, ' ', total - 5);
		if (sp1) {
			h->httpCode = atol(sp1 + 1);
			char* sp2 = (char*)memchr(sp1 + 1, ' ', total - (size_t)(sp1 + 1 - buffer));
			if (sp2) {
				size_t tlen = total - (size_t)(sp2 + 1 - buffer);
				if (tlen >= 2) tlen -= 2;
				if (tlen > 0 && tlen < sizeof(h->statusText)) {
					memcpy(h->statusText, sp2 + 1, tlen);
					h->statusText[tlen] = '\0';
				}
			}
		}
		return total;
	}
	if (total <= 2) {
		h->headersComplete = true;
		return total;
	}
	char* colon = (char*)memchr(buffer, ':', total);
	if (!colon) return total;
	const char* valStart = colon + 1;
	while (valStart < buffer + total && *valStart == ' ')
		valStart++;
	size_t keyLen = (size_t)(colon - buffer);
	size_t valLen = total - (size_t)(valStart - buffer);
	if (valLen >= 2) valLen -= 2;
	append_header(&h->headerKeys, &h->headerVals,
		&h->headerCount, &h->headerAlloc,
		buffer, keyLen, valStart, valLen);
	return total;
}

// -----------------------------------------------------------------------------
// T6c ? http_stream_read / HttpStreamReadContinuation
// -----------------------------------------------------------------------------

static int HttpStreamReadContinuation(lua_State* L, int status, lua_KContext ctx);

static int http_stream_read(void* native, lua_State* L, size_t len) {
	(void)len;
	return HttpStreamReadContinuation(L, LUA_OK, 0);
}

static int HttpStreamReadContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaStream* s = lua_toluastream(L, 1);
	LuaHttpStreamNative* h = (LuaHttpStreamNative*)s->native;

	int running = 0;
	curl_multi_perform(h->multi, &running);

	int msgsLeft;
	CURLMsg* msg;
	while ((msg = curl_multi_info_read(h->multi, &msgsLeft)) != NULL) {
		if (msg->msg == CURLMSG_DONE && msg->easy_handle == h->easy) {
			h->done = true;
			h->addedToMulti = false;
			curl_multi_remove_handle(h->multi, h->easy);
		}
	}

	if (h->chunkHead) {
		ChunkNode* node = h->chunkHead;
		h->chunkHead = node->next;
		if (!h->chunkHead)
			h->chunkTail = NULL;
		lua_pushlstring(L, node->data, node->len);
		kitsune_free(node->data);
		kitsune_free(node);
		return 1;
	}

	if (h->done) {
		lua_pushnil(L);
		return 1;
	}

	return lua_yieldk(L, 0, 0, HttpStreamReadContinuation);
}

// -----------------------------------------------------------------------------
// T6d ? http_stream_info / HttpStreamInfoContinuation
// -----------------------------------------------------------------------------

static int http_stream_info(void* native, lua_State* L) {
	LuaHttpStreamNative* h = (LuaHttpStreamNative*)native;
	lua_newtable(L);
	lua_pushstring(L, "http");
	lua_setfield(L, -2, "type");
	if (h->httpCode > 0) {
		lua_pushinteger(L, h->httpCode);
		lua_setfield(L, -2, "Code");
		lua_pushstring(L, h->statusText);
		lua_setfield(L, -2, "Status");
		const char* effectiveUrl = NULL;
		if (h->easy)
			curl_easy_getinfo(h->easy, CURLINFO_EFFECTIVE_URL, &effectiveUrl);
		if (effectiveUrl) {
			lua_pushstring(L, effectiveUrl);
			lua_setfield(L, -2, "Url");
		}
		lua_newtable(L);
		for (int i = 0; i < h->headerCount; i++) {
			lua_pushstring(L, h->headerKeys[i]);
			lua_pushstring(L, h->headerVals[i]);
			lua_rawset(L, -3);
		}
		lua_setfield(L, -2, "Headers");
	} else {
		lua_pushstring(L, h->errorBuf[0] ? h->errorBuf : "request failed");
		lua_setfield(L, -2, "Status");
	}
	return 1;
}

// -----------------------------------------------------------------------------
// T6e ? http_stream_hasdata
// -----------------------------------------------------------------------------

static int http_stream_hasdata(void* native) {
	LuaHttpStreamNative* h = (LuaHttpStreamNative*)native;
	if (h->done)
		return h->chunkHead != NULL ? 1 : -1;
	int running = 0;
	curl_multi_perform(h->multi, &running);
	int msgsLeft;
	CURLMsg* msg;
	while ((msg = curl_multi_info_read(h->multi, &msgsLeft)) != NULL) {
		if (msg->msg == CURLMSG_DONE && msg->easy_handle == h->easy) {
			h->done = true;
			h->addedToMulti = false;
			curl_multi_remove_handle(h->multi, h->easy);
		}
	}
	return h->chunkHead != NULL ? 1 : (h->done ? -1 : 0);
}

// -----------------------------------------------------------------------------
// T6f ? http_stream_close
// -----------------------------------------------------------------------------

static void http_stream_close(void* native, lua_State* L) {
	LuaHttpStreamNative* h = (LuaHttpStreamNative*)native;
	if (h->addedToMulti && h->multi && h->easy) {
		curl_multi_remove_handle(h->multi, h->easy);
		h->addedToMulti = false;
	}
	if (h->requestHdrs) {
		curl_slist_free_all(h->requestHdrs);
		h->requestHdrs = NULL;
	}
	if (h->easy) {
		curl_easy_cleanup(h->easy);
		h->easy = NULL;
	}
	// Free chunk queue
	ChunkNode* node = h->chunkHead;
	while (node) {
		ChunkNode* next = node->next;
		kitsune_free(node->data);
		kitsune_free(node);
		node = next;
	}
	h->chunkHead = NULL;
	h->chunkTail = NULL;
	free_header_arrays(h->headerKeys, h->headerVals, h->headerCount);
	h->headerKeys  = NULL;
	h->headerVals  = NULL;
	h->headerCount = 0;
	kitsune_free(h);
}

// -----------------------------------------------------------------------------
// T6g ? g_httpStreamVtable
// -----------------------------------------------------------------------------

static const LuaStreamVtable g_httpStreamVtable = {
	http_stream_read,    // read - may call lua_yieldk
	NULL,                // write - HTTP responses are read-only
	NULL,                // setpos
	NULL,                // curpos
	NULL,                // getlen
	http_stream_close,   // close
	http_stream_info,    // info - synchronous; headers guaranteed after connect
	http_stream_hasdata, // hasdata - non-NULL signals async
};

static int HttpStreamConnectContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaStream* s = lua_toluastream(L, -1);
	LuaHttpStreamNative* h = (LuaHttpStreamNative*)s->native;

	int running = 0;
	curl_multi_perform(h->multi, &running);

	if (!h->done) {
		int msgsLeft;
		CURLMsg* msg;
		while ((msg = curl_multi_info_read(h->multi, &msgsLeft)) != NULL) {
			if (msg->msg == CURLMSG_DONE && msg->easy_handle == h->easy) {
				h->done = true;
				if (h->addedToMulti) {
					curl_multi_remove_handle(h->multi, h->easy);
					h->addedToMulti = false;
				}
			}
		}
	}

	if (!h->headersComplete && !h->done)
		return lua_yieldk(L, 0, 0, HttpStreamConnectContinuation);

	if (h->done && !h->headersComplete) {
		lua_pop(L, 1);
		lua_pushnil(L);
		lua_pushstring(L, h->errorBuf[0] ? h->errorBuf : "request failed");
		return 2;
	}

	return 1;
}

int client_stream(lua_State* L) {
	LuaHttpClient* client = (LuaHttpClient*)luaL_checkudata(L, 1, LUAHTTPCLIENT);
	const char* method = luaL_checkstring(L, 2);
	const char* url    = luaL_checkstring(L, 3);

	size_t      bodyLen  = 0;
	const char* body     = NULL;
	LuaStream*  streamIn = NULL;
	if (lua_type(L, 4) == LUA_TSTRING) {
		body = lua_tolstring(L, 4, &bodyLen);
	} else if (lua_isuserdata(L, 4) && lua_isstream(L, 4)) {
		streamIn = lua_toluastream(L, 4);
		if (!streamIn->vtbl) {
			lua_pushnil(L);
			lua_pushstring(L, "stream body must be a native-backend stream");
			return 2;
		}
	}
	int headersIdx = lua_istable(L, 5) ? 5 : 0;

	lua_rawgetp(L, LUA_REGISTRYINDEX, &g_curlm_key);
	CURLM* multi = (CURLM*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (!multi) {
		lua_pushnil(L);
		lua_pushstring(L, "HTTP module not initialised");
		return 2;
	}

	LuaHttpStreamNative* h = (LuaHttpStreamNative*)kitsune_malloc(sizeof(LuaHttpStreamNative));
	if (!h) {
		lua_pushnil(L);
		lua_pushstring(L, "out of memory");
		return 2;
	}
	memset(h, 0, sizeof(LuaHttpStreamNative));
	h->multi = multi;

	h->easy = curl_easy_init();
	if (!h->easy) {
		kitsune_free(h);
		lua_pushnil(L);
		lua_pushstring(L, "curl_easy_init failed");
		return 2;
	}

	struct curl_slist* hdrs = build_headers(L, client, headersIdx);

	curl_easy_setopt(h->easy, CURLOPT_URL,            url);
	curl_easy_setopt(h->easy, CURLOPT_CUSTOMREQUEST,  method);
	curl_easy_setopt(h->easy, CURLOPT_HTTPHEADER,     hdrs);
	curl_easy_setopt(h->easy, CURLOPT_WRITEFUNCTION,  WriteStreamBodyCallback);
	curl_easy_setopt(h->easy, CURLOPT_WRITEDATA,      h);
	curl_easy_setopt(h->easy, CURLOPT_HEADERFUNCTION, WriteStreamHeaderCallback);
	curl_easy_setopt(h->easy, CURLOPT_HEADERDATA,     h);
	curl_easy_setopt(h->easy, CURLOPT_ERRORBUFFER,    h->errorBuf);
	curl_easy_setopt(h->easy, CURLOPT_FOLLOWLOCATION, client->followRedirects ? 1L : 0L);
	curl_easy_setopt(h->easy, CURLOPT_SSL_VERIFYPEER, client->verifySsl ? 1L : 0L);
	curl_easy_setopt(h->easy, CURLOPT_SSL_VERIFYHOST, client->verifySsl ? 2L : 0L);
	curl_easy_setopt(h->easy, CURLOPT_NOSIGNAL,       1L);
	if (client->timeoutMs > 0)
		curl_easy_setopt(h->easy, CURLOPT_TIMEOUT_MS, (long)client->timeoutMs);

	// Keep hdrs alive for the lifetime of the easy handle — curl does NOT copy it.
	h->requestHdrs = hdrs;

	if (streamIn) {
		// Streaming upload bodies need a stateful read callback; reject for now.
		curl_easy_cleanup(h->easy);
		curl_slist_free_all(hdrs);
		kitsune_free(h);
		lua_pushnil(L);
		lua_pushstring(L, "stream body not supported for streaming requests; use a string body");
		return 2;
	} else if (body && bodyLen > 0) {
		curl_easy_setopt(h->easy, CURLOPT_POSTFIELDS,    body);
		curl_easy_setopt(h->easy, CURLOPT_POSTFIELDSIZE, (long)bodyLen);
	}

	curl_multi_add_handle(multi, h->easy);
	h->addedToMulti = true;

	// One pass to start the connection; yield until response headers arrive.
	int running = 0;
	curl_multi_perform(multi, &running);

	lua_pushluastream_native(L, &g_httpStreamVtable, h, STREAM_CAP_READ);
	return lua_yieldk(L, 0, 0, HttpStreamConnectContinuation);
}

// -----------------------------------------------------------------------------
// T7a ? ws_stream_read / WsReadContinuation
// -----------------------------------------------------------------------------

static int WsReadContinuation(lua_State* L, int status, lua_KContext ctx);

static int ws_stream_read(void* native, lua_State* L, size_t len) {
	(void)len;
	return WsReadContinuation(L, LUA_OK, 0);
}

static void ws_frag_reset(LuaWebSocketNative* ws) {
	if (ws->fragBuf) {
		kitsune_free(ws->fragBuf);
		ws->fragBuf  = NULL;
		ws->fragLen  = 0;
		ws->fragAlloc = 0;
	}
}

static int WsReadContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaStream* s = lua_toluastream(L, 1);
	LuaWebSocketNative* ws = (LuaWebSocketNative*)s->native;
	if (ws->closed) {
		ws_frag_reset(ws);
		lua_pushnil(L);
		return 1;
	}
	// After CURLMSG_DONE the WebSocket socket is in non-blocking mode.
	// Call curl_ws_recv directly; do NOT call curl_multi_perform here as
	// driving an already-done handle interferes with the open socket.
	char buf[65536];
	size_t nrecv = 0;
	const struct curl_ws_frame* meta = NULL;
	CURLcode rc = curl_ws_recv(ws->easy, buf, sizeof(buf), &nrecv, &meta);
	if (rc == CURLE_AGAIN)
		return lua_yieldk(L, 0, 0, WsReadContinuation);
	if (rc != CURLE_OK) {
		ws_frag_reset(ws);
		lua_pushnil(L);
		return 1;
	}
	ws->lastFrameFlags = meta->flags;
	ws->lastBytesLeft  = (size_t)meta->bytesleft;
	if (meta->flags & CURLWS_CLOSE) {
		ws->closed = true;
		// Send close response per RFC 6455.
		if (ws->easy) {
			size_t nsent = 0;
			curl_ws_send(ws->easy, buf, nrecv, &nsent, 0, CURLWS_CLOSE);
		}
		ws_frag_reset(ws);
		lua_pushnil(L);
		return 1;
	}
	if (meta->flags & CURLWS_PING) {
		// Respond with PONG and keep reading.
		if (ws->easy) {
			size_t nsent = 0;
			curl_ws_send(ws->easy, buf, nrecv, &nsent, 0, CURLWS_PONG);
		}
		return lua_yieldk(L, 0, 0, WsReadContinuation);
	}
	if (meta->flags & CURLWS_PONG)
		return lua_yieldk(L, 0, 0, WsReadContinuation);
	// Optimistic fast path: entire frame arrived in one call.
	if (meta->bytesleft == 0 && ws->fragLen == 0) {
		lua_pushlstring(L, buf, nrecv);
		return 1;
	}
	// Fragment path: append chunk to reassembly buffer.
	if (nrecv > 0) {
		if (ws->fragLen + nrecv + 1 > ws->fragAlloc) {
			size_t newAlloc = ws->fragLen + nrecv + 8192;
			char* nb = (char*)kitsune_realloc(ws->fragBuf, newAlloc);
			if (!nb) {
				ws_frag_reset(ws);
				lua_pushnil(L);
				return 1;
			}
			ws->fragBuf   = nb;
			ws->fragAlloc = newAlloc;
		}
		memcpy(ws->fragBuf + ws->fragLen, buf, nrecv);
		ws->fragLen += nrecv;
	}
	if (meta->bytesleft > 0)
		return lua_yieldk(L, 0, 0, WsReadContinuation);
	// All fragments collected.
	lua_pushlstring(L, ws->fragBuf, ws->fragLen);
	ws_frag_reset(ws);
	return 1;
}

// -----------------------------------------------------------------------------
// T7b ? ws_stream_write (vtable; used by Stream:Write())
// -----------------------------------------------------------------------------

static bool ws_vtable_write(void* native, const BYTE* data, size_t len) {
	LuaWebSocketNative* ws = (LuaWebSocketNative*)native;
	if (!ws->connected || ws->closed) return false;
	size_t nsent = 0;
	unsigned int wsType = (ws->client && ws->client->binaryMode) ? CURLWS_BINARY : CURLWS_TEXT;
	return curl_ws_send(ws->easy, data, len, &nsent, 0, wsType) == CURLE_OK;
}

int client_set_binary(lua_State* L) {
	LuaHttpClient* client = (LuaHttpClient*)luaL_checkudata(L, 1, LUAHTTPCLIENT);
	client->binaryMode = lua_toboolean(L, 2) != 0;
	return 0;
}

// -----------------------------------------------------------------------------
// T7d — ws_stream_info (synchronous; returns last frame metadata)
// -----------------------------------------------------------------------------

static int ws_stream_info(void* native, lua_State* L) {
	LuaWebSocketNative* ws = (LuaWebSocketNative*)native;
	lua_newtable(L);
	lua_pushboolean(L, (ws->lastFrameFlags & CURLWS_BINARY) ? 1 : 0);
	lua_setfield(L, -2, "Binary");
	int opcode = 1;
	if (ws->lastFrameFlags & CURLWS_BINARY)       opcode = 2;
	else if (ws->lastFrameFlags & CURLWS_CLOSE)   opcode = 8;
	else if (ws->lastFrameFlags & CURLWS_PING)    opcode = 9;
	else if (ws->lastFrameFlags & CURLWS_PONG)    opcode = 10;
	lua_pushinteger(L, opcode);
	lua_setfield(L, -2, "Opcode");
	lua_pushinteger(L, (lua_Integer)ws->lastBytesLeft);
	lua_setfield(L, -2, "BytesLeft");
	return 1;
}

// -----------------------------------------------------------------------------
// T7e — ws_stream_hasdata
// -----------------------------------------------------------------------------

static int ws_stream_hasdata(void* native) {
	LuaWebSocketNative* ws = (LuaWebSocketNative*)native;
	// After the WebSocket upgrade the multi handle is no longer used.
	// Return 1 (may have data) if connected; caller will try curl_ws_recv.
	return (ws->connected && !ws->closed) ? 1 : 0;
}

// -----------------------------------------------------------------------------
// T7f — ws_stream_close
// -----------------------------------------------------------------------------

static void ws_stream_close(void* native, lua_State* L) {
	LuaWebSocketNative* ws = (LuaWebSocketNative*)native;
	if (!ws->closed) {
		size_t nsent = 0;
		curl_ws_send(ws->easy, "", 0, &nsent, 0, CURLWS_CLOSE);
		ws->closed = true;
	}
	if (ws->fragBuf) {
		kitsune_free(ws->fragBuf);
		ws->fragBuf  = NULL;
		ws->fragLen  = 0;
		ws->fragAlloc = 0;
	}
	if (ws->multi && ws->easy)
		curl_multi_remove_handle(ws->multi, ws->easy);
	if (ws->requestHdrs) {
		curl_slist_free_all(ws->requestHdrs);
		ws->requestHdrs = NULL;
	}
	if (ws->easy) {
		curl_easy_cleanup(ws->easy);
		ws->easy = NULL;
	}
	if (L && ws->clientRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, ws->clientRef);
		ws->clientRef = LUA_NOREF;
	}
	kitsune_free(ws);
}

// -----------------------------------------------------------------------------
// T7g ? g_wsStreamVtable
// -----------------------------------------------------------------------------

static const LuaStreamVtable g_wsStreamVtable = {
	ws_stream_read,      // read - may call lua_yieldk
	ws_vtable_write,     // write - CURLWS_TEXT or CURLWS_BINARY per STREAM_WRITE_FLAG_BINARY
	NULL,                // setpos
	NULL,                // curpos
	NULL,                // getlen
	ws_stream_close,     // close
	ws_stream_info,      // info - synchronous frame metadata
	ws_stream_hasdata,   // hasdata - non-NULL signals async
};

// -----------------------------------------------------------------------------
// T7h ? WsConnectContinuation
// -----------------------------------------------------------------------------

static int WsConnectContinuation(lua_State* L, int status, lua_KContext ctx) {
	LuaStream* s = lua_toluastream(L, -1);
	LuaWebSocketNative* ws = (LuaWebSocketNative*)s->native;

	int running = 0;
	curl_multi_perform(ws->multi, &running);

	int msgsLeft;
	CURLMsg* msg;
	while ((msg = curl_multi_info_read(ws->multi, &msgsLeft)) != NULL) {
		if (msg->msg == CURLMSG_DONE && msg->easy_handle == ws->easy) {
			if (msg->data.result != CURLE_OK) {
				lua_pop(L, 1);
				lua_pushnil(L);
				lua_pushstring(L, ws->errorBuf[0] ? ws->errorBuf : "WebSocket connect failed");
				return 2;
			}
			ws->connected = true;
			return 1;
		}
	}
	return lua_yieldk(L, 0, 0, WsConnectContinuation);
}

// -----------------------------------------------------------------------------
// T7i ? client_connect
// -----------------------------------------------------------------------------

int client_connect(lua_State* L) {
	LuaHttpClient* client = (LuaHttpClient*)luaL_checkudata(L, 1, LUAHTTPCLIENT);
	const char* url = luaL_checkstring(L, 2);
	int headersIdx  = lua_istable(L, 3) ? 3 : 0;

	lua_rawgetp(L, LUA_REGISTRYINDEX, &g_curlm_key);
	CURLM* multi = (CURLM*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (!multi) {
		lua_pushnil(L);
		lua_pushstring(L, "HTTP module not initialised");
		return 2;
	}

	LuaWebSocketNative* ws = (LuaWebSocketNative*)kitsune_malloc(sizeof(LuaWebSocketNative));
	if (!ws) {
		lua_pushnil(L);
		lua_pushstring(L, "out of memory");
		return 2;
	}
	memset(ws, 0, sizeof(LuaWebSocketNative));
	ws->multi     = multi;
	ws->clientRef = LUA_NOREF;
	ws->client    = client;
	lua_pushvalue(L, 1);  // push the LuaHttpClient userdata
	ws->clientRef = luaL_ref(L, LUA_REGISTRYINDEX);

	ws->easy = curl_easy_init();
	if (!ws->easy) {
		kitsune_free(ws);
		lua_pushnil(L);
		lua_pushstring(L, "curl_easy_init failed");
		return 2;
	}

	struct curl_slist* hdrs = build_headers(L, client, headersIdx);

	curl_easy_setopt(ws->easy, CURLOPT_URL,             url);
	curl_easy_setopt(ws->easy, CURLOPT_CONNECT_ONLY,    2L);
	curl_easy_setopt(ws->easy, CURLOPT_HTTP_VERSION,    CURL_HTTP_VERSION_1_1);
	curl_easy_setopt(ws->easy, CURLOPT_HTTPHEADER,      hdrs);
	curl_easy_setopt(ws->easy, CURLOPT_ERRORBUFFER,     ws->errorBuf);
	curl_easy_setopt(ws->easy, CURLOPT_SSL_VERIFYPEER,  client->verifySsl ? 1L : 0L);
	curl_easy_setopt(ws->easy, CURLOPT_SSL_VERIFYHOST,  client->verifySsl ? 2L : 0L);
	curl_easy_setopt(ws->easy, CURLOPT_NOSIGNAL,        1L);
	if (client->timeoutMs > 0)
		curl_easy_setopt(ws->easy, CURLOPT_TIMEOUT_MS, (long)client->timeoutMs);

	// Keep hdrs alive for the lifetime of the easy handle ? curl does NOT copy it.
	ws->requestHdrs = hdrs;
	curl_multi_add_handle(multi, ws->easy);

	lua_pushluastream_native(L, &g_wsStreamVtable, ws,
		STREAM_CAP_READ | STREAM_CAP_WRITE);

	// Yield until the WebSocket handshake completes
	return lua_yieldk(L, 0, 0, WsConnectContinuation);
}

#endif  // KITSUNE_HTTP
