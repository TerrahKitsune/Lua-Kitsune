#include "luafunctions.h"
#include "registerluatable.h"
#include "registerluavtable.h"
#include <stdlib.h>
#include <string.h>
SQLITE_EXTENSION_INIT3
#include "kitsuneext.h"

// Entry in the list of Lua functions registered as SQLite scalars.
// funcVar is freed via KitsuneVariableFree in lua_cleanup_kitsune_state,
// which handles luaL_unref and the free of the struct itself.
struct RegisteredFunc {
	KitsuneVariable* funcVar;
	RegisteredFunc* next;
};

static KitsuneExtState* g_extState = NULL;

void lua_init_kitsune_state() {
	g_extState = (KitsuneExtState*)malloc(sizeof(KitsuneExtState));
	if (g_extState) {
		g_extState->db = NULL;
		g_extState->funcs = NULL;
	}
}

// Anchors a Lua function variable in the registry and adds it to the extension state.
// Returns a kitsune_malloc'd KitsuneVariable* with its own independent registry ref.
// The caller must NOT free it — it is owned by g_extState and freed by
// lua_cleanup_kitsune_state via KitsuneVariableFree. Returns NULL on failure.
KitsuneVariable* lua_add_kitsune_state(const KitsuneVariable* var) {
	if (!g_extState || !var) return NULL;

	KitsuneVariable* funcVar = KitsuneAnchorVariable(var);
	if (!funcVar) return NULL;

	RegisteredFunc* entry = (RegisteredFunc*)malloc(sizeof(RegisteredFunc));
	if (!entry) {
		KitsuneVariableFree(funcVar);
		return NULL;
	}

	entry->funcVar = funcVar;
	entry->next = g_extState->funcs;
	g_extState->funcs = entry;
	return funcVar;
}

void lua_cleanup_kitsune_state() {
	if (!g_extState) return;
	RegisteredFunc* entry = g_extState->funcs;
	while (entry) {
		RegisteredFunc* next = entry->next;
		KitsuneVariableFree(entry->funcVar);
		free(entry);
		entry = next;
	}
	free(g_extState);
	g_extState = NULL;
}

// Raises a KITSUNE_TERROR through resultSetter. Always returns 1 so callers can tail-return.
static int reg_error(kitsune_ResultSetter resultSetter, const char* msg) {
	KitsuneVariable err = {};
	err.type = KITSUNE_TERROR;
	err.data = (unsigned char*)msg;
	err.length = strlen(msg);
	resultSetter(&err);
	return 1;
}

// -- Static helpers for SQLiteExt.Query --------------------------------------

// Bind a KitsuneVariable value to a prepared statement parameter by index.
static void bind_kv_to_stmt(sqlite3_stmt* stmt, int idx, const KitsuneVariable* v) {
	if (!v) { sqlite3_bind_null(stmt, idx); return; }
	switch (v->type) {
	case KITSUNE_TINTEGER:
		sqlite3_bind_int64(stmt, idx, v->integer);
		break;
	case KITSUNE_TNUMBER:
		sqlite3_bind_double(stmt, idx, v->number);
		break;
	case KITSUNE_TSTRING:
		sqlite3_bind_text(stmt, idx, (const char*)v->data, (int)v->length, SQLITE_STATIC);
		break;
	case KITSUNE_TBOOLEAN:
		sqlite3_bind_int(stmt, idx, v->boolean ? 1 : 0);
		break;
	default:
		sqlite3_bind_null(stmt, idx);
		break;
	}
}

// Fill a KitsuneVariable from a SQLite result column.
// String and blob data is sqlite3_malloc'd; must be freed with free_sqlite_kv_tree.
static void fill_kv_from_col(sqlite3_stmt* stmt, int col, KitsuneVariable* out) {
	memset(out, 0, sizeof(KitsuneVariable));
	switch (sqlite3_column_type(stmt, col)) {
	case SQLITE_INTEGER:
		out->type = KITSUNE_TINTEGER;
		out->integer = sqlite3_column_int64(stmt, col);
		break;
	case SQLITE_FLOAT:
		out->type = KITSUNE_TNUMBER;
		out->number = sqlite3_column_double(stmt, col);
		break;
	case SQLITE_TEXT: {
		const char* text = (const char*)sqlite3_column_text(stmt, col);
		int         bytes = sqlite3_column_bytes(stmt, col);
		out->type = KITSUNE_TSTRING;
		out->data = (unsigned char*)sqlite3_malloc(bytes + 1);
		if (out->data) { memcpy(out->data, text, bytes + 1); out->length = (size_t)bytes; }
		break;
	}
	case SQLITE_BLOB: {
		const void* blob = sqlite3_column_blob(stmt, col);
		int         bytes = sqlite3_column_bytes(stmt, col);
		out->type = KITSUNE_TSTRING;
		out->data = (unsigned char*)sqlite3_malloc(bytes);
		if (out->data) { memcpy(out->data, blob, bytes); out->length = (size_t)bytes; }
		break;
	}
	default:
		out->type = KITSUNE_TNIL;
		break;
	}
}

// Recursively free a KitsuneVariable tree whose string data and nodes were sqlite3_malloc'd.
static void free_sqlite_kv_tree(KitsuneVariable* v) {
	if (!v) return;
	if (v->type == KITSUNE_TSTRING && v->data) {
		sqlite3_free(v->data);
		v->data = NULL;
	}
	else if (v->type == KITSUNE_TTABLECONTENTS && v->table) {
		KitsuneKeyValuePairVariableNode* node = v->table;
		while (node) {
			KitsuneKeyValuePairVariableNode* next = node->next;
			free_sqlite_kv_tree(&node->key);
			free_sqlite_kv_tree(&node->value);
			sqlite3_free(node);
			node = next;
		}
		v->table = NULL;
	}
}

// Execute a bound SQLite statement and collect all rows into a KITSUNE_TTABLE.
// Result layout: { [1] = { col_name = value, ... }, [2] = { ... }, ... }
// Returns SQLITE_DONE on success; any other code indicates a step error.
// All allocations inside *out_result use sqlite3_malloc; call free_sqlite_kv_tree to release.
static int execute_query_into_kv(sqlite3_stmt* stmt, KitsuneVariable* out_result) {
	memset(out_result, 0, sizeof(KitsuneVariable));
	out_result->type = KITSUNE_TTABLECONTENTS;
	KitsuneKeyValuePairVariableNode** result_tail = &out_result->table;
	int col_count = sqlite3_column_count(stmt);
	int row_num = 0;
	int step_rc;

	while ((step_rc = sqlite3_step(stmt)) == SQLITE_ROW) {
		row_num++;

		KitsuneVariable row = {};
		row.type = KITSUNE_TTABLECONTENTS;
		KitsuneKeyValuePairVariableNode** row_tail = &row.table;

		for (int i = 0; i < col_count; i++) {
			KitsuneKeyValuePairVariableNode* cell =
				(KitsuneKeyValuePairVariableNode*)sqlite3_malloc(sizeof(KitsuneKeyValuePairVariableNode));
			if (!cell) continue;
			memset(cell, 0, sizeof(KitsuneKeyValuePairVariableNode));

			const char* colname = sqlite3_column_name(stmt, i);
			if (!colname) {
				sqlite3_free(cell);
				continue;
			}
			size_t nlen = strlen(colname);
			cell->key.type = KITSUNE_TSTRING;
			cell->key.data = (unsigned char*)sqlite3_malloc((int)nlen + 1);
			if (cell->key.data) { memcpy(cell->key.data, colname, nlen + 1); cell->key.length = nlen; }

			fill_kv_from_col(stmt, i, &cell->value);
			*row_tail = cell;
			row_tail = &cell->next;
		}

		KitsuneKeyValuePairVariableNode* row_node =
			(KitsuneKeyValuePairVariableNode*)sqlite3_malloc(sizeof(KitsuneKeyValuePairVariableNode));
		if (row_node) {
			memset(row_node, 0, sizeof(KitsuneKeyValuePairVariableNode));
			row_node->key.type = KITSUNE_TINTEGER;
			row_node->key.integer = row_num;
			row_node->value = row;
			*result_tail = row_node;
			result_tail = &row_node->next;
		}
		else {
			free_sqlite_kv_tree(&row);
		}
	}
	return step_rc;
}

// Bind @paramName parameters from a KitsuneVariable table to a prepared statement.
static void bind_params_from_table(sqlite3_stmt* stmt, KitsuneKeyValuePairVariableNode* params) {
	for (KitsuneKeyValuePairVariableNode* node = params; node; node = node->next) {
		if (node->key.type != KITSUNE_TSTRING || !node->key.data) continue;
		char* pname = (char*)sqlite3_malloc((int)node->key.length + 2);
		if (!pname) continue;
		pname[0] = '@';
		memcpy(pname + 1, node->key.data, node->key.length);
		pname[node->key.length + 1] = '\0';
		int idx = sqlite3_bind_parameter_index(stmt, pname);
		sqlite3_free(pname);
		if (idx > 0) bind_kv_to_stmt(stmt, idx, &node->value);
	}
}

// SQLiteExt.Query(sql[, params]) — executes a SQL query and returns all rows as a table.
// params: optional {paramName = value} table for @paramName placeholders.
// Returns: { [1] = { col = val, ... }, [2] = { ... }, ... }
static int query_cb(int argc, const KitsuneVariable* argv, kitsune_ResultSetter resultSetter, void* userdata) {
	sqlite3* db = ((KitsuneExtState*)userdata)->db;

	if (argc < 1 || argv[0].type != KITSUNE_TSTRING || !argv[0].data)
		return reg_error(resultSetter, "SQLiteExt.Query(sql[, params]): sql must be a string");

	sqlite3_stmt* stmt = NULL;
	if (sqlite3_prepare_v2(db, (const char*)argv[0].data, -1, &stmt, NULL) != SQLITE_OK)
		return reg_error(resultSetter, sqlite3_errmsg(db));

	if (argc >= 2 && argv[1].type == KITSUNE_TTABLE) {
		KitsuneVariable* contents = KitsuneGetTableContents(&argv[1]);
		if (contents) {
			if (contents->type == KITSUNE_TTABLECONTENTS)
				bind_params_from_table(stmt, contents->table);
			KitsuneVariableFree(contents);
		}
	}

	KitsuneVariable result = {};
	int step_rc = execute_query_into_kv(stmt, &result);
	sqlite3_finalize(stmt);

	if (step_rc != SQLITE_DONE) {
		free_sqlite_kv_tree(&result);
		return reg_error(resultSetter, sqlite3_errmsg(db));
	}

	resultSetter(&result);
	free_sqlite_kv_tree(&result);
	return 1;
}

// SQLiteExt.Scalar(sql[, params]) — executes a SQL query and returns the first column of the first row.
// Returns nil if the query produces no rows. params follow the same @paramName convention as Query.
static int scalar_cb(int argc, const KitsuneVariable* argv, kitsune_ResultSetter resultSetter, void* userdata) {
	sqlite3* db = ((KitsuneExtState*)userdata)->db;

	if (argc < 1 || argv[0].type != KITSUNE_TSTRING || !argv[0].data)
		return reg_error(resultSetter, "SQLiteExt.Scalar(sql[, params]): sql must be a string");

	sqlite3_stmt* stmt = NULL;
	if (sqlite3_prepare_v2(db, (const char*)argv[0].data, -1, &stmt, NULL) != SQLITE_OK)
		return reg_error(resultSetter, sqlite3_errmsg(db));

	if (argc >= 2 && argv[1].type == KITSUNE_TTABLE) {
		KitsuneVariable* contents = KitsuneGetTableContents(&argv[1]);
		if (contents) {
			if (contents->type == KITSUNE_TTABLECONTENTS)
				bind_params_from_table(stmt, contents->table);
			KitsuneVariableFree(contents);
		}
	}

	KitsuneVariable result = {};
	int step_rc = sqlite3_step(stmt);

	if (step_rc == SQLITE_ROW && sqlite3_column_count(stmt) > 0) {
		fill_kv_from_col(stmt, 0, &result);
	}
	else if (step_rc != SQLITE_DONE && step_rc != SQLITE_ROW) {
		sqlite3_finalize(stmt);
		return reg_error(resultSetter, sqlite3_errmsg(db));
	}

	sqlite3_finalize(stmt);
	resultSetter(&result);
	free_sqlite_kv_tree(&result); // frees sqlite3_malloc'd string data if result is a string
	return 1;
}

// SQLite scalar callback for a function registered via RegisterFunction(name, fn).
// pApp (sqlite3_user_data) is the KitsuneVariable* holding the Lua function reference.
static void lua_registered_func_callback(sqlite3_context* context, int argc, sqlite3_value** argv) {
	KitsuneVariable* funcVar = (KitsuneVariable*)sqlite3_user_data(context);
	int luaArgc = 0;
	KitsuneVariable* args = sqlite_build_args(context, argc, argv, 0, &luaArgc);
	if (luaArgc < 0) return;
	KitsuneVariable* result = KitsuneExecuteVariable(funcVar, luaArgc, args);
	if (args) sqlite3_free(args);
	kitsune_result_to_sqlite(context, result);
}

// -- RegisterAggregate --------------------------------------------------------

// SQLite aggregate xStep: called once per row.
// Calls funcVar with (ctx, false, col1, col2, ...) as direct parameters.
// ctx is a per-aggregation-group Lua table stored via sqlite3_aggregate_context.
// String data in args borrows SQLite-owned memory; valid for the duration of this call.
static void lua_aggregate_step(sqlite3_context* context, int argc, sqlite3_value** argv) {
	KitsuneVariable* funcVar = (KitsuneVariable*)sqlite3_user_data(context);

	// sqlite3_aggregate_context returns the same zeroed block for every xStep/xFinal
	// call belonging to the same aggregation group.  We store a KitsuneVariable* in it.
	KitsuneVariable** pCtx = (KitsuneVariable**)sqlite3_aggregate_context(context, sizeof(KitsuneVariable*));
	if (!pCtx) {
		sqlite3_result_error_nomem(context);
		return;
	}
	if (!*pCtx) {
		// First call for this group: create a fresh empty Lua table as the context.
		KitsuneVariable emptyTable = {};
		emptyTable.type = KITSUNE_TTABLECONTENTS;
		*pCtx = KitsuneAnchorVariable(&emptyTable);
		if (!*pCtx) {
			sqlite3_result_error_nomem(context);
			return;
		}
	}

	// args: ctx, false, col1, col2, ...
	int luaArgc = argc + 2;
	KitsuneVariable* args = (KitsuneVariable*)sqlite3_malloc(sizeof(KitsuneVariable) * luaArgc);
	if (!args) {
		sqlite3_result_error_nomem(context);
		return;
	}

	args[0] = **pCtx;

	memset(&args[1], 0, sizeof(KitsuneVariable));
	args[1].type = KITSUNE_TBOOLEAN;
	args[1].boolean = false;

	for (int i = 0; i < argc; i++)
		sqlite_val_to_kitsune(argv[i], &args[i + 2]);

	KitsuneVariable* result = KitsuneExecuteVariable(funcVar, luaArgc, args);
	sqlite3_free(args);

	if (result) {
		if (result->type == KITSUNE_TERROR) {
			if (result->data && result->length > 0)
				sqlite3_result_error(context, (const char*)result->data, (int)result->length);
			else
				sqlite3_result_error(context, "aggregate step error", -1);
		}
		KitsuneVariableFree(result);
	}
}

// SQLite aggregate xFinal: called once at end to collect the result.
// Calls funcVar with (ctx, true) as the sole parameters, then frees the ctx table.
static void lua_aggregate_final(sqlite3_context* context) {
	KitsuneVariable* funcVar = (KitsuneVariable*)sqlite3_user_data(context);

	// Retrieve the per-group context (may be NULL if no rows were processed).
	KitsuneVariable** pCtx = (KitsuneVariable**)sqlite3_aggregate_context(context, sizeof(KitsuneVariable*));

	KitsuneVariable args[2] = {};
	int luaArgc;

	if (pCtx && *pCtx) {
		args[0] = **pCtx;
		args[1].type = KITSUNE_TBOOLEAN;
		args[1].boolean = true;
		luaArgc = 2;
	}
	else {
		// No rows were stepped (empty group): pass nil ctx, true.
		args[0].type = KITSUNE_TNIL;
		args[1].type = KITSUNE_TBOOLEAN;
		args[1].boolean = true;
		luaArgc = 2;
	}

	KitsuneVariable* result = KitsuneExecuteVariable(funcVar, luaArgc, args);

	if (pCtx && *pCtx) {
		KitsuneVariableFree(*pCtx);
		*pCtx = NULL;
	}

	kitsune_result_to_sqlite(context, result);
}

// Kitsune callback for RegisterAggregate(name, fn).
// argv[0] = SQL function name (KITSUNE_TSTRING)
// argv[1] = Lua aggregate function (KITSUNE_TFUNCTION)
// userdata = KitsuneExtState*
static int register_aggregate_cb(int argc, const KitsuneVariable* argv, kitsune_ResultSetter resultSetter, void* userdata) {
	if (argc < 2 || argv[0].type != KITSUNE_TSTRING || argv[1].type != KITSUNE_TFUNCTION || !g_extState)
		return reg_error(resultSetter, "RegisterAggregate(name, function): invalid arguments");

	KitsuneVariable* funcVar = lua_add_kitsune_state(&argv[1]);
	if (!funcVar)
		return reg_error(resultSetter, "RegisterAggregate: failed to anchor function");

	sqlite3_create_function(((KitsuneExtState*)userdata)->db, (const char*)argv[0].data, -1,
		SQLITE_UTF8, funcVar, NULL, lua_aggregate_step, lua_aggregate_final);

	return 1; // success; RegisterAggregate returns nothing to Lua
}

// Kitsune callback for RegisterFunction(name, fn).
// argv[0] = SQL function name (KITSUNE_TSTRING)
// argv[1] = Lua function to register (KITSUNE_TFUNCTION)
// userdata = sqlite3* db
static int register_function_cb(int argc, const KitsuneVariable* argv, kitsune_ResultSetter resultSetter, void* userdata) {
	if (argc < 2 || argv[0].type != KITSUNE_TSTRING || argv[1].type != KITSUNE_TFUNCTION || !g_extState)
		return reg_error(resultSetter, "RegisterFunction(name, function): invalid arguments");

	// Register the function on the Lua environment and anchor it in g_extState.
	KitsuneVariable* funcVar = lua_add_kitsune_state(&argv[1]);
	if (!funcVar)
		return reg_error(resultSetter, "RegisterFunction: failed to anchor function");

	sqlite3_create_function(((KitsuneExtState*)userdata)->db, (const char*)argv[0].data, -1,
		SQLITE_UTF8, funcVar, lua_registered_func_callback, NULL, NULL);

	return 1; // success; RegisterFunction returns nothing to Lua
}

int lua_register_kitsune_functions(sqlite3* db, char** pzErrMsg) {
	if (!g_extState) return SQLITE_OK;
	// Set g_extState->db only on the first load_extension call so that all
	// connections use the same persistent db handle for SQLiteExt.* and RegisterTable.
	if (!g_extState->db)
		g_extState->db = db;
	// Always (re-)register Lua globals: KitsuneEngine creates a fresh Lua state
	// each time it is re-initialised, so globals must be populated on every load.
	KitsuneRegisterFunction("SQLiteExt.RegisterFunction", register_function_cb, g_extState);
	KitsuneRegisterFunction("SQLiteExt.RegisterAggregate", register_aggregate_cb, g_extState);
	KitsuneRegisterFunction("SQLiteExt.Query", query_cb, g_extState);
	KitsuneRegisterFunction("SQLiteExt.Scalar", scalar_cb, g_extState);
	KitsuneRegisterFunction("SQLiteExt.RegisterTable", register_table_cb, g_extState);
	KitsuneRegisterFunction("SQLiteExt.RegisterVirtualTable", register_virtual_table_cb, g_extState);
	return SQLITE_OK;
}
