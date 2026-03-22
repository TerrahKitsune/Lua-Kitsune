#pragma once

#include <stddef.h>

typedef unsigned int Oid;
#define InvalidOid ((Oid) 0)

#ifdef __cplusplus
extern "C" {
#endif

#define BOOLOID      16
#define BYTEAOID     17
#define CHAROID      18
#define INT8OID      20
#define INT2OID      21
#define INT4OID      23
#define FLOAT4OID   700
#define FLOAT8OID   701
#define NUMERICOID 1700
#define TEXTOID      25
#define VARCHAROID 1043
#define BPCHAROID  1042
#define JSONOID     114
#define JSONBOID   3802

typedef enum {
	CONNECTION_OK,
	CONNECTION_BAD,
	CONNECTION_STARTED,
	CONNECTION_MADE,
	CONNECTION_AWAITING_RESPONSE,
	CONNECTION_AUTH_OK,
	CONNECTION_SETENV,
	CONNECTION_SSL_STARTUP,
	CONNECTION_NEEDED,
	CONNECTION_CHECK_WRITABLE,
	CONNECTION_CONSUME,
	CONNECTION_GSS_STARTUP,
	CONNECTION_CHECK_TARGET,
	CONNECTION_CHECK_STANDBY
} ConnStatusType;

typedef enum {
	PGRES_EMPTY_QUERY = 0,
	PGRES_COMMAND_OK,
	PGRES_TUPLES_OK,
	PGRES_COPY_OUT,
	PGRES_COPY_IN,
	PGRES_BAD_RESPONSE,
	PGRES_NONFATAL_ERROR,
	PGRES_FATAL_ERROR,
	PGRES_COPY_BOTH,
	PGRES_SINGLE_TUPLE,
	PGRES_PIPELINE_SYNC,
	PGRES_PIPELINE_ABORTED
} ExecStatusType;

typedef struct pg_conn PGconn;
typedef struct pg_result PGresult;

extern PGconn*        PQconnectdb(const char* conninfo);
extern void           PQfinish(PGconn* conn);
extern ConnStatusType PQstatus(const PGconn* conn);
extern char*          PQerrorMessage(const PGconn* conn);
extern int            PQsetClientEncoding(PGconn* conn, const char* encoding);

extern PGresult*      PQexec(PGconn* conn, const char* query);
extern PGresult*      PQexecParams(PGconn* conn, const char* command, int nParams, const Oid* paramTypes, const char* const* paramValues, const int* paramLengths, const int* paramFormats, int resultFormat);

extern ExecStatusType PQresultStatus(const PGresult* res);
extern char*          PQresultErrorMessage(const PGresult* res);
extern void           PQclear(PGresult* res);

extern int            PQntuples(const PGresult* res);
extern int            PQnfields(const PGresult* res);
extern char*          PQgetvalue(const PGresult* res, int row_number, int column_number);
extern int            PQgetisnull(const PGresult* res, int row_number, int column_number);
extern int            PQgetlength(const PGresult* res, int row_number, int column_number);
extern char*          PQfname(const PGresult* res, int field_num);
extern int            PQfnumber(const PGresult* res, const char* field_name);
extern Oid            PQftype(const PGresult* res, int field_num);

extern char*          PQescapeLiteral(PGconn* conn, const char* str, size_t length);
extern char*          PQescapeIdentifier(PGconn* conn, const char* str, size_t length);
extern size_t         PQescapeStringConn(PGconn* conn, char* to, const char* from, size_t length, int* error);
extern void           PQfreemem(void* ptr);

#ifdef __cplusplus
}
#endif
