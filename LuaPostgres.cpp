#include "LuaPostgres.h"
#include "luawchar.h"
#pragma comment(lib, "postgres/lib/libpq.lib")


static void PushPostgresValue(lua_State* L, const char* val, int len, Oid type);

static int JsonRef = LUA_NOREF;

static void PushAsParamString(lua_State* L, int index) {

	if (index < 0) index = lua_gettop(L) + index + 1;

	if (lua_istable(L, index)) {

		if (JsonRef == LUA_NOREF) {
			lua_getglobal(L, "Json");
			lua_pushliteral(L, "Create");
			lua_gettable(L, -2);
			if (lua_pcall(L, 0, 1, 0)) {
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

		if (lua_pcall(L, 2, 1, 0)) {
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

LuaPostgres* lua_topostgres(lua_State* L, int index) {

	LuaPostgres* pg = (LuaPostgres*)luaL_checkudata(L, index, LUAPOSTGRES);
	if (pg == NULL) {
		luaL_error(L, "parameter is not a %s", LUAPOSTGRES);
		return NULL;
	}
	return pg;
}

LuaPostgres* lua_pushpostgres(lua_State* L) {

	LuaPostgres* pg = (LuaPostgres*)lua_newuserdata(L, sizeof(LuaPostgres));
	if (pg == NULL) {
		luaL_error(L, "Unable to create postgres connection");
		return NULL;
	}

	luaL_getmetatable(L, LUAPOSTGRES);
	lua_setmetatable(L, -2);
	memset(pg, 0, sizeof(LuaPostgres));

	return pg;
}

static void FreeParams(LuaPostgres* pg) {

	if (pg->paramValues) {
		for (int i = 0; i < pg->nParams; i++) {
			if (pg->paramValues[i]) {
				gff_free(pg->paramValues[i]);
			}
		}
		gff_free(pg->paramValues);
		pg->paramValues = NULL;
	}

	if (pg->paramLengths) {
		gff_free(pg->paramLengths);
		pg->paramLengths = NULL;
	}

	pg->nParams = 0;
}

static void SetDone(LuaPostgres* pg, const char* error, PGresult* result) {

	if (pg->query) {
		gff_free(pg->query);
	}
	pg->query = NULL;
	pg->querylen = 0;
	FreeParams(pg);
	pg->isParamQuery = false;
	pg->currentRow = -1;

	if (pg->result) {
		PQclear(pg->result);
		pg->result = NULL;
	}

	pg->result = result;

	if (error) {
		if (pg->error) {
			gff_free(pg->error);
		}

		pg->error = (char*)gff_malloc(strlen(error) + 1);
		if (pg->error) {
			strcpy(pg->error, error);
		}
	}
	else {
		if (pg->error) {
			gff_free(pg->error);
			pg->error = NULL;
		}
	}

	pg->busy = false;
}

DWORD WINAPI PostgresQueryThread(LPVOID param) {

	LuaPostgres* pg = (LuaPostgres*)param;

	while (pg->alive) {

		if (pg->busy) {

			PGresult* result = NULL;

			if (pg->isParamQuery) {
				result = PQexecParams(pg->connection, pg->query, pg->nParams, NULL,
					(const char* const*)pg->paramValues, pg->paramLengths, NULL, 0);
			}
			else {
				result = PQexec(pg->connection, pg->query);
			}

			if (!result) {
				SetDone(pg, PQerrorMessage(pg->connection), NULL);
				continue;
			}

			ExecStatusType status = PQresultStatus(result);
			if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
				SetDone(pg, PQresultErrorMessage(result), NULL);
				PQclear(result);
				continue;
			}

			SetDone(pg, NULL, result);
		}

		WaitForSingleObject(pg->interrupt, INFINITE);
		ResetEvent(pg->interrupt);
	}

	return 0;
}

int PostgresIsBusy(lua_State* L) {

	LuaPostgres* pg = lua_topostgres(L, 1);
	lua_pushboolean(L, pg->busy);
	return 1;
}

int PostgresConnect(lua_State* L) {

	const char* conninfo = luaL_checkstring(L, 1);

	LuaPostgres* pg = lua_pushpostgres(L);
	pg->connection = PQconnectdb(conninfo);

	if (!pg->connection) {
		luaL_error(L, "Failed to allocate postgres connection");
		return 0;
	}

	if (PQstatus(pg->connection) != CONNECTION_OK) {
		lua_pushfstring(L, "Failed to connect: %s", PQerrorMessage(pg->connection));
		lua_error(L);
		return 0;
	}

	PQsetClientEncoding(pg->connection, "UTF8");

	pg->alive = true;

	pg->interrupt = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!pg->interrupt) {
		pg->alive = false;
		luaL_error(L, "Failed to create event for postgres connection");
		return 0;
	}

	pg->thread = CreateThread(NULL, 0, PostgresQueryThread, pg, 0, NULL);
	if (!pg->thread) {
		pg->alive = false;
		luaL_error(L, "Failed to create thread for postgres connection");
		return 0;
	}

	return 1;
}

int PostgresQuery(lua_State* L) {

	LuaPostgres* pg = lua_topostgres(L, 1);
	size_t qlen;
	const char* query = luaL_checklstring(L, 2, &qlen);

	if (!pg->connection) {
		luaL_error(L, "Connection is closed");
		return 0;
	}
	else if (pg->busy) {
		lua_pushboolean(L, false);
		lua_pushstring(L, "Busy");
		return 2;
	}

	if (pg->result) {
		PQclear(pg->result);
		pg->result = NULL;
	}

	if (pg->error) {
		gff_free(pg->error);
		pg->error = NULL;
	}

	if (pg->query) {
		gff_free(pg->query);
		pg->query = NULL;
		pg->querylen = 0;
	}

	FreeParams(pg);

	pg->query = (char*)gff_malloc(qlen + 1);
	if (!pg->query) {
		luaL_error(L, "Out of memory");
		return 0;
	}

	memcpy(pg->query, query, qlen);
	pg->query[qlen] = '\0';
	pg->querylen = qlen;
	pg->currentRow = -1;

	if (lua_istable(L, 3)) {

		int nParams = 0;
		for (size_t k = 0; k + 1 < qlen; k++) {
			if (query[k] == '$' && query[k + 1] >= '1' && query[k + 1] <= '9') {
				int n = 0;
				size_t j = k + 1;
				while (j < qlen && query[j] >= '0' && query[j] <= '9') {
					n = n * 10 + (query[j] - '0');
					j++;
				}
				if (n > nParams) nParams = n;
			}
		}

		if (nParams > 0) {

			pg->paramValues = (char**)gff_malloc(sizeof(char*) * nParams);
			pg->paramLengths = (int*)gff_malloc(sizeof(int) * nParams);

			if (!pg->paramValues || !pg->paramLengths) {
				luaL_error(L, "Out of memory");
				return 0;
			}

			memset(pg->paramValues, 0, sizeof(char*) * nParams);
			memset(pg->paramLengths, 0, sizeof(int) * nParams);
			pg->nParams = nParams;

			for (int i = 0; i < nParams; i++) {

				lua_rawgeti(L, 3, i + 1);

				if (lua_isnil(L, -1)) {
					pg->paramValues[i] = NULL;
					pg->paramLengths[i] = 0;
					lua_pop(L, 1);
				}
				else {
					PushAsParamString(L, -1);

					size_t plen;
					const char* pval = lua_tolstring(L, -1, &plen);

					pg->paramValues[i] = (char*)gff_malloc(plen + 1);
					if (!pg->paramValues[i]) {
						lua_pop(L, 2);
						luaL_error(L, "Out of memory");
						return 0;
					}

					memcpy(pg->paramValues[i], pval, plen);
					pg->paramValues[i][plen] = '\0';
					pg->paramLengths[i] = (int)plen;
					lua_pop(L, 2);
				}
			}

			pg->isParamQuery = true;
		}
	}

	pg->busy = true;
	SetEvent(pg->interrupt);

	lua_pushboolean(L, true);
	return 1;
}

int PostgresFetch(lua_State* L) {

	LuaPostgres* pg = lua_topostgres(L, 1);

	if (!pg->alive) {
		luaL_error(L, "Connection is closed");
		return 0;
	}

	while (pg->busy) {
		Sleep(1);
	}

	if (pg->error) {
		lua_pushboolean(L, false);
		lua_pushstring(L, pg->error);
		return 2;
	}

	if (!pg->result) {
		lua_pushboolean(L, false);
		return 1;
	}

	int ntuples = PQntuples(pg->result);

	if (pg->currentRow == -1) {

		if (ntuples == 0) {
			PQclear(pg->result);
			pg->result = NULL;
			lua_pushboolean(L, false);
			return 1;
		}

		pg->currentRow = 0;
		lua_pushboolean(L, true);
		return 1;
	}

	pg->currentRow++;

	if (pg->currentRow < ntuples) {
		lua_pushboolean(L, true);
		return 1;
	}

	PQclear(pg->result);
	pg->result = NULL;
	pg->currentRow = -1;
	lua_pushboolean(L, false);
	return 1;
}

int PostgresGetRow(lua_State* L) {

	LuaPostgres* pg = lua_topostgres(L, 1);

	if (!pg->alive) {
		luaL_error(L, "Connection is closed");
		return 0;
	}

	if (!pg->result || pg->currentRow < 0) {
		lua_pushnil(L);
		return 1;
	}

	int nfields = PQnfields(pg->result);

	if (lua_type(L, 2) == LUA_TSTRING) {

		int col = PQfnumber(pg->result, lua_tostring(L, 2));
		if (col < 0) {
			lua_pushnil(L);
			return 1;
		}

		if (PQgetisnull(pg->result, pg->currentRow, col)) {
			lua_pushnil(L);
		}
		else {
			PushPostgresValue(L, PQgetvalue(pg->result, pg->currentRow, col),
			PQgetlength(pg->result, pg->currentRow, col),
			PQftype(pg->result, col));
		}
		return 1;
	}

	int idx = (int)luaL_optinteger(L, 2, -1);

	if (idx > 0) {

		idx--;
		if (idx >= nfields) {
			lua_pushnil(L);
			return 1;
		}

		if (PQgetisnull(pg->result, pg->currentRow, idx)) {
			lua_pushnil(L);
		}
		else {
			PushPostgresValue(L, PQgetvalue(pg->result, pg->currentRow, idx),
				PQgetlength(pg->result, pg->currentRow, idx),
				PQftype(pg->result, idx));
		}
		return 1;
	}

	lua_createtable(L, 0, nfields);

	for (int i = 0; i < nfields; i++) {

		lua_pushstring(L, PQfname(pg->result, i));

		if (PQgetisnull(pg->result, pg->currentRow, i)) {
			lua_pushnil(L);
		}
		else {
			PushPostgresValue(L, PQgetvalue(pg->result, pg->currentRow, i),
				PQgetlength(pg->result, pg->currentRow, i),
				PQftype(pg->result, i));
		}

		lua_settable(L, -3);
	}

	return 1;
}

int PostgresFinish(lua_State* L) {

	LuaPostgres* pg = lua_topostgres(L, 1);

	while (pg->busy) {
		Sleep(1);
	}

	if (pg->result) {
		PQclear(pg->result);
		pg->result = NULL;
	}

	if (pg->error) {
		gff_free(pg->error);
		pg->error = NULL;
	}

	pg->currentRow = -1;

	return 0;
}

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
	case NUMERICOID:
		lua_pushnumber(L, strtod(val, &endptr));
		break;
	default:
		lua_pushlstring(L, val, len);
		break;
	}
}

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

int luapostgres_gc(lua_State* L) {

	LuaPostgres* pg = lua_topostgres(L, 1);

	pg->alive = false;

	if (pg->thread) {
		SetEvent(pg->interrupt);
		WaitForSingleObject(pg->thread, INFINITE);
		CloseHandle(pg->thread);
		pg->thread = NULL;
	}

	if (pg->interrupt) {
		CloseHandle(pg->interrupt);
		pg->interrupt = NULL;
	}

	if (pg->result) {
		PQclear(pg->result);
		pg->result = NULL;
	}

	if (pg->connection) {
		PQfinish(pg->connection);
		pg->connection = NULL;
	}

	if (pg->query) {
		gff_free(pg->query);
		pg->query = NULL;
		pg->querylen = 0;
	}

	if (pg->error) {
		gff_free(pg->error);
		pg->error = NULL;
	}

	FreeParams(pg);

	return 0;
}

int luapostgres_tostring(lua_State* L) {

	LuaPostgres* pg = lua_topostgres(L, 1);
	char buf[1024];
	sprintf(buf, "Postgres: 0x%016llX", (DWORD64)pg);
	lua_pushstring(L, buf);
	return 1;
}