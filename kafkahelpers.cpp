#include "kafkahelpers.h"

rd_kafka_conf_t* lua_tokafkaconf(lua_State* L, int idx, const char* defaultgroup) {

	rd_kafka_conf_t* conf = rd_kafka_conf_new();
	bool didGroup = false;

	const char* confname;
	const char* confvalue;
	char errbuf[kafka_error_buffer_len];

	if (lua_type(L, idx) == LUA_TTABLE) {

		lua_pushnil(L);
		while (lua_next(L, idx) != 0) {

			confname = luaL_checkstring(L, -2);
			confvalue = luaL_checkstring(L, -1);

			if (!didGroup && strcmp(confname, "group.id") == 0) {
				didGroup = true;
			}

			if (rd_kafka_conf_set(conf, confname, confvalue, errbuf, kafka_error_buffer_len) != RD_KAFKA_CONF_OK) {

				rd_kafka_conf_destroy(conf);
				luaL_error(L, "Unable to set conf %s = %s: %s", confname, confvalue, errbuf);
			}

			lua_pop(L, 1);
		}
	}

	if (!didGroup) {
		if (rd_kafka_conf_set(conf, "group.id", defaultgroup, errbuf, kafka_error_buffer_len) != RD_KAFKA_CONF_OK) {

			rd_kafka_conf_destroy(conf);
			luaL_error(L, "Unable to set conf %s = %s: %s", "group.id", defaultgroup, errbuf);
		}
	}

	return conf;
}

void lua_pushkafkapartition(lua_State* L, const rd_kafka_metadata_partition* partition) {

	lua_createtable(L, 0, 6);

	lua_pushstring(L, "Error");
	lua_pushstring(L, rd_kafka_err2str(partition->err));
	lua_settable(L, -3);

	lua_pushstring(L, "ErrorCode");
	lua_pushinteger(L, partition->err);
	lua_settable(L, -3);

	lua_pushstring(L, "Id");
	lua_pushinteger(L, partition->id);
	lua_settable(L, -3);

	lua_pushstring(L, "Leader");
	lua_pushinteger(L, partition->leader);
	lua_settable(L, -3);

	lua_pushstring(L, "InSyncReplicas");
	lua_createtable(L, partition->isr_cnt, 0);
	for (size_t i = 0; i < partition->isr_cnt; i++)
	{
		lua_pushinteger(L, partition->isrs[i]);
		lua_rawseti(L, -2, i + 1);
	}
	lua_settable(L, -3);

	lua_pushstring(L, "Replicas");
	lua_createtable(L, partition->replica_cnt, 0);
	for (size_t i = 0; i < partition->replica_cnt; i++)
	{
		lua_pushinteger(L, partition->replicas[i]);
		lua_rawseti(L, -2, i + 1);
	}
	lua_settable(L, -3);
}

void lua_pushkafkatopic(lua_State* L, const rd_kafka_metadata_topic* topic) {

	lua_createtable(L, 0, 5);

	lua_pushstring(L, "Error");
	lua_pushstring(L, rd_kafka_err2str(topic->err));
	lua_settable(L, -3);

	lua_pushstring(L, "ErrorCode");
	lua_pushinteger(L, topic->err);
	lua_settable(L, -3);

	lua_pushstring(L, "Name");
	lua_pushstring(L, topic->topic);
	lua_settable(L, -3);

	lua_pushstring(L, "Partitions");
	lua_createtable(L, topic->partition_cnt, 0);
	for (size_t i = 0; i < topic->partition_cnt; i++)
	{
		lua_pushkafkapartition(L, &topic->partitions[i]);
		lua_rawseti(L, -2, i + 1);
	}
	lua_settable(L, -3);
}

void lua_pushkafkaptopicpartition(lua_State* L, const rd_kafka_topic_partition_t* topic) {

	lua_createtable(L, 0, 6);

	lua_pushstring(L, "Error");
	lua_pushstring(L, rd_kafka_err2str(topic->err));
	lua_settable(L, -3);

	lua_pushstring(L, "ErrorCode");
	lua_pushinteger(L, topic->err);
	lua_settable(L, -3);

	lua_pushstring(L, "Metadata");
	if (topic->metadata && topic->metadata_size > 0)
		lua_pushlstring(L, (const char*)topic->metadata, topic->metadata_size);
	else
		lua_pushstring(L, "");
	lua_settable(L, -3);

	lua_pushstring(L, "Offset");
	lua_pushinteger(L, topic->offset);
	lua_settable(L, -3);

	lua_pushstring(L, "Partition");
	lua_pushinteger(L, topic->partition);
	lua_settable(L, -3);

	lua_pushstring(L, "Topic");
	lua_pushstring(L, topic->topic);
	lua_settable(L, -3);
}

void lua_pushkafkabroker(lua_State* L, const rd_kafka_metadata_broker* broker) {

	lua_createtable(L, 0, 3);

	lua_pushstring(L, "Host");
	lua_pushstring(L, broker->host);
	lua_settable(L, -3);

	lua_pushstring(L, "Id");
	lua_pushinteger(L, broker->id);
	lua_settable(L, -3);

	lua_pushstring(L, "Port");
	lua_pushinteger(L, broker->port);
	lua_settable(L, -3);
}

int kafka_query_watermarks(lua_State* L, rd_kafka_t* rd) {
	const char* topic = luaL_checkstring(L, 2);
	int32_t partition = (int32_t)luaL_checkinteger(L, 3);
	int timeout = (int)luaL_optinteger(L, 4, 5000);

	int64_t low = 0;
	int64_t high = 0;
	rd_kafka_resp_err_t err = rd_kafka_query_watermark_offsets(
		rd, topic, partition, &low, &high, timeout);

	if (err) {
		lua_pushboolean(L, false);
		lua_pushstring(L, rd_kafka_err2str(err));
		return 2;
	}

	lua_pushboolean(L, true);
	lua_pushinteger(L, (lua_Integer)low);
	lua_pushinteger(L, (lua_Integer)high);
	return 3;
}

// ---------------------------------------------------------------------------
// Shared helper: polls an admin queue and extracts per-topic error.
// On timeout or event-level error pushes false+msg and returns 2.
// On per-topic error pushes false+msg and returns 2.
// On success pushes true and returns 1.
// The caller must have already issued the admin request to rkqu.
// ---------------------------------------------------------------------------
static int kafka_poll_admin_result(
	lua_State* L,
	rd_kafka_queue_t* rkqu,
	int poll_timeout_ms,
	bool is_create)
{
	rd_kafka_event_t* event = rd_kafka_queue_poll(rkqu, poll_timeout_ms);
	if (!event) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "Admin operation timed out waiting for broker response");
		return 2;
	}

	rd_kafka_resp_err_t event_err = rd_kafka_event_error(event);
	if (event_err) {
		const char* msg = rd_kafka_event_error_string(event);
		rd_kafka_event_destroy(event);
		lua_pushboolean(L, false);
		lua_pushstring(L, msg);
		return 2;
	}

	size_t cnt = 0;
	const rd_kafka_topic_result_t** results;

	if (is_create) {
		const rd_kafka_CreateTopics_result_t* r = rd_kafka_event_CreateTopics_result(event);
		results = rd_kafka_CreateTopics_result_topics(r, &cnt);
	}
	else {
		const rd_kafka_DeleteTopics_result_t* r = rd_kafka_event_DeleteTopics_result(event);
		results = rd_kafka_DeleteTopics_result_topics(r, &cnt);
	}

	rd_kafka_resp_err_t topic_err = RD_KAFKA_RESP_ERR_NO_ERROR;
	char topic_errbuf[512] = { 0 };
	if (cnt > 0) {
		topic_err = rd_kafka_topic_result_error(results[0]);
		if (topic_err) {
			const char* em = rd_kafka_topic_result_error_string(results[0]);
			if (em)
				strncpy(topic_errbuf, em, sizeof(topic_errbuf) - 1);
		}
	}

	rd_kafka_event_destroy(event);

	if (topic_err) {
		lua_pushboolean(L, false);
		lua_pushstring(L, topic_errbuf[0] ? topic_errbuf : rd_kafka_err2str(topic_err));
		return 2;
	}

	lua_pushboolean(L, true);
	return 1;
}

int kafka_admin_create_topic(lua_State* L, rd_kafka_t* rd) {
	const char* name = luaL_checkstring(L, 2);
	int num_partitions = (int)luaL_checkinteger(L, 3);
	int replication_factor = lua_isnoneornil(L, 6) ? -1 : (int)luaL_checkinteger(L, 6);
	int timeout_ms = (int)luaL_optinteger(L, 7, 10000);

	char errstr[512];
	rd_kafka_NewTopic_t* new_topic = rd_kafka_NewTopic_new(
		name, num_partitions, replication_factor, errstr, sizeof(errstr));

	if (!new_topic) {
		lua_pushboolean(L, false);
		lua_pushstring(L, errstr);
		return 2;
	}

	if (!lua_isnoneornil(L, 4)) {
		char val[32];
		snprintf(val, sizeof(val), "%lld", (long long)luaL_checkinteger(L, 4));
		rd_kafka_resp_err_t cerr = rd_kafka_NewTopic_set_config(new_topic, "retention.ms", val);
		if (cerr) {
			rd_kafka_NewTopic_destroy(new_topic);
			lua_pushboolean(L, false);
			lua_pushstring(L, rd_kafka_err2str(cerr));
			return 2;
		}
	}

	if (!lua_isnoneornil(L, 5)) {
		char val[32];
		snprintf(val, sizeof(val), "%lld", (long long)luaL_checkinteger(L, 5));
		rd_kafka_resp_err_t cerr = rd_kafka_NewTopic_set_config(new_topic, "retention.bytes", val);
		if (cerr) {
			rd_kafka_NewTopic_destroy(new_topic);
			lua_pushboolean(L, false);
			lua_pushstring(L, rd_kafka_err2str(cerr));
			return 2;
		}
	}

	char opts_errstr[256];
	rd_kafka_AdminOptions_t* options = rd_kafka_AdminOptions_new(rd, RD_KAFKA_ADMIN_OP_CREATETOPICS);
	rd_kafka_AdminOptions_set_operation_timeout(options, timeout_ms, opts_errstr, sizeof(opts_errstr));

	rd_kafka_queue_t* queue = rd_kafka_queue_new(rd);

	rd_kafka_CreateTopics(rd, &new_topic, 1, options, queue);
	rd_kafka_NewTopic_destroy(new_topic);
	rd_kafka_AdminOptions_destroy(options);

	int ret = kafka_poll_admin_result(L, queue, timeout_ms + 5000, true);
	rd_kafka_queue_destroy(queue);
	return ret;
}

int kafka_admin_destroy_topic(lua_State* L, rd_kafka_t* rd) {
	const char* name = luaL_checkstring(L, 2);
	int timeout_ms = (int)luaL_optinteger(L, 3, 10000);

	rd_kafka_DeleteTopic_t* del_topic = rd_kafka_DeleteTopic_new(name);

	char opts_errstr[256];
	rd_kafka_AdminOptions_t* options = rd_kafka_AdminOptions_new(rd, RD_KAFKA_ADMIN_OP_DELETETOPICS);
	rd_kafka_AdminOptions_set_operation_timeout(options, timeout_ms, opts_errstr, sizeof(opts_errstr));

	rd_kafka_queue_t* queue = rd_kafka_queue_new(rd);

	rd_kafka_DeleteTopics(rd, &del_topic, 1, options, queue);
	rd_kafka_DeleteTopic_destroy(del_topic);
	rd_kafka_AdminOptions_destroy(options);

	int ret = kafka_poll_admin_result(L, queue, timeout_ms + 5000, false);
	rd_kafka_queue_destroy(queue);
	return ret;
}

int kafka_get_metadata(lua_State* L, rd_kafka_t* rd) {
	int timeout_ms = (int)luaL_optinteger(L, 2, 5000);

	const struct rd_kafka_metadata* metadata = NULL;
	rd_kafka_resp_err_t err = rd_kafka_metadata(rd, 1, NULL, &metadata, timeout_ms);

	if (err) {
		lua_pushboolean(L, false);
		lua_pushstring(L, rd_kafka_err2str(err));
		return 2;
	}

	lua_pushboolean(L, true);

	lua_createtable(L, 0, 4);

	lua_pushstring(L, "Brokers");
	lua_createtable(L, metadata->broker_cnt, 0);
	for (int i = 0; i < metadata->broker_cnt; i++) {
		lua_pushkafkabroker(L, &metadata->brokers[i]);
		lua_rawseti(L, -2, i + 1);
	}
	lua_settable(L, -3);

	lua_pushstring(L, "Topics");
	lua_createtable(L, metadata->topic_cnt, 0);
	for (int i = 0; i < metadata->topic_cnt; i++) {
		lua_pushkafkatopic(L, &metadata->topics[i]);
		lua_rawseti(L, -2, i + 1);
	}
	lua_settable(L, -3);

	lua_pushstring(L, "OrigBrokerId");
	lua_pushinteger(L, metadata->orig_broker_id);
	lua_settable(L, -3);

	lua_pushstring(L, "OrigBrokerName");
	lua_pushstring(L, metadata->orig_broker_name ? metadata->orig_broker_name : "");
	lua_settable(L, -3);

	rd_kafka_metadata_destroy(metadata);
	return 2;
}

int kafka_get_topic_config(lua_State* L, rd_kafka_t* rd) {
	const char* topic_name = luaL_checkstring(L, 2);
	int timeout_ms = (int)luaL_optinteger(L, 3, 5000);

	rd_kafka_ConfigResource_t* res = rd_kafka_ConfigResource_new(RD_KAFKA_RESOURCE_TOPIC, topic_name);

	char opts_errstr[256];
	rd_kafka_AdminOptions_t* options = rd_kafka_AdminOptions_new(rd, RD_KAFKA_ADMIN_OP_DESCRIBECONFIGS);
	rd_kafka_AdminOptions_set_operation_timeout(options, timeout_ms, opts_errstr, sizeof(opts_errstr));

	rd_kafka_queue_t* queue = rd_kafka_queue_new(rd);

	rd_kafka_DescribeConfigs(rd, &res, 1, options, queue);
	rd_kafka_ConfigResource_destroy(res);
	rd_kafka_AdminOptions_destroy(options);

	rd_kafka_event_t* event = rd_kafka_queue_poll(queue, timeout_ms + 5000);
	rd_kafka_queue_destroy(queue);

	if (!event) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "DescribeConfigs timed out waiting for broker response");
		return 2;
	}

	rd_kafka_resp_err_t event_err = rd_kafka_event_error(event);
	if (event_err) {
		const char* msg = rd_kafka_event_error_string(event);
		lua_pushboolean(L, false);
		lua_pushstring(L, msg);
		rd_kafka_event_destroy(event);
		return 2;
	}

	const rd_kafka_DescribeConfigs_result_t* result = rd_kafka_event_DescribeConfigs_result(event);
	size_t resource_cnt;
	const rd_kafka_ConfigResource_t** resources = rd_kafka_DescribeConfigs_result_resources(result, &resource_cnt);

	if (resource_cnt == 0) {
		rd_kafka_event_destroy(event);
		lua_pushboolean(L, false);
		lua_pushstring(L, "No config resources returned");
		return 2;
	}

	rd_kafka_resp_err_t res_err = rd_kafka_ConfigResource_error(resources[0]);
	if (res_err) {
		const char* msg = rd_kafka_ConfigResource_error_string(resources[0]);
		lua_pushboolean(L, false);
		lua_pushstring(L, msg ? msg : rd_kafka_err2str(res_err));
		rd_kafka_event_destroy(event);
		return 2;
	}

	size_t entry_cnt;
	const rd_kafka_ConfigEntry_t** entries = rd_kafka_ConfigResource_configs(resources[0], &entry_cnt);

	lua_pushboolean(L, true);
	lua_createtable(L, 0, (int)entry_cnt);

	for (size_t i = 0; i < entry_cnt; i++) {
		// Skip synonym entries — they are alternate representations of the same setting
		if (rd_kafka_ConfigEntry_is_synonym(entries[i]))
			continue;

		const char* name = rd_kafka_ConfigEntry_name(entries[i]);
		const char* value = rd_kafka_ConfigEntry_value(entries[i]);
		if (name) {
			lua_pushstring(L, name);
			lua_pushstring(L, value ? value : "");
			lua_settable(L, -3);
		}
	}

	rd_kafka_event_destroy(event);
	return 2;
}

int kafka_set_topic_config(lua_State* L, rd_kafka_t* rd) {
	const char* topic_name = luaL_checkstring(L, 2);
	luaL_checktype(L, 3, LUA_TTABLE);
	int timeout_ms = (int)luaL_optinteger(L, 4, 10000);

	rd_kafka_ConfigResource_t* res = rd_kafka_ConfigResource_new(RD_KAFKA_RESOURCE_TOPIC, topic_name);

	lua_pushnil(L);
	while (lua_next(L, 3) != 0) {
		const char* key = luaL_checkstring(L, -2);
		const char* val = luaL_checkstring(L, -1);

		rd_kafka_error_t* cerr = rd_kafka_ConfigResource_add_incremental_config(
			res, key, RD_KAFKA_ALTER_CONFIG_OP_TYPE_SET, val);

		if (cerr) {
			const char* msg = rd_kafka_error_string(cerr);
			rd_kafka_error_destroy(cerr);
			rd_kafka_ConfigResource_destroy(res);
			lua_pop(L, 2);
			lua_pushboolean(L, false);
			lua_pushstring(L, msg);
			return 2;
		}
		lua_pop(L, 1);
	}

	char opts_errstr[256];
	rd_kafka_AdminOptions_t* options = rd_kafka_AdminOptions_new(rd, RD_KAFKA_ADMIN_OP_ALTERCONFIGS);
	rd_kafka_AdminOptions_set_operation_timeout(options, timeout_ms, opts_errstr, sizeof(opts_errstr));

	rd_kafka_queue_t* queue = rd_kafka_queue_new(rd);

	rd_kafka_IncrementalAlterConfigs(rd, &res, 1, options, queue);
	rd_kafka_ConfigResource_destroy(res);
	rd_kafka_AdminOptions_destroy(options);

	rd_kafka_event_t* event = rd_kafka_queue_poll(queue, timeout_ms + 5000);
	rd_kafka_queue_destroy(queue);

	if (!event) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "IncrementalAlterConfigs timed out waiting for broker response");
		return 2;
	}

	rd_kafka_resp_err_t event_err = rd_kafka_event_error(event);
	if (event_err) {
		const char* msg = rd_kafka_event_error_string(event);
		lua_pushboolean(L, false);
		lua_pushstring(L, msg);
		rd_kafka_event_destroy(event);
		return 2;
	}

	const rd_kafka_IncrementalAlterConfigs_result_t* result = rd_kafka_event_IncrementalAlterConfigs_result(event);
	size_t cnt;
	const rd_kafka_ConfigResource_t** resources = rd_kafka_IncrementalAlterConfigs_result_resources(result, &cnt);

	rd_kafka_resp_err_t res_err = RD_KAFKA_RESP_ERR_NO_ERROR;
	char res_errbuf[512] = { 0 };
	if (cnt > 0) {
		res_err = rd_kafka_ConfigResource_error(resources[0]);
		if (res_err) {
			const char* em = rd_kafka_ConfigResource_error_string(resources[0]);
			if (em)
				strncpy(res_errbuf, em, sizeof(res_errbuf) - 1);
		}
	}

	rd_kafka_event_destroy(event);

	if (res_err) {
		lua_pushboolean(L, false);
		lua_pushstring(L, res_errbuf[0] ? res_errbuf : rd_kafka_err2str(res_err));
		return 2;
	}

	lua_pushboolean(L, true);
	return 1;
}

// ---------------------------------------------------------------------------
// Internal helpers: build rd_kafka_topic_partition_list_t from Lua values
// ---------------------------------------------------------------------------

// Parses a "topic:N" string into topic + partition.
static void kafka_parse_tp_key(const char* entry, char* tbuf, size_t tbuf_sz, int32_t* out_part) {
	*out_part = RD_KAFKA_PARTITION_UA;
	const char* colon = strchr(entry, ':');
	if (colon) {
		size_t tlen = (size_t)(colon - entry);
		if (tlen >= tbuf_sz)
			tlen = tbuf_sz - 1;
		strncpy(tbuf, entry, tlen);
		tbuf[tlen] = '\0';
		char* end;
		long p = strtol(colon + 1, &end, 10);
		if (end != colon + 1 && (*end == '\0' || *end == ':') && p >= 0)
			*out_part = (int32_t)p;
	}
	else {
		strncpy(tbuf, entry, tbuf_sz - 1);
		tbuf[tbuf_sz - 1] = '\0';
	}
}

// Builds a partition list from a Lua array {"topic:N", ...} at idx.
// Returns NULL when idx is nil/absent (caller treats as "all").
static rd_kafka_topic_partition_list_t* kafka_build_tplist(lua_State* L, int idx) {
	if (lua_isnoneornil(L, idx))
		return NULL;
	luaL_checktype(L, idx, LUA_TTABLE);
	int n = (int)luaL_len(L, idx);
	rd_kafka_topic_partition_list_t* list = rd_kafka_topic_partition_list_new(n);
	char tbuf[512];
	for (int i = 1; i <= n; i++) {
		lua_rawgeti(L, idx, i);
		const char* entry = lua_tostring(L, -1);
		lua_pop(L, 1);
		if (!entry)
			continue;
		int32_t part;
		kafka_parse_tp_key(entry, tbuf, sizeof(tbuf), &part);
		rd_kafka_topic_partition_list_add(list, tbuf, part);
	}
	return list;
}

// Builds a partition+offset list from a Lua table {['topic:N']=offset, ...}.
static rd_kafka_topic_partition_list_t* kafka_build_offset_tplist(lua_State* L, int idx) {
	luaL_checktype(L, idx, LUA_TTABLE);
	int count = 0;
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		count++;
		lua_pop(L, 1);
	}
	rd_kafka_topic_partition_list_t* list = rd_kafka_topic_partition_list_new(count);
	char tbuf[512];
	lua_pushnil(L);
	while (lua_next(L, idx) != 0) {
		const char* key = lua_tostring(L, -2);
		int64_t offset = (int64_t)luaL_checkinteger(L, -1);
		lua_pop(L, 1);
		if (!key)
			continue;
		int32_t part;
		kafka_parse_tp_key(key, tbuf, sizeof(tbuf), &part);
		rd_kafka_topic_partition_t* tp = rd_kafka_topic_partition_list_add(list, tbuf, part);
		tp->offset = offset;
	}
	return list;
}

// Shared event-level error check. Returns 0 on success (does not push).
// On failure pushes false+errmsg and returns 2.
static int kafka_check_event_error(lua_State* L, rd_kafka_event_t* event) {
	rd_kafka_resp_err_t err = rd_kafka_event_error(event);
	if (!err)
		return 0;
	const char* msg = rd_kafka_event_error_string(event);
	lua_pushboolean(L, false);
	lua_pushstring(L, msg ? msg : rd_kafka_err2str(err));
	rd_kafka_event_destroy(event);
	return 2;
}

// ---------------------------------------------------------------------------
// kafka_list_groups
// ---------------------------------------------------------------------------

int kafka_list_groups(lua_State* L, rd_kafka_t* rd) {
	int timeout_ms = (int)luaL_optinteger(L, 2, 5000);

	char opts_errstr[256];
	rd_kafka_AdminOptions_t* options = rd_kafka_AdminOptions_new(rd, RD_KAFKA_ADMIN_OP_LISTCONSUMERGROUPS);
	rd_kafka_AdminOptions_set_request_timeout(options, timeout_ms, opts_errstr, sizeof(opts_errstr));

	rd_kafka_queue_t* queue = rd_kafka_queue_new(rd);
	rd_kafka_ListConsumerGroups(rd, options, queue);
	rd_kafka_AdminOptions_destroy(options);

	rd_kafka_event_t* event = rd_kafka_queue_poll(queue, timeout_ms + 5000);
	rd_kafka_queue_destroy(queue);

	if (!event) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "ListConsumerGroups timed out");
		return 2;
	}

	int r = kafka_check_event_error(L, event);
	if (r)
		return r;

	const rd_kafka_ListConsumerGroups_result_t* result = rd_kafka_event_ListConsumerGroups_result(event);
	size_t cnt;
	const rd_kafka_ConsumerGroupListing_t** groups = rd_kafka_ListConsumerGroups_result_valid(result, &cnt);

	lua_pushboolean(L, true);
	lua_createtable(L, (int)cnt, 0);

	for (size_t i = 0; i < cnt; i++) {
		lua_createtable(L, 0, 2);

		lua_pushstring(L, "GroupId");
		lua_pushstring(L, rd_kafka_ConsumerGroupListing_group_id(groups[i]));
		lua_settable(L, -3);

		lua_pushstring(L, "State");
		lua_pushstring(L, rd_kafka_consumer_group_state_name(
			rd_kafka_ConsumerGroupListing_state(groups[i])));
		lua_settable(L, -3);

		lua_rawseti(L, -2, (int)i + 1);
	}

	rd_kafka_event_destroy(event);
	return 2;
}

// ---------------------------------------------------------------------------
// kafka_describe_groups
// ---------------------------------------------------------------------------

int kafka_describe_groups(lua_State* L, rd_kafka_t* rd) {
	luaL_checktype(L, 2, LUA_TTABLE);
	int timeout_ms = (int)luaL_optinteger(L, 3, 5000);

	int n = (int)luaL_len(L, 2);
	const char** group_ids = (const char**)malloc((size_t)n * sizeof(const char*));
	if (!group_ids) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "out of memory");
		return 2;
	}
	for (int i = 0; i < n; i++) {
		lua_rawgeti(L, 2, i + 1);
		group_ids[i] = lua_tostring(L, -1);
		lua_pop(L, 1);
	}

	char opts_errstr[256];
	rd_kafka_AdminOptions_t* options = rd_kafka_AdminOptions_new(rd, RD_KAFKA_ADMIN_OP_DESCRIBECONSUMERGROUPS);
	rd_kafka_AdminOptions_set_request_timeout(options, timeout_ms, opts_errstr, sizeof(opts_errstr));

	rd_kafka_queue_t* queue = rd_kafka_queue_new(rd);
	rd_kafka_DescribeConsumerGroups(rd, group_ids, (size_t)n, options, queue);
	rd_kafka_AdminOptions_destroy(options);
	free(group_ids);

	rd_kafka_event_t* event = rd_kafka_queue_poll(queue, timeout_ms + 5000);
	rd_kafka_queue_destroy(queue);

	if (!event) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "DescribeConsumerGroups timed out");
		return 2;
	}

	int r = kafka_check_event_error(L, event);
	if (r)
		return r;

	const rd_kafka_DescribeConsumerGroups_result_t* result = rd_kafka_event_DescribeConsumerGroups_result(event);
	size_t gcnt;
	const rd_kafka_ConsumerGroupDescription_t** descs = rd_kafka_DescribeConsumerGroups_result_groups(result, &gcnt);

	lua_pushboolean(L, true);
	lua_createtable(L, (int)gcnt, 0);

	for (size_t i = 0; i < gcnt; i++) {
		const rd_kafka_ConsumerGroupDescription_t* d = descs[i];

		lua_createtable(L, 0, 6);

		lua_pushstring(L, "GroupId");
		lua_pushstring(L, rd_kafka_ConsumerGroupDescription_group_id(d));
		lua_settable(L, -3);

		lua_pushstring(L, "State");
		lua_pushstring(L, rd_kafka_consumer_group_state_name(
			rd_kafka_ConsumerGroupDescription_state(d)));
		lua_settable(L, -3);

		lua_pushstring(L, "Protocol");
		const char* assignor = rd_kafka_ConsumerGroupDescription_partition_assignor(d);
		lua_pushstring(L, assignor ? assignor : "");
		lua_settable(L, -3);

		const rd_kafka_error_t* gerr = rd_kafka_ConsumerGroupDescription_error(d);
		lua_pushstring(L, "Error");
		lua_pushstring(L, gerr ? rd_kafka_error_string(gerr) : "");
		lua_settable(L, -3);

		const rd_kafka_Node_t* coord = rd_kafka_ConsumerGroupDescription_coordinator(d);
		lua_pushstring(L, "Coordinator");
		if (coord) {
			lua_createtable(L, 0, 3);
			lua_pushstring(L, "Id");
			lua_pushinteger(L, rd_kafka_Node_id(coord));
			lua_settable(L, -3);
			lua_pushstring(L, "Host");
			lua_pushstring(L, rd_kafka_Node_host(coord));
			lua_settable(L, -3);
			lua_pushstring(L, "Port");
			lua_pushinteger(L, rd_kafka_Node_port(coord));
			lua_settable(L, -3);
		}
		else {
			lua_pushnil(L);
		}
		lua_settable(L, -3);

		size_t mcnt = rd_kafka_ConsumerGroupDescription_member_count(d);
		lua_pushstring(L, "Members");
		lua_createtable(L, (int)mcnt, 0);

		for (size_t m = 0; m < mcnt; m++) {
			const rd_kafka_MemberDescription_t* mem = rd_kafka_ConsumerGroupDescription_member(d, m);

			lua_createtable(L, 0, 4);

			lua_pushstring(L, "ClientId");
			lua_pushstring(L, rd_kafka_MemberDescription_client_id(mem));
			lua_settable(L, -3);

			lua_pushstring(L, "ConsumerId");
			lua_pushstring(L, rd_kafka_MemberDescription_consumer_id(mem));
			lua_settable(L, -3);

			lua_pushstring(L, "Host");
			lua_pushstring(L, rd_kafka_MemberDescription_host(mem));
			lua_settable(L, -3);

			const rd_kafka_MemberAssignment_t* asgn = rd_kafka_MemberDescription_assignment(mem);
			const rd_kafka_topic_partition_list_t* plist = asgn ? rd_kafka_MemberAssignment_partitions(asgn) : NULL;
			lua_pushstring(L, "Partitions");
			if (plist && plist->cnt > 0) {
				lua_createtable(L, plist->cnt, 0);
				for (int p = 0; p < plist->cnt; p++) {
					lua_createtable(L, 0, 2);
					lua_pushstring(L, "Topic");
					lua_pushstring(L, plist->elems[p].topic);
					lua_settable(L, -3);
					lua_pushstring(L, "Partition");
					lua_pushinteger(L, plist->elems[p].partition);
					lua_settable(L, -3);
					lua_rawseti(L, -2, p + 1);
				}
			}
			else {
				lua_createtable(L, 0, 0);
			}
			lua_settable(L, -3);

			lua_rawseti(L, -2, (int)m + 1);
		}
		lua_settable(L, -3);

		lua_rawseti(L, -2, (int)i + 1);
	}

	rd_kafka_event_destroy(event);
	return 2;
}

// ---------------------------------------------------------------------------
// kafka_delete_group
// ---------------------------------------------------------------------------

int kafka_delete_group(lua_State* L, rd_kafka_t* rd) {
	const char* group_id = luaL_checkstring(L, 2);
	int timeout_ms = (int)luaL_optinteger(L, 3, 10000);

	rd_kafka_DeleteGroup_t* del = rd_kafka_DeleteGroup_new(group_id);

	char opts_errstr[256];
	rd_kafka_AdminOptions_t* options = rd_kafka_AdminOptions_new(rd, RD_KAFKA_ADMIN_OP_DELETEGROUPS);
	rd_kafka_AdminOptions_set_request_timeout(options, timeout_ms, opts_errstr, sizeof(opts_errstr));

	rd_kafka_queue_t* queue = rd_kafka_queue_new(rd);
	rd_kafka_DeleteGroups(rd, &del, 1, options, queue);
	rd_kafka_DeleteGroup_destroy(del);
	rd_kafka_AdminOptions_destroy(options);

	rd_kafka_event_t* event = rd_kafka_queue_poll(queue, timeout_ms + 5000);
	rd_kafka_queue_destroy(queue);

	if (!event) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "DeleteGroups timed out");
		return 2;
	}

	int r = kafka_check_event_error(L, event);
	if (r)
		return r;

	const rd_kafka_DeleteGroups_result_t* result = rd_kafka_event_DeleteGroups_result(event);
	size_t cnt;
	const rd_kafka_group_result_t** results = rd_kafka_DeleteGroups_result_groups(result, &cnt);

	const rd_kafka_error_t* grp_err = (cnt > 0) ? rd_kafka_group_result_error(results[0]) : NULL;
	if (grp_err) {
		const char* msg = rd_kafka_error_string(grp_err);
		lua_pushboolean(L, false);
		lua_pushstring(L, msg ? msg : "DeleteGroup failed");
		rd_kafka_event_destroy(event);
		return 2;
	}

	rd_kafka_event_destroy(event);
	lua_pushboolean(L, true);
	return 1;
}

// ---------------------------------------------------------------------------
// kafka_get_group_offsets
// ---------------------------------------------------------------------------

int kafka_get_group_offsets(lua_State* L, rd_kafka_t* rd) {
	const char* group_id = luaL_checkstring(L, 2);
	int timeout_ms = (int)luaL_optinteger(L, 4, 5000);

	rd_kafka_topic_partition_list_t* parts = kafka_build_tplist(L, 3);

	rd_kafka_ListConsumerGroupOffsets_t* req = rd_kafka_ListConsumerGroupOffsets_new(group_id, parts);
	if (parts)
		rd_kafka_topic_partition_list_destroy(parts);

	char opts_errstr[256];
	rd_kafka_AdminOptions_t* options = rd_kafka_AdminOptions_new(rd, RD_KAFKA_ADMIN_OP_LISTCONSUMERGROUPOFFSETS);
	rd_kafka_AdminOptions_set_request_timeout(options, timeout_ms, opts_errstr, sizeof(opts_errstr));

	rd_kafka_queue_t* queue = rd_kafka_queue_new(rd);
	rd_kafka_ListConsumerGroupOffsets(rd, &req, 1, options, queue);
	rd_kafka_ListConsumerGroupOffsets_destroy(req);
	rd_kafka_AdminOptions_destroy(options);

	rd_kafka_event_t* event = rd_kafka_queue_poll(queue, timeout_ms + 5000);
	rd_kafka_queue_destroy(queue);

	if (!event) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "ListConsumerGroupOffsets timed out");
		return 2;
	}

	int r = kafka_check_event_error(L, event);
	if (r)
		return r;

	const rd_kafka_ListConsumerGroupOffsets_result_t* result = rd_kafka_event_ListConsumerGroupOffsets_result(event);
	size_t gcnt;
	const rd_kafka_group_result_t** results = rd_kafka_ListConsumerGroupOffsets_result_groups(result, &gcnt);

	if (gcnt > 0) {
		const rd_kafka_error_t* grp_err = rd_kafka_group_result_error(results[0]);
		if (grp_err) {
			const char* msg = rd_kafka_error_string(grp_err);
			lua_pushboolean(L, false);
			lua_pushstring(L, msg ? msg : "ListConsumerGroupOffsets failed");
			rd_kafka_event_destroy(event);
			return 2;
		}
	}

	lua_pushboolean(L, true);
	lua_createtable(L, 0, 8);

	if (gcnt > 0) {
		const rd_kafka_topic_partition_list_t* offsets = rd_kafka_group_result_partitions(results[0]);
		if (offsets) {
			char key_buf[640];
			for (int i = 0; i < offsets->cnt; i++) {
				const rd_kafka_topic_partition_t* tp = &offsets->elems[i];
				snprintf(key_buf, sizeof(key_buf), "%s:%d", tp->topic, tp->partition);
				lua_pushstring(L, key_buf);
				lua_pushinteger(L, (lua_Integer)tp->offset);
				lua_settable(L, -3);
			}
		}
	}

	rd_kafka_event_destroy(event);
	return 2;
}

// ---------------------------------------------------------------------------
// kafka_set_group_offsets
// ---------------------------------------------------------------------------

int kafka_set_group_offsets(lua_State* L, rd_kafka_t* rd) {
	const char* group_id = luaL_checkstring(L, 2);
	int timeout_ms = (int)luaL_optinteger(L, 4, 10000);

	rd_kafka_topic_partition_list_t* parts = kafka_build_offset_tplist(L, 3);

	rd_kafka_AlterConsumerGroupOffsets_t* req = rd_kafka_AlterConsumerGroupOffsets_new(group_id, parts);
	rd_kafka_topic_partition_list_destroy(parts);

	char opts_errstr[256];
	rd_kafka_AdminOptions_t* options = rd_kafka_AdminOptions_new(rd, RD_KAFKA_ADMIN_OP_ALTERCONSUMERGROUPOFFSETS);
	rd_kafka_AdminOptions_set_request_timeout(options, timeout_ms, opts_errstr, sizeof(opts_errstr));

	rd_kafka_queue_t* queue = rd_kafka_queue_new(rd);
	rd_kafka_AlterConsumerGroupOffsets(rd, &req, 1, options, queue);
	rd_kafka_AlterConsumerGroupOffsets_destroy(req);
	rd_kafka_AdminOptions_destroy(options);

	rd_kafka_event_t* event = rd_kafka_queue_poll(queue, timeout_ms + 5000);
	rd_kafka_queue_destroy(queue);

	if (!event) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "AlterConsumerGroupOffsets timed out");
		return 2;
	}

	int r = kafka_check_event_error(L, event);
	if (r)
		return r;

	const rd_kafka_AlterConsumerGroupOffsets_result_t* result = rd_kafka_event_AlterConsumerGroupOffsets_result(event);
	size_t gcnt;
	const rd_kafka_group_result_t** results = rd_kafka_AlterConsumerGroupOffsets_result_groups(result, &gcnt);

	if (gcnt > 0) {
		const rd_kafka_error_t* grp_err = rd_kafka_group_result_error(results[0]);
		if (grp_err) {
			const char* msg = rd_kafka_error_string(grp_err);
			lua_pushboolean(L, false);
			lua_pushstring(L, msg ? msg : "AlterConsumerGroupOffsets failed");
			rd_kafka_event_destroy(event);
			return 2;
		}
	}

	rd_kafka_event_destroy(event);
	lua_pushboolean(L, true);
	return 1;
}

// ---------------------------------------------------------------------------
// kafka_delete_group_offsets
// ---------------------------------------------------------------------------

int kafka_delete_group_offsets(lua_State* L, rd_kafka_t* rd) {
	const char* group_id = luaL_checkstring(L, 2);
	int timeout_ms = (int)luaL_optinteger(L, 4, 10000);

	rd_kafka_topic_partition_list_t* parts = kafka_build_tplist(L, 3);
	if (!parts) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "partitions array required for DeleteGroupOffsets");
		return 2;
	}

	rd_kafka_DeleteConsumerGroupOffsets_t* req = rd_kafka_DeleteConsumerGroupOffsets_new(group_id, parts);
	rd_kafka_topic_partition_list_destroy(parts);

	char opts_errstr[256];
	rd_kafka_AdminOptions_t* options = rd_kafka_AdminOptions_new(rd, RD_KAFKA_ADMIN_OP_DELETECONSUMERGROUPOFFSETS);
	rd_kafka_AdminOptions_set_request_timeout(options, timeout_ms, opts_errstr, sizeof(opts_errstr));

	rd_kafka_queue_t* queue = rd_kafka_queue_new(rd);
	rd_kafka_DeleteConsumerGroupOffsets(rd, &req, 1, options, queue);
	rd_kafka_DeleteConsumerGroupOffsets_destroy(req);
	rd_kafka_AdminOptions_destroy(options);

	rd_kafka_event_t* event = rd_kafka_queue_poll(queue, timeout_ms + 5000);
	rd_kafka_queue_destroy(queue);

	if (!event) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "DeleteConsumerGroupOffsets timed out");
		return 2;
	}

	int r = kafka_check_event_error(L, event);
	if (r)
		return r;

	const rd_kafka_DeleteConsumerGroupOffsets_result_t* result = rd_kafka_event_DeleteConsumerGroupOffsets_result(event);
	size_t gcnt;
	const rd_kafka_group_result_t** results = rd_kafka_DeleteConsumerGroupOffsets_result_groups(result, &gcnt);

	if (gcnt > 0) {
		const rd_kafka_error_t* grp_err = rd_kafka_group_result_error(results[0]);
		if (grp_err) {
			const char* msg = rd_kafka_error_string(grp_err);
			lua_pushboolean(L, false);
			lua_pushstring(L, msg ? msg : "DeleteConsumerGroupOffsets failed");
			rd_kafka_event_destroy(event);
			return 2;
		}
	}

	rd_kafka_event_destroy(event);
	lua_pushboolean(L, true);
	return 1;
}