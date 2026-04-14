#include "registerluatable.h"
#include <string.h>
#include <ctype.h>
SQLITE_EXTENSION_INIT3
#include "kitsuneext.h"

// ---- structs -----------------------------------------------------------------

struct LuaTableModule {
	KitsuneVariable* tableVar; // KitsuneAnchorVariable result; freed only in lua_table_free_module
	int fieldCount;
	char** fieldNames;         // sqlite3_malloc'd array of sqlite3_malloc+memcpy'd strings
};

struct LuaTableVTab {
	sqlite3_vtab base;         // must be first
	LuaTableModule* mod;       // back-pointer; NOT owned by this struct
};

struct LuaTableCursor {
	sqlite3_vtab_cursor base;
	KitsuneVariable* entry;    // full-scan: current KitsuneNext result; NULL before first call
	KitsuneVariable* pkAnchor; // PK-lookup: anchored constraint value; NULL in full-scan mode
	int isPkLookup;
	sqlite3_int64 rowid;       // sequential counter for non-integer PKs (full scan only)
	int eof;
};

// ---- module destructor — sole cleanup path for LuaTableModule ---------------

static void lua_table_free_module(void* pAux) {
	LuaTableModule* mod = (LuaTableModule*)pAux;
	if (!mod)
		return;
	KitsuneVariableFree(mod->tableVar);
	if (mod->fieldNames) {
		for (int i = 0; i < mod->fieldCount; i++)
			sqlite3_free(mod->fieldNames[i]);
		sqlite3_free(mod->fieldNames);
	}
	sqlite3_free(mod);
}

// ---- push helper -------------------------------------------------------------

static void push_kv_to_sqlite(sqlite3_context* ctx, const KitsuneVariable* v) {
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

// ---- xConnect / xCreate — same function -------------------------------------

static int lua_table_connect(sqlite3* db, void* pAux, int argc, const char* const* argv,
	sqlite3_vtab** ppVtab, char** pzErr) {
	(void)argc; (void)argv; (void)pzErr;
	LuaTableModule* mod = (LuaTableModule*)pAux;

	LuaTableVTab* vtab = (LuaTableVTab*)sqlite3_malloc(sizeof(LuaTableVTab));
	if (!vtab)
		return SQLITE_NOMEM;
	memset(vtab, 0, sizeof(LuaTableVTab));
	vtab->mod = mod;

	// Build DDL: "CREATE TABLE x(col1 PRIMARY KEY, col2, ...) WITHOUT ROWID;"
	// With max 64 fields of max ~64 bytes each, 8 KB is more than enough.
	char* ddl = (char*)sqlite3_malloc(8192);
	if (!ddl) {
		sqlite3_free(vtab);
		return SQLITE_NOMEM;
	}

	strcpy(ddl, "CREATE TABLE x(");
	for (int i = 0; i < mod->fieldCount; i++) {
		if (i > 0)
			strcat(ddl, ",");
		strcat(ddl, mod->fieldNames[i]);
		if (i == 0)
			strcat(ddl, " PRIMARY KEY");
	}
	strcat(ddl, ") WITHOUT ROWID;");

	int rc = sqlite3_declare_vtab(db, ddl);
	sqlite3_free(ddl);

	if (rc != SQLITE_OK) {
		sqlite3_free(vtab);
		return rc;
	}

	*ppVtab = (sqlite3_vtab*)vtab;
	return SQLITE_OK;
}

// ---- xBestIndex --------------------------------------------------------------

static int lua_table_best_index(sqlite3_vtab* pVtab, sqlite3_index_info* info) {
	(void)pVtab;
	for (int i = 0; i < info->nConstraint; i++) {
		if (info->aConstraint[i].iColumn == 0 &&
			info->aConstraint[i].usable &&
			info->aConstraint[i].op == SQLITE_INDEX_CONSTRAINT_EQ) {
			info->idxNum = 1;
			info->estimatedCost = 10;
			info->idxFlags = SQLITE_INDEX_SCAN_UNIQUE;
			info->aConstraintUsage[i].argvIndex = 1;
			return SQLITE_OK;
		}
	}
	return SQLITE_OK;
}

// ---- xDisconnect / xDestroy — same function ---------------------------------

static int lua_table_disconnect(sqlite3_vtab* pVtab) {
	sqlite3_free(pVtab); // free vtab shell only; mod is owned by the module destructor
	return SQLITE_OK;
}

// ---- xOpen -------------------------------------------------------------------

static int lua_table_open(sqlite3_vtab* pVtab, sqlite3_vtab_cursor** ppCursor) {
	(void)pVtab;
	LuaTableCursor* cursor = (LuaTableCursor*)sqlite3_malloc(sizeof(LuaTableCursor));
	if (!cursor)
		return SQLITE_NOMEM;
	memset(cursor, 0, sizeof(LuaTableCursor));
	*ppCursor = (sqlite3_vtab_cursor*)cursor;
	return SQLITE_OK;
}

// ---- xClose ------------------------------------------------------------------

static int lua_table_close(sqlite3_vtab_cursor* pCursor) {
	LuaTableCursor* cursor = (LuaTableCursor*)pCursor;
	KitsuneVariableFree(cursor->entry);
	KitsuneVariableFree(cursor->pkAnchor);
	sqlite3_free(cursor);
	return SQLITE_OK;
}

// ---- xFilter -----------------------------------------------------------------

static int lua_table_filter(sqlite3_vtab_cursor* pCursor, int idxNum, const char* idxStr,
	int argc, sqlite3_value** argv) {
	(void)idxStr;
	LuaTableCursor* cursor = (LuaTableCursor*)pCursor;
	LuaTableVTab* vtab = (LuaTableVTab*)pCursor->pVtab;

	KitsuneVariableFree(cursor->entry);
	cursor->entry = NULL;
	KitsuneVariableFree(cursor->pkAnchor);
	cursor->pkAnchor = NULL;
	cursor->eof = 0;
	cursor->rowid = 0;

	if (idxNum == 1 && argc == 1) {
		// PK equality lookup path
		KitsuneVariable tmp = {};
		sqlite_val_to_kitsune(argv[0], &tmp);
		cursor->pkAnchor = KitsuneAnchorVariable(&tmp);
		if (!cursor->pkAnchor)
			return SQLITE_NOMEM;
		cursor->isPkLookup = 1;

		KitsuneVariable* probe = KitsuneGetIndex(vtab->mod->tableVar, cursor->pkAnchor);
		if (!probe || probe->type == KITSUNE_TNIL || probe->type == KITSUNE_TERROR)
			cursor->eof = 1;
		KitsuneVariableFree(probe);
	}
	else {
		// Full scan path
		cursor->isPkLookup = 0;
		cursor->entry = KitsuneNext(vtab->mod->tableVar, NULL);
		if (!cursor->entry || cursor->entry->type != KITSUNE_TTABLECONTENTS)
			cursor->eof = 1;
	}

	return SQLITE_OK;
}

// ---- xNext -------------------------------------------------------------------

static int lua_table_next(sqlite3_vtab_cursor* pCursor) {
	LuaTableCursor* cursor = (LuaTableCursor*)pCursor;
	LuaTableVTab* vtab = (LuaTableVTab*)pCursor->pVtab;

	if (cursor->isPkLookup) {
		cursor->eof = 1;
		return SQLITE_OK;
	}

	// entry ownership transfers into KitsuneNext; do not access it after this call
	cursor->entry = KitsuneNext(vtab->mod->tableVar, cursor->entry);
	if (!cursor->entry || cursor->entry->type != KITSUNE_TTABLECONTENTS)
		cursor->eof = 1;
	cursor->rowid++;
	return SQLITE_OK;
}

// ---- xEof --------------------------------------------------------------------

static int lua_table_eof(sqlite3_vtab_cursor* pCursor) {
	return ((LuaTableCursor*)pCursor)->eof;
}

// ---- xColumn -----------------------------------------------------------------

static int lua_table_column(sqlite3_vtab_cursor* pCursor, sqlite3_context* ctx, int N) {
	LuaTableCursor* cursor = (LuaTableCursor*)pCursor;
	LuaTableVTab* vtab = (LuaTableVTab*)pCursor->pVtab;
	int fieldCount = vtab->mod->fieldCount;

	if (cursor->isPkLookup) {
		if (N == 0) {
			push_kv_to_sqlite(ctx, cursor->pkAnchor);
			return SQLITE_OK;
		}

		KitsuneVariable* rowVal = KitsuneGetIndex(vtab->mod->tableVar, cursor->pkAnchor);
		if (!rowVal || rowVal->type == KITSUNE_TERROR) {
			sqlite3_result_null(ctx);
			KitsuneVariableFree(rowVal);
			return SQLITE_OK;
		}
		if (fieldCount == 2) {
			push_kv_to_sqlite(ctx, rowVal);
			KitsuneVariableFree(rowVal);
			return SQLITE_OK;
		}
		// fieldCount > 2
		if (rowVal->type != KITSUNE_TTABLE) {
			if (N == 1)
				push_kv_to_sqlite(ctx, rowVal);
			else
				sqlite3_result_null(ctx);
			KitsuneVariableFree(rowVal);
			return SQLITE_OK;
		}
		KitsuneVariable intKey = {};
		intKey.type = KITSUNE_TINTEGER;
		intKey.integer = N;
		KitsuneVariable* fieldVal = KitsuneGetIndex(rowVal, &intKey);
		if (!fieldVal) {
			sqlite3_result_null(ctx);
			KitsuneVariableFree(rowVal);
			return SQLITE_OK;
		}
		push_kv_to_sqlite(ctx, fieldVal);
		KitsuneVariableFree(fieldVal);
		KitsuneVariableFree(rowVal);
		return SQLITE_OK;
	}

	// Full scan path
	KeyValuePairKitsuneVariableNode* node = cursor->entry->table;
	if (N == 0) {
		push_kv_to_sqlite(ctx, &node->key);
		return SQLITE_OK;
	}
	if (fieldCount == 2) {
		push_kv_to_sqlite(ctx, &node->value);
		return SQLITE_OK;
	}
	// fieldCount > 2
	if (node->value.type != KITSUNE_TTABLE) {
		if (N == 1)
			push_kv_to_sqlite(ctx, &node->value);
		else
			sqlite3_result_null(ctx);
		return SQLITE_OK;
	}
	KitsuneVariable intKey = {};
	intKey.type = KITSUNE_TINTEGER;
	intKey.integer = N;
	KitsuneVariable* fieldVal = KitsuneGetIndex(&node->value, &intKey);
	if (!fieldVal) {
		sqlite3_result_null(ctx);
		return SQLITE_OK;
	}
	push_kv_to_sqlite(ctx, fieldVal);
	KitsuneVariableFree(fieldVal);
	return SQLITE_OK;
}

// ---- xRowid — no-op for WITHOUT ROWID tables --------------------------------

static int lua_table_rowid(sqlite3_vtab_cursor* pCursor, sqlite_int64* pRowid) {
	(void)pCursor;
	*pRowid = 0;
	return SQLITE_OK;
}

// ---- pks_equal — compare two sqlite3_value PK values for equality -----------

static int pks_equal(sqlite3_value* a, sqlite3_value* b) {
	int ta = sqlite3_value_type(a);
	if (ta != sqlite3_value_type(b))
		return 0;
	switch (ta) {
	case SQLITE_INTEGER:
		return sqlite3_value_int64(a) == sqlite3_value_int64(b);
	case SQLITE_FLOAT:
		return sqlite3_value_double(a) == sqlite3_value_double(b);
	default: {
		int n = sqlite3_value_bytes(a);
		return n == sqlite3_value_bytes(b) &&
			memcmp(sqlite3_value_blob(a), sqlite3_value_blob(b), n) == 0;
	}
	}
}

// ---- set_row_value — write tableVar[pkVar] = row value from argv ------------
// fieldCount == 2: scalar argv[3]; fieldCount > 2: new Lua sub-table.
// String data in nodes[].value borrows SQLite-owned memory valid for xUpdate;
// KitsuneAnchorVariable copies it into the Lua heap before returning.

static int set_row_value(sqlite3_vtab* pVtab, LuaTableModule* mod,
	const KitsuneVariable* pkVar, int argc, sqlite3_value** argv) {
	if (mod->fieldCount == 2) {
		KitsuneVariable val = {};
		sqlite_val_to_kitsune(argv[3], &val);
		if (!KitsuneSetIndex(mod->tableVar, pkVar, &val)) {
			pVtab->zErrMsg = sqlite3_mprintf("write failed");
			return SQLITE_ERROR;
		}
		return SQLITE_OK;
	}
	// fieldCount > 2: build a new Lua sub-table from the non-PK column values.
	int valueCount = argc - 3; // = fieldCount - 1
	KeyValuePairKitsuneVariableNode nodes[63];
	memset(nodes, 0, sizeof(nodes));
	for (int i = 0; i < valueCount; i++) {
		nodes[i].key.type = KITSUNE_TINTEGER;
		nodes[i].key.integer = i + 1;
		sqlite_val_to_kitsune(argv[3 + i], &nodes[i].value);
		nodes[i].next = (i + 1 < valueCount) ? &nodes[i + 1] : NULL;
	}
	KitsuneVariable contents = {};
	contents.type = KITSUNE_TTABLECONTENTS;
	contents.table = nodes; // KitsuneAnchorVariable with TTABLECONTENTS creates a new Lua table
	KitsuneVariable* rowTable = KitsuneAnchorVariable(&contents);
	if (!rowTable) {
		pVtab->zErrMsg = sqlite3_mprintf("out of memory");
		return SQLITE_NOMEM;
	}
	int ok = KitsuneSetIndex(mod->tableVar, pkVar, rowTable);
	KitsuneVariableFree(rowTable);
	if (!ok) {
		pVtab->zErrMsg = sqlite3_mprintf("write failed");
		return SQLITE_ERROR;
	}
	return SQLITE_OK;
}

// ---- xUpdate -----------------------------------------------------------------
//
// SQLite calls xUpdate for all three write operations using this argv layout
// for WITHOUT ROWID virtual tables (declared with a single-column PRIMARY KEY):
//
//   DELETE  argc == 1
//     argv[0]          old PK value of the row to delete
//
//   INSERT  argc == fieldCount + 2,  argv[0] is SQL NULL
//     argv[0]          NULL  (no old row)
//     argv[1]          NULL  (no synthetic rowid; WITHOUT ROWID has none)
//     argv[2]          col0  — PRIMARY KEY column value  ← use this as the key
//     argv[3..argc-1]  col1, col2, …  (non-PK column values)
//
//   UPDATE  argc == fieldCount + 2,  argv[0] is NOT NULL
//     argv[0]          old PK  (= old "rowid" for WITHOUT ROWID)
//     argv[1]          new PK  (= new "rowid" for WITHOUT ROWID; equals argv[0]
//                              when the PK is unchanged, differs on PK rename)
//     argv[2]          col0  — new PRIMARY KEY column value  (equals argv[1])
//     argv[3..argc-1]  col1, col2, …  (new non-PK column values)
//
// Non-PK column values always live at argv[3..argc-1] (= fieldCount - 1 values)
// regardless of operation.

static int lua_table_update(sqlite3_vtab* pVtab, int argc, sqlite3_value** argv, sqlite3_int64* pRowid) {

	(void)pRowid; // always NULL for WITHOUT ROWID tables
	LuaTableVTab* vtab = (LuaTableVTab*)pVtab;
	LuaTableModule* mod = vtab->mod;

	KitsuneVariable nil = {};
	nil.type = KITSUNE_TNIL;

	// ---- DELETE (argc == 1) --------------------------------------------------
	// argv[0] = PK of the row to delete.
	// Setting tableVar[oldPK] = nil removes the key from the Lua table.
	// Deleting a key that does not exist is a no-op in Lua (returns true).
	if (argc == 1) {
		KitsuneVariable oldPK = {};
		sqlite_val_to_kitsune(argv[0], &oldPK);
		if (!KitsuneSetIndex(mod->tableVar, &oldPK, &nil)) {
			pVtab->zErrMsg = sqlite3_mprintf("delete failed");
			return SQLITE_ERROR;
		}
		return SQLITE_OK;
	}

	// argc guard: every INSERT/UPDATE must supply exactly fieldCount + 2 values.
	if (argc != mod->fieldCount + 2)
		return SQLITE_ERROR;

	// ---- INSERT (argv[0] is NULL) --------------------------------------------
	// For WITHOUT ROWID, argv[1] is always NULL (SQLite has no rowid to assign).
	// The actual PK value comes from argv[2] (col0), not argv[1].
	// Non-PK column values follow at argv[3..argc-1].
	if (sqlite3_value_type(argv[0]) == SQLITE_NULL) {
		KitsuneVariable newPK = {};
		sqlite_val_to_kitsune(argv[2], &newPK); // col0 = PK for WITHOUT ROWID

		// Duplicate check: the key must not already exist in the Lua table.
		KitsuneVariable* existing = KitsuneGetIndex(mod->tableVar, &newPK);
		if (!existing) {
			pVtab->zErrMsg = sqlite3_mprintf("insert check failed");
			return SQLITE_ERROR;
		}
		if (existing->type == KITSUNE_TERROR) {
			if (existing->data && existing->length > 0)
				pVtab->zErrMsg = sqlite3_mprintf("%.*s", (int)existing->length, (const char*)existing->data);
			KitsuneVariableFree(existing);
			return SQLITE_ERROR;
		}
		if (existing->type != KITSUNE_TNIL) {
			pVtab->zErrMsg = sqlite3_mprintf("Duplicate key: %s", sqlite3_value_text(argv[2]));
			KitsuneVariableFree(existing);
			return SQLITE_CONSTRAINT;
		}
		KitsuneVariableFree(existing);

		// Write tableVar[newPK] = scalar (fieldCount==2) or new sub-table (fieldCount>2).
		return set_row_value(pVtab, mod, &newPK, argc, argv);
	}
	else {
		// ---- UPDATE (argv[0] is NOT NULL) ------------------------------------
		// argv[0] = old PK, argv[1] = new PK.
		// argv[0] == argv[1]: value-only update (PK unchanged).
		// argv[0] != argv[1]: PK rename — write new key, then delete old key.
		// Safe order: set new key first so the old row survives any write failure.
		KitsuneVariable oldPK = {};
		sqlite_val_to_kitsune(argv[0], &oldPK);
		KitsuneVariable newPK = {};
		sqlite_val_to_kitsune(argv[1], &newPK); // new "rowid" = new PK for WITHOUT ROWID

		// Existence check: the old key must be present (defensive guard; SQLite
		// normally only calls xUpdate for rows that were found by xFilter).
		KitsuneVariable* existing = KitsuneGetIndex(mod->tableVar, &oldPK);
		if (!existing) {
			pVtab->zErrMsg = sqlite3_mprintf("update check failed");
			return SQLITE_ERROR;
		}
		if (existing->type == KITSUNE_TERROR) {
			if (existing->data && existing->length > 0)
				pVtab->zErrMsg = sqlite3_mprintf("%.*s", (int)existing->length, (const char*)existing->data);
			KitsuneVariableFree(existing);
			return SQLITE_ERROR;
		}
		if (existing->type == KITSUNE_TNIL) {
			pVtab->zErrMsg = sqlite3_mprintf("Update key not found: %s", sqlite3_value_text(argv[0]));
			KitsuneVariableFree(existing);
			return SQLITE_ERROR;
		}
		KitsuneVariableFree(existing);

		// Write new value at newPK first — old row stays intact if this fails.
		int rc = set_row_value(pVtab, mod, &newPK, argc, argv);
		if (rc != SQLITE_OK)
			return rc;

		// PK rename: delete the old key now that the new one is committed.
		if (!pks_equal(argv[0], argv[1]))
			KitsuneSetIndex(mod->tableVar, &oldPK, &nil);

		return SQLITE_OK;
	}
}

// ---- static module ----------------------------------------------------------

static sqlite3_module g_luaTableModule = {
	0,                       // iVersion
	lua_table_connect,       // xCreate
	lua_table_connect,       // xConnect
	lua_table_best_index,    // xBestIndex
	lua_table_disconnect,    // xDisconnect
	lua_table_disconnect,    // xDestroy
	lua_table_open,          // xOpen
	lua_table_close,         // xClose
	lua_table_filter,        // xFilter
	lua_table_next,          // xNext
	lua_table_eof,           // xEof
	lua_table_column,        // xColumn
	lua_table_rowid,         // xRowid
	lua_table_update,        // xUpdate
	NULL, NULL, NULL, NULL,  // xBegin, xSync, xCommit, xRollback
	NULL,                    // xFindFunction
	NULL,                    // xRename
	NULL, NULL, NULL,        // xSavepoint, xRelease, xRollbackTo
	NULL                     // xShadowName
};

// ---- local error helper ------------------------------------------------------

static int rt_error(kitsune_ResultSetter resultSetter, const char* msg) {
	KitsuneVariable err = {};
	err.type = KITSUNE_TERROR;
	err.data = (unsigned char*)msg;
	err.length = strlen(msg);
	resultSetter(&err);
	return 1;
}

// ---- register_table_cb -------------------------------------------------------

int register_table_cb(int argc, const KitsuneVariable* argv, kitsune_ResultSetter resultSetter, void* userdata) {

	sqlite3* db = ((KitsuneExtState*)userdata)->db;

	if (argc < 3 ||
		argv[0].type != KITSUNE_TSTRING || !argv[0].data ||
		argv[1].type != KITSUNE_TTABLE ||
		argv[2].type != KITSUNE_TTABLE)
		return rt_error(resultSetter, "SQLiteExt.RegisterTable(name, fields, table): invalid arguments");

	const char* name = (const char*)argv[0].data;

	// Get field count. fields is always a programmer-supplied Lua array {"f1","f2",...}
	// so KitsuneGetLength reliably returns its sequence length.
	KitsuneVariable* lenVar = KitsuneGetLength(&argv[1]);
	if (!lenVar || lenVar->type != KITSUNE_TINTEGER || lenVar->integer < 2) {
		KitsuneVariableFree(lenVar);
		return rt_error(resultSetter, "RegisterTable: fields array must contain at least 2 entries");
	}
	int N = (int)lenVar->integer;
	KitsuneVariableFree(lenVar);

	// Allocate the module up front so error paths can use lua_table_free_module.
	LuaTableModule* mod = (LuaTableModule*)sqlite3_malloc(sizeof(LuaTableModule));
	if (!mod)
		return rt_error(resultSetter, "RegisterTable: out of memory");
	memset(mod, 0, sizeof(LuaTableModule));
	mod->fieldCount = N;

	mod->fieldNames = (char**)sqlite3_malloc(sizeof(char*) * N);
	if (!mod->fieldNames) {
		sqlite3_free(mod);
		return rt_error(resultSetter, "RegisterTable: out of memory");
	}
	memset(mod->fieldNames, 0, sizeof(char*) * N);

	// Fetch and validate each field name by explicit integer index 1..N.
	for (int i = 0; i < N; i++) {
		KitsuneVariable intKey = {};
		intKey.type = KITSUNE_TINTEGER;
		intKey.integer = i + 1;
		KitsuneVariable* fieldVar = KitsuneGetIndex(&argv[1], &intKey);

		if (!fieldVar || fieldVar->type != KITSUNE_TSTRING || !fieldVar->data) {
			KitsuneVariableFree(fieldVar);
			lua_table_free_module(mod);
			return rt_error(resultSetter, "RegisterTable: field names must be strings");
		}

		size_t len = fieldVar->length;
		const unsigned char* data = fieldVar->data;
		for (size_t c = 0; c < len; c++) {
			if (!isalpha((unsigned char)data[c])) {
				KitsuneVariableFree(fieldVar);
				lua_table_free_module(mod);
				return rt_error(resultSetter, "RegisterTable: field names may only contain letters");
			}
		}

		char* copy = (char*)sqlite3_malloc((int)len + 1);
		if (!copy) {
			KitsuneVariableFree(fieldVar);
			lua_table_free_module(mod);
			return rt_error(resultSetter, "RegisterTable: out of memory");
		}
		memcpy(copy, data, len);
		copy[len] = '\0';
		mod->fieldNames[i] = copy;
		KitsuneVariableFree(fieldVar);
	}

	// Anchor the data table.
	mod->tableVar = KitsuneAnchorVariable(&argv[2]);
	if (!mod->tableVar) {
		lua_table_free_module(mod);
		return rt_error(resultSetter, "RegisterTable: failed to anchor table");
	}

	// Drop any existing vtab first. xDestroy only frees the vtab shell (lua_table_disconnect
	// never touches mod), so this is safe even if the old module is about to be replaced.
	// This ensures the subsequent CREATE always runs xConnect with the fresh mod — without
	// the DROP, "IF NOT EXISTS" would leave the vtab shell pointing at a freed old mod.
	char* dropSql = sqlite3_mprintf("DROP TABLE IF EXISTS \"%w\"", name);
	if (dropSql) {
		sqlite3_exec(db, dropSql, NULL, NULL, NULL);
		sqlite3_free(dropSql);
	}

	// Register the module (destructor takes ownership of mod).
	sqlite3_create_module_v2(db, name, &g_luaTableModule, mod, lua_table_free_module);

	// Create the virtual table fresh (always runs xConnect with the new mod).
	char* createSql = sqlite3_mprintf("CREATE VIRTUAL TABLE \"%w\" USING \"%w\"", name, name);
	if (createSql) {
		sqlite3_exec(db, createSql, NULL, NULL, NULL);
		sqlite3_free(createSql);
	}

	return 1; // success; SQLiteExt.RegisterTable returns nothing to Lua
}