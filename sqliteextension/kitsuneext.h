#pragma once
// Internal SQLite<->Kitsune conversion helpers shared between sqlitefunctions.cpp and luafunctions.cpp.
// Include AFTER SQLITE_EXTENSION_INIT1/3 in each TU so that sqlite3_api is in scope.
#include "dllmain.h"

// Process-wide extension state: allocated in DLL_PROCESS_ATTACH, freed in DLL_PROCESS_DETACH.
// db is set on the first load_extension call and never overwritten — g_extState is shared
// across all connections so re-registering on subsequent loads would rebind SQLiteExt.*
// to a different handle on every call.
struct RegisteredFunc; // defined in luafunctions.cpp
struct KitsuneExtState {
	sqlite3* db;
	RegisteredFunc* funcs;
};

// Converts one SQLite column value to a KitsuneVariable.
// Data pointers borrow directly from SQLite-owned memory; valid for the duration of the call only.
static inline void sqlite_val_to_kitsune(sqlite3_value* val, KitsuneVariable* kv) {
	kv->length = 0;
	int type = sqlite3_value_type(val);
	switch (type) {
	case SQLITE_INTEGER:
		kv->type = KITSUNE_TINTEGER;
		kv->integer = sqlite3_value_int64(val);
		break;
	case SQLITE_FLOAT:
		kv->type = KITSUNE_TNUMBER;
		kv->number = sqlite3_value_double(val);
		break;
	case SQLITE_TEXT:
		kv->type = KITSUNE_TSTRING;
		kv->data = (unsigned char*)sqlite3_value_text(val);
		kv->length = (size_t)sqlite3_value_bytes(val);
		break;
	case SQLITE_BLOB:
		kv->type = KITSUNE_TSTRING;
		kv->data = (unsigned char*)sqlite3_value_blob(val);
		kv->length = (size_t)sqlite3_value_bytes(val);
		break;
	default:
		kv->type = KITSUNE_TNIL;
		kv->data = NULL;
		break;
	}
}

// Allocates and fills a KitsuneVariable array from argv[offset..argc-1].
// Returns NULL with *outCount == 0 when there are no args (not an error).
// Returns NULL with *outCount == -1 on OOM; the SQLite error is already set — caller must return.
// On success returns the array; caller must sqlite3_free it.
static inline KitsuneVariable* sqlite_build_args(sqlite3_context* context, int argc, sqlite3_value** argv,
	int offset, int* outCount) {
	*outCount = argc - offset;
	if (*outCount <= 0) {
		*outCount = 0;
		return NULL;
	}
	KitsuneVariable* args = (KitsuneVariable*)sqlite3_malloc((int)(sizeof(KitsuneVariable) * (*outCount)));
	if (!args) {
		sqlite3_result_error_nomem(context);
		*outCount = -1;
		return NULL;
	}
	for (int i = 0; i < *outCount; i++)
		sqlite_val_to_kitsune(argv[offset + i], &args[i]);
	return args;
}

// Forwards a KitsuneVariable result to the SQLite context and frees the variable.
// Handles NULL (engine not initialised / no slots), KITSUNE_TERROR, and all value types.
static inline void kitsune_result_to_sqlite(sqlite3_context* context, KitsuneVariable* result) {
	if (!result) {
		sqlite3_result_error(context, "execution failed", -1);
		return;
	}
	if (result->type == KITSUNE_TERROR) {
		if (result->data && result->length > 0)
			sqlite3_result_error(context, (const char*)result->data, (int)result->length);
		else
			sqlite3_result_error(context, "Lua error", -1);
		KitsuneVariableFree(result);
		return;
	}
	switch (result->type) {
	case KITSUNE_TINTEGER:
		sqlite3_result_int64(context, result->integer);
		break;
	case KITSUNE_TNUMBER:
		sqlite3_result_double(context, result->number);
		break;
	case KITSUNE_TSTRING:
		sqlite3_result_text(context, (const char*)result->data, (int)result->length, SQLITE_TRANSIENT);
		break;
	case KITSUNE_TBOOLEAN:
		sqlite3_result_int(context, result->boolean ? 1 : 0);
		break;
	default:
		sqlite3_result_null(context);
		break;
	}
	KitsuneVariableFree(result);
}
