#pragma once
#include "LuaMainInclude.h"
#include "librdkafka/rdkafka.h"
#include "Platform.h"

static const char* LUAKAFKAPRODUCER = "KAFKAPRODUCER";

typedef struct LuaKafkaProducer {
	rd_kafka_t* rd;
} LuaKafkaProducer;

LuaKafkaProducer* lua_pushkafkaproducer(lua_State* L);
LuaKafkaProducer* luaL_checkkafkaproducer(lua_State* L, int index);

int Kafka_NewProducer(lua_State* L);
int KafkaProducer_Send(lua_State* L);
int KafkaProducer_GetOffsets(lua_State* L);
int KafkaProducer_GetMetadata(lua_State* L);
int KafkaProducer_CreateTopic(lua_State* L);
int KafkaProducer_DestroyTopic(lua_State* L);
int KafkaProducer_GetTopicConfig(lua_State* L);
int KafkaProducer_SetTopicConfig(lua_State* L);
int KafkaProducer_ListGroups(lua_State* L);
int KafkaProducer_DescribeGroups(lua_State* L);
int KafkaProducer_DeleteGroup(lua_State* L);
int KafkaProducer_GetGroupOffsets(lua_State* L);
int KafkaProducer_SetGroupOffsets(lua_State* L);
int KafkaProducer_DeleteGroupOffsets(lua_State* L);
int KafkaProducer_Close(lua_State* L);
int KafkaProducer_ToString(lua_State* L);
