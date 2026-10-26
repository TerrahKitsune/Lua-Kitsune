#include "redis/LuaRedisPubSub.h"
#include "alivetoken/LuaAliveToken.h"
#include "KitsuneInternal.h"
#include <string.h>

static void SetDidWork(lua_State* L) {
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

#ifndef _WIN32
#include <sys/select.h>
#endif

// Returns true if the socket has data ready to read without blocking.
// On Windows select() ignores the first argument; on Linux it must be fd+1.
static bool RedisDataAvailable(redisContext* ctx) {
	fd_set rfds;
	FD_ZERO(&rfds);
	FD_SET(ctx->fd, &rfds);
	struct timeval tv = {0, 0};
#ifdef _WIN32
	return select(0, &rfds, NULL, NULL, &tv) > 0;
#else
	return select((int)ctx->fd + 1, &rfds, NULL, NULL, &tv) > 0;
#endif
}

static int PubSubCont(lua_State* L, int status, lua_KContext ctx);

int PubSubCoroutineBody(lua_State* L) {
	LuaRedisPubSubState* state = (LuaRedisPubSubState*)lua_touserdata(L, lua_upvalueindex(1));
	return PubSubCont(L, LUA_OK, (lua_KContext)(intptr_t)state);
}

static int PubSubCont(lua_State* L, int status, lua_KContext ctx) {
	LuaRedisPubSubState* state = (LuaRedisPubSubState*)(intptr_t)ctx;
	(void)status;

	// shouldQuit is passed as the first resume argument.
	if (lua_toboolean(L, 1)) {
		if (state->aliveTokenRef != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, state->aliveTokenRef);
			state->aliveTokenRef = LUA_NOREF;
		}
		if (state->context) {
			redisReply* r = (redisReply*)redisCommand(state->context,
				state->isPattern ? "PUNSUBSCRIBE" : "UNSUBSCRIBE");
			if (r)
				freeReplyObject(r);
		}
		// Clear registry entries so GC can reclaim the state and the parent LuaRedis.
		lua_pushlightuserdata(L, (void*)state);
		lua_pushnil(L);
		lua_rawset(L, LUA_REGISTRYINDEX);
		lua_pushlightuserdata(L, (void*)L);  // L is the coroutine's lua_State*
		lua_pushnil(L);
		lua_rawset(L, LUA_REGISTRYINDEX);
		return 0;
	}

	// AliveToken check — treat a disposed token as a stop flag
	if (state->aliveTokenRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, state->aliveTokenRef);
		int alive = AliveTokenIsAlive(L, -1);
		lua_pop(L, 1);
		if (alive == 0) {
			luaL_unref(L, LUA_REGISTRYINDEX, state->aliveTokenRef);
			state->aliveTokenRef = LUA_NOREF;
			if (state->context) {
				redisReply* r = (redisReply*)redisCommand(state->context,
					state->isPattern ? "PUNSUBSCRIBE" : "UNSUBSCRIBE");
				if (r)
					freeReplyObject(r);
			}
			lua_pushlightuserdata(L, (void*)state);
			lua_pushnil(L);
			lua_rawset(L, LUA_REGISTRYINDEX);
			lua_pushlightuserdata(L, (void*)L);
			lua_pushnil(L);
			lua_rawset(L, LUA_REGISTRYINDEX);
			return 0;
		}
	}

	lua_settop(L, 0);

	if (!state->context || state->context->err) {
		lua_pushnil(L);
		lua_pushstring(L, (state->context && state->context->errstr[0]) ?
			state->context->errstr : "connection lost");
		return 2;
	}

	if (!RedisDataAvailable(state->context))
		return lua_yieldk(L, 0, ctx, PubSubCont);  // no data — no work done

	void* replyPtr = NULL;
	if (redisGetReply(state->context, &replyPtr) == REDIS_ERR || !replyPtr) {
		lua_pushnil(L);
		lua_pushstring(L, state->context->errstr[0] ? state->context->errstr : "pub/sub read error");
		return 2;
	}

	redisReply* r = (redisReply*)replyPtr;

	if (r->type == REDIS_REPLY_ARRAY && r->element[0] &&
		r->element[0]->type == REDIS_REPLY_STRING) {
		// SUBSCRIBE message: ["message", channel, payload]
		if (r->elements >= 3 &&
			r->element[0]->len == 7 &&
			memcmp(r->element[0]->str, "message", 7) == 0 &&
			r->element[1] && r->element[2]) {
					lua_pushlstring(L, r->element[1]->str, r->element[1]->len);  // channel
					lua_pushlstring(L, r->element[2]->str, r->element[2]->len);  // message
					freeReplyObject(r);
					SetDidWork(L);  // received a message
					return lua_yieldk(L, 2, ctx, PubSubCont);
				}
				// PSUBSCRIBE message: ["pmessage", pattern, channel, payload]
				if (r->elements >= 4 &&
					r->element[0]->len == 8 &&
					memcmp(r->element[0]->str, "pmessage", 8) == 0 &&
					r->element[1] && r->element[2] && r->element[3]) {
					lua_pushlstring(L, r->element[1]->str, r->element[1]->len);  // pattern
					lua_pushlstring(L, r->element[2]->str, r->element[2]->len);  // channel
					lua_pushlstring(L, r->element[3]->str, r->element[3]->len);  // message
					freeReplyObject(r);
					SetDidWork(L);  // received a message
					return lua_yieldk(L, 3, ctx, PubSubCont);
				}
			}

			// Subscribe ack, unsubscribe notification, or other non-message frame: yield 0.
				freeReplyObject(r);
				return lua_yieldk(L, 0, ctx, PubSubCont);
}

int RedisPubSubCoroutine_GC(lua_State* L) {
	lua_State* co = lua_tothread(L, 1);
	if (!co)
		return 0;

	// Retrieve the state pointer stored at registry[co] and clear registry[state].
	lua_pushlightuserdata(L, (void*)co);
	lua_rawget(L, LUA_REGISTRYINDEX);
	LuaRedisPubSubState* state = (LuaRedisPubSubState*)lua_touserdata(L, -1);
	if (state) {
		if (state->aliveTokenRef != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, state->aliveTokenRef);
			state->aliveTokenRef = LUA_NOREF;
		}
		lua_pushlightuserdata(L, (void*)state);
		lua_pushnil(L);
		lua_rawset(L, LUA_REGISTRYINDEX);
	}
	lua_pop(L, 1);

	lua_pushlightuserdata(L, (void*)co);
	lua_pushnil(L);
	lua_rawset(L, LUA_REGISTRYINDEX);
	return 0;
}

int RedisPubSubState_GC(lua_State* L) {
	LuaRedisPubSubState* state = (LuaRedisPubSubState*)luaL_checkudata(L, 1, REDISPUBSUBSTATE);
	if (state->context) {
		redisFree(state->context);  // closes socket; ssl is borrowed and not freed here
		state->context = NULL;
	}
	if (state->redisRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, state->redisRef);
		state->redisRef = LUA_NOREF;
	}
	if (state->aliveTokenRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, state->aliveTokenRef);
		state->aliveTokenRef = LUA_NOREF;
	}
	return 0;
}

static int MakeSubscribeCoroutine(lua_State* L, LuaRedisPubSubState* state) {
	int stateIdx = lua_gettop(L);  // state userdata is on top

	lua_State* co = lua_newthread(L);
	// L stack: [..., state, co_thread]

	// Move the body closure (with state as upvalue) onto co's stack.
	lua_pushvalue(L, stateIdx);
	lua_pushcclosure(L, PubSubCoroutineBody, 1);
	lua_xmove(L, co, 1);
	// co stack: [PubSubCoroutineBody_closure]
	// L stack:  [..., state, co_thread]

	// registry[co] = state — RedisPubSubCoroutine_GC uses this to clear registry[state].
	lua_pushlightuserdata(L, (void*)co);
	lua_pushvalue(L, stateIdx);
	lua_rawset(L, LUA_REGISTRYINDEX);

	// registry[state] = state — keeps state alive as long as co is alive.
	lua_pushlightuserdata(L, (void*)state);
	lua_pushvalue(L, stateIdx);
	lua_rawset(L, LUA_REGISTRYINDEX);

	luaL_getmetatable(L, REDISPUBSUBCOROUTINE);
	lua_setmetatable(L, -2);

	return 1;  // co_thread is on top
}

static int InternalSubscribe(lua_State* L, bool isPattern) {
	LuaRedis* redis = luaL_checkredis(L, 1);
	if (!redis->context) {
		lua_pushnil(L);
		lua_pushstring(L, "Redis not connected");
		return 2;
	}
	if (!redis->host) {
		lua_pushnil(L);
		lua_pushstring(L, "Connection info unavailable for Subscribe");
		return 2;
	}

	int top   = lua_gettop(L);
	int nchan = top - 1;
	if (nchan < 1) {
		lua_pushnil(L);
		lua_pushstring(L, "Subscribe requires at least one channel");
		return 2;
	}

	// Open a dedicated connection for pub/sub (the main context must stay in command mode).
	struct timeval tv = { redis->timeoutSec > 0 ? redis->timeoutSec : 10, 0 };
	redisOptions options;
	memset(&options, 0, sizeof(options));
	REDIS_OPTIONS_SET_TCP(&options, redis->host, redis->port);
	options.connect_timeout = &tv;

	redisContext* subCtx = redisConnectWithOptions(&options);
	if (!subCtx || subCtx->err) {
		char errbuf[256] = {0};
		if (subCtx) {
			strncpy(errbuf, subCtx->errstr, sizeof(errbuf) - 1);
			redisFree(subCtx);
		}
		lua_pushnil(L);
		lua_pushfstring(L, "Subscribe connection error: %s", errbuf[0] ? errbuf : "out of memory");
		return 2;
	}

	if (redis->ssl) {
		if (redisInitiateSSLWithContext(subCtx, redis->ssl) != REDIS_OK) {
			char errbuf[256] = {0};
			strncpy(errbuf, subCtx->errstr, sizeof(errbuf) - 1);
			redisFree(subCtx);
			lua_pushnil(L);
			lua_pushfstring(L, "Subscribe SSL error: %s", errbuf);
			return 2;
		}
	}

	if (redis->password) {
		redisReply* authReply = (redisReply*)redisCommand(subCtx, "AUTH %s", redis->password);
		if (!authReply || authReply->type == REDIS_REPLY_ERROR) {
			char errbuf[256] = {0};
			if (authReply) {
				strncpy(errbuf, authReply->str, sizeof(errbuf) - 1);
				freeReplyObject(authReply);
			}
			redisFree(subCtx);
			lua_pushnil(L);
			lua_pushfstring(L, "Subscribe AUTH failed: %s", errbuf[0] ? errbuf : "no reply");
			return 2;
		}
		freeReplyObject(authReply);
	}

	// Build and pipeline SUBSCRIBE/PSUBSCRIBE channel1 channel2 ...
	char** argv    = (char**)kitsune_malloc((size_t)(nchan + 1) * sizeof(char*));
	size_t* argvlen = (size_t*)kitsune_malloc((size_t)(nchan + 1) * sizeof(size_t));
	if (!argv || !argvlen) {
		kitsune_free(argv);
		kitsune_free(argvlen);
		redisFree(subCtx);
		lua_pushnil(L);
		lua_pushstring(L, "out of memory");
		return 2;
	}

	const char* cmd = isPattern ? "PSUBSCRIBE" : "SUBSCRIBE";
	argv[0]    = (char*)cmd;
	argvlen[0] = strlen(cmd);
	for (int i = 0; i < nchan; i++)
		argv[i + 1] = (char*)luaL_checklstring(L, i + 2, &argvlen[i + 1]);

	int rc = redisAppendCommandArgv(subCtx, nchan + 1, (const char**)argv, argvlen);
	kitsune_free(argv);
	kitsune_free(argvlen);

	if (rc == REDIS_ERR) {
		redisFree(subCtx);
		lua_pushnil(L);
		lua_pushstring(L, "failed to queue SUBSCRIBE command");
		return 2;
	}

	// Flush the output buffer to the socket.
	int done = 0;
	while (!done) {
		if (redisBufferWrite(subCtx, &done) == REDIS_ERR) {
			char errbuf[256] = {0};
			strncpy(errbuf, subCtx->errstr, sizeof(errbuf) - 1);
			redisFree(subCtx);
			lua_pushnil(L);
			lua_pushfstring(L, "SUBSCRIBE write error: %s", errbuf);
			return 2;
		}
	}

	LuaRedisPubSubState* state = (LuaRedisPubSubState*)lua_newuserdata(L, sizeof(LuaRedisPubSubState));
	if (!state) {
		redisFree(subCtx);
		luaL_error(L, "out of memory");
		return 0;
	}
	luaL_getmetatable(L, REDISPUBSUBSTATE);
	lua_setmetatable(L, -2);
	memset(state, 0, sizeof(LuaRedisPubSubState));
	state->context    = subCtx;
	state->ssl        = redis->ssl;  // borrowed; not freed by state GC
	state->isPattern = isPattern;
	state->redisRef  = LUA_NOREF;
	state->aliveTokenRef = LUA_NOREF;

	// Anchor the parent LuaRedis so its ssl context outlives this pub/sub connection.
	lua_pushvalue(L, 1);
	state->redisRef = luaL_ref(L, LUA_REGISTRYINDEX);

	return MakeSubscribeCoroutine(L, state);
}

// co:SetAliveToken(token | nil) — arg 1 is the coroutine thread, arg 2 is the token or nil.
int RedisPubSubCoroutine_SetAliveToken(lua_State* L) {
	lua_State* co = lua_tothread(L, 1);
	if (!co)
		return luaL_argerror(L, 1, "expected coroutine");
	lua_pushlightuserdata(L, (void*)co);
	lua_rawget(L, LUA_REGISTRYINDEX);
	LuaRedisPubSubState* state = (LuaRedisPubSubState*)lua_touserdata(L, -1);
	lua_pop(L, 1);
	if (!state)
		return 0;
	if (state->aliveTokenRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, state->aliveTokenRef);
		state->aliveTokenRef = LUA_NOREF;
	}
	if (!lua_isnil(L, 2) && !lua_isnone(L, 2)) {
		luaL_checkudata(L, 2, LUAALIVETOKEN);
		lua_pushvalue(L, 2);
		state->aliveTokenRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}
	return 0;
}

int Redis_Subscribe(lua_State* L) {
	return InternalSubscribe(L, false);
}

int Redis_PSubscribe(lua_State* L) {
	return InternalSubscribe(L, true);
}
