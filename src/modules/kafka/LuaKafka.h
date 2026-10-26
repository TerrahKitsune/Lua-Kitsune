#pragma once
#include "LuaMainInclude.h"
#include "librdkafka/rdkafka.h"
#include "Platform.h"

#define KAFKA_ERROR_BUFFER_LEN 1024

void KafkaLogger(const rd_kafka_t* rk, int level, const char* fac, const char* buf);
int Kafka_Logs(lua_State* L);
