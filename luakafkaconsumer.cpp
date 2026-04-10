#include "luakafkaconsumer.h"
#include "kafkahelpers.h"
#include "luakafka.h"
#include <string.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// push/get helpers
// ---------------------------------------------------------------------------

LuaKafkaConsumer* lua_pushkafkaconsumer(lua_State* L) {
	LuaKafkaConsumer* c = (LuaKafkaConsumer*)lua_newuserdata(L, sizeof(LuaKafkaConsumer));
	if (!c)
		luaL_error(L, "Unable to push kafka consumer");
	luaL_getmetatable(L, LUAKAFKACONSUMER);
	lua_setmetatable(L, -2);
	memset(c, 0, sizeof(LuaKafkaConsumer));
	return c;
}

LuaKafkaConsumer* lua_tokafkaconsumer(lua_State* L, int index) {
	LuaKafkaConsumer* c = (LuaKafkaConsumer*)luaL_checkudata(L, index, LUAKAFKACONSUMER);
	if (!c)
		luaL_error(L, "parameter is not a %s", LUAKAFKACONSUMER);
	return c;
}

// ---------------------------------------------------------------------------
// Message table builder
// ---------------------------------------------------------------------------

static void push_consume_message(lua_State* L, const rd_kafka_message_t* msg) {
	lua_createtable(L, 0, 10);
	int t = lua_gettop(L);

	lua_pushstring(L, "Key");
	if (msg->key && msg->key_len > 0)
		lua_pushlstring(L, (const char*)msg->key, msg->key_len);
	else
		lua_pushnil(L);
	lua_settable(L, t);

	lua_pushstring(L, "Value");
	if (msg->payload && msg->len > 0)
		lua_pushlstring(L, (const char*)msg->payload, msg->len);
	else
		lua_pushstring(L, "");
	lua_settable(L, t);

	lua_pushstring(L, "Topic");
	lua_pushstring(L, msg->rkt ? rd_kafka_topic_name(msg->rkt) : "");
	lua_settable(L, t);

	lua_pushstring(L, "Partition");
	lua_pushinteger(L, msg->partition);
	lua_settable(L, t);

	lua_pushstring(L, "Offset");
	lua_pushinteger(L, msg->offset);
	lua_settable(L, t);

	lua_pushstring(L, "ErrorCode");
	lua_pushinteger(L, msg->err);
	lua_settable(L, t);

	lua_pushstring(L, "Error");
	lua_pushstring(L, rd_kafka_err2str(msg->err));
	lua_settable(L, t);

	rd_kafka_timestamp_type_t tstype;
	lua_pushstring(L, "Timestamp");
	lua_pushinteger(L, rd_kafka_message_timestamp(msg, &tstype));
	lua_settable(L, t);

	lua_pushstring(L, "Latency");
	lua_pushinteger(L, rd_kafka_message_latency(msg));
	lua_settable(L, t);

	lua_pushstring(L, "Headers");
	rd_kafka_headers_t* headers;
	if (rd_kafka_message_headers(msg, &headers) == RD_KAFKA_RESP_ERR_NO_ERROR) {
		size_t cnt = rd_kafka_header_cnt(headers);
		lua_createtable(L, 0, (int)cnt);
		const char* name;
		const char* data;
		size_t dsz;
		for (size_t i = 0; i < cnt; i++) {
			if (rd_kafka_header_get_all(headers, i, &name, (const void**)&data, &dsz) == RD_KAFKA_RESP_ERR_NO_ERROR) {
				lua_pushstring(L, name);
				lua_pushlstring(L, data, dsz);
				lua_settable(L, -3);
			}
		}
	}
	else {
		lua_createtable(L, 0, 0);
	}
	lua_settable(L, t);
}

// ---------------------------------------------------------------------------
// Consume coroutine
// ---------------------------------------------------------------------------

// Forward declaration of the continuation used by lua_yieldk.
static int consume_cont(lua_State* L, int status, lua_KContext ctx);

// Entry point — called on the first coroutine.resume(co, ...).
// State is upvalue[1]; shouldQuit is the first resume argument.
int ConsumeCoroutineBody(lua_State* L) {
	LuaKafkaConsumeState* state = (LuaKafkaConsumeState*)lua_touserdata(L, lua_upvalueindex(1));
	return consume_cont(L, LUA_OK, (lua_KContext)(intptr_t)state);
}

// Continuation — re-entered on every subsequent resume.
static int consume_cont(lua_State* L, int status, lua_KContext ctx) {
	LuaKafkaConsumeState* state = (LuaKafkaConsumeState*)(intptr_t)ctx;

	// shouldQuit is the first argument from the current resume
	if (lua_toboolean(L, 1)) {
		// Stop path: has_pending is left intact so consumer:Commit() can
		// still be called after the coroutine is stopped.
		lua_pushlightuserdata(L, (void*)state);
		lua_pushnil(L);
		lua_rawset(L, LUA_REGISTRYINDEX);
		lua_pushlightuserdata(L, (void*)L);
		lua_pushnil(L);
		lua_rawset(L, LUA_REGISTRYINDEX);
		return 0;
	}

	// Close the previous commit window — the caller had one resume to call Commit
	state->consumer->has_pending = false;

	rd_kafka_message_t* msg = rd_kafka_consumer_poll(state->consumer->rd, state->poll_timeout_ms);

	lua_settop(L, 0);

	if (!msg)
		return lua_yieldk(L, 0, ctx, consume_cont);

	if (state->autocommit) {
		rd_kafka_commit_message(state->consumer->rd, msg, 1);
	}
	else {
		// Record commit coordinates before freeing the message.
		// push_consume_message copies all data into Lua strings so the
		// raw rd_kafka_message_t* is not needed across the yield boundary.
		state->consumer->has_pending = true;
		state->consumer->pending_partition = msg->partition;
		state->consumer->pending_offset = msg->offset;
		const char* tname = msg->rkt ? rd_kafka_topic_name(msg->rkt) : "";
		strncpy(state->consumer->pending_topic, tname, sizeof(state->consumer->pending_topic) - 1);
		state->consumer->pending_topic[sizeof(state->consumer->pending_topic) - 1] = '\0';
	}

	push_consume_message(L, msg);
	rd_kafka_message_destroy(msg);  // all fields copied to Lua; safe to free before yield
	return lua_yieldk(L, 1, ctx, consume_cont);
}

// AutoCommit — called as co:AutoCommit(bool).
// self is the thread at index 1; bool is at index 2.
int ConsumeAutoCommit(lua_State* L) {
	lua_State* co = lua_tothread(L, 1);
	if (!co)
		return luaL_argerror(L, 1, "expected coroutine");
	lua_pushlightuserdata(L, (void*)co);
	lua_rawget(L, LUA_REGISTRYINDEX);
	LuaKafkaConsumeState* state = (LuaKafkaConsumeState*)lua_touserdata(L, -1);
	if (state)
		state->autocommit = (bool)lua_toboolean(L, 2);
	return 0;
}

// __gc for the coroutine thread metatable.
// Cleans up the registry entry and any pending message.
int ConsumeCoroutineGC(lua_State* L) {
	lua_State* co = lua_tothread(L, 1);
	if (!co)
		return 0;
	lua_pushlightuserdata(L, (void*)co);
	lua_rawget(L, LUA_REGISTRYINDEX);
	LuaKafkaConsumeState* state = (LuaKafkaConsumeState*)lua_touserdata(L, -1);
	if (state) {
		// consumer->pending is owned by the consumer, not the coroutine.
		// ConsumerGC will free it; we only release the registry anchors here.
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

// ---------------------------------------------------------------------------
// Offset helpers
// ---------------------------------------------------------------------------

// Converts a string offset keyword or stringified integer to a librdkafka
// offset constant. Accepts "earliest"/"beginning", "latest"/"end", "stored",
// or any integer value (including librdkafka sentinel values like -2).
static int64_t kafka_offset_from_str(const char* s) {
	if (strcmp(s, "earliest") == 0 || strcmp(s, "beginning") == 0)
		return RD_KAFKA_OFFSET_BEGINNING;
	if (strcmp(s, "latest") == 0 || strcmp(s, "end") == 0)
		return RD_KAFKA_OFFSET_END;
	if (strcmp(s, "stored") == 0)
		return RD_KAFKA_OFFSET_STORED;
	char* endp;
	long long v = strtoll(s, &endp, 10);
	if (endp != s && *endp == '\0')
		return (int64_t)v;
	return RD_KAFKA_OFFSET_STORED;
}

// Reads a Kafka offset from a Lua value at index idx.
// Accepts a string keyword ("earliest", "latest", "stored") or an integer.
static int64_t luakafkaoffset(lua_State* L, int idx) {
	if (lua_type(L, idx) == LUA_TSTRING)
		return kafka_offset_from_str(lua_tostring(L, idx));
	return (int64_t)luaL_checkinteger(L, idx);
}

// ---------------------------------------------------------------------------
// Internal helper — builds the topic/partition list from a Lua array.
// Assign entries have the form "topic", "topic:partition", or
// "topic:partition:offset" where offset is a number or keyword.
// Subscribe entries are plain topic name strings (partition ignored).
// Returns NULL and pushes nil + errmsg on error (caller should return 2).
// ---------------------------------------------------------------------------

static rd_kafka_topic_partition_list_t* build_partition_list(lua_State* L, int table_idx, bool with_partitions) {
	if (!lua_istable(L, table_idx)) {
		lua_pushnil(L);
		lua_pushstring(L, "expected array of topic strings");
		return NULL;
	}

	int n = (int)luaL_len(L, table_idx);
	rd_kafka_topic_partition_list_t* list = rd_kafka_topic_partition_list_new(n);

	for (int i = 1; i <= n; i++) {
		lua_rawgeti(L, table_idx, i);
		const char* entry = lua_tostring(L, -1);
		lua_pop(L, 1);

		if (!entry) {
			rd_kafka_topic_partition_list_destroy(list);
			lua_pushnil(L);
			lua_pushfstring(L, "topics[%d] is not a string", i);
			return NULL;
		}

		if (with_partitions) {
			// Format: "topic"  or  "topic:partition"  or  "topic:partition:offset"
			// Kafka topic names cannot contain ':', so the first colon is always
			// the topic/partition boundary.
			char topic_buf[512];
			int32_t partition = RD_KAFKA_PARTITION_UA;
			int64_t offset = RD_KAFKA_OFFSET_STORED;

			const char* first_colon = strchr(entry, ':');
			if (first_colon) {
				size_t tlen = (size_t)(first_colon - entry);
				if (tlen >= sizeof(topic_buf))
					tlen = sizeof(topic_buf) - 1;
				strncpy(topic_buf, entry, tlen);
				topic_buf[tlen] = '\0';
				entry = topic_buf;

				const char* after_part = first_colon + 1;
				char* end;
				long p = strtol(after_part, &end, 10);
				if (end != after_part && (*end == '\0' || *end == ':') && p >= 0) {
					partition = (int32_t)p;
					if (*end == ':')
						offset = kafka_offset_from_str(end + 1);
				}
			}

			rd_kafka_topic_partition_t* tp = rd_kafka_topic_partition_list_add(list, entry, partition);
			tp->offset = offset;
		}
		else {
			rd_kafka_topic_partition_list_add(list, entry, RD_KAFKA_PARTITION_UA);
		}
	}

	return list;
}

// ---------------------------------------------------------------------------
// Internal helper — creates the consume coroutine from an existing consumer.
// Called by both ConsumerSubscribe and ConsumerAssign after the librdkafka
// subscription/assignment has already been applied.
// ---------------------------------------------------------------------------

static int make_consume_coroutine(lua_State* L, LuaKafkaConsumer* consumer) {
	// Create the shared state userdata
	LuaKafkaConsumeState* state = (LuaKafkaConsumeState*)lua_newuserdata(L, sizeof(LuaKafkaConsumeState));
	if (!state)
		luaL_error(L, "Unable to allocate consume state");
	luaL_getmetatable(L, LUAKAFKACONSUMERSTATE);
	lua_setmetatable(L, -2);
	state->consumer = consumer;
	state->autocommit = true;
	state->poll_timeout_ms = 0;
	// state is now at the top of L's stack; remember its absolute index
	int state_idx = lua_gettop(L);

	// Create the coroutine thread
	lua_State* co = lua_newthread(L);
	// stack: [..., state, thread]

	// Create the body closure (state as upvalue) on L, then move to co
	lua_pushvalue(L, state_idx);
	lua_pushcclosure(L, ConsumeCoroutineBody, 1);
	lua_xmove(L, co, 1);
	// co stack: [ConsumeCoroutineBody_closure]
	// L stack:  [..., state, thread]

	// Store state in the registry keyed by the co pointer so AutoCommit
	// and ConsumeCoroutineGC can find it without going through upvalues
	lua_pushlightuserdata(L, (void*)co);
	lua_pushvalue(L, state_idx);
	lua_rawset(L, LUA_REGISTRYINDEX);

	// Anchor the consumer userdata so it cannot be GC'd before this coroutine
	// finishes. The coroutine holds state->owner (rd_kafka_t*) and would
	// use-after-free if the consumer is destroyed first.
	// Key = state address (distinct from the co-pointer key above).
	lua_pushlightuserdata(L, (void*)state);
	lua_pushvalue(L, 1); // consumer userdata is always arg 1 of Subscribe/Assign
	lua_rawset(L, LUA_REGISTRYINDEX);

	// Set KAFKACONSUMECOROUTINE metatable on the thread
	// (thread is at the top of L's stack)
	luaL_getmetatable(L, LUAKAFKACONSUMECOROUTINE);
	lua_setmetatable(L, -2);

	// Return the thread; state below it will be cleaned up by Lua
	return 1;
}

// ---------------------------------------------------------------------------
// Consumer API
// ---------------------------------------------------------------------------

int CreateConsumer(lua_State* L) {
	rd_kafka_conf_t* conf = lua_tokafkaconf(L, 1, "LUAC");
	rd_kafka_conf_set_log_cb(conf, kafka_logger);

	char errbuf[kafka_error_buffer_len];
	rd_kafka_t* rd = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errbuf, kafka_error_buffer_len);
	if (!rd) {
		rd_kafka_conf_destroy(conf);
		lua_pushnil(L);
		lua_pushstring(L, errbuf);
		return 2;
	}

	rd_kafka_poll_set_consumer(rd);

	LuaKafkaConsumer* c = lua_pushkafkaconsumer(L);
	c->rd = rd;
	return 1;
}

int ConsumerSubscribe(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		luaL_error(L, "Consumer not open");
		return 0;
	}

	rd_kafka_topic_partition_list_t* list = build_partition_list(L, 2, false);
	if (!list)
		return 2;

	rd_kafka_resp_err_t err = rd_kafka_subscribe(c->rd, list);
	rd_kafka_topic_partition_list_destroy(list);

	if (err) {
		lua_pushnil(L);
		lua_pushstring(L, rd_kafka_err2str(err));
		return 2;
	}

	return make_consume_coroutine(L, c);
}

int ConsumerAssign(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		luaL_error(L, "Consumer not open");
		return 0;
	}

	rd_kafka_topic_partition_list_t* list = build_partition_list(L, 2, true);
	if (!list)
		return 2;

	rd_kafka_resp_err_t err = rd_kafka_assign(c->rd, list);
	rd_kafka_topic_partition_list_destroy(list);

	if (err) {
		lua_pushnil(L);
		lua_pushstring(L, rd_kafka_err2str(err));
		return 2;
	}

	return make_consume_coroutine(L, c);
}

int ConsumerCommit(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		luaL_error(L, "Consumer not open");
		return 0;
	}

	if (!c->has_pending) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "no pending message to commit");
		return 2;
	}

	// Clear before committing to prevent double-commit.
	// async=1: queued by librdkafka background threads; rd_kafka_poll_set_consumer
	// routes responses through rd_kafka_consumer_poll so a sync commit (async=0)
	// would deadlock once the coroutine is stopped.
	c->has_pending = false;

	rd_kafka_topic_partition_list_t* offsets = rd_kafka_topic_partition_list_new(1);
	rd_kafka_topic_partition_list_add(offsets, c->pending_topic, c->pending_partition)->offset =
		c->pending_offset + 1;
	rd_kafka_resp_err_t err = rd_kafka_commit(c->rd, offsets, 1);
	rd_kafka_topic_partition_list_destroy(offsets);

	lua_pushboolean(L, !err);
	if (err) {
		lua_pushstring(L, rd_kafka_err2str(err));
		return 2;
	}
	return 1;
}

int ConsumerGetOffsets(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		luaL_error(L, "Consumer not open");
		return 0;
	}
	return kafka_query_watermarks(L, c->rd);
}

int ConsumerSeek(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		luaL_error(L, "Consumer not open");
		return 0;
	}

	const char* topic = luaL_checkstring(L, 2);
	int32_t partition = (int32_t)luaL_checkinteger(L, 3);
	int64_t offset = luakafkaoffset(L, 4);
	int timeout = (int)luaL_optinteger(L, 5, 5000);

	rd_kafka_topic_partition_list_t* parts = rd_kafka_topic_partition_list_new(1);
	rd_kafka_topic_partition_list_add(parts, topic, partition)->offset = offset;

	rd_kafka_error_t* err = rd_kafka_seek_partitions(c->rd, parts, timeout);
	rd_kafka_topic_partition_list_destroy(parts);

	if (err) {
		const char* msg = rd_kafka_error_string(err);
		rd_kafka_error_destroy(err);
		lua_pushboolean(L, false);
		lua_pushstring(L, msg);
		return 2;
	}
	lua_pushboolean(L, true);
	return 1;
}

int ConsumerGetMetadata(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		luaL_error(L, "Consumer not open");
		return 0;
	}
	return kafka_get_metadata(L, c->rd);
}

int ConsumerGetTopicConfig(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		luaL_error(L, "Consumer not open");
		return 0;
	}
	return kafka_get_topic_config(L, c->rd);
}

int ConsumerSetTopicConfig(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		luaL_error(L, "Consumer not open");
		return 0;
	}
	return kafka_set_topic_config(L, c->rd);
}

int ConsumerListGroups(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		luaL_error(L, "Consumer not open");
		return 0;
	}
	return kafka_list_groups(L, c->rd);
}

int ConsumerDescribeGroups(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		luaL_error(L, "Consumer not open");
		return 0;
	}
	return kafka_describe_groups(L, c->rd);
}

int ConsumerDeleteGroup(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		luaL_error(L, "Consumer not open");
		return 0;
	}
	return kafka_delete_group(L, c->rd);
}

int ConsumerGetGroupOffsets(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		luaL_error(L, "Consumer not open");
		return 0;
	}
	return kafka_get_group_offsets(L, c->rd);
}

int ConsumerSetGroupOffsets(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		luaL_error(L, "Consumer not open");
		return 0;
	}
	return kafka_set_group_offsets(L, c->rd);
}

int ConsumerDeleteGroupOffsets(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		luaL_error(L, "Consumer not open");
		return 0;
	}
	return kafka_delete_group_offsets(L, c->rd);
}

int ConsumerGC(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (c->rd) {
		// Async close: background thread sends LeaveGroup.  All events
		// (rebalance, commit ACKs) are forwarded to closeq.
		// Rebalance events MUST be explicitly acknowledged via rd_kafka_assign;
		// without this the cgrp thread waits forever and
		// rd_kafka_consumer_closed() never returns true, causing the destroy
		// fallback to block for the full session-timeout (~45 s).
		rd_kafka_queue_t* closeq = rd_kafka_queue_new(c->rd);
		rd_kafka_error_t* close_err = rd_kafka_consumer_close_queue(c->rd, closeq);
		if (!close_err) {
			for (int i = 0; i < 100 && !rd_kafka_consumer_closed(c->rd); i++) {
				rd_kafka_event_t* ev = rd_kafka_queue_poll(closeq, 100);
				if (ev) {
					if (rd_kafka_event_type(ev) == RD_KAFKA_EVENT_REBALANCE) {
						if (rd_kafka_event_error(ev) == RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS)
							rd_kafka_assign(c->rd, rd_kafka_event_topic_partition_list(ev));
						else
							rd_kafka_assign(c->rd, NULL);
					}
					rd_kafka_event_destroy(ev);
				}
			}
		}
		else {
			rd_kafka_error_destroy(close_err);
		}
		rd_kafka_queue_destroy(closeq);

		if (rd_kafka_consumer_closed(c->rd))
			rd_kafka_destroy(c->rd);
		else
			rd_kafka_destroy_flags(c->rd, RD_KAFKA_DESTROY_F_NO_CONSUMER_CLOSE);

		c->rd = NULL;
	}
	return 0;
}

int ConsumerToString(lua_State* L) {
	char buf[64];
	sprintf(buf, "KafkaConsumer: %p", (void*)lua_tokafkaconsumer(L, 1));
	lua_pushstring(L, buf);
	return 1;
}
