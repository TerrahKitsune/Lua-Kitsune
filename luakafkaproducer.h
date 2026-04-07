#pragma once
#include "lua_main_incl.h"
#include "librdkafka/rdkafka.h"
#include "platform.h"

static const char* LUAKAFKAPRODUCER = "KAFKAPRODUCER";

typedef struct LuaKafkaProducer {
	rd_kafka_t* rd;
} LuaKafkaProducer;

LuaKafkaProducer* lua_pushkafkaproducer(lua_State* L);
LuaKafkaProducer* lua_tokafkaproducer(lua_State* L, int index);

int CreateProducer(lua_State* L);
int ProducerSend(lua_State* L);
int ProducerGetOffsets(lua_State* L);
int ProducerGetMetadata(lua_State* L);
int ProducerCreateTopic(lua_State* L);
int ProducerDestroyTopic(lua_State* L);
int ProducerGetTopicConfig(lua_State* L);
int ProducerSetTopicConfig(lua_State* L);
int ProducerListGroups(lua_State* L);
int ProducerDescribeGroups(lua_State* L);
int ProducerDeleteGroup(lua_State* L);
int ProducerGetGroupOffsets(lua_State* L);
int ProducerSetGroupOffsets(lua_State* L);
int ProducerDeleteGroupOffsets(lua_State* L);
int ProducerGC(lua_State* L);
int ProducerToString(lua_State* L);
