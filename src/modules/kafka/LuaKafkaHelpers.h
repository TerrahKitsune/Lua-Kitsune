#pragma once
#include "kafka/LuaKafka.h"

void PushKafkaBroker(lua_State* L, const rd_kafka_metadata_broker* broker);
void PushKafkaTopic(lua_State* L, const rd_kafka_metadata_topic* topic);
rd_kafka_conf_t* ToKafkaConf(lua_State* L, int idx, const char* defaultgroup);

// Shared watermark query — pushes (true, low, high) on success,
// (false, errmsg) on failure. Args are at indices 2 (topic), 3 (partition),
// 4 (opt timeout_ms). Self (the Lua userdata) is at index 1.
int KafkaQueryWatermarks(lua_State* L, rd_kafka_t* rd);

// Shared admin helpers. Self is at index 1 in all cases.
//
// KafkaAdminCreateTopic:
//   2 = name (string)
//   3 = num_partitions (integer)
//   4 = retention_ms (integer or nil  — nil keeps broker default)
//   5 = retention_bytes (integer or nil — nil keeps broker default)
//   6 = replication_factor (integer or nil — nil/-1 uses broker default)
//   7 = timeout_ms (integer, default 10000)
// Returns (true) on success, (false, errmsg) on failure.
int KafkaAdminCreateTopic(lua_State* L, rd_kafka_t* rd);

// KafkaAdminDestroyTopic:
//   2 = name (string)
//   3 = timeout_ms (integer, default 10000)
// Returns (true) on success, (false, errmsg) on failure.
int KafkaAdminDestroyTopic(lua_State* L, rd_kafka_t* rd);

// KafkaGetMetadata: fetches broker and topic metadata from the cluster.
//   2 = timeout_ms (integer, default 5000)
// Returns (true, metadata_table) on success, (false, errmsg) on failure.
// metadata_table = { Brokers={...}, Topics={...}, OrigBrokerId=N, OrigBrokerName='' }
int KafkaGetMetadata(lua_State* L, rd_kafka_t* rd);

// KafkaGetTopicConfig: fetches all configuration entries for one topic.
//   2 = topic name (string)
//   3 = timeout_ms (integer, default 5000)
// Returns (true, config_table) on success, (false, errmsg) on failure.
// config_table maps config-name strings to their current value strings.
int KafkaGetTopicConfig(lua_State* L, rd_kafka_t* rd);

// KafkaSetTopicConfig: incrementally updates a topic's configuration.
// Only the keys present in the config table are changed; all other settings
// are left at their current broker values (IncrementalAlterConfigs SET).
//   2 = topic name (string)
//   3 = config table  { ['key'] = 'value', ... }
//   4 = timeout_ms (integer, default 10000)
// Returns (true) on success, (false, errmsg) on failure.
int KafkaSetTopicConfig(lua_State* L, rd_kafka_t* rd);

// Group admin

// KafkaListGroups: list all consumer groups in the cluster.
//   2 = timeout_ms (integer, default 5000)
// Returns (true, {{GroupId, State}, ...}) or (false, errmsg).
int KafkaListGroups(lua_State* L, rd_kafka_t* rd);

// KafkaDescribeGroups: detailed info for one or more groups.
//   2 = group-id array ({"grp1", ...})
//   3 = timeout_ms (integer, default 5000)
// Returns (true, {{GroupId, State, Protocol, Coordinator, Members}, ...})
// or (false, errmsg).
int KafkaDescribeGroups(lua_State* L, rd_kafka_t* rd);

// KafkaDeleteGroup: delete a consumer group (must be empty).
//   2 = group id (string)
//   3 = timeout_ms (integer, default 10000)
// Returns (true) or (false, errmsg).
int KafkaDeleteGroup(lua_State* L, rd_kafka_t* rd);

// KafkaGetGroupOffsets: get committed offsets for a group.
//   2 = group id (string)
//   3 = partitions array ({"topic:N", ...}) or nil for all
//   4 = timeout_ms (integer, default 5000)
// Returns (true, {['topic:N']=offset, ...}) or (false, errmsg).
int KafkaGetGroupOffsets(lua_State* L, rd_kafka_t* rd);

// KafkaSetGroupOffsets: set committed offsets for a group.
//   2 = group id (string)
//   3 = offsets table ({['topic:N']=offset, ...})
//   4 = timeout_ms (integer, default 10000)
// Returns (true) or (false, errmsg).
int KafkaSetGroupOffsets(lua_State* L, rd_kafka_t* rd);

// KafkaDeleteGroupOffsets: remove committed offsets for a group.
//   2 = group id (string)
//   3 = partitions array ({"topic:N", ...})
//   4 = timeout_ms (integer, default 10000)
// Returns (true) or (false, errmsg).
int KafkaDeleteGroupOffsets(lua_State* L, rd_kafka_t* rd);