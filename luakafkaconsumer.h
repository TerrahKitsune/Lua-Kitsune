#pragma once
#include "lua_main_incl.h"
#include "librdkafka/rdkafka.h"
#include "platform.h"

static const char* LUAKAFKACONSUMER = "KAFKACONSUMER";
static const char* LUAKAFKACONSUMERSTATE = "KAFKACONSUMERSTATE";
static const char* LUAKAFKACONSUMECOROUTINE = "KAFKACONSUMECOROUTINE";

typedef struct LuaKafkaConsumer {
	rd_kafka_t* rd;
	// Pending commit coordinates — plain value types, no lifetime dependency.
	// Set by consume_cont when a message is yielded with autocommit=false;
	// consumed (and cleared) by ConsumerCommit.
	// Kafka topic names are capped at 249 chars; 512 is always sufficient.
	bool has_pending;
	int32_t pending_partition;
	int64_t pending_offset;
	char pending_topic[512];
} LuaKafkaConsumer;

typedef struct LuaKafkaConsumeState {
	LuaKafkaConsumer* consumer;
	bool autocommit;
	int poll_timeout_ms;
	int aliveTokenRef;  // LUA_NOREF when not set
} LuaKafkaConsumeState;

LuaKafkaConsumer* lua_pushkafkaconsumer(lua_State* L);
LuaKafkaConsumer* lua_tokafkaconsumer(lua_State* L, int index);

int CreateConsumer(lua_State* L);
int ConsumerSubscribe(lua_State* L);
int ConsumerAssign(lua_State* L);
int ConsumerCommit(lua_State* L);
int ConsumerSeek(lua_State* L);
int ConsumerGetOffsets(lua_State* L);
int ConsumerGetMetadata(lua_State* L);
int ConsumerGetTopicConfig(lua_State* L);
int ConsumerSetTopicConfig(lua_State* L);
int ConsumerListGroups(lua_State* L);
int ConsumerDescribeGroups(lua_State* L);
int ConsumerDeleteGroup(lua_State* L);
int ConsumerGetGroupOffsets(lua_State* L);
int ConsumerSetGroupOffsets(lua_State* L);
int ConsumerDeleteGroupOffsets(lua_State* L);
int ConsumerGC(lua_State* L);
int ConsumerToString(lua_State* L);

int ConsumeCoroutineBody(lua_State* L);
int ConsumeAutoCommit(lua_State* L);
int ConsumeCoroutineGC(lua_State* L);
int ConsumerSetAliveToken(lua_State* L);
