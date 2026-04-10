#include "luakafkaproducer.h"
#include "kafkahelpers.h"
#include "luakafka.h"
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

LuaKafkaProducer* lua_tokafkaproducer(lua_State* L, int index) {
	LuaKafkaProducer* p = (LuaKafkaProducer*)luaL_checkudata(L, index, LUAKAFKAPRODUCER);
	if (!p)
		luaL_error(L, "parameter is not a %s", LUAKAFKAPRODUCER);
	return p;
}

int CreateProducer(lua_State* L) {
	rd_kafka_conf_t* conf = lua_tokafkaconf(L, 1, "LUAP");
	rd_kafka_conf_set_log_cb(conf, kafka_logger);

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

int ProducerSend(lua_State* L) {
	LuaKafkaProducer* p = lua_tokafkaproducer(L, 1);
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

int ProducerGetOffsets(lua_State* L) {
	LuaKafkaProducer* p = lua_tokafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return kafka_query_watermarks(L, p->rd);
}

int ProducerGetMetadata(lua_State* L) {
	LuaKafkaProducer* p = lua_tokafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return kafka_get_metadata(L, p->rd);
}

int ProducerCreateTopic(lua_State* L) {
	LuaKafkaProducer* p = lua_tokafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return kafka_admin_create_topic(L, p->rd);
}

int ProducerDestroyTopic(lua_State* L) {
	LuaKafkaProducer* p = lua_tokafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return kafka_admin_destroy_topic(L, p->rd);
}

int ProducerGetTopicConfig(lua_State* L) {
	LuaKafkaProducer* p = lua_tokafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return kafka_get_topic_config(L, p->rd);
}

int ProducerSetTopicConfig(lua_State* L) {
	LuaKafkaProducer* p = lua_tokafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return kafka_set_topic_config(L, p->rd);
}

int ProducerListGroups(lua_State* L) {
	LuaKafkaProducer* p = lua_tokafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return kafka_list_groups(L, p->rd);
}

int ProducerDescribeGroups(lua_State* L) {
	LuaKafkaProducer* p = lua_tokafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return kafka_describe_groups(L, p->rd);
}

int ProducerDeleteGroup(lua_State* L) {
	LuaKafkaProducer* p = lua_tokafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return kafka_delete_group(L, p->rd);
}

int ProducerGetGroupOffsets(lua_State* L) {
	LuaKafkaProducer* p = lua_tokafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return kafka_get_group_offsets(L, p->rd);
}

int ProducerSetGroupOffsets(lua_State* L) {
	LuaKafkaProducer* p = lua_tokafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return kafka_set_group_offsets(L, p->rd);
}

int ProducerDeleteGroupOffsets(lua_State* L) {
	LuaKafkaProducer* p = lua_tokafkaproducer(L, 1);
	if (!p->rd) {
		luaL_error(L, "Producer not open");
		return 0;
	}
	return kafka_delete_group_offsets(L, p->rd);
}

int ProducerGC(lua_State* L) {
	LuaKafkaProducer* p = lua_tokafkaproducer(L, 1);
	if (p->rd) {
		rd_kafka_flush(p->rd, 5000);
		rd_kafka_destroy(p->rd);
		p->rd = NULL;
	}
	return 0;
}

int ProducerToString(lua_State* L) {
	char buf[64];
	sprintf(buf, "KafkaProducer: %p", (void*)lua_tokafkaproducer(L, 1));
	lua_pushstring(L, buf);
	return 1;
}
