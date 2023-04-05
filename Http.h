#pragma once
#include "lua_main_incl.h"
#include "networking.h"

#define IP_ADDR_SIZE 255
#define STATUS_SIZE 255

static const char* LUAHTTP = "LuaHTTP";

typedef struct HttpBuffer {

	char* buf;
	size_t alloc;
	size_t len;
	size_t pos;

	FILE* fp;

} HttpBuffer;

HttpBuffer* bopen();
HttpBuffer* bopen(FILE* fp);
void bclose(HttpBuffer* buf);
void brewind(HttpBuffer* buf);
size_t bread(void* dest, size_t bytes, HttpBuffer* buf);
size_t bwrite(const void* src, size_t bytes, HttpBuffer* buf);
long btell(HttpBuffer* buf);
int bgetc(HttpBuffer* buf);
int bseek(HttpBuffer* buf, long offset, int origin);
int bprintf(HttpBuffer* buf, const char* format, ...);
FILE* bdumptmp(HttpBuffer* buf);

typedef struct LuaHttp {

	HttpBuffer* content;
	HttpBuffer* buffer;
	HANDLE thread;

	char* membuffer;

	time_t start;
	time_t timeout;

	unsigned long recv;
	unsigned long send;

	bool success;
	bool cancel;
	bool ssl;
	int port;
	char ip[IP_ADDR_SIZE];
	char status[STATUS_SIZE];

} LuaHttp;

int StartHttp(lua_State* L);
int GetStatus(lua_State* L);
int GetResult(lua_State* L);
int SetHttpTimeout(lua_State* L);
int GetRaw(lua_State* L);
int WaitForFinish(lua_State* L);
int UrlEncode(lua_State* L);
int UrlDecode(lua_State* L);

LuaHttp* lua_tohttp(lua_State* L, int index);
LuaHttp* luaL_checkhttp(lua_State* L, int index);
LuaHttp* lua_pushhttp(lua_State* L);

int luahttp_gc(lua_State* L);
int luahttp_tostring(lua_State* L);