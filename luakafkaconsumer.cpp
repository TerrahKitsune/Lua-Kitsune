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
// Returns NULL and pushes false + errmsg on error (caller should return 2).
// ---------------------------------------------------------------------------

static rd_kafka_topic_partition_list_t* build_partition_list(lua_State* L, int table_idx, bool with_partitions) {
	if (!lua_istable(L, table_idx)) {
		lua_pushboolean(L, false);
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
			lua_pushboolean(L, false);
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
// Consumer API
// ---------------------------------------------------------------------------

int CreateConsumer(lua_State* L) {
	rd_kafka_conf_t* conf = lua_tokafkaconf(L, 1, "LUAC");
	rd_kafka_conf_set_log_cb(conf, kafka_logger);

	// Force manual offset commit so Poll() controls exactly when offsets
	// advance. This prevents librdkafka's background auto-commit from racing
	// with the next-Poll commit and double-committing or skipping messages.
	rd_kafka_conf_set(conf, "enable.auto.commit", "false", NULL, 0);

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
	c->msg = NULL;
	return 1;
}

// consumer:Subscribe({"topic-a", "topic-b"}) -> true | false, errmsg
// Overwrites any previous subscription or assignment.
int ConsumerSubscribe(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd)
		return luaL_error(L, "Consumer not open");

	rd_kafka_topic_partition_list_t* list = build_partition_list(L, 2, false);
	if (!list)
		return 2;

	rd_kafka_resp_err_t err = rd_kafka_subscribe(c->rd, list);
	rd_kafka_topic_partition_list_destroy(list);

	if (err) {
		lua_pushboolean(L, false);
		lua_pushstring(L, rd_kafka_err2str(err));
		return 2;
	}

	lua_pushboolean(L, true);
	return 1;
}

// consumer:Assign({"topic:partition", "topic:partition:offset"}) -> true | false, errmsg
// Overwrites any previous subscription or assignment.
// Offset keywords: "earliest" = beginning, "latest" = end, omitted/nil = stored.
// Seek cannot be called until Assign has been called and librdkafka has established
// the partition assignment (drive Poll() for a short warm-up period first).
int ConsumerAssign(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd)
		return luaL_error(L, "Consumer not open");

	rd_kafka_topic_partition_list_t* list = build_partition_list(L, 2, true);
	if (!list)
		return 2;

	rd_kafka_resp_err_t err = rd_kafka_assign(c->rd, list);
	rd_kafka_topic_partition_list_destroy(list);

	if (err) {
		lua_pushboolean(L, false);
		lua_pushstring(L, rd_kafka_err2str(err));
		return 2;
	}

	lua_pushboolean(L, true);
	return 1;
}

// consumer:Poll() -> true | true, msg | false, errmsg
//
// Commits the previous real message (if any) then polls for the next one.
// Returns:
//   false, errmsg  — consumer is broken and cannot continue
//   true, msg      — a message or status event was received; inspect msg.ErrorCode
//   true           — idle, no message available; caller should Yield() and retry
//
// Commit happens automatically at the top of Poll() for the message returned
// by the previous Poll() call. There is no separate Commit() method.
int ConsumerPoll(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "Consumer not open");
		return 2;
	}

	// Commit the previous real message now that the caller has processed it.
	if (c->msg) {
		rd_kafka_commit_message(c->rd, c->msg, 1);  // async=1; non-blocking
		rd_kafka_message_destroy(c->msg);
		c->msg = NULL;
	}

	// Poll with timeout=0: this runs on the Kitsune scheduler thread and must
	// never block. The caller inserts Yield() when msg is nil to cooperate.
	rd_kafka_message_t* msg = rd_kafka_consumer_poll(c->rd, 0);

	if (!msg) {
		lua_pushboolean(L, true);
		return 1;
	}

	// Only store real messages for the next-Poll commit. Error/status
	// notifications (EOF, rebalance, transport errors) carry sentinel offsets
	// that must not be committed.
	if (msg->err == RD_KAFKA_RESP_ERR_NO_ERROR)
		c->msg = msg;

	push_consume_message(L, msg);

	// If the message was not stored (error/status), free it now; all data has
	// been copied into the Lua table.
	if (msg->err != RD_KAFKA_RESP_ERR_NO_ERROR)
		rd_kafka_message_destroy(msg);

	lua_pushboolean(L, true);
	lua_insert(L, -2);  // stack: true, msgtable
	return 2;
}

int ConsumerGetOffsets(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd)
		return luaL_error(L, "Consumer not open");
	return kafka_query_watermarks(L, c->rd);
}

int ConsumerSeek(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd)
		return luaL_error(L, "Consumer not open");

	const char* topic = luaL_checkstring(L, 2);
	int32_t partition = (int32_t)luaL_checkinteger(L, 3);
	int64_t offset = luakafkaoffset(L, 4);
	int timeout = (int)luaL_optinteger(L, 5, 5000);

	rd_kafka_topic_partition_list_t* parts = rd_kafka_topic_partition_list_new(1);
	rd_kafka_topic_partition_list_add(parts, topic, partition)->offset = offset;

	rd_kafka_error_t* err = rd_kafka_seek_partitions(c->rd, parts, timeout);
	rd_kafka_topic_partition_list_destroy(parts);

	if (err) {
		const char* errmsg = rd_kafka_error_string(err);
		rd_kafka_error_destroy(err);
		lua_pushboolean(L, false);
		lua_pushstring(L, errmsg);
		return 2;
	}
	lua_pushboolean(L, true);
	return 1;
}

int ConsumerGetMetadata(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd)
		return luaL_error(L, "Consumer not open");
	return kafka_get_metadata(L, c->rd);
}

int ConsumerGetTopicConfig(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd)
		return luaL_error(L, "Consumer not open");
	return kafka_get_topic_config(L, c->rd);
}

int ConsumerSetTopicConfig(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd)
		return luaL_error(L, "Consumer not open");
	return kafka_set_topic_config(L, c->rd);
}

int ConsumerListGroups(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd)
		return luaL_error(L, "Consumer not open");
	return kafka_list_groups(L, c->rd);
}

int ConsumerDescribeGroups(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd)
		return luaL_error(L, "Consumer not open");
	return kafka_describe_groups(L, c->rd);
}

int ConsumerDeleteGroup(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd)
		return luaL_error(L, "Consumer not open");
	return kafka_delete_group(L, c->rd);
}

int ConsumerGetGroupOffsets(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd)
		return luaL_error(L, "Consumer not open");
	return kafka_get_group_offsets(L, c->rd);
}

int ConsumerSetGroupOffsets(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd)
		return luaL_error(L, "Consumer not open");
	return kafka_set_group_offsets(L, c->rd);
}

int ConsumerDeleteGroupOffsets(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (!c->rd)
		return luaL_error(L, "Consumer not open");
	return kafka_delete_group_offsets(L, c->rd);
}

int ConsumerGC(lua_State* L) {
	LuaKafkaConsumer* c = lua_tokafkaconsumer(L, 1);
	if (c->rd) {
		// Free any pending message before closing so rd_kafka_destroy does not
		// see an outstanding reference.
		if (c->msg) {
			rd_kafka_message_destroy(c->msg);
			c->msg = NULL;
		}

		// Async close: background thread sends LeaveGroup. All events
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
