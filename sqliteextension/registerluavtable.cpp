#include "registerluavtable.h"
#include "luafunctions.h"
#include "vtabhelpers.h"
#include <string.h>
#include <stdlib.h>
#include <float.h>
#include <stdio.h>
SQLITE_EXTENSION_INIT3
#include "kitsuneext.h"

// ---- structs -----------------------------------------------------------------

struct LuaVTabModule {
	KitsuneVariable* readFunc;    // anchored via lua_add_kitsune_state; NOT freed in destructor
	KitsuneVariable* indexFunc;   // anchored via lua_add_kitsune_state; NULL = full-scan only
	KitsuneVariable* updateFunc;  // anchored via lua_add_kitsune_state; NULL = read-only
	KitsuneVariable* contextVar;  // vtable-level shared Lua table; owned by mod, freed in destructor
	int fieldCount;
	char** fieldNames;            // sqlite3_malloc'd array of sqlite3_malloc'd strings
};

struct LuaVTabVTab {
	sqlite3_vtab base;   // must be first
	LuaVTabModule* mod;  // back-pointer; NOT owned by this struct
};

struct LuaVTabCursor {
	sqlite3_vtab_cursor base;
	KitsuneVariable* currentRow;  // KITSUNE_TTABLE result from last reader call; NULL = eof
	KitsuneVariable* indexData;   // anchored constraints table for index scan; NULL = full scan
	sqlite3_int64 rowid;          // 1-based nth counter; reset to 1 on each xFilter
	int eof;
};

// ---- module destructor — sole cleanup path for LuaVTabModule ----------------

static void lua_vtab_free_module(void* pAux) {
	LuaVTabModule* mod = (LuaVTabModule*)pAux;
	if (!mod)
		return;
	// readFunc/indexFunc/updateFunc are owned by g_extState, NOT freed here
	KitsuneVariableFree(mod->contextVar);
	if (mod->fieldNames) {
		for (int i = 0; i < mod->fieldCount; i++)
			sqlite3_free(mod->fieldNames[i]);
		sqlite3_free(mod->fieldNames);
	}
	sqlite3_free(mod);
}

// ---- op string helper -------------------------------------------------------

static const char* vtab_op_string(unsigned char op) {
	switch (op) {
	case SQLITE_INDEX_CONSTRAINT_EQ: return "=";
	case SQLITE_INDEX_CONSTRAINT_GT: return ">";
	case SQLITE_INDEX_CONSTRAINT_LT: return "<";
	case SQLITE_INDEX_CONSTRAINT_GE: return ">=";
	case SQLITE_INDEX_CONSTRAINT_LE: return "<=";
	default:                         return NULL;
	}
}

// ---- reader call helper (shared by xFilter and xNext) -----------------------
// Calls readFunc(ctx, nth, index) where index is nil for a full scan.
// Stores result in cursor->currentRow and sets cursor->eof on nil/error/non-table.

static void lua_vtab_call_reader(LuaVTabCursor* cursor, LuaVTabModule* mod) {
	KitsuneVariable args[3] = {};
	args[0] = *mod->contextVar;
	args[1].type = KITSUNE_TINTEGER;
	args[1].integer = cursor->rowid;
	if (cursor->indexData)
		args[2] = *cursor->indexData;
	else
		args[2].type = KITSUNE_TNIL;

	KitsuneVariable* result = KitsuneExecuteVariable(mod->readFunc, 3, args);
	if (!result || result->type == KITSUNE_TNIL || result->type == KITSUNE_TNONE ||
		result->type == KITSUNE_TERROR) {
		KitsuneVariableFree(result);
		cursor->currentRow = NULL;
		cursor->eof = 1;
	}
	else {
		cursor->currentRow = result;
	}
}

// ---- xConnect / xCreate — same function -------------------------------------

static int lua_vtab_connect(sqlite3* db, void* pAux, int argc, const char* const* argv,
	sqlite3_vtab** ppVtab, char** pzErr) {
	(void)argc; (void)argv; (void)pzErr;
	LuaVTabModule* mod = (LuaVTabModule*)pAux;

	LuaVTabVTab* vtab = (LuaVTabVTab*)sqlite3_malloc(sizeof(LuaVTabVTab));
	if (!vtab)
		return SQLITE_NOMEM;
	memset(vtab, 0, sizeof(LuaVTabVTab));
	vtab->mod = mod;

	char* ddl = vtab_build_ddl(mod->fieldNames, mod->fieldCount);
	if (!ddl) {
		sqlite3_free(vtab);
		return SQLITE_NOMEM;
	}

	int rc = sqlite3_declare_vtab(db, ddl);
	sqlite3_free(ddl);

	if (rc != SQLITE_OK) {
		sqlite3_free(vtab);
		return rc;
	}

	*ppVtab = (sqlite3_vtab*)vtab;
	return SQLITE_OK;
}

// ---- xBestIndex -------------------------------------------------------------
// For each usable constraint with a supported op, call indexFunc(ctx, op, colName).
// Constraints where indexFunc returns truthy are marked as handled; SQLite post-filters
// the rest. indexFunc is called once per constraint so partial acceptance is natural.

static int lua_vtab_best_index(sqlite3_vtab* pVtab, sqlite3_index_info* info) {
	LuaVTabVTab* vtab = (LuaVTabVTab*)pVtab;
	LuaVTabModule* mod = vtab->mod;

	if (!mod->indexFunc)
		return SQLITE_OK;

	// idxStr format: "colnum:op," per accepted constraint, null-terminated.
	// Worst case per entry: 2 digits + ':' + 2 op chars + ',' = 6 chars.
	// VTAB_MAX_FIELDS entries + null terminator.
	const int IDXSTR_CAP = VTAB_MAX_FIELDS * 7 + 1;
	char* idxStr = (char*)sqlite3_malloc(IDXSTR_CAP);
	if (!idxStr)
		return SQLITE_NOMEM;
	int idxStrPos = 0;
	idxStr[0] = '\0';

	int argvIdx = 1;
	bool anyUnique = false;
	double minCost = DBL_MAX;
	bool hasExplicitCost = false;

	for (int i = 0; i < info->nConstraint; i++) {
		if (!info->aConstraint[i].usable)
			continue;
		const char* opStr = vtab_op_string(info->aConstraint[i].op);
		if (!opStr)
			continue;
		int colIdx = info->aConstraint[i].iColumn; // 0-based
		if (colIdx < 0 || colIdx >= mod->fieldCount)
			continue;
		const char* colName = mod->fieldNames[colIdx];

		// Call indexFunc(ctx, op, colName) — once per constraint candidate.
		// Return value convention:
		//   nil / false / 0 / negative  ? decline this constraint
		//   true                        ? accept, unique (SQLITE_INDEX_SCAN_UNIQUE, estimatedRows=1)
		//   positive integer            ? accept, cost = value (KITSUNE_TINTEGER > 0)
		//   positive float              ? accept, cost = value (KITSUNE_TNUMBER > 0)
		KitsuneVariable args[3] = {};
		args[0] = *mod->contextVar;
		args[1].type = KITSUNE_TSTRING;
		args[1].data = (unsigned char*)opStr;
		args[1].length = strlen(opStr);
		args[2].type = KITSUNE_TSTRING;
		args[2].data = (unsigned char*)colName;
		args[2].length = strlen(colName);

		KitsuneVariable* result = KitsuneExecuteVariable(mod->indexFunc, 3, args);

		bool accepted = false;
		bool isUnique = false;
		double cost = 0.0;

		if (result) {
			if (result->type == KITSUNE_TBOOLEAN && result->boolean) {
				accepted = true;
				isUnique = true;
			}
			else if (result->type == KITSUNE_TINTEGER && result->integer > 0) {
				accepted = true;
				cost = (double)result->integer;
				hasExplicitCost = true;
			}
			else if (result->type == KITSUNE_TNUMBER && result->number > 0.0) {
				accepted = true;
				cost = result->number;
				hasExplicitCost = true;
			}
			// nil / false / 0 / negative ? decline (no action)
		}
		KitsuneVariableFree(result);

		if (!accepted)
			continue;

		if (isUnique)
			anyUnique = true;
		else if (cost < minCost)
			minCost = cost;

		info->aConstraintUsage[i].argvIndex = argvIdx++;

		// Encode as "colnum:op," (1-based column index) for xFilter to look up the field name.
		idxStrPos += snprintf(idxStr + idxStrPos, IDXSTR_CAP - idxStrPos, "%d:%s,", colIdx + 1, opStr);
	}

	if (argvIdx == 1) {
		// No constraints accepted — full scan.
		sqlite3_free(idxStr);
		return SQLITE_OK;
	}

	info->idxNum = 1;
	if (anyUnique) {
		info->idxFlags |= SQLITE_INDEX_SCAN_UNIQUE;
		info->estimatedRows = 1;
		info->estimatedCost = 1.0;
	}
	else {
		info->estimatedCost = hasExplicitCost ? minCost : 10.0;
	}
	info->idxStr = idxStr;
	info->needToFreeIdxStr = 1;

	return SQLITE_OK;
}

// ---- xDisconnect / xDestroy - same function ---------------------------------

static int lua_vtab_disconnect(sqlite3_vtab* pVtab) {
	sqlite3_free(pVtab); // free vtab shell only; mod is owned by the module destructor
	return SQLITE_OK;
}

// ---- xOpen -------------------------------------------------------------------

static int lua_vtab_open(sqlite3_vtab* pVtab, sqlite3_vtab_cursor** ppCursor) {
	(void)pVtab;
	LuaVTabCursor* cursor = (LuaVTabCursor*)sqlite3_malloc(sizeof(LuaVTabCursor));
	if (!cursor)
		return SQLITE_NOMEM;
	memset(cursor, 0, sizeof(LuaVTabCursor));
	*ppCursor = (sqlite3_vtab_cursor*)cursor;
	return SQLITE_OK;
}

// ---- xClose ------------------------------------------------------------------

static int lua_vtab_close(sqlite3_vtab_cursor* pCursor) {
	LuaVTabCursor* cursor = (LuaVTabCursor*)pCursor;
	KitsuneVariableFree(cursor->currentRow);
	KitsuneVariableFree(cursor->indexData);
	sqlite3_free(cursor);
	return SQLITE_OK;
}

// ---- xFilter -----------------------------------------------------------------
//
// idxNum == 0: full scan — reader called with (ctx, nth, nil).
// idxNum == 1: index scan — parse idxStr ("colnum:op," pairs) + argv values to
//              build the anchored index table {[1]={Column=name,Op=op,Value=v},...},
//              reader called with (ctx, nth, index).
//
// xFilter may be called multiple times on the same cursor (nested-loop join).
// The vtable-level context persists across all calls; nth resets to 1 each xFilter.

static int lua_vtab_filter(sqlite3_vtab_cursor* pCursor, int idxNum, const char* idxStr,
	int argc, sqlite3_value** argv) {
	LuaVTabCursor* cursor = (LuaVTabCursor*)pCursor;
	LuaVTabVTab* vtab = (LuaVTabVTab*)pCursor->pVtab;

	KitsuneVariableFree(cursor->currentRow);
	cursor->currentRow = NULL;
	KitsuneVariableFree(cursor->indexData);
	cursor->indexData = NULL;
	cursor->rowid = 1;
	cursor->eof = 0;

	if (idxNum == 1 && idxStr && argc > 0) {
		// Build nested KITSUNE_TTABLECONTENTS: each entry {column=N, op="=", value=v}.
		// Inner nodes borrow SQLite-owned memory for string values; KitsuneAnchorVariable
		// copies everything into the Lua heap before returning.
		KeyValuePairKitsuneVariableNode innerNodes[VTAB_MAX_FIELDS][3]; // column, op, value per entry
		KitsuneVariable innerVars[VTAB_MAX_FIELDS];
		KeyValuePairKitsuneVariableNode outerNodes[VTAB_MAX_FIELDS];
		char opBufs[VTAB_MAX_FIELDS][4]; // null-terminated op string per entry (max ">=" + null)
		memset(innerNodes, 0, sizeof(innerNodes));
		memset(innerVars, 0, sizeof(innerVars));
		memset(outerNodes, 0, sizeof(outerNodes));

		int count = 0;
		const char* p = idxStr;

		while (*p && count < argc && count < VTAB_MAX_FIELDS) {
			char* endp;
			int col = (int)strtol(p, &endp, 10);
			if (endp == p || *endp != ':')
				break;
			p = endp + 1;

			int opLen = 0;
			while (*p && *p != ',' && opLen < 3)
				opBufs[count][opLen++] = *p++;
			opBufs[count][opLen] = '\0';
			if (*p == ',')
				p++;

			KitsuneVariable val = {};
			sqlite_val_to_kitsune(argv[count], &val);

			// node 0: Column (field name string)
			innerNodes[count][0].key.type = KITSUNE_TSTRING;
			innerNodes[count][0].key.data = (unsigned char*)"Column";
			innerNodes[count][0].key.length = 6;
			innerNodes[count][0].value.type = KITSUNE_TSTRING;
			innerNodes[count][0].value.data = (unsigned char*)vtab->mod->fieldNames[col - 1];
			innerNodes[count][0].value.length = strlen(vtab->mod->fieldNames[col - 1]);
			innerNodes[count][0].next = &innerNodes[count][1];

			// node 1: Op
			innerNodes[count][1].key.type = KITSUNE_TSTRING;
			innerNodes[count][1].key.data = (unsigned char*)"Op";
			innerNodes[count][1].key.length = 2;
			innerNodes[count][1].value.type = KITSUNE_TSTRING;
			innerNodes[count][1].value.data = (unsigned char*)opBufs[count];
			innerNodes[count][1].value.length = (size_t)opLen;
			innerNodes[count][1].next = &innerNodes[count][2];

			// node 2: Value
			innerNodes[count][2].key.type = KITSUNE_TSTRING;
			innerNodes[count][2].key.data = (unsigned char*)"Value";
			innerNodes[count][2].key.length = 5;
			innerNodes[count][2].value = val;
			innerNodes[count][2].next = NULL;

			innerVars[count].type = KITSUNE_TTABLECONTENTS;
			innerVars[count].table = &innerNodes[count][0];

			outerNodes[count].key.type = KITSUNE_TINTEGER;
			outerNodes[count].key.integer = count + 1;
			outerNodes[count].value = innerVars[count];
			count++;
		}

		for (int i = 0; i < count; i++)
			outerNodes[i].next = (i + 1 < count) ? &outerNodes[i + 1] : NULL;

		KitsuneVariable outerContents = {};
		outerContents.type = KITSUNE_TTABLECONTENTS;
		outerContents.table = (count > 0) ? &outerNodes[0] : NULL;

		cursor->indexData = KitsuneAnchorVariable(&outerContents);
		if (!cursor->indexData)
			return SQLITE_NOMEM;
	}

	lua_vtab_call_reader(cursor, vtab->mod);
	return SQLITE_OK;
}

// ---- xNext -------------------------------------------------------------------

static int lua_vtab_next(sqlite3_vtab_cursor* pCursor) {
	LuaVTabCursor* cursor = (LuaVTabCursor*)pCursor;
	LuaVTabVTab* vtab = (LuaVTabVTab*)pCursor->pVtab;
	KitsuneVariableFree(cursor->currentRow);
	cursor->currentRow = NULL;
	cursor->rowid++;
	lua_vtab_call_reader(cursor, vtab->mod);
	return SQLITE_OK;
}

// ---- xEof --------------------------------------------------------------------

static int lua_vtab_eof(sqlite3_vtab_cursor* pCursor) {
	return ((LuaVTabCursor*)pCursor)->eof;
}

// ---- xColumn -----------------------------------------------------------------
// currentRow is a KITSUNE_TTABLE ref (result of KitsuneExecuteVariable).
// Use KitsuneGetIndex with a 1-based integer key to retrieve each column.

static int lua_vtab_column(sqlite3_vtab_cursor* pCursor, sqlite3_context* ctx, int N) {
	LuaVTabCursor* cursor = (LuaVTabCursor*)pCursor;
	if (!cursor->currentRow) {
		sqlite3_result_null(ctx);
		return SQLITE_OK;
	}
	KitsuneVariable intKey = {};
	intKey.type = KITSUNE_TINTEGER;
	intKey.integer = N + 1; // Lua 1-based
	KitsuneVariable* fieldVal = KitsuneGetIndex(cursor->currentRow, &intKey);
	vtab_push_kv_to_sqlite(ctx, fieldVal);
	KitsuneVariableFree(fieldVal);
	return SQLITE_OK;
}

// ---- xRowid - no-op for WITHOUT ROWID tables --------------------------------

static int lua_vtab_rowid(sqlite3_vtab_cursor* pCursor, sqlite_int64* pRowid) {
	(void)pCursor;
	*pRowid = 0;
	return SQLITE_OK;
}

// ---- xUpdate -----------------------------------------------------------------
//
// WITHOUT ROWID argv layout (single-column PRIMARY KEY):
//
//   DELETE  argc == 1
//     argv[0]  old PK value
//
//   INSERT  argc == fieldCount + 2,  argv[0] is SQL NULL
//     argv[0]  NULL
//     argv[1]  NULL (no rowid for WITHOUT ROWID)
//     argv[2..argc-1]  all column values (col0 = PK first)
//
//   UPDATE  argc == fieldCount + 2,  argv[0] is NOT NULL
//     argv[0]  old PK
//     argv[1]  new PK
//     argv[2..argc-1]  new column values
//
// Maps to Lua call: insertupdatedelete(ctx, pk, data)
//   DELETE:  ctx = vtable ctx, pk = argv[0], data = nil
//   INSERT:  ctx = vtable ctx, pk = nil,     data = {argv[2], argv[3], ...}
//   UPDATE:  ctx = vtable ctx, pk = argv[0], data = {argv[2], argv[3], ...}

static int lua_vtab_update(sqlite3_vtab* pVtab, int argc, sqlite3_value** argv,
	sqlite3_int64* pRowid) {
	(void)pRowid;
	LuaVTabVTab* vtab = (LuaVTabVTab*)pVtab;
	LuaVTabModule* mod = vtab->mod;

	if (!mod->updateFunc) {
		pVtab->zErrMsg = sqlite3_mprintf("Readonly");
		return SQLITE_READONLY;
	}

	KitsuneVariable nil = {};
	nil.type = KITSUNE_TNIL;

	KitsuneVariable args[3];
	args[0] = *mod->contextVar;

	if (argc == 1) {
		// DELETE
		sqlite_val_to_kitsune(argv[0], &args[1]);
		args[2] = nil;
	}
	else {
		// INSERT or UPDATE — build data table from argv[2..argc-1] on the stack.
		int dataCount = argc - 2;
		KeyValuePairKitsuneVariableNode dataNodes[VTAB_MAX_FIELDS];
		memset(dataNodes, 0, sizeof(dataNodes));
		int nodeCount = dataCount < VTAB_MAX_FIELDS ? dataCount : VTAB_MAX_FIELDS;
		for (int i = 0; i < nodeCount; i++) {
			dataNodes[i].key.type = KITSUNE_TINTEGER;
			dataNodes[i].key.integer = i + 1;
			sqlite_val_to_kitsune(argv[2 + i], &dataNodes[i].value);
			dataNodes[i].next = (i + 1 < nodeCount) ? &dataNodes[i + 1] : NULL;
		}
		KitsuneVariable dataContents = {};
		dataContents.type = KITSUNE_TTABLECONTENTS;
		dataContents.table = (nodeCount > 0) ? &dataNodes[0] : NULL;

		if (sqlite3_value_type(argv[0]) == SQLITE_NULL)
			args[1] = nil; // INSERT: pk = nil
		else
			sqlite_val_to_kitsune(argv[0], &args[1]); // UPDATE: pk = old PK

		args[2] = dataContents;
	}

	KitsuneVariable* result = KitsuneExecuteVariable(mod->updateFunc, 3, args);
	if (result && result->type == KITSUNE_TERROR) {
		if (result->data && result->length > 0)
			pVtab->zErrMsg = sqlite3_mprintf("%.*s", (int)result->length, (const char*)result->data);
		else
			pVtab->zErrMsg = sqlite3_mprintf("update error");
		KitsuneVariableFree(result);
		return SQLITE_ERROR;
	}
	KitsuneVariableFree(result);
	return SQLITE_OK;
}

// ---- static module ----------------------------------------------------------

static sqlite3_module g_luaVTabModule = {
	0,                        // iVersion
	lua_vtab_connect,         // xCreate
	lua_vtab_connect,         // xConnect
	lua_vtab_best_index,      // xBestIndex
	lua_vtab_disconnect,      // xDisconnect
	lua_vtab_disconnect,      // xDestroy
	lua_vtab_open,            // xOpen
	lua_vtab_close,           // xClose
	lua_vtab_filter,          // xFilter
	lua_vtab_next,            // xNext
	lua_vtab_eof,             // xEof
	lua_vtab_column,          // xColumn
	lua_vtab_rowid,           // xRowid
	lua_vtab_update,          // xUpdate
	NULL, NULL, NULL, NULL,   // xBegin, xSync, xCommit, xRollback
	NULL,                     // xFindFunction
	NULL,                     // xRename
	NULL, NULL, NULL,         // xSavepoint, xRelease, xRollbackTo
	NULL                      // xShadowName
};

// ---- register_virtual_table_cb ----------------------------------------------

int register_virtual_table_cb(int argc, const KitsuneVariable* argv,
	kitsune_ResultSetter resultSetter, void* userdata) {

	sqlite3* db = ((KitsuneExtState*)userdata)->db;

	if (argc < 3 ||
		argv[0].type != KITSUNE_TSTRING || !argv[0].data ||
		argv[1].type != KITSUNE_TTABLE ||
		argv[2].type != KITSUNE_TFUNCTION)
		return vtab_cb_error(resultSetter,
			"SQLiteExt.RegisterVirtualTable(name, fields, reader[, indexfn][, updater]): invalid arguments");

	const char* name = (const char*)argv[0].data;

	// Get field count.
	KitsuneVariable* lenVar = KitsuneGetLength(&argv[1]);
	if (!lenVar || lenVar->type != KITSUNE_TINTEGER || lenVar->integer < 2) {
		KitsuneVariableFree(lenVar);
		return vtab_cb_error(resultSetter, "RegisterVirtualTable: fields array must contain at least 2 entries");
	}
	if (lenVar->integer > VTAB_MAX_FIELDS) {
		KitsuneVariableFree(lenVar);
		return vtab_cb_error(resultSetter, "RegisterVirtualTable: too many fields (max " VTAB_MAX_FIELDS_STR ")");
	}
	int N = (int)lenVar->integer;
	KitsuneVariableFree(lenVar);

	// Allocate module up front so error paths can call lua_vtab_free_module.
	LuaVTabModule* mod = (LuaVTabModule*)sqlite3_malloc(sizeof(LuaVTabModule));
	if (!mod)
		return vtab_cb_error(resultSetter, "RegisterVirtualTable: out of memory");
	memset(mod, 0, sizeof(LuaVTabModule));
	mod->fieldCount = N;

	mod->fieldNames = (char**)sqlite3_malloc(sizeof(char*) * N);
	if (!mod->fieldNames) {
		sqlite3_free(mod);
		return vtab_cb_error(resultSetter, "RegisterVirtualTable: out of memory");
	}
	memset(mod->fieldNames, 0, sizeof(char*) * N);

	// Fetch and validate each field name (letters only, matching RegisterTable).
	for (int i = 0; i < N; i++) {
		KitsuneVariable intKey = {};
		intKey.type = KITSUNE_TINTEGER;
		intKey.integer = i + 1;
		KitsuneVariable* fieldVar = KitsuneGetIndex(&argv[1], &intKey);

		if (!fieldVar || fieldVar->type != KITSUNE_TSTRING || !fieldVar->data) {
			KitsuneVariableFree(fieldVar);
			lua_vtab_free_module(mod);
			return vtab_cb_error(resultSetter, "RegisterVirtualTable: field names must be strings");
		}

		if (!vtab_valid_field_name(fieldVar->data, fieldVar->length)) {
			KitsuneVariableFree(fieldVar);
			lua_vtab_free_module(mod);
			return vtab_cb_error(resultSetter, "RegisterVirtualTable: field names must start with a letter or underscore and contain only letters, digits, or underscores (max " VTAB_MAX_FIELD_NAME_LEN_STR " chars)");
		}
		size_t len = fieldVar->length;

		char* copy = (char*)sqlite3_malloc((int)len + 1);
		if (!copy) {
			KitsuneVariableFree(fieldVar);
			lua_vtab_free_module(mod);
			return vtab_cb_error(resultSetter, "RegisterVirtualTable: out of memory");
		}
		memcpy(copy, fieldVar->data, len);
		copy[len] = '\0';
		mod->fieldNames[i] = copy;
		KitsuneVariableFree(fieldVar);
	}

	// Anchor required reader function.
	mod->readFunc = lua_add_kitsune_state(&argv[2]);
	if (!mod->readFunc) {
		lua_vtab_free_module(mod);
		return vtab_cb_error(resultSetter, "RegisterVirtualTable: failed to anchor reader function");
	}

	// Optional index function (argv[3]).
	if (argc > 3 && argv[3].type == KITSUNE_TFUNCTION) {
		mod->indexFunc = lua_add_kitsune_state(&argv[3]);
		if (!mod->indexFunc) {
			lua_vtab_free_module(mod);
			return vtab_cb_error(resultSetter, "RegisterVirtualTable: failed to anchor index function");
		}
	}

	// Optional update function (argv[4]).
	if (argc > 4 && argv[4].type == KITSUNE_TFUNCTION) {
		mod->updateFunc = lua_add_kitsune_state(&argv[4]);
		if (!mod->updateFunc) {
			lua_vtab_free_module(mod);
			return vtab_cb_error(resultSetter, "RegisterVirtualTable: failed to anchor update function");
		}
	}

	// Drop any existing vtab first so xConnect is always called with the fresh mod.
	// xDestroy only frees the vtab shell, so this is safe while the old module is live.
	char* dropSql = sqlite3_mprintf("DROP TABLE IF EXISTS \"%w\"", name);
	if (dropSql) {
		sqlite3_exec(db, dropSql, NULL, NULL, NULL);
		sqlite3_free(dropSql);
	}

	// Create the vtable-level shared context (passed to reader, indexFunc, updateFunc).
	KitsuneVariable cv = {};
	cv.type = KITSUNE_TTABLECONTENTS;
	mod->contextVar = KitsuneAnchorVariable(&cv);
	if (!mod->contextVar) {
		lua_vtab_free_module(mod);
		return vtab_cb_error(resultSetter, "RegisterVirtualTable: out of memory");
	}

	// Register the module (destructor takes ownership of mod).
	sqlite3_create_module_v2(db, name, &g_luaVTabModule, mod, lua_vtab_free_module);

	// Create the virtual table (triggers xConnect with the fresh mod).
	char* createSql = sqlite3_mprintf("CREATE VIRTUAL TABLE \"%w\" USING \"%w\"", name, name);
	if (createSql) {
		sqlite3_exec(db, createSql, NULL, NULL, NULL);
		sqlite3_free(createSql);
	}

	return 1; // success; RegisterVirtualTable returns nothing to Lua
}
