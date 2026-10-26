#include "kafka/LuaKafka.h"
#include "kafka/LuaKafkaMain.h"
#include "kafka/LuaKafkaProducer.h"
#include "kafka/LuaKafkaConsumer.h"

static const luaL_Reg kafkaproducerfunctions[] = {
	{ "Send",               KafkaProducer_Send               },
	{ "GetOffsets",         KafkaProducer_GetOffsets         },
	{ "GetMetadata",        KafkaProducer_GetMetadata        },
	{ "CreateTopic",        KafkaProducer_CreateTopic        },
	{ "DestroyTopic",       KafkaProducer_DestroyTopic       },
	{ "GetTopicConfig",     KafkaProducer_GetTopicConfig     },
	{ "SetTopicConfig",     KafkaProducer_SetTopicConfig     },
	{ "ListGroups",         KafkaProducer_ListGroups         },
	{ "DescribeGroups",     KafkaProducer_DescribeGroups     },
	{ "DeleteGroup",        KafkaProducer_DeleteGroup        },
	{ "GetGroupOffsets",    KafkaProducer_GetGroupOffsets    },
	{ "SetGroupOffsets",    KafkaProducer_SetGroupOffsets    },
	{ "DeleteGroupOffsets", KafkaProducer_DeleteGroupOffsets },
	{ "Close",              KafkaProducer_Close                 },
	{ NULL, NULL }
};

static const luaL_Reg kafkaproducermeta[] = {
	{ "__gc",       KafkaProducer_Close       },
	{ "__tostring", KafkaProducer_ToString },
	{ NULL, NULL }
};

static const luaL_Reg kafkaconsumerfunctions[] = {
	{ "Subscribe",          KafkaConsumer_Subscribe          },
	{ "Assign",             KafkaConsumer_Assign             },
	{ "Poll",               KafkaConsumer_Poll               },
	{ "Seek",               KafkaConsumer_Seek               },
	{ "GetOffsets",         KafkaConsumer_GetOffsets         },
	{ "GetMetadata",        KafkaConsumer_GetMetadata        },
	{ "GetTopicConfig",     KafkaConsumer_GetTopicConfig     },
	{ "SetTopicConfig",     KafkaConsumer_SetTopicConfig     },
	{ "ListGroups",         KafkaConsumer_ListGroups         },
	{ "DescribeGroups",     KafkaConsumer_DescribeGroups     },
	{ "DeleteGroup",        KafkaConsumer_DeleteGroup        },
	{ "GetGroupOffsets",    KafkaConsumer_GetGroupOffsets    },
	{ "SetGroupOffsets",    KafkaConsumer_SetGroupOffsets    },
	{ "DeleteGroupOffsets", KafkaConsumer_DeleteGroupOffsets },
	{ "Close",              KafkaConsumer_Close                 },
	{ NULL, NULL }
};

static const luaL_Reg kafkaconsumermeta[] = {
	{ "__gc",       KafkaConsumer_Close       },
	{ "__tostring", KafkaConsumer_ToString },
	{ NULL, NULL }
};

static const luaL_Reg kafkamodule[] = {
	{ "NewProducer", Kafka_NewProducer },
	{ "NewConsumer", Kafka_NewConsumer },
	{ "Logs",        Kafka_Logs    },
	{ NULL, NULL }
};

static void RegisterType(lua_State* L, const char* metaname,
	const luaL_Reg* functions, const luaL_Reg* meta) {

	lua_newtable(L);
	luaL_setfuncs(L, functions, 0);

	luaL_newmetatable(L, metaname);
	luaL_setfuncs(L, meta, 0);

	lua_pushliteral(L, "__index");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);
	lua_pushliteral(L, "__metatable");
	lua_pushvalue(L, -3);
	lua_rawset(L, -3);

	lua_pop(L, 2);
}

int luaopen_kafka(lua_State* L) {

	RegisterType(L, LUAKAFKAPRODUCER, kafkaproducerfunctions, kafkaproducermeta);
	RegisterType(L, LUAKAFKACONSUMER, kafkaconsumerfunctions, kafkaconsumermeta);

	luaL_newlibtable(L, kafkamodule);
	luaL_setfuncs(L, kafkamodule, 0);

	return 1;
}
