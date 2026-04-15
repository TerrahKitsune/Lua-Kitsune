#ifdef _WIN32
#include <WinSock2.h>
#pragma comment(lib, "postgres/lib/libpq.lib")
#endif
#include "platform.h"
#include "LuaPostgres.h"
#include "luawchar.h"
#include "luaidentifier.h"
#include "luadatetime.h"
#include "luadecimal.h"

// -- Platform helper: set socket non-blocking ----------------------------------
#ifdef _WIN32
static void set_fd_nonblocking(int fd) {
	u_long one = 1;
	ioctlsocket((SOCKET)(uintptr_t)fd, FIONBIO, &one);
}
#else
#include <fcntl.h>
static void set_fd_nonblocking(int fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags >= 0)
		fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
#endif

// -- OID constants -------------------------------------------------------------
// The Windows bundled libpq-fe.h stub defines these directly.
// The system libpq-fe.h on Linux omits them (they live in server/catalog/pg_type_d.h
// which requires postgresql-server-dev, not just libpq-dev).  Individual guards
// mean each constant is protected independently regardless of partial definitions.
#ifndef BOOLOID
#  define BOOLOID      16
#endif
#ifndef BYTEAOID
#  define BYTEAOID     17
#endif
#ifndef INT8OID
#  define INT8OID      20
#endif
#ifndef INT2OID
#  define INT2OID      21
#endif
#ifndef INT4OID
#  define INT4OID      23
#endif
#ifndef FLOAT4OID
#  define FLOAT4OID   700
#endif
#ifndef FLOAT8OID
#  define FLOAT8OID   701
#endif
#ifndef NUMERICOID
#  define NUMERICOID 1700
#endif
#ifndef UUIDOID
#  define UUIDOID    2950
#endif
#ifndef DATEOID
#  define DATEOID    1082
#endif
#ifndef TIMEOID
#  define TIMEOID    1083
#endif
#ifndef TIMETZOID
#  define TIMETZOID  1266
#endif
#ifndef TIMESTAMPOID
#  define TIMESTAMPOID   1114
#endif
#ifndef TIMESTAMPTZOID
#  define TIMESTAMPTZOID 1184
#endif

// -- Helper mode constants -----------------------------------------------------
#define PG_HELPER_RAW      0
#define PG_HELPER_NONQUERY 1
#define PG_HELPER_SCALAR   2
#define PG_HELPER_QUERYALL 3

// -- LuaPostgresQuery ----------------------------------------------------------
typedef struct LuaPostgresQuery {
	LuaPostgres* conn;
	int          connRef;
	char**       paramValues;
	int*         paramLengths;
	int          nParams;
	char*        sql;
	PGresult*    result;
	char*        error;
	int          cancelFnRef;
	int          helperMode;
	int          accumTableIdx; // QueryAll: absolute stack index of row table
	int          accumRowIdx;   // stream cursor (0-based); QueryAll uses rawlen instead
} LuaPostgresQuery;

// -- JSON ref for PushAsParamString --------------------------------------------
static int        s_JsonRef      = LUA_NOREF;
static lua_State* s_JsonRefState = NULL;

// -- Forward declarations ------------------------------------------------------
static void FreeQuery(lua_State* L, LuaPostgresQuery* q);
static int  QueryStreamCont(lua_State* L, int status, lua_KContext ctx);
static int  HelperWaitCont(lua_State* L, int status, lua_KContext ctx);
static int  HelperStreamCont(lua_State* L, int status, lua_KContext ctx);

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
	else if (lua_isidentifier(L, index)) {
		lua_identifier_push_string(L, index);
	}
	else if (lua_isdatetime(L, index)) {
		lua_datetime_push_string(L, index);
	}
	else if (lua_isdecimal(L, index)) {
		lua_decimal_push_string(L, index);
	}
	else {
		luaL_tolstring(L, index, NULL);
	}
}

// -- PushPostgresValue ---------------------------------------------------------
static void PushPostgresValue(lua_State* L, const char* val, int len, Oid type) {
	if (!val) {
		lua_pushnil(L);
		return;
	}
	char* endptr;
	switch (type) {
	case BOOLOID:
		lua_pushboolean(L, val[0] == 't' || val[0] == 'T' || val[0] == '1');
		break;
	case INT2OID:
	case INT4OID:
	case INT8OID:
		lua_pushinteger(L, strtoll(val, &endptr, 10));
		break;
	case FLOAT4OID:
	case FLOAT8OID:
		lua_pushnumber(L, strtod(val, &endptr));
		break;
	case NUMERICOID: {
		LuaDecimal tmp;
		if (decimal_parse_c(val, (size_t)len, &tmp))
			*lua_pushdecimal(L) = tmp;
		else
			lua_pushlstring(L, val, (size_t)len);
		break;
	}
	case UUIDOID:
		if (!lua_pushidentifier_fromstring(L, val, (size_t)len))
			lua_pushlstring(L, val, (size_t)len);
		break;
	case DATEOID:
	case TIMEOID:
	case TIMETZOID:
	case TIMESTAMPOID:
	case TIMESTAMPTZOID: {
		LuaDateTime tmp;
		memset(&tmp, 0, sizeof(tmp));
		if (datetime_parse_c(val, &tmp))
			*lua_pushdatetime(L) = tmp;
		else
			lua_pushlstring(L, val, (size_t)len);
		break;
	}
	default:
		lua_pushlstring(L, val, len);
		break;
	}
}

// -- BuildQueryParams ----------------------------------------------------------
static void BuildQueryParams(LuaPostgresQuery* q, const char* sql, size_t sqllen,
	lua_State* L, int paramTableIdx) {
	q->sql = (char*)kitsune_malloc(sqllen + 1);
	if (!q->sql) {
		luaL_error(L, "Out of memory");
		return;
	}
	memcpy(q->sql, sql, sqllen);
	q->sql[sqllen] = '\0';

	if (!lua_istable(L, paramTableIdx)) {
		q->nParams = 0;
		return;
	}

	int nParams = 0;
	for (size_t k = 0; k + 1 < sqllen; k++) {
		if (sql[k] == '$' && sql[k + 1] >= '1' && sql[k + 1] <= '9') {
			int n = 0;
			size_t j = k + 1;
			while (j < sqllen && sql[j] >= '0' && sql[j] <= '9') {
				n = n * 10 + (sql[j] - '0');
				j++;
			}
			if (n > nParams)
				nParams = n;
		}
	}

	if (nParams == 0) {
		q->nParams = 0;
		return;
	}

	q->paramValues  = (char**)kitsune_malloc(sizeof(char*) * nParams);
	if (!q->paramValues) {
		luaL_error(L, "Out of memory");
		return;
	}

	q->paramLengths = (int*)kitsune_malloc(sizeof(int) * nParams);
	if (!q->paramLengths) {
		kitsune_free(q->paramValues);
		q->paramValues = NULL;
		luaL_error(L, "Out of memory");
		return;
	}
	memset(q->paramValues,  0, sizeof(char*) * nParams);
	memset(q->paramLengths, 0, sizeof(int)   * nParams);
	q->nParams = nParams;

	for (int i = 0; i < nParams; i++) {
		lua_rawgeti(L, paramTableIdx, i + 1);
		if (lua_isnil(L, -1)) {
			q->paramValues[i]  = NULL;
			q->paramLengths[i] = 0;
			lua_pop(L, 1);
		}
		else {
			PushAsParamString(L, -1);
			size_t plen;
			const char* pval = lua_tolstring(L, -1, &plen);
			q->paramValues[i] = (char*)kitsune_malloc(plen + 1);
			if (!q->paramValues[i]) {
				lua_pop(L, 2);
				luaL_error(L, "Out of memory");
				return;
			}
			memcpy(q->paramValues[i], pval, plen);
			q->paramValues[i][plen] = '\0';
			q->paramLengths[i] = (int)plen;
			lua_pop(L, 2);
		}
	}
}

// -- FreeQuery -----------------------------------------------------------------
static void FreeQuery(lua_State* L, LuaPostgresQuery* q) {
	if (!q)
		return;

	if (q->result) {
		PQclear(q->result);
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

	if (q->cancelFnRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, q->cancelFnRef);
		q->cancelFnRef = LUA_NOREF;
	}

	if (q->paramValues) {
		for (int i = 0; i < q->nParams; i++) {
			if (q->paramValues[i])
				kitsune_free(q->paramValues[i]);
		}
		kitsune_free(q->paramValues);
		q->paramValues = NULL;
	}

	if (q->paramLengths) {
		kitsune_free(q->paramLengths);
		q->paramLengths = NULL;
	}

	if (q->sql) {
		kitsune_free(q->sql);
		q->sql = NULL;
	}

	if (q->error) {
		kitsune_free(q->error);
		q->error = NULL;
	}

	kitsune_free(q);
}

// -- lua_topostgres / lua_pushpostgres -----------------------------------------
LuaPostgres* lua_topostgres(lua_State* L, int index) {
	LuaPostgres* pg = (LuaPostgres*)lua_touserdata(L, index);
	if (!pg) {
		luaL_error(L, "parameter is not a %s", LUAPOSTGRES);
		return NULL;
	}
	return pg;
}

LuaPostgres* lua_pushpostgres(lua_State* L) {
	LuaPostgres* pg = (LuaPostgres*)lua_newuserdata(L, sizeof(LuaPostgres));
	if (!pg) {
		luaL_error(L, "Unable to create postgres connection");
		return NULL;
	}
	luaL_getmetatable(L, LUAPOSTGRES);
	lua_setmetatable(L, -2);
	memset(pg, 0, sizeof(LuaPostgres));
	pg->queryRef = LUA_NOREF;
	return pg;
}

// -- PostgresEscapeValue -------------------------------------------------------
int PostgresEscapeValue(lua_State* L) {
	LuaPostgres* pg = lua_topostgres(L, 1);
	size_t len;
	const char* str = luaL_checklstring(L, 2, &len);

	if (!pg->connection) {
		luaL_error(L, "Connection is closed");
		return 0;
	}

	char* escaped = PQescapeLiteral(pg->connection, str, len);
	if (!escaped) {
		luaL_error(L, "Failed to escape value");
		return 0;
	}

	lua_pushstring(L, escaped);
	PQfreemem(escaped);
	return 1;
}

// -- PostgresIsBusy ------------------------------------------------------------
int PostgresIsBusy(lua_State* L) {
	LuaPostgres* pg = lua_topostgres(L, 1);
	if (pg->queryRef == LUA_NOREF) {
		lua_pushboolean(L, false);
		return 1;
	}
	lua_rawgeti(L, LUA_REGISTRYINDEX, pg->queryRef);
	lua_State* T = lua_tothread(L, -1);
	lua_pop(L, 1);
	lua_pushboolean(L, T != NULL && lua_status(T) == LUA_YIELD);
	return 1;
}

// -- Connect -------------------------------------------------------------------
int PostgresConnect(lua_State* L) {
	const char* conninfo = luaL_checkstring(L, 1);
	LuaPostgres* pg = lua_pushpostgres(L);

	pg->connection = PQconnectdb(conninfo);
	if (!pg->connection) {
		lua_pushnil(L);
		lua_pushliteral(L, "PQconnectdb: out of memory");
		return 2;
	}

	if (PQstatus(pg->connection) != CONNECTION_OK) {
		const char* err = PQerrorMessage(pg->connection);
		PQfinish(pg->connection);
		pg->connection = NULL;
		lua_pushnil(L);
		lua_pushfstring(L, "Failed to connect: %s", err && err[0] ? err : "unknown error");
		return 2;
	}

	PQsetClientEncoding(pg->connection, "UTF8");
	// Put the socket into non-blocking mode so PQconsumeInput/PQflush
	// return immediately when no data is available during query polling.
	set_fd_nonblocking(PQsocket(pg->connection));
	return 1;
}

// -- Query continuation chain --------------------------------------------------
static int QueryStreamCont(lua_State* L, int status, lua_KContext ctx) {
	(void)status;
	LuaPostgresQuery* q = (LuaPostgresQuery*)(intptr_t)ctx;

	if (lua_toboolean(L, 1)) {
		FreeQuery(L, q);
		return 0;
	}

	if (!q->result) {
		FreeQuery(L, q);
		lua_pushnil(L);
		return 1;
	}

	int row = q->accumRowIdx;
	if (row >= PQntuples(q->result)) {
		FreeQuery(L, q);
		lua_pushnil(L);
		return 1;
	}
	q->accumRowIdx++;

	int nfields = PQnfields(q->result);
	lua_createtable(L, nfields, 0);
	for (int i = 0; i < nfields; i++) {
		if (PQgetisnull(q->result, row, i))
			lua_pushnil(L);
		else
			PushPostgresValue(L,
				PQgetvalue(q->result, row, i),
				PQgetlength(q->result, row, i),
				PQftype(q->result, i));
		lua_rawseti(L, -2, i + 1);
	}
	return lua_yieldk(L, 1, ctx, QueryStreamCont);
}

static int QueryPollCont(lua_State* L, int status, lua_KContext ctx) {
	(void)status;
	LuaPostgresQuery* q = (LuaPostgresQuery*)(intptr_t)ctx;

	if (lua_toboolean(L, 1)) {
		FreeQuery(L, q);
		return 0;
	}

	if (!PQconsumeInput(q->conn->connection)) {
		const char* err = PQerrorMessage(q->conn->connection);
		lua_pushstring(L, err && err[0] ? err : "PQconsumeInput error");
		return lua_yieldk(L, 1, ctx, QueryStreamCont);
	}

	if (PQisBusy(q->conn->connection)) {
		lua_pushnil(L);
		return lua_yieldk(L, 1, ctx, QueryPollCont);
	}

	// Result ready
	PGresult* result = PQgetResult(q->conn->connection);

	// Drain remaining results
	PGresult* extra;
	while ((extra = PQgetResult(q->conn->connection)) != NULL)
		PQclear(extra);

	if (!result) {
		lua_pushstring(L, "PQgetResult returned NULL");
		return lua_yieldk(L, 1, ctx, QueryStreamCont);
	}

	ExecStatusType es = PQresultStatus(result);

	if (es == PGRES_COMMAND_OK) {
		const char* t = PQcmdTuples(result);
		lua_Integer affected = (t && t[0]) ? (lua_Integer)strtoll(t, NULL, 10) : 0;
		PQclear(result);
		lua_pushinteger(L, affected);
		return lua_yieldk(L, 1, ctx, QueryStreamCont);
	}

	if (es == PGRES_TUPLES_OK) {
		q->result      = result;
		q->accumRowIdx = 0;
		lua_pushinteger(L, (lua_Integer)PQntuples(result));
		return lua_yieldk(L, 1, ctx, QueryStreamCont);
	}

	// Error
	const char* err = PQresultErrorMessage(result);
	lua_pushstring(L, err && err[0] ? err : "query error");
	PQclear(result);
	return lua_yieldk(L, 1, ctx, QueryStreamCont);
}

static int QueryFlushCont(lua_State* L, int status, lua_KContext ctx) {
	(void)status;
	LuaPostgresQuery* q = (LuaPostgresQuery*)(intptr_t)ctx;

	if (lua_toboolean(L, 1)) {
		FreeQuery(L, q);
		return 0;
	}

	int flush = PQflush(q->conn->connection);
	if (flush == 0) {
		lua_pushnil(L);
		return lua_yieldk(L, 1, ctx, QueryPollCont);
	}
	if (flush < 0) {
		const char* err = PQerrorMessage(q->conn->connection);
		lua_pushstring(L, err && err[0] ? err : "PQflush error");
		return lua_yieldk(L, 1, ctx, QueryStreamCont);
	}
	// flush == 1: more to send
	lua_pushnil(L);
	return lua_yieldk(L, 1, ctx, QueryFlushCont);
}

static int PostgresQueryBody(lua_State* L) {
	LuaPostgresQuery* q = (LuaPostgresQuery*)lua_touserdata(L, lua_upvalueindex(1));

	if (lua_toboolean(L, 1)) {
		FreeQuery(L, q);
		return 0;
	}

	int ok;
	if (q->nParams > 0) {
		ok = PQsendQueryParams(q->conn->connection, q->sql,
			q->nParams, NULL,
			(const char* const*)q->paramValues,
			q->paramLengths, NULL, 0);
	}
	else {
		ok = PQsendQuery(q->conn->connection, q->sql);
	}

	if (!ok) {
		const char* err = PQerrorMessage(q->conn->connection);
		lua_pushstring(L, err && err[0] ? err : "PQsendQuery failed");
		return lua_yieldk(L, 1, (lua_KContext)(intptr_t)q, QueryStreamCont);
	}

	lua_pushnil(L);
	return lua_yieldk(L, 1, (lua_KContext)(intptr_t)q, QueryFlushCont);
}

// -- SetupQueryCoroutine -------------------------------------------------------
static LuaPostgresQuery* SetupQueryCoroutine(lua_State* L, LuaPostgres* pg,
	int connIdx, const char* sql, size_t sqllen, int paramTableIdx) {
	LuaPostgresQuery* q = (LuaPostgresQuery*)kitsune_malloc(sizeof(LuaPostgresQuery));
	if (!q) {
		luaL_error(L, "Out of memory");
		return NULL;
	}
	memset(q, 0, sizeof(LuaPostgresQuery));
	q->conn        = pg;
	q->connRef     = LUA_NOREF;
	q->cancelFnRef = LUA_NOREF;

	lua_pushvalue(L, connIdx);
	q->connRef = luaL_ref(L, LUA_REGISTRYINDEX);

	BuildQueryParams(q, sql, sqllen, L, paramTableIdx);

	lua_State* T = lua_newthread(L);
	lua_pushlightuserdata(T, q);
	lua_pushcclosure(T, PostgresQueryBody, 1);

	pg->queryRef   = luaL_ref(L, LUA_REGISTRYINDEX);
	pg->activeQuery = q;

	lua_rawgeti(L, LUA_REGISTRYINDEX, pg->queryRef);
	return q;
}

// -- PostgresQuery -------------------------------------------------------------
int PostgresQuery(lua_State* L) {
	LuaPostgres* pg = lua_topostgres(L, 1);

	if (!pg->connection) {
		lua_pushnil(L);
		lua_pushliteral(L, "Connection is closed");
		return 2;
	}

	if (pg->queryRef != LUA_NOREF) {
		lua_pushnil(L);
		lua_pushliteral(L, "Connection already has an active query");
		return 2;
	}

	size_t sqllen;
	const char* sql = luaL_checklstring(L, 2, &sqllen);

	SetupQueryCoroutine(L, pg, 1, sql, sqllen, 3);
	return 1;
}

// -- Helper continuations ------------------------------------------------------
static int HelperStreamCont(lua_State* L, int status, lua_KContext ctx) {
	(void)status;
	LuaPostgresQuery* q = (LuaPostgresQuery*)(intptr_t)ctx;

	// Save accumTableIdx before any resume that might FreeQuery.
	int accumIdx = q->accumTableIdx;

	if (q->cancelFnRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, q->cancelFnRef);
		int cancelled = (lua_pcall_nohook(L, 0, 1, 0) == LUA_OK) && lua_toboolean(L, -1);
		lua_pop(L, 1);
		if (cancelled) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, q->conn->queryRef);
			lua_State* T = lua_tothread(L, -1);
			lua_pop(L, 1);
			if (T) {
				lua_pushboolean(T, 1);
				int nr2;
				lua_resume(T, L, 1, &nr2);
				if (nr2 > 0)
					lua_pop(T, nr2);
			}
			lua_pushboolean(L, 0);
			lua_pushliteral(L, "cancelled");
			return 2;
		}
	}

	lua_rawgeti(L, LUA_REGISTRYINDEX, q->conn->queryRef);
	lua_State* T = lua_tothread(L, -1);
	lua_pop(L, 1);

	if (!T) {
		lua_pushboolean(L, 1);
		lua_pushvalue(L, accumIdx);
		return 2;
	}

	int nr = 0;
	int rc = lua_resume(T, L, 0, &nr);

	if (rc == LUA_YIELD && nr > 0 && lua_istable(T, -1)) {
		// q is still alive — T yielded, hasn't called FreeQuery yet
		lua_xmove(T, L, 1);
		if (nr > 1)
			lua_pop(T, nr - 1);
		// Use rawlen to avoid conflict with q->accumRowIdx (stream cursor)
		int nextIdx = (int)lua_rawlen(L, accumIdx) + 1;
		lua_rawseti(L, accumIdx, nextIdx);
		return lua_yieldk(L, 0, ctx, HelperStreamCont);
	}

	// T returned nil — QueryStreamCont already called FreeQuery
	if (nr > 0)
		lua_pop(T, nr);
	lua_pushboolean(L, 1);
	lua_pushvalue(L, accumIdx);
	return 2;
}

static int HelperWaitCont(lua_State* L, int status, lua_KContext ctx) {
	(void)status;
	LuaPostgresQuery* q = (LuaPostgresQuery*)(intptr_t)ctx;

	if (q->cancelFnRef != LUA_NOREF) {
		lua_rawgeti(L, LUA_REGISTRYINDEX, q->cancelFnRef);
		int cancelled = (lua_pcall_nohook(L, 0, 1, 0) == LUA_OK) && lua_toboolean(L, -1);
		lua_pop(L, 1);
		if (cancelled) {
			lua_rawgeti(L, LUA_REGISTRYINDEX, q->conn->queryRef);
			lua_State* T = lua_tothread(L, -1);
			lua_pop(L, 1);
			if (T) {
				lua_pushboolean(T, 1);
				int nr2;
				lua_resume(T, L, 1, &nr2);
				if (nr2 > 0)
					lua_pop(T, nr2);
			}
			FreeQuery(L, q);
			lua_pushboolean(L, 0);
			lua_pushliteral(L, "cancelled");
			return 2;
		}
	}

	lua_rawgeti(L, LUA_REGISTRYINDEX, q->conn->queryRef);
	lua_State* T = lua_tothread(L, -1);
	lua_pop(L, 1);

	if (!T) {
		FreeQuery(L, q);
		lua_pushboolean(L, 0);
		lua_pushliteral(L, "connection lost");
		return 2;
	}

	int nr = 0;
	int rc = lua_resume(T, L, 0, &nr);

	if (rc != LUA_OK && rc != LUA_YIELD) {
		if (nr > 0)
			lua_pop(T, nr);
		FreeQuery(L, q);
		lua_pushboolean(L, 0);
		lua_pushliteral(L, "query coroutine error");
		return 2;
	}

	// Still polling — T yielded nil
	if (nr == 0 || lua_isnil(T, -1)) {
		if (nr > 0)
			lua_pop(T, nr);
		return lua_yieldk(L, 0, ctx, HelperWaitCont);
	}

	// Query-level error string — T is now in QueryStreamCont; stop it
	if (lua_type(T, -1) == LUA_TSTRING) {
		size_t elen;
		const char* err = lua_tolstring(T, -1, &elen);
		lua_pushlstring(L, err, elen);
		lua_pop(T, nr);
		lua_pushboolean(T, 1);
		int nr2;
		lua_resume(T, L, 1, &nr2);
		if (nr2 > 0)
			lua_pop(T, nr2);
		lua_pushboolean(L, 0);
		lua_insert(L, -2);
		return 2;
	}

	// Rowcount integer — T is now suspended in QueryStreamCont
	lua_Integer rowcount = lua_tointeger(T, -1);
	lua_pop(T, nr);

	if (q->helperMode == PG_HELPER_NONQUERY) {
		lua_pushboolean(T, 1);
		int nr2;
		lua_resume(T, L, 1, &nr2);
		if (nr2 > 0)
			lua_pop(T, nr2);
		lua_pushboolean(L, 1);
		lua_pushinteger(L, rowcount);
		return 2;
	}

	if (q->helperMode == PG_HELPER_SCALAR) {
		int nr2 = 0;
		int rc2 = lua_resume(T, L, 0, &nr2);
		if (rc2 == LUA_YIELD && nr2 > 0 && lua_istable(T, -1)) {
			lua_rawgeti(T, -1, 1);
			lua_xmove(T, L, 1);
			lua_pop(T, nr2);
			lua_pushboolean(T, 1);
			int nr3;
			lua_resume(T, L, 1, &nr3);
			if (nr3 > 0)
				lua_pop(T, nr3);
			lua_pushboolean(L, 1);
			lua_insert(L, -2);
			return 2;
		}
		if (nr2 > 0)
			lua_pop(T, nr2);
		lua_pushboolean(L, 1);
		lua_pushnil(L);
		return 2;
	}

	// PG_HELPER_QUERYALL: yield L and start streaming
	return lua_yieldk(L, 0, ctx, HelperStreamCont);
}

static int PostgresHelperRun(lua_State* L, int mode) {
	LuaPostgres* pg = lua_topostgres(L, 1);

	if (!pg->connection) {
		luaL_error(L, "Connection is closed");
		return 0;
	}

	if (pg->queryRef != LUA_NOREF) {
		lua_pushnil(L);
		lua_pushliteral(L, "Connection already has an active query");
		return 2;
	}

	size_t sqllen;
	const char* sql = luaL_checklstring(L, 2, &sqllen);

	SetupQueryCoroutine(L, pg, 1, sql, sqllen, 3);
	lua_pop(L, 1); // pop coroutine thread (anchored in pg->queryRef)

	LuaPostgresQuery* q = (LuaPostgresQuery*)pg->activeQuery;
	q->helperMode    = mode;
	q->cancelFnRef   = LUA_NOREF;
	q->accumTableIdx = 0;
	q->accumRowIdx   = 0;

	if (!lua_isnoneornil(L, 4) && lua_isfunction(L, 4)) {
		lua_pushvalue(L, 4);
		q->cancelFnRef = luaL_ref(L, LUA_REGISTRYINDEX);
	}

	if (mode == PG_HELPER_QUERYALL) {
		lua_newtable(L);
		q->accumTableIdx = lua_gettop(L);
	}

	return lua_yieldk(L, 0, (lua_KContext)(intptr_t)q, HelperWaitCont);
}

int PostgresNonQuery(lua_State* L) { return PostgresHelperRun(L, PG_HELPER_NONQUERY); }
int PostgresScalar(lua_State* L)   { return PostgresHelperRun(L, PG_HELPER_SCALAR);   }
int PostgresQueryAll(lua_State* L) { return PostgresHelperRun(L, PG_HELPER_QUERYALL); }

// -- luapostgres_gc ------------------------------------------------------------
int luapostgres_gc(lua_State* L) {
	LuaPostgres* pg = (LuaPostgres*)lua_touserdata(L, 1);
	if (!pg)
		return 0;

	if (pg->activeQuery)
		FreeQuery(L, (LuaPostgresQuery*)pg->activeQuery);

	if (pg->queryRef != LUA_NOREF) {
		luaL_unref(L, LUA_REGISTRYINDEX, pg->queryRef);
		pg->queryRef = LUA_NOREF;
	}

	if (pg->connection) {
		PQfinish(pg->connection);
		pg->connection = NULL;
	}

	if (pg->error) {
		kitsune_free(pg->error);
		pg->error = NULL;
	}

	return 0;
}

// -- luapostgres_tostring ------------------------------------------------------
int luapostgres_tostring(lua_State* L) {
	LuaPostgres* pg = lua_topostgres(L, 1);
	char buf[64];
	snprintf(buf, sizeof(buf), "Postgres: 0x%016" PRIx64, (uint64_t)(uintptr_t)pg);
	lua_pushstring(L, buf);
	return 1;
}
