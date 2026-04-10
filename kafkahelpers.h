#pragma once
#include "luakafka.h"

void lua_pushkafkabroker(lua_State* L, const rd_kafka_metadata_broker* broker);
void lua_pushkafkatopic(lua_State* L, const rd_kafka_metadata_topic* topic);
rd_kafka_conf_t* lua_tokafkaconf(lua_State* L, int idx, const char* defaultgroup);

// Shared watermark query — pushes (true, low, high) on success,
// (false, errmsg) on failure. Args are at indices 2 (topic), 3 (partition),
// 4 (opt timeout_ms). Self (the Lua userdata) is at index 1.
int kafka_query_watermarks(lua_State* L, rd_kafka_t* rd);

// Shared admin helpers. Self is at index 1 in all cases.
//
// kafka_admin_create_topic:
//   2 = name (string)
//   3 = num_partitions (integer)
//   4 = retention_ms (integer or nil  — nil keeps broker default)
//   5 = retention_bytes (integer or nil — nil keeps broker default)
//   6 = replication_factor (integer or nil — nil/-1 uses broker default)
//   7 = timeout_ms (integer, default 10000)
// Returns (true) on success, (false, errmsg) on failure.
int kafka_admin_create_topic(lua_State* L, rd_kafka_t* rd);

// kafka_admin_destroy_topic:
//   2 = name (string)
//   3 = timeout_ms (integer, default 10000)
// Returns (true) on success, (false, errmsg) on failure.
int kafka_admin_destroy_topic(lua_State* L, rd_kafka_t* rd);

// kafka_get_metadata: fetches broker and topic metadata from the cluster.
//   2 = timeout_ms (integer, default 5000)
// Returns (true, metadata_table) on success, (false, errmsg) on failure.
// metadata_table = { Brokers={...}, Topics={...}, OrigBrokerId=N, OrigBrokerName='' }
int kafka_get_metadata(lua_State* L, rd_kafka_t* rd);

// kafka_get_topic_config: fetches all configuration entries for one topic.
//   2 = topic name (string)
//   3 = timeout_ms (integer, default 5000)
// Returns (true, config_table) on success, (false, errmsg) on failure.
// config_table maps config-name strings to their current value strings.
int kafka_get_topic_config(lua_State* L, rd_kafka_t* rd);

// kafka_set_topic_config: incrementally updates a topic's configuration.
// Only the keys present in the config table are changed; all other settings
// are left at their current broker values (IncrementalAlterConfigs SET).
//   2 = topic name (string)
//   3 = config table  { ['key'] = 'value', ... }
//   4 = timeout_ms (integer, default 10000)
// Returns (true) on success, (false, errmsg) on failure.
int kafka_set_topic_config(lua_State* L, rd_kafka_t* rd);

// ── Group admin ──────────────────────────────────────────────────────────────

// kafka_list_groups: list all consumer groups in the cluster.
//   2 = timeout_ms (integer, default 5000)
// Returns (true, {{GroupId, State}, ...}) or (false, errmsg).
int kafka_list_groups(lua_State* L, rd_kafka_t* rd);

// kafka_describe_groups: detailed info for one or more groups.
//   2 = group-id array ({"grp1", ...})
//   3 = timeout_ms (integer, default 5000)
// Returns (true, {{GroupId, State, Protocol, Coordinator, Members}, ...})
// or (false, errmsg).
int kafka_describe_groups(lua_State* L, rd_kafka_t* rd);

// kafka_delete_group: delete a consumer group (must be empty).
//   2 = group id (string)
//   3 = timeout_ms (integer, default 10000)
// Returns (true) or (false, errmsg).
int kafka_delete_group(lua_State* L, rd_kafka_t* rd);

// kafka_get_group_offsets: get committed offsets for a group.
//   2 = group id (string)
//   3 = partitions array ({"topic:N", ...}) or nil for all
//   4 = timeout_ms (integer, default 5000)
// Returns (true, {['topic:N']=offset, ...}) or (false, errmsg).
int kafka_get_group_offsets(lua_State* L, rd_kafka_t* rd);

// kafka_set_group_offsets: set committed offsets for a group.
//   2 = group id (string)
//   3 = offsets table ({['topic:N']=offset, ...})
//   4 = timeout_ms (integer, default 10000)
// Returns (true) or (false, errmsg).
int kafka_set_group_offsets(lua_State* L, rd_kafka_t* rd);

// kafka_delete_group_offsets: remove committed offsets for a group.
//   2 = group id (string)
//   3 = partitions array ({"topic:N", ...})
//   4 = timeout_ms (integer, default 10000)
// Returns (true) or (false, errmsg).
int kafka_delete_group_offsets(lua_State* L, rd_kafka_t* rd);