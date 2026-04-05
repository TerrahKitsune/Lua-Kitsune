#include "LuaMySQL.h"
#include "stream.h"
#include "luawchar.h"
#ifdef _WIN32
#pragma comment(lib, "mysql/libmysql.lib")
#endif

// -- LuaMySQLQuery -------------------------------------------------------------
typedef struct LuaMySQLQuery {
	LuaMySQL*  conn;
	int        connRef;
	char*      sql;
	size_t     sqllen;
	MYSQL_RES* result;
	char*      error;
} LuaMySQLQuery;

// -- Static JSON ref for PushAsParamString -------------------------------------
static int        s_JsonRef      = LUA_NOREF;
static lua_State* s_JsonRefState = NULL;

// -- PushAsParamString ---------------------------------------------------------
static void PushAsParamString(lua_State* L, int index) {
	if (index < 0)
		index = lua_gettop(L) + index + 1;

	if (lua_istable(L, index)) {
		if (s_JsonRef == LUA_NOREF || s_JsonRefState != L) {
			s_JsonRef      = LUA_NOREF;
			s_JsonRefState = L;
			lua_getglobal(L, "Json");
			lua_pushliteral(L, "Create");
			lua_gettable(L, -2);
			if (lua_pcall_nohook(L, 0, 1, 0)) {
				lua_error(L);
				return;
			}
			s_JsonRef = luaL_ref(L, LUA_REGISTRYINDEX);
			lua_pop(L, 1);
		}

		lua_rawgeti(L, LUA_REGISTRYINDEX, s_JsonRef);
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

// -- PushMySQLValue ------------------------------------------------------------
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
		lua_pushluastream(L, (uint8_t*)data, length);
		break;
	default:
		lua_pushlstring(L, data, length);
		break;
	}
}

// -- BuildQueryWithParams ------------------------------------------------------
static void BuildQueryWithParams(LuaMySQLQuery* q, const char* sql, size_t sqllen,
	lua_State* L, int paramTableIdx) {
	if (!lua_istable(L, paramTableIdx)) {
		q->sql = (char*)gff_malloc(sqllen + 1);
		if (!q->sql)
			luaL_error(L, "Out of memory");
		memcpy(q->sql, sql, sqllen);
		q->sql[sqllen] = '\0';
		q->sqllen = sqllen;
		return;
	}

	int nParams = 0;
	for (size_t k = 0; k < sqllen; k++) {
		if (sql[k] == '?')
			nParams++;
	}

	if (nParams == 0) {
		q->sql = (char*)gff_malloc(sqllen + 1);
		if (!q->sql)
			luaL_error(L, "Out of memory");
		memcpy(q->sql, sql, sqllen);
		q->sql[sqllen] = '\0';
		q->sqllen = sqllen;
		return;
	}

	char** paramValues  = (char**)gff_malloc(sizeof(char*) * nParams);
	int*   paramLengths = (int*)gff_malloc(sizeof(int) * nParams);
	if (!paramValues || !paramLengths) {
		if (paramValues)
			gff_free(paramValues);
		if (paramLengths)
			gff_free(paramLengths);
		luaL_error(L, "Out of memory");
		return;
	}
	memset(paramValues,  0, sizeof(char*) * nParams);
	memset(paramLengths, 0, sizeof(int)   * nParams);

	for (int i = 0; i < nParams; i++) {
		lua_rawgeti(L, paramTableIdx, i + 1);
		if (lua_isnil(L, -1)) {
			paramValues[i]  = NULL;
			paramLengths[i] = 0;
			lua_pop(L, 1);
		}
		else {
			PushAsParamString(L, -1);
			size_t plen;
			const char* pval = lua_tolstring(L, -1, &plen);
			paramValues[i] = (char*)gff_malloc(plen + 1);
			if (!paramValues[i]) {
				lua_pop(L, 2);
				for (int j = 0; j < i; j++) {
					if (paramValues[j])
						gff_free(paramValues[j]);
				}
				gff_free(paramValues);
				gff_free(paramLengths);
				luaL_error(L, "Out of memory");
				return;
			}
			memcpy(paramValues[i], pval, plen);
			paramValues[i][plen] = '\0';
			paramLengths[i] = (int)plen;
			lua_pop(L, 2);
		}
	}

	size_t totalLen = sqllen;
	for (int i = 0; i < nParams; i++) {
		totalLen += paramValues[i]
			? (size_t)paramLengths[i] * 2 + 2
			: 4;
	}

	char* builtQuery = (char*)gff_malloc(totalLen + 1);
	if (!builtQuery) {
		for (int i = 0; i < nParams; i++) {
			if (paramValues[i])
				gff_free(paramValues[i]);
		}
		gff_free(paramValues);
		gff_free(paramLengths);
		luaL_error(L, "Out of memory");
		return;
	}

	size_t outPos  = 0;
	int    paramIdx = 0;
	for (size_t queryPos = 0; queryPos < sqllen; queryPos++) {
		if (sql[queryPos] == '?' && paramIdx < nParams) {
			if (paramValues[paramIdx]) {
				char* escapeBuf = (char*)gff_malloc((size_t)paramLengths[paramIdx] * 2 + 1);
				if (escapeBuf) {
					unsigned long elen = mysql_real_escape_string(
						q->conn->connection, escapeBuf,
						paramValues[paramIdx], (unsigned long)paramLengths[paramIdx]);
					builtQuery[outPos++] = '\'';
					memcpy(builtQuery + outPos, escapeBuf, elen);
					outPos += elen;
					builtQuery[outPos++] = '\'';
					gff_free(escapeBuf);
				}
				else {
					builtQuery[outPos++] = '\'';
					memcpy(builtQuery + outPos, paramValues[paramIdx], paramLengths[paramIdx]);
					outPos += paramLengths[paramIdx];
					builtQuery[outPos++] = '\'';
				}
			}
			else {
				memcpy(builtQuery + outPos, "NULL", 4);
				outPos += 4;
			}
			paramIdx++;
		}
		else {
			builtQuery[outPos++] = sql[queryPos];
		}
	}
	builtQuery[outPos] = '\0';

	for (int i = 0; i < nParams; i++) {
		if (paramValues[i])
			gff_free(paramValues[i]);
	}
	gff_free(paramValues);
	gff_free(paramLengths);

	q->sql    = builtQuery;
	q->sqllen = outPos;
}

// -- FreeQuery -----------------------------------------------------------------
// Frees all resources owned by a LuaMySQLQuery. Clears conn->activeQuery and
// conn->queryRef (including luaL_unref for the coroutine anchor) before freeing
// the struct itself. Safe to call with individual fields already NULL.
static void FreeQuery(lua_State* L, LuaMySQLQuery* q) {
	if (!q)
		return;

	if (q->result) {
		mysql_free_result(q->result);
		q->result = NULL;
	}

	if (q->conn) {
		if (q->conn->queryRef != LUA_NOREF) {
			luaL_unref(L, LUA_REGISTRYINDEX, q->conn->queryRef);
			q->conn->queryRef = LUA_NOREF;
		}
		q->conn->activeQuery = NULL;
	}

	if (q->connRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, q->connRef);
		q->connRef = LUA_NOREF;
	}

	if (q->sql)
		gff_free(q->sql);
	if (q->error)
		gff_free(q->error);
	gff_free(q);
}

// -- lua_tomysql / lua_pushmysql -----------------------------------------------
LuaMySQL* lua_tomysql(lua_State* L, int index) {
	LuaMySQL* luamysql = (LuaMySQL*)lua_touserdata(L, index);
	if (!luamysql) {
		luaL_error(L, "parameter is not a %s", LUAMYSQL);
		return NULL;
	}
	return luamysql;
}

LuaMySQL* lua_pushmysql(lua_State* L) {
	LuaMySQL* luamysql = (LuaMySQL*)lua_newuserdata(L, sizeof(LuaMySQL));
	if (!luamysql) {
		luaL_error(L, "Unable to create mysql connection");
		return NULL;
	}
	luaL_getmetatable(L, LUAMYSQL);
	lua_setmetatable(L, -2);
	memset(luamysql, 0, sizeof(LuaMySQL));
	luamysql->queryRef = LUA_NOREF;
	return luamysql;
}

// -- MySqlEscapeValue ----------------------------------------------------------
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

// -- MySqlIsBusy ---------------------------------------------------------------
int MySqlIsBusy(lua_State* L) {
	LuaMySQL* m = lua_tomysql(L, 1);
	if (m->queryRef == LUA_NOREF) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, m->queryRef);
	lua_State* T = lua_tothread(L, -1);
	lua_pop(L, 1);
	lua_pushboolean(L, T != NULL && lua_status(T) == LUA_YIELD);
	return 1;
}

// -- QueryStreamCont — shared across Windows and Linux -------------------------
// Called when the query coroutine (T) is resumed after yielding the rowcount.
// Yields one integer-keyed row table per resume; yields nil (returns) when done.
static int QueryStreamCont(lua_State* L, int status, lua_KContext ctx) {
	(void)status;
	LuaMySQLQuery* q = (LuaMySQLQuery*)(intptr_t)ctx;

	if (lua_toboolean(L, 1)) {
		FreeQuery(L, q);
		return 0;
	}

	if (!q->result) {
		FreeQuery(L, q);
		lua_pushnil(L);
		return 1;
	}

	MYSQL_ROW row = mysql_fetch_row(q->result);
	if (!row) {
		FreeQuery(L, q);
		lua_pushnil(L);
		return 1;
	}

	int nfields         = (int)mysql_num_fields(q->result);
	MYSQL_FIELD* fields = mysql_fetch_fields(q->result);
	unsigned long* lens = mysql_fetch_lengths(q->result);

	lua_createtable(L, nfields, 0);
	for (int i = 0; i < nfields; i++) {
		if (!row[i])
			lua_pushnil(L);
		else
			PushMySQLValue(L, row[i], lens[i], fields[i].type);
		lua_rawseti(L, -2, i + 1);
	}
	return lua_yieldk(L, 1, ctx, QueryStreamCont);
}

// -----------------------------------------------------------------------------
// Platform-specific query body and wait/run/store continuations
// -----------------------------------------------------------------------------

// -- Cross-platform nonblocking query continuations --------------------------

static int QueryStoreCont(lua_State* L, int status, lua_KContext ctx) {
	(void)status;
	LuaMySQLQuery* q = (LuaMySQLQuery*)(intptr_t)ctx;

	if (lua_toboolean(L, 1)) {
		if (q->result) {
			mysql_free_result(q->result);
			q->result = NULL;
		}
		return 0;
	}

	net_async_status nas = mysql_store_result_nonblocking(q->conn->connection, &q->result);
	if (nas == NET_ASYNC_NOT_READY) {
		lua_pushnil(L);
		return lua_yieldk(L, 1, ctx, QueryStoreCont);
	}

	if (nas == NET_ASYNC_ERROR || (nas == NET_ASYNC_COMPLETE && !q->result && mysql_errno(q->conn->connection) != 0)) {
		const char* err = mysql_error(q->conn->connection);
		lua_pushstring(L, err && err[0] ? err : "query error");
		return lua_yieldk(L, 1, ctx, QueryStreamCont);
	}

	my_ulonglong rowcount = q->result
		? mysql_num_rows(q->result)
		: mysql_affected_rows(q->conn->connection);

	lua_pushinteger(L, (lua_Integer)rowcount);
	return lua_yieldk(L, 1, ctx, QueryStreamCont);
}

static int QueryRunCont(lua_State* L, int status, lua_KContext ctx) {
	(void)status;
	LuaMySQLQuery* q = (LuaMySQLQuery*)(intptr_t)ctx;

	if (lua_toboolean(L, 1)) {
		return 0;
	}

	net_async_status nas = mysql_real_query_nonblocking(
		q->conn->connection, q->sql, (unsigned long)q->sqllen);

	if (nas == NET_ASYNC_NOT_READY) {
		lua_pushnil(L);
		return lua_yieldk(L, 1, ctx, QueryRunCont);
	}

	if (nas == NET_ASYNC_ERROR) {
		const char* err = mysql_error(q->conn->connection);
		lua_pushstring(L, err && err[0] ? err : "mysql_real_query_nonblocking error");
		return lua_yieldk(L, 1, ctx, QueryStreamCont);
	}

	lua_pushnil(L);
	return lua_yieldk(L, 1, ctx, QueryStoreCont);
}

static int MySqlQueryBody(lua_State* L) {
	LuaMySQLQuery* q = (LuaMySQLQuery*)lua_touserdata(L, lua_upvalueindex(1));

	if (lua_toboolean(L, 1)) {
		return 0;
	}

	net_async_status nas = mysql_real_query_nonblocking(
		q->conn->connection, q->sql, (unsigned long)q->sqllen);

	if (nas == NET_ASYNC_NOT_READY) {
		lua_pushnil(L);
		return lua_yieldk(L, 1, (lua_KContext)(intptr_t)q, QueryRunCont);
	}

	if (nas == NET_ASYNC_ERROR) {
		const char* err = mysql_error(q->conn->connection);
		lua_pushstring(L, err && err[0] ? err : "mysql_real_query_nonblocking error");
		return lua_yieldk(L, 1, (lua_KContext)(intptr_t)q, QueryStreamCont);
	}

	lua_pushnil(L);
	return lua_yieldk(L, 1, (lua_KContext)(intptr_t)q, QueryStoreCont);
}

// -- Internal: create and anchor a query coroutine
// Allocates LuaMySQLQuery, builds SQL, creates T with MySqlQueryBody closure,
// anchors T in m->queryRef, sets m->activeQuery. Leaves T on top of L's stack.
// conn is at L stack position connIdx. paramTableIdx may be 0 (no params).
static LuaMySQLQuery* SetupQueryCoroutine(lua_State* L, LuaMySQL* m,
	int connIdx, const char* sql, size_t sqllen, int paramTableIdx) {
	LuaMySQLQuery* q = (LuaMySQLQuery*)gff_malloc(sizeof(LuaMySQLQuery));
	if (!q) {
		luaL_error(L, "Out of memory");
		return NULL;
	}
	memset(q, 0, sizeof(LuaMySQLQuery));
	q->conn    = m;
	q->connRef = LUA_NOREF;

	lua_pushvalue(L, connIdx);
	q->connRef = luaL_ref(L, LUA_REGISTRYINDEX);

	BuildQueryWithParams(q, sql, sqllen, L, paramTableIdx);

	lua_State* T = lua_newthread(L);
	lua_pushlightuserdata(T, q);
	lua_pushcclosure(T, MySqlQueryBody, 1);

	m->queryRef   = luaL_ref(L, LUA_REGISTRYINDEX);
	m->activeQuery = q;

	lua_rawgeti(L, LUA_REGISTRYINDEX, m->queryRef);
	return q;
}

// -- MySqlQuery ----------------------------------------------------------------
int MySqlQuery(lua_State* L) {
	LuaMySQL* m = lua_tomysql(L, 1);

	if (!m->connection) {
		luaL_error(L, "Connection is closed");
		return 0;
	}

	if (m->queryRef != LUA_NOREF) {
		lua_pushnil(L);
		lua_pushliteral(L, "Connection already has an active query");
		return 2;
	}

	size_t sqllen;
	const char* sql = luaL_checklstring(L, 2, &sqllen);

	SetupQueryCoroutine(L, m, 1, sql, sqllen, 3);
	return 1;
}

// -- MySqlConnect --------------------------------------------------------------
static int MySqlConnectCont(lua_State* L, int status, lua_KContext ctx) {
	(void)status;
	int connIdx        = (int)(intptr_t)ctx;
	LuaMySQL* mysqld   = (LuaMySQL*)lua_touserdata(L, connIdx);
	const char* host   = lua_tostring(L, 1);
	const char* user   = lua_tostring(L, 2);
	const char* pass   = lua_tostring(L, 3);
	const char* db     = lua_tostring(L, 4);
	int port           = (int)luaL_optinteger(L, 5, 3306);

	net_async_status nas = mysql_real_connect_nonblocking(
		mysqld->connection, host, user, pass, db, port, NULL, 0);

	if (nas == NET_ASYNC_NOT_READY)
		return lua_yieldk(L, 0, ctx, MySqlConnectCont);

	if (nas == NET_ASYNC_ERROR) {
		const char* err = mysql_error(mysqld->connection);
		mysql_close(mysqld->connection);
		mysqld->connection = NULL;
		lua_pushnil(L);
		lua_pushfstring(L, "Failed to connect: %s", err ? err : "unknown error");
		return 2;
	}

	lua_pushvalue(L, connIdx);
	return 1;
}

int MySqlConnect(lua_State* L) {
	const char* host     = luaL_checkstring(L, 1);
	const char* user     = luaL_checkstring(L, 2);
	const char* password = luaL_checkstring(L, 3);
	const char* database = luaL_checkstring(L, 4);
	int port             = (int)luaL_optinteger(L, 5, 3306);
	unsigned int timeout = (unsigned int)luaL_optinteger(L, 6, 10);

	MYSQL* con = mysql_init(NULL);
	if (!con) {
		lua_pushnil(L);
		lua_pushliteral(L, "Failed to init mysql");
		return 2;
	}

	mysql_options(con, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
	mysql_options(con, MYSQL_SET_CHARSET_NAME, "utf8mb4");

	LuaMySQL* mysqld = lua_pushmysql(L);
	mysqld->connection = con;
	int connIdx = lua_gettop(L);

	net_async_status nas = mysql_real_connect_nonblocking(
		con, host, user, password, database, port, NULL, 0);

	if (nas == NET_ASYNC_COMPLETE) {
		return 1;
	}

	if (nas == NET_ASYNC_ERROR) {
		const char* err = mysql_error(con);
		mysql_close(con);
		mysqld->connection = NULL;
		lua_pushnil(L);
		lua_pushfstring(L, "Failed to connect: %s", err ? err : "unknown error");
		return 2;
	}

	return lua_yieldk(L, 0, (lua_KContext)(intptr_t)connIdx, MySqlConnectCont);
}

// -- luamysql_gc ---------------------------------------------------------------
int luamysql_gc(lua_State* L) {
	LuaMySQL* m = (LuaMySQL*)lua_touserdata(L, 1);
	if (!m)
		return 0;

	if (m->activeQuery) {
		FreeQuery(L, (LuaMySQLQuery*)m->activeQuery);
	}

	if (m->queryRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, m->queryRef);
		m->queryRef = LUA_NOREF;
	}

	if (m->connection) {
		mysql_close(m->connection);
		m->connection = NULL;
	}

	if (m->error) {
		gff_free(m->error);
		m->error = NULL;
	}

	return 0;
}

// -- luamysql_tostring ---------------------------------------------------------
int luamysql_tostring(lua_State* L) {
	LuaMySQL* m = lua_tomysql(L, 1);
	char buf[64];
	snprintf(buf, sizeof(buf), "MySQL: 0x%016" PRIx64, (uint64_t)(uintptr_t)m);
	lua_pushstring(L, buf);
	return 1;
}

