#pragma once
#include "lua_main_incl.h"
#include "librdkafka/rdkafka.h"
#include "platform.h"

#define kafka_error_buffer_len 1024

void kafka_logger(const rd_kafka_t* rk, int level, const char* fac, const char* buf);
int GetLastLogs(lua_State* L);
