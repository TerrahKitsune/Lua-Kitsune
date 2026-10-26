#include "kafka/LuaKafka.h"
#include "KitsuneFile.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <mutex>
#include "kafka/LuaKafkaHelpers.h"

#define KAFKA_LAST_ERROR_BUFFER_LEN 10240
static char lasterror[KAFKA_LAST_ERROR_BUFFER_LEN] = { 0 };
static FILE* KafkaLogFile = NULL;
static std::mutex g_kafkaLogMutex;

void KafkaLogger(const rd_kafka_t* rk, int level, const char* fac, const char* buf) {

	time_t rawtime;
	time(&rawtime);
	struct tm timeinfo;
#ifdef _WIN32
	localtime_s(&timeinfo, &rawtime);
#else
	localtime_r(&rawtime, &timeinfo);
#endif
	char timestamp[100];
	strftime(timestamp, sizeof(timestamp), "%x %X", &timeinfo);

	std::lock_guard<std::mutex> lock(g_kafkaLogMutex);

	if (KafkaLogFile) {
		fprintf(KafkaLogFile, "[%s] [%p] [%d] [%s]: %s\n", timestamp, (const void*)rk, level, fac, buf);
		fflush(KafkaLogFile);
	}

	size_t len = strlen(lasterror);
	snprintf(&lasterror[len], KAFKA_LAST_ERROR_BUFFER_LEN - len - 1, "[%s] [%p] [%d] [%s]: %s\n", timestamp, (const void*)rk, level, fac, buf);
}

int Kafka_Logs(lua_State* L) {

	const char* logfile = luaL_optstring(L, lua_type(L, 1) == LUA_TSTRING ? 1 : 2, NULL);

	std::lock_guard<std::mutex> lock(g_kafkaLogMutex);

	if (logfile) {
		if (KafkaLogFile) {
			fclose(KafkaLogFile);
			KafkaLogFile = NULL;
		}
		if (logfile[0] != '\0') {
			KafkaLogFile = kitsune_fopen(logfile, "a");
		}
	}

	lua_pushstring(L, lasterror);
	lasterror[0] = '\0';
	return 1;
}