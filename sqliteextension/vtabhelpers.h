#pragma once
#include "dllmain.h"

// Maximum number of fields a virtual table may declare.
#define VTAB_MAX_FIELDS 64
#define VTAB_MAX_FIELDS_STR "64"

// Maximum length (in bytes, excluding null terminator) of a single field name.
#define VTAB_MAX_FIELD_NAME_LEN 63
#define VTAB_MAX_FIELD_NAME_LEN_STR "63"

// Returns 1 if name is a valid SQLite identifier (non-empty, starts with a letter or
// underscore, remaining characters are letters, digits, or underscores); 0 otherwise.
int vtab_valid_field_name(const unsigned char* data, size_t len);

// Sets a KITSUNE_TERROR result via resultSetter and returns 1.
// Convenience wrapper for kitsune_CFunction error returns.
int vtab_cb_error(kitsune_ResultSetter resultSetter, const char* msg);

// Allocates and returns a "CREATE TABLE x(f0 PRIMARY KEY, f1, ...) WITHOUT ROWID;"
// DDL string suitable for sqlite3_declare_vtab.  The first field is always the
// PRIMARY KEY.  fieldCount must be <= VTAB_MAX_FIELDS and every name must satisfy
// vtab_valid_field_name.  Returns NULL on out-of-memory.  Caller must sqlite3_free.
char* vtab_build_ddl(char** fieldNames, int fieldCount);

// Writes a KitsuneVariable value as a SQLite result on the given context.
// Tables are serialized as JSON; NULL is returned for nil and unsupported types.
void vtab_push_kv_to_sqlite(sqlite3_context* ctx, const KitsuneVariable* v);
