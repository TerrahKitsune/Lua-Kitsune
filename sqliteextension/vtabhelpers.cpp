#include "vtabhelpers.h"
#include <string.h>
#include <ctype.h>
#include <assert.h>
SQLITE_EXTENSION_INIT3
#include "kitsuneext.h"

int vtab_cb_error(kitsune_ResultSetter resultSetter, const char* msg) {
	KitsuneVariable err = {};
	err.type = KITSUNE_TERROR;
	err.data = (unsigned char*)msg;
	err.length = strlen(msg);
	resultSetter(&err);
	return 1;
}

int vtab_valid_field_name(const unsigned char* data, size_t len) {
	if (!data || len == 0 || len > VTAB_MAX_FIELD_NAME_LEN)
		return 0;
	if (!isalpha((unsigned char)data[0]) && data[0] != '_')
		return 0;
	for (size_t i = 1; i < len; i++) {
		if (!isalnum((unsigned char)data[i]) && data[i] != '_')
			return 0;
	}
	return 1;
}

char* vtab_build_ddl(char** fieldNames, int fieldCount) {
	// Compute exact required size:
	//   "CREATE TABLE x(" = 16
	//   per field: name + optional " PRIMARY KEY" (12) + optional "," = name_len + 13
	//   ") WITHOUT ROWID;" = 17
	//   null terminator = 1
	size_t needed = 16 + 17 + 1;
	for (int i = 0; i < fieldCount; i++)
		needed += strlen(fieldNames[i]) + 13; // worst case: name + " PRIMARY KEY" + ","

	char* ddl = (char*)sqlite3_malloc((int)needed);
	if (!ddl)
		return NULL;

	size_t pos = 0;

#define DDL_APPEND(s, slen) do { \
		assert(pos + (slen) < needed); \
		memcpy(ddl + pos, (s), (slen)); \
		pos += (slen); \
	} while(0)

	DDL_APPEND("CREATE TABLE x(", 15);
	for (int i = 0; i < fieldCount; i++) {
		size_t nlen = strlen(fieldNames[i]);
		if (i > 0)
			DDL_APPEND(",", 1);
		DDL_APPEND(fieldNames[i], nlen);
		if (i == 0)
			DDL_APPEND(" PRIMARY KEY", 12);
	}
	DDL_APPEND(") WITHOUT ROWID;", 16);
	ddl[pos] = '\0';

#undef DDL_APPEND

	return ddl;
}

void vtab_push_kv_to_sqlite(sqlite3_context* ctx, const KitsuneVariable* v) {
	if (!v) {
		sqlite3_result_null(ctx);
		return;
	}
	switch (v->type) {
	case KITSUNE_TINTEGER:
		sqlite3_result_int64(ctx, v->integer);
		break;
	case KITSUNE_TNUMBER:
		sqlite3_result_double(ctx, v->number);
		break;
	case KITSUNE_TSTRING:
		sqlite3_result_text(ctx, (const char*)v->data, (int)v->length, SQLITE_TRANSIENT);
		break;
	case KITSUNE_TBOOLEAN:
		sqlite3_result_int(ctx, v->boolean ? 1 : 0);
		break;
	case KITSUNE_TTABLE: {
		KitsuneVariable* json = KitsuneGetTableContentsAsJson(v);
		if (json && json->type == KITSUNE_TJSON && json->data)
			sqlite3_result_text(ctx, (const char*)json->data, (int)json->length, SQLITE_TRANSIENT);
		else
			sqlite3_result_null(ctx);
		KitsuneVariableFree(json);
		break;
	}
	default:
		sqlite3_result_null(ctx);
		break;
	}
}
