#pragma once
#include "../SQLite/sqlite3ext.h"
#include "../lua_main_incl.h"

SQLITE_EXTENSION_INIT3

#define EL_RESULT_NONE 0
#define EL_RESULT_SIMPLE 1
#define EL_RESULT_ARRAY 2
#define EL_RESULT_TABLE 3
#define EL_RESULT_FUNC 4
#define EL_RESULT_THREAD 5

#define RES_TYPE_VTABLE 1
#define RES_TYPE_TABLE 2
#define RES_TYPE_FUNCTION 3
#define RES_TYPE_AGGREGATE_FUNCTION 4
#define RES_TYPE_WINDOW 5

typedef struct {
	int type;
	char* name;
	void* ptr;
} ResRegistration;

typedef struct {
	lua_State* L;
	sqlite3* db;
	size_t numbRegistrations;
	ResRegistration* Registrations;
} ResState;

ResRegistration* GetRegistration(ResState* state, const char* name);
ResRegistration* AddRegistration(ResState* state, const char* name, int type);

void lua_pushsqlite3value(lua_State* L, sqlite3_value* value);
void lua_tosqlite3value(lua_State* L, int idx, sqlite3_context* context);

int sqlite3_createfunction(lua_State* L, ResState* state);
int sqlite3_registertable(lua_State* L, ResState* state);