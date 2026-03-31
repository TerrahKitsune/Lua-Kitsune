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

static int        JsonRef      = LUA_NOREF;
static lua_State* JsonRefState = NULL;

static void PushAsParamString(lua_State* L, int index) {

	if (index < 0) index = lua_gettop(L) + index + 1;

	if (lua_istable(L, index)) {

		if (JsonRef == LUA_NOREF || JsonRefState != L) {
			JsonRef      = LUA_NOREF;
			JsonRefState = L;
			lua_getglobal(L, "Json");
			lua_pushliteral(L, "Create");
			lua_gettable(L, -2);
			if (lua_pcall_nohook(L, 0, 1, 0)) {
				lua_error(L);
				return;
			}
			JsonRef = luaL_ref(L, LUA_REGISTRYINDEX);
			lua_pop(L, 1);
		}

		lua_rawgeti(L, LUA_REGISTRYINDEX, JsonRef);
		lua_pushliteral(L, "Encode");
		lua_gettable(L, -2);
		lua_pushvalue(L, -2);
		lua_pushvalue(L, index);

		if (lua_pcall_nohook(L, 2, 1, 0)) {
			lua_error(L);
			return;
		}

		lua_remove(L, -2);
	}
	else if (lua_iswchar(L, index)) {
		lua_pushvalue(L, index);
		ToUtf8(L);
		lua_remove(L, -2);
	}
	else {
		luaL_tolstring(L, index, NULL);
	}
}

static void FreeParams(LuaMySQL* luamysql) {

	if (luamysql->paramValues) {
		for (int i = 0; i < luamysql->nParams; i++) {
			if (luamysql->paramValues[i]) {
				gff_free(luamysql->paramValues[i]);
			}
		}
		gff_free(luamysql->paramValues);
		luamysql->paramValues = NULL;
	}

	if (luamysql->paramLengths) {
		gff_free(luamysql->paramLengths);
		luamysql->paramLengths = NULL;
	}

	luamysql->nParams = 0;
	luamysql->isParamQuery = false;
}

int MySqlEscapeValue(lua_State* L) {

	LuaMySQL* luamysql = lua_tomysql(L, 1);
	size_t len;
	const char* str = luaL_checklstring(L, 2, &len);

	if (!luamysql->connection) {
		luaL_error(L, "Connection is closed");
		return 0;
	}

	char* escaped = (char*)gff_malloc((2 * len) + 1);
	if (!escaped) {
		luaL_error(L, "Out of memory");
		return 0;
	}

	unsigned long newlen = mysql_real_escape_string(luamysql->connection, escaped, str, (unsigned long)len);
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

	FreeParams(luamysql);
	luamysql->currentRow = NULL;
	luamysql->currentRowLengths = NULL;

	luamysql->query = (char*)gff_malloc(qlen + 1);

	if (!luamysql->query) {
		luaL_error(L, "Out of memory");
		return 0;
	}

	memcpy(luamysql->query, query, qlen);
	luamysql->query[qlen] = '\0';
	luamysql->querylen = qlen;

	if (lua_istable(L, 3)) {

		int nParams = 0;
		for (size_t k = 0; k < qlen; k++) {
			if (query[k] == '?') nParams++;
		}

		if (nParams > 0) {

			luamysql->paramValues = (char**)gff_malloc(sizeof(char*) * nParams);
			luamysql->paramLengths = (int*)gff_malloc(sizeof(int) * nParams);

			if (!luamysql->paramValues || !luamysql->paramLengths) {
				luaL_error(L, "Out of memory");
				return 0;
			}

			memset(luamysql->paramValues, 0, sizeof(char*) * nParams);
			memset(luamysql->paramLengths, 0, sizeof(int) * nParams);
			luamysql->nParams = nParams;

			for (int i = 0; i < nParams; i++) {

				lua_rawgeti(L, 3, i + 1);

				if (lua_isnil(L, -1)) {
					luamysql->paramValues[i] = NULL;
					luamysql->paramLengths[i] = 0;
					lua_pop(L, 1);
				}
				else {
					PushAsParamString(L, -1);

					size_t plen;
					const char* pval = lua_tolstring(L, -1, &plen);

					luamysql->paramValues[i] = (char*)gff_malloc(plen + 1);
					if (!luamysql->paramValues[i]) {
						lua_pop(L, 2);
						luaL_error(L, "Out of memory");
						return 0;
					}

					memcpy(luamysql->paramValues[i], pval, plen);
					luamysql->paramValues[i][plen] = '\0';
					luamysql->paramLengths[i] = (int)plen;
					lua_pop(L, 2);
				}
			}

			luamysql->isParamQuery = true;
		}
	}

	luamysql->busy = true;
	SetEvent(luamysql->interrupt);

	lua_pushboolean(L, true);
	return 1;
}

static void SetDone(LuaMySQL* mysqld, const char* error, MYSQL_RES* result) {

	if (mysqld->query) {
		gff_free(mysqld->query);
	}
	mysqld->query = NULL;
	mysqld->querylen = 0;
	FreeParams(mysqld);
	mysqld->currentRow = NULL;
	mysqld->currentRowLengths = NULL;
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
	else {
		if (mysqld->error) {
			gff_free(mysqld->error);
			mysqld->error = NULL;
		}
	}
}

DWORD WINAPI QueryThread(LPVOID param) {
	LuaMySQL* mysqld = (LuaMySQL*)param;
	MYSQL_RES* result;

	while (mysqld->alive) {

		if (mysqld->busy) {

			int queryResult = 0;

			if (mysqld->isParamQuery) {

				size_t totalLen = mysqld->querylen;
				for (int i = 0; i < mysqld->nParams; i++) {
					if (mysqld->paramValues[i]) {
						totalLen += (size_t)mysqld->paramLengths[i] * 2 + 2;
					}
					else {
						totalLen += 4;
					}
				}

				char* builtQuery = (char*)gff_malloc(totalLen + 1);
				if (!builtQuery) {
					SetDone(mysqld, "Out of memory", NULL);
					continue;
				}

				size_t queryPos = 0, outPos = 0;
				int paramIdx = 0;
				while (queryPos < mysqld->querylen) {
					if (mysqld->query[queryPos] == '?' && paramIdx < mysqld->nParams) {
						if (mysqld->paramValues[paramIdx]) {
							char* escapeBuf = (char*)gff_malloc((size_t)mysqld->paramLengths[paramIdx] * 2 + 1);
							if (escapeBuf) {
								unsigned long elen = mysql_real_escape_string(mysqld->connection, escapeBuf,
									mysqld->paramValues[paramIdx], (unsigned long)mysqld->paramLengths[paramIdx]);
								builtQuery[outPos++] = '\'';
								memcpy(builtQuery + outPos, escapeBuf, elen);
								outPos += elen;
								builtQuery[outPos++] = '\'';
								gff_free(escapeBuf);
							}
						}
						else {
							memcpy(builtQuery + outPos, "NULL", 4);
							outPos += 4;
						}
						paramIdx++;
						queryPos++;
					}
					else {
						builtQuery[outPos++] = mysqld->query[queryPos++];
					}
				}
				builtQuery[outPos] = '\0';

				queryResult = mysql_real_query(mysqld->connection, builtQuery, (unsigned long)outPos);
				gff_free(builtQuery);
			}
			else {
				queryResult = mysqld->query ? mysql_real_query(mysqld->connection, mysqld->query, (unsigned long)mysqld->querylen) : 0;
			}

			if (queryResult) {
				SetDone(mysqld, mysql_error(mysqld->connection), NULL);
				continue;
			}

			result = mysql_store_result(mysqld->connection);
			if (!result) {
				const char* err = mysql_error(mysqld->connection);
				SetDone(mysqld, (err && err[0] != '\0') ? err : NULL, NULL);
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

static void PushMySQLValue(lua_State* L, const char* data, unsigned long length, enum_field_types type) {
	char* endptr;
	switch (type) {
	case MYSQL_TYPE_NULL:
		lua_pushnil(L);
		break;
	case MYSQL_TYPE_DECIMAL:
	case MYSQL_TYPE_FLOAT:
	case MYSQL_TYPE_BIT:
	case MYSQL_TYPE_NEWDECIMAL:
	case MYSQL_TYPE_DOUBLE:
		lua_pushnumber(L, strtod(data, &endptr));
		break;
	case MYSQL_TYPE_SHORT:
	case MYSQL_TYPE_LONGLONG:
	case MYSQL_TYPE_TINY:
	case MYSQL_TYPE_LONG:
	case MYSQL_TYPE_INT24:
		lua_pushinteger(L, strtoll(data, &endptr, 10));
		break;
	case MYSQL_TYPE_TINY_BLOB:
	case MYSQL_TYPE_MEDIUM_BLOB:
	case MYSQL_TYPE_LONG_BLOB:
	case MYSQL_TYPE_BLOB:
		lua_pushluastream(L, (BYTE*)data, length);
		break;
	default:
		lua_pushlstring(L, data, length);
		break;
	}
}

int MySqlFetch(lua_State* L) {

	LuaMySQL* luamysql = lua_tomysql(L, 1);

	if (!luamysql || !luamysql->alive) {
		luaL_error(L, "Connection is closed");
		return 0;
	}

	while (luamysql->busy) {
		Sleep(1);
	}

	if (luamysql->error) {
		lua_pushboolean(L, false);
		lua_pushstring(L, luamysql->error);
		return 2;
	}

	if (!luamysql->result) {
		lua_pushboolean(L, false);
		return 1;
	}

	luamysql->currentRow = mysql_fetch_row(luamysql->result);
	if (luamysql->currentRow) {
		luamysql->currentRowLengths = mysql_fetch_lengths(luamysql->result);
		lua_pushboolean(L, true);
	}
	else {
		luamysql->currentRowLengths = NULL;
		mysql_free_result(luamysql->result);
		luamysql->result = NULL;
		lua_pushboolean(L, false);
	}

	return 1;
}

int MySqlGetRow(lua_State* L) {

	LuaMySQL* luamysql = lua_tomysql(L, 1);

	if (!luamysql || !luamysql->alive) {
		luaL_error(L, "Connection is closed");
		return 0;
	}

	if (!luamysql->result || !luamysql->currentRow) {
		lua_pushnil(L);
		return 1;
	}

	int nfields = (int)mysql_num_fields(luamysql->result);
	MYSQL_FIELD* fields = mysql_fetch_fields(luamysql->result);

	if (lua_type(L, 2) == LUA_TSTRING) {

		const char* fname = lua_tostring(L, 2);
		for (int i = 0; i < nfields; i++) {
			if (strcmp(fields[i].name, fname) == 0) {
				if (!luamysql->currentRow[i]) {
					lua_pushnil(L);
				}
				else {
					PushMySQLValue(L, luamysql->currentRow[i], luamysql->currentRowLengths[i], fields[i].type);
				}
				return 1;
			}
		}
		lua_pushnil(L);
		return 1;
	}

	int idx = (int)luaL_optinteger(L, 2, -1);

	if (idx > 0) {

		idx--;
		if (idx >= nfields) {
			lua_pushnil(L);
			return 1;
		}

		if (!luamysql->currentRow[idx]) {
			lua_pushnil(L);
		}
		else {
			PushMySQLValue(L, luamysql->currentRow[idx], luamysql->currentRowLengths[idx], fields[idx].type);
		}
		return 1;
	}

	lua_createtable(L, 0, nfields);

	for (int i = 0; i < nfields; i++) {

		lua_pushstring(L, fields[i].name);

		if (!luamysql->currentRow[i]) {
			lua_pushnil(L);
		}
		else {
			PushMySQLValue(L, luamysql->currentRow[i], luamysql->currentRowLengths[i], fields[i].type);
		}

		lua_settable(L, -3);
	}

	return 1;
}

int MySqlFinish(lua_State* L) {

	LuaMySQL* luamysql = lua_tomysql(L, 1);

	while (luamysql->busy) {
		Sleep(1);
	}

	luamysql->currentRow = NULL;
	luamysql->currentRowLengths = NULL;

	if (luamysql->result) {
		mysql_free_result(luamysql->result);
		luamysql->result = NULL;
	}

	if (luamysql->error) {
		gff_free(luamysql->error);
		luamysql->error = NULL;
	}

	return 0;
}

int MySqlConnect(lua_State* L) {

	const char* host = luaL_checkstring(L, 1);
	const char* user = luaL_checkstring(L, 2);
	const char* password = luaL_checkstring(L, 3);
	const char* database = luaL_checkstring(L, 4);
	int port = (int)luaL_optinteger(L, 5, 3306);
	unsigned int timeout = (unsigned int)luaL_optinteger(L, 6, 10);

	MYSQL* con = mysql_init(NULL);
	if (!con) {
		luaL_error(L, "Failed to init mysql");
		return 0;
	}

	mysql_options(con, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
	mysql_options(con, MYSQL_SET_CHARSET_NAME, "utf8mb4");

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

	luamysql->currentRow = NULL;
	luamysql->currentRowLengths = NULL;

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

	FreeParams(luamysql);

	return 0;
}

int luamysql_tostring(lua_State* L) {

	LuaMySQL* sq = lua_tomysql(L, 1);
	char my[1024];
	sprintf(my, "MySQL: 0x%016llX", (DWORD64)sq);
	lua_pushstring(L, my);
	return 1;
}