#pragma once
#include "LuaMainInclude.h"
#include "librdkafka/rdkafka.h"
#include "Platform.h"

static const char* LUAKAFKACONSUMER = "KAFKACONSUMER";

typedef struct LuaKafkaConsumer {
	rd_kafka_t* rd;
	rd_kafka_message_t* msg;  // last real message; committed and freed at the top of the next Poll()
} LuaKafkaConsumer;

LuaKafkaConsumer* lua_pushkafkaconsumer(lua_State* L);
LuaKafkaConsumer* luaL_checkkafkaconsumer(lua_State* L, int index);

int Kafka_NewConsumer(lua_State* L);
int KafkaConsumer_Subscribe(lua_State* L);
int KafkaConsumer_Assign(lua_State* L);
int KafkaConsumer_Poll(lua_State* L);
int KafkaConsumer_Seek(lua_State* L);
int KafkaConsumer_GetOffsets(lua_State* L);
int KafkaConsumer_GetMetadata(lua_State* L);
int KafkaConsumer_GetTopicConfig(lua_State* L);
int KafkaConsumer_SetTopicConfig(lua_State* L);
int KafkaConsumer_ListGroups(lua_State* L);
int KafkaConsumer_DescribeGroups(lua_State* L);
int KafkaConsumer_DeleteGroup(lua_State* L);
int KafkaConsumer_GetGroupOffsets(lua_State* L);
int KafkaConsumer_SetGroupOffsets(lua_State* L);
int KafkaConsumer_DeleteGroupOffsets(lua_State* L);
int KafkaConsumer_Close(lua_State* L);
int KafkaConsumer_ToString(lua_State* L);
