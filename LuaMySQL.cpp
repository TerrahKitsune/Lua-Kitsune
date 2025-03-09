#include "LuaMySQL.h"
#include "stream.h"
#include "luawchar.h"
#pragma comment(lib, "mysql/libmysql.lib")


LuaMySQL* lua_tomysql(lua_State* L, int index) {

	LuaMySQL* luamysql = (LuaMySQL*)lua_touserdata(L, index);
	if (luamysql == NULL) {
		luaL_error(L, "parameter is not a %s", LUAMYSQL);
		return NULL;
	}
	return luamysql;
}

LuaMySQL* lua_pushmysql(lua_State* L) {

	LuaMySQL* luamysql = (LuaMySQL*)lua_newuserdata(L, sizeof(LuaMySQL));
	if (luamysql == NULL) {
		luaL_error(L, "Unable to create mysql connection");
		return NULL;
	}

	luaL_getmetatable(L, LUAMYSQL);
	lua_setmetatable(L, -2);
	memset(luamysql, 0, sizeof(LuaMySQL));
	luamysql->thread = INVALID_HANDLE_VALUE;
	luamysql->interrupt = INVALID_HANDLE_VALUE;

	return luamysql;
}

int EscapeValue(lua_State* L) {
	size_t len;
	const char* data;

	if (lua_isstring(L, -1)) {
		data = lua_tolstring(L, -1, &len);
	}
	else if (lua_isstream(L, -1)) {
		LuaStream* stream = lua_toluastream(L, -1);
		if (stream) {
			len = stream->len;
			data = (const char*)stream->data;
		}
		else {
			len = 0;
			data = NULL;
		}
	}
	else if (lua_iswchar(L, -1)) {
		ToUtf8(L);
		lua_copy(L, -1, -2);
		lua_pop(L, 1);
		data = lua_tolstring(L, -1, &len);
	}
	else {
		data = luaL_tolstring(L, -1, &len);
		lua_pop(L, 1);
	}

	if (len == 0 || !data) {

		lua_pushstring(L, "");
		return 1;
	}

	char* escaped = (char*)gff_malloc((2 * len) + 1);
	if (!escaped) {
		luaL_error(L, "Out of memory");
		return 0;
	}

	size_t newlen = mysql_escape_string(escaped, data, len);
	lua_pushlstring(L, escaped, newlen);
	gff_free(escaped);

	return 1;
}

int MySqlIsBusy(lua_State* L) {
	LuaMySQL* luamysql = lua_tomysql(L, 1);
	lua_pushboolean(L, luamysql->busy);
	return 1;
}

int MySqlQuery(lua_State* L) {

	LuaMySQL* luamysql = lua_tomysql(L, 1);
	size_t qlen;
	const char* query = luaL_checklstring(L, 2, &qlen);

	if (!luamysql->connection) {
		luaL_error(L, "Connection is closed");
		return 0;
	}
	else if (luamysql->busy) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "Busy");
		return 2;
	}

	if (luamysql->result) {
		mysql_free_result(luamysql->result);
		luamysql->result = NULL;
	}

	if (luamysql->error) {
		gff_free(luamysql->error);
		luamysql->error = NULL;
	}

	if (luamysql->query) {
		gff_free(luamysql->query);
		luamysql->query = NULL;
		luamysql->querylen = 0;
	}

	luamysql->query = (char*)gff_malloc(qlen);

	if (!luamysql->query) {
		luaL_error(L, "Out of memory");
		return 0;
	}

	memcpy(luamysql->query, query, qlen);
	luamysql->querylen = qlen;
	luamysql->busy = true;
	SetEvent(luamysql->interrupt);

	lua_pushboolean(L, true);
	return 1;
}

void SetDone(LuaMySQL* mysqld, const char* error, MYSQL_RES* result) {

	if (mysqld->query) {
		gff_free(mysqld->query);
	}
	mysqld->query = NULL;
	mysqld->querylen = 0;
	mysqld->busy = false;

	if (mysqld->result) {
		mysql_free_result(mysqld->result);
		mysqld->result = NULL;
	}

	mysqld->result = result;

	if (error) {
		if (mysqld->error) {
			gff_free(mysqld->error);
		}

		mysqld->error = (char*)gff_malloc(strlen(error) + 1);
		if (mysqld->error) {
			strcpy(mysqld->error, error);
		}
	}
}

DWORD WINAPI QueryThread(LPVOID param) {
	LuaMySQL* mysqld = (LuaMySQL*)param;
	MYSQL_RES* result;

	while (mysqld->alive) {

		if (mysqld->busy) {

			if (mysqld->query && mysql_real_query(mysqld->connection, mysqld->query, mysqld->querylen)) {
				SetDone(mysqld, mysql_error(mysqld->connection), NULL);
				continue;
			}

			result = mysql_store_result(mysqld->connection);

			if (!result) {
				SetDone(mysqld, mysql_error(mysqld->connection), NULL);
			}
			else {
				SetDone(mysqld, NULL, result);
			}
		}

		WaitForSingleObject(mysqld->interrupt, INFINITE);
		ResetEvent(mysqld->interrupt);
	}

	return 0;
}

int MySqlGetRow(lua_State* L) {

	LuaMySQL* luamysql = lua_tomysql(L, 1);

	if (!luamysql || !luamysql->alive) {
		luaL_error(L, "Connection is closed");
		return 0;
	}

	while (luamysql->busy) {
		Sleep(1);
	}

	if (luamysql->error) {
		lua_pushnil(L);
		lua_pushstring(L, luamysql->error);
		return 2;
	}

	if (!luamysql->result) {
		lua_pushnil(L);
		lua_pushstring(L, "no result");
		return 2;
	}

	size_t numbfields = mysql_num_fields(luamysql->result);
	MYSQL_ROW row = mysql_fetch_row(luamysql->result);
	if (row) {
		unsigned long* fieldlengths = mysql_fetch_lengths(luamysql->result);
		lua_createtable(L, numbfields, 0);
		for (int n = 0; n < numbfields; n++) {
			lua_pushlstring(L, row[n], fieldlengths[n]);
			lua_rawseti(L, -2, n + 1);
		}
	}
	else {
		lua_pushnil(L);
		mysql_free_result(luamysql->result);
		luamysql->result = NULL;
	}

	return 1;
}

int MySqlGetFields(lua_State* L) {
	LuaMySQL* luamysql = lua_tomysql(L, 1);

	if (!luamysql || !luamysql->alive) {
		luaL_error(L, "Connection is closed");
		return 0;
	}

	while (luamysql->busy) {
		Sleep(1);
	}

	if (luamysql->error) {
		lua_pushnil(L);
		lua_pushstring(L, luamysql->error);
		return 2;
	}

	if (!luamysql->result) {
		lua_pushnil(L);
		lua_pushstring(L, "no result");
		return 2;
	}

	size_t numbfields = mysql_num_fields(luamysql->result);
	MYSQL_FIELD* fields = mysql_fetch_fields(luamysql->result);

	lua_createtable(L, numbfields, 0);

	for (int n = 0; n < numbfields; n++) {

		lua_newtable(L);

		lua_pushstring(L, "catalog");
		lua_pushlstring(L, fields[n].catalog, fields[n].catalog_length);
		lua_settable(L, -3);

		lua_pushstring(L, "charsetnr");
		lua_pushinteger(L, fields[n].charsetnr);
		lua_settable(L, -3);

		lua_pushstring(L, "charsetnr");
		lua_pushlstring(L, fields[n].db, fields[n].db_length);
		lua_settable(L, -3);

		lua_pushstring(L, "decimals");
		lua_pushinteger(L, fields[n].decimals);
		lua_settable(L, -3);

		lua_pushstring(L, "def");
		lua_pushlstring(L, fields[n].def, fields[n].def_length);
		lua_settable(L, -3);

		lua_pushstring(L, "flags");
		lua_pushinteger(L, fields[n].flags);
		lua_settable(L, -3);

		lua_pushstring(L, "length");
		lua_pushinteger(L, fields[n].length);
		lua_settable(L, -3);

		lua_pushstring(L, "maxlength");
		lua_pushinteger(L, fields[n].max_length);
		lua_settable(L, -3);

		lua_pushstring(L, "name");
		lua_pushlstring(L, fields[n].name, fields[n].name_length);
		lua_settable(L, -3);

		lua_pushstring(L, "orgname");
		lua_pushlstring(L, fields[n].org_name, fields[n].org_name_length);
		lua_settable(L, -3);

		lua_pushstring(L, "orgtable");
		lua_pushlstring(L, fields[n].org_table, fields[n].org_table_length);
		lua_settable(L, -3);

		lua_pushstring(L, "table");
		lua_pushlstring(L, fields[n].table, fields[n].table_length);
		lua_settable(L, -3);

		lua_pushstring(L, "type");
		lua_pushinteger(L, fields[n].type);
		lua_settable(L, -3);

		lua_rawseti(L, -2, n + 1);
	}

	return 1;
}

int MySqlGetResult(lua_State* L) {
	LuaMySQL* luamysql = lua_tomysql(L, 1);

	if (!luamysql || !luamysql->alive) {
		luaL_error(L, "Connection is closed");
		return 0;
	}

	while (luamysql->busy) {
		Sleep(1);
	}

	if (luamysql->error) {
		lua_pushnil(L);
		lua_pushstring(L, luamysql->error);
		return 2;
	}

	if (!luamysql->result) {
		lua_pushnil(L);
		lua_pushstring(L, "no result");
		return 2;
	}

	size_t rows = mysql_num_rows(luamysql->result);
	size_t numbfields = mysql_num_fields(luamysql->result);
	MYSQL_ROW row;
	MYSQL_FIELD* fields = mysql_fetch_fields(luamysql->result);
	int nth = 1;
	char* endptr;
	MYSQL_FIELD* field;
	const char* fielddata;
	unsigned long* fieldlengths;
	unsigned long length;

	lua_createtable(L, rows, 0);

	while ((row = mysql_fetch_row(luamysql->result))) {

		fieldlengths = mysql_fetch_lengths(luamysql->result);
		lua_createtable(L, numbfields, 0);

		for (int i = 0; i < numbfields; i++) {
			field = &fields[i];
			fielddata = row[i];
			length = fieldlengths[i];

			if (row[i]) {
				switch (field->type) {
				case MYSQL_TYPE_NULL:
					lua_pushnil(L);
					break;
				case MYSQL_TYPE_DECIMAL:
				case MYSQL_TYPE_FLOAT:
				case MYSQL_TYPE_BIT:
				case MYSQL_TYPE_NEWDECIMAL:
				case MYSQL_TYPE_DOUBLE:
					lua_pushnumber(L, strtod(fielddata, &endptr));
					break;
				case MYSQL_TYPE_SHORT:
				case MYSQL_TYPE_LONGLONG:
				case MYSQL_TYPE_TINY:
				case MYSQL_TYPE_LONG:
				case MYSQL_TYPE_INT24:
					lua_pushinteger(L, strtoll(fielddata, &endptr, 10));
					break;
				case MYSQL_TYPE_TINY_BLOB:
				case MYSQL_TYPE_MEDIUM_BLOB:
				case MYSQL_TYPE_LONG_BLOB:
				case MYSQL_TYPE_BLOB:
					lua_pushluastream(L, (BYTE*)fielddata, length);
					break;
				default:
					lua_pushlstring(L, fielddata, length);
					break;
				}
			}
			else {
				lua_pushnil(L);
			}

			lua_rawseti(L, -2, i + 1);
		}

		lua_rawseti(L, -2, nth++);
	}

	if (luamysql->result) {
		mysql_free_result(luamysql->result);
		luamysql->result = NULL;
	}

	return 1;
}

int MySqlConnect(lua_State* L) {

	const char* host = luaL_checkstring(L, 1);
	const char* user = luaL_checkstring(L, 2);
	const char* password = luaL_checkstring(L, 3);
	const char* database = luaL_checkstring(L, 4);
	int port = luaL_optinteger(L, 5, 3306);
	unsigned int timeout = luaL_optinteger(L, 6, 10);

	MYSQL* con = mysql_init(NULL);
	if (!con) {
		luaL_error(L, "Failed to init mysql");
		return 0;
	}

	mysql_options(con, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);

	if (!mysql_real_connect(con, host, user, password, database, port, NULL, 0)) {
		lua_pushfstring(L, "Failed to connect: %s", mysql_error(con));
		mysql_close(con);
		lua_error(L);
		return 0;
	}

	LuaMySQL* mysqld = lua_pushmysql(L);

	mysqld->connection = con;
	mysqld->busy = false;
	mysqld->alive = true;
	mysqld->interrupt = CreateEvent(NULL, TRUE, FALSE, NULL);
	mysqld->thread = CreateThread(
		NULL,
		0,
		QueryThread,
		mysqld,
		0,
		NULL);

	return 1;
}

int luamysql_gc(lua_State* L) {

	LuaMySQL* luamysql = (LuaMySQL*)lua_tomysql(L, 1);

	if (!luamysql) {
		return 0;
	}

	luamysql->alive = false;

	if (luamysql->thread != INVALID_HANDLE_VALUE) {
		SetEvent(luamysql->interrupt);
		WaitForSingleObject(luamysql->thread, INFINITE);
		CloseHandle(luamysql->thread);
		luamysql->thread = INVALID_HANDLE_VALUE;
	}

	if (luamysql->interrupt) {
		CloseHandle(luamysql->interrupt);
		luamysql->interrupt = INVALID_HANDLE_VALUE;
	}

	if (luamysql->result) {
		mysql_free_result(luamysql->result);
		luamysql->result = NULL;
	}

	if (luamysql->connection) {
		mysql_close(luamysql->connection);
		luamysql->connection = NULL;
	}

	if (luamysql->query) {
		gff_free(luamysql->query);
		luamysql->query = NULL;
		luamysql->querylen = 0;
	}

	if (luamysql->error) {
		gff_free(luamysql->error);
		luamysql->error = NULL;
	}

	return 0;
}

int luamysql_tostring(lua_State* L) {

	LuaMySQL* sq = lua_tomysql(L, 1);
	char my[1024];
	sprintf(my, "MySQL: 0x%016llX", (DWORD64)sq);
	lua_pushstring(L, my);
	return 1;
}