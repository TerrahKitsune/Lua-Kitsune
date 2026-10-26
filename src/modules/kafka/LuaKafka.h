#pragma once
#include "LuaMainInclude.h"
#include "librdkafka/rdkafka.h"
#include "Platform.h"

#define kafka_error_buffer_len 1024

void KafkaLogger(const rd_kafka_t* rk, int level, const char* fac, const char* buf);
int Kafka_Logs(lua_State* L);
