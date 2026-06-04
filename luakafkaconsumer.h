#pragma once
#include "lua_main_incl.h"
#include "librdkafka/rdkafka.h"
#include "platform.h"

static const char* LUAKAFKACONSUMER = "KAFKACONSUMER";

typedef struct LuaKafkaConsumer {
	rd_kafka_t* rd;
	rd_kafka_message_t* msg;  // last real message; committed and freed at the top of the next Poll()
} LuaKafkaConsumer;

LuaKafkaConsumer* lua_pushkafkaconsumer(lua_State* L);
LuaKafkaConsumer* lua_tokafkaconsumer(lua_State* L, int index);

int CreateConsumer(lua_State* L);
int ConsumerSubscribe(lua_State* L);
int ConsumerAssign(lua_State* L);
int ConsumerPoll(lua_State* L);
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
