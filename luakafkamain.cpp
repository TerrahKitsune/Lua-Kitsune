#include "luakafka.h"
#include "luakafkamain.h"
#include "luakafkaproducer.h"
#include "luakafkaconsumer.h"

static const luaL_Reg kafkaproducerfunctions[] = {
    { "Send",               ProducerSend               },
    { "GetOffsets",         ProducerGetOffsets         },
    { "GetMetadata",        ProducerGetMetadata        },
    { "CreateTopic",        ProducerCreateTopic        },
    { "DestroyTopic",       ProducerDestroyTopic       },
    { "GetTopicConfig",     ProducerGetTopicConfig     },
    { "SetTopicConfig",     ProducerSetTopicConfig     },
    { "ListGroups",         ProducerListGroups         },
    { "DescribeGroups",     ProducerDescribeGroups     },
    { "DeleteGroup",        ProducerDeleteGroup        },
    { "GetGroupOffsets",    ProducerGetGroupOffsets    },
    { "SetGroupOffsets",    ProducerSetGroupOffsets    },
    { "DeleteGroupOffsets", ProducerDeleteGroupOffsets },
    { "Close",              ProducerGC                 },
    { NULL, NULL }
};

static const luaL_Reg kafkaproducermeta[] = {
    { "__gc",       ProducerGC       },
    { "__tostring", ProducerToString },
    { NULL, NULL }
};

static const luaL_Reg kafkaconsumerfunctions[] = {
    { "Subscribe",          ConsumerSubscribe          },
    { "Assign",             ConsumerAssign             },
    { "Poll",               ConsumerPoll               },
    { "Seek",               ConsumerSeek               },
    { "GetOffsets",         ConsumerGetOffsets         },
    { "GetMetadata",        ConsumerGetMetadata        },
    { "GetTopicConfig",     ConsumerGetTopicConfig     },
    { "SetTopicConfig",     ConsumerSetTopicConfig     },
    { "ListGroups",         ConsumerListGroups         },
    { "DescribeGroups",     ConsumerDescribeGroups     },
    { "DeleteGroup",        ConsumerDeleteGroup        },
    { "GetGroupOffsets",    ConsumerGetGroupOffsets    },
    { "SetGroupOffsets",    ConsumerSetGroupOffsets    },
    { "DeleteGroupOffsets", ConsumerDeleteGroupOffsets },
    { "Close",              ConsumerGC                 },
    { NULL, NULL }
};

static const luaL_Reg kafkaconsumermeta[] = {
    { "__gc",       ConsumerGC       },
    { "__tostring", ConsumerToString },
    { NULL, NULL }
};

static const luaL_Reg kafkamodule[] = {
    { "NewProducer", CreateProducer },
    { "NewConsumer", CreateConsumer },
    { "Logs",        GetLastLogs    },
    { NULL, NULL }
};

static void register_type(lua_State* L, const char* metaname,
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

    register_type(L, LUAKAFKAPRODUCER, kafkaproducerfunctions, kafkaproducermeta);
    register_type(L, LUAKAFKACONSUMER, kafkaconsumerfunctions, kafkaconsumermeta);

    // Module table returned to Lua
    luaL_newlibtable(L, kafkamodule);
    luaL_setfuncs(L, kafkamodule, 0);

    return 1;
}
