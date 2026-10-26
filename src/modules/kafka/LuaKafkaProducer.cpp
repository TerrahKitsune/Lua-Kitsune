#include "kafka/LuaKafkaProducer.h"
#include "kafka/LuaKafkaHelpers.h"
#include "kafka/LuaKafka.h"
#include <string.h>
#include <stdlib.h>

LuaKafkaProducer* lua_pushkafkaproducer(lua_State* L) {
	LuaKafkaProducer* p = (LuaKafkaProducer*)lua_newuserdata(L, sizeof(LuaKafkaProducer));
	if (!p)
		luaL_error(L, "Unable to push kafka producer");
	luaL_getmetatable(L, LUAKAFKAPRODUCER);
	lua_setmetatable(L, -2);
	memset(p, 0, sizeof(LuaKafkaProducer));
	return p;
}

LuaKafkaProducer* luaL_checkkafkaproducer(lua_State* L, int index) {
	LuaKafkaProducer* p = (LuaKafkaProducer*)luaL_checkudata(L, index, LUAKAFKAPRODUCER);
	if (!p)
		luaL_error(L, "parameter is not a %s", LUAKAFKAPRODUCER);
	return p;
}

int Kafka_NewProducer(lua_State* L) {
	rd_kafka_conf_t* conf = ToKafkaConf(L, 1, "LUAP");
	rd_kafka_conf_set_log_cb(conf, KafkaLogger);

	char errbuf[kafka_error_buffer_len];
	rd_kafka_t* rd = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errbuf, kafka_error_buffer_len);
	if (!rd) {
		rd_kafka_conf_destroy(conf);
		lua_pushnil(L);
		lua_pushstring(L, errbuf);
		return 2;
	}

	LuaKafkaProducer* p = lua_pushkafkaproducer(L);
	p->rd = rd;
	return 1;
}

int KafkaProducer_Send(lua_State* L) {
	LuaKafkaProducer* p = luaL_checkkafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}

	const char* topic = luaL_checkstring(L, 2);

	size_t keylen = 0;
	const char* key = luaL_optlstring(L, 3, NULL, &keylen);

	size_t vallen;
	const char* value = luaL_checklstring(L, 4, &vallen);

	rd_kafka_headers_t* headers = NULL;

	if (lua_type(L, 5) == LUA_TTABLE) {
		size_t count = 0;
		lua_pushnil(L);
		while (lua_next(L, 5) != 0) {
			count++;
			lua_pop(L, 1);
		}

		headers = rd_kafka_headers_new(count);

		lua_pushnil(L);
		while (lua_next(L, 5) != 0) {
			size_t namesize, datasize;
			const char* name = lua_tolstring(L, -2, &namesize);
			const char* data = lua_tolstring(L, -1, &datasize);

			if (name && data) {
				rd_kafka_resp_err_t herr = rd_kafka_header_add(headers, name, namesize, data, datasize);
				if (herr) {
					rd_kafka_headers_destroy(headers);
					lua_pop(L, 2);
					lua_pushboolean(L, false);
					lua_pushstring(L, rd_kafka_err2str(herr));
					return 2;
				}
			}
			lua_pop(L, 1);
		}
	}

	// Optional partition at index 6; omit or pass nil to use the default partitioner.
	// Always create a topic handle: RD_KAFKA_V_RKT + PARTITION_UA invokes the
	// configured partitioner, identical to RD_KAFKA_V_TOPIC with no partition.
	// This collapses the (partition/no-partition) × (headers/no-headers) matrix
	// from 4 call sites to 2.
	int32_t partition = (int32_t)luaL_optinteger(L, 6, RD_KAFKA_PARTITION_UA);

	rd_kafka_topic_t* rkt = rd_kafka_topic_new(p->rd, topic, NULL);
	if (!rkt) {
		if (headers)
			rd_kafka_headers_destroy(headers);
		lua_pushboolean(L, false);
		lua_pushstring(L, rd_kafka_err2str(rd_kafka_last_error()));
		return 2;
	}

	rd_kafka_resp_err_t err;

	if (headers) {
		err = (rd_kafka_resp_err_t)rd_kafka_producev(
			p->rd,
			RD_KAFKA_V_RKT(rkt),
			RD_KAFKA_V_PARTITION(partition),
			RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY | RD_KAFKA_MSG_F_BLOCK),
			RD_KAFKA_V_KEY(key, keylen),
			RD_KAFKA_V_VALUE((void*)value, vallen),
			RD_KAFKA_V_HEADERS(headers),
			RD_KAFKA_V_END);
	}
	else {
		err = (rd_kafka_resp_err_t)rd_kafka_producev(
			p->rd,
			RD_KAFKA_V_RKT(rkt),
			RD_KAFKA_V_PARTITION(partition),
			RD_KAFKA_V_MSGFLAGS(RD_KAFKA_MSG_F_COPY | RD_KAFKA_MSG_F_BLOCK),
			RD_KAFKA_V_KEY(key, keylen),
			RD_KAFKA_V_VALUE((void*)value, vallen),
			RD_KAFKA_V_END);
	}

	rd_kafka_topic_destroy(rkt);

	if (err && headers)
		rd_kafka_headers_destroy(headers);

	lua_pushboolean(L, !err);
	if (err) {
		lua_pushstring(L, rd_kafka_err2str(rd_kafka_last_error()));
		return 2;
	}
	return 1;
}

int KafkaProducer_GetOffsets(lua_State* L) {
	LuaKafkaProducer* p = luaL_checkkafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return KafkaQueryWatermarks(L, p->rd);
}

int KafkaProducer_GetMetadata(lua_State* L) {
	LuaKafkaProducer* p = luaL_checkkafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return KafkaGetMetadata(L, p->rd);
}

int KafkaProducer_CreateTopic(lua_State* L) {
	LuaKafkaProducer* p = luaL_checkkafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return KafkaAdminCreateTopic(L, p->rd);
}

int KafkaProducer_DestroyTopic(lua_State* L) {
	LuaKafkaProducer* p = luaL_checkkafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return KafkaAdminDestroyTopic(L, p->rd);
}

int KafkaProducer_GetTopicConfig(lua_State* L) {
	LuaKafkaProducer* p = luaL_checkkafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return KafkaGetTopicConfig(L, p->rd);
}

int KafkaProducer_SetTopicConfig(lua_State* L) {
	LuaKafkaProducer* p = luaL_checkkafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return KafkaSetTopicConfig(L, p->rd);
}

int KafkaProducer_ListGroups(lua_State* L) {
	LuaKafkaProducer* p = luaL_checkkafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return KafkaListGroups(L, p->rd);
}

int KafkaProducer_DescribeGroups(lua_State* L) {
	LuaKafkaProducer* p = luaL_checkkafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return KafkaDescribeGroups(L, p->rd);
}

int KafkaProducer_DeleteGroup(lua_State* L) {
	LuaKafkaProducer* p = luaL_checkkafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return KafkaDeleteGroup(L, p->rd);
}

int KafkaProducer_GetGroupOffsets(lua_State* L) {
	LuaKafkaProducer* p = luaL_checkkafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return KafkaGetGroupOffsets(L, p->rd);
}

int KafkaProducer_SetGroupOffsets(lua_State* L) {
	LuaKafkaProducer* p = luaL_checkkafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return KafkaSetGroupOffsets(L, p->rd);
}

int KafkaProducer_DeleteGroupOffsets(lua_State* L) {
	LuaKafkaProducer* p = luaL_checkkafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return KafkaDeleteGroupOffsets(L, p->rd);
}

int KafkaProducer_Close(lua_State* L) {
	LuaKafkaProducer* p = luaL_checkkafkaproducer(L, 1);
	if (p->rd) {
		rd_kafka_flush(p->rd, 5000);
		rd_kafka_destroy(p->rd);
		p->rd = NULL;
	}
	return 0;
}

int KafkaProducer_ToString(lua_State* L) {
	char buf[64];
	sprintf(buf, "KafkaProducer: %p", (void*)luaL_checkkafkaproducer(L, 1));
	lua_pushstring(L, buf);
	return 1;
}
