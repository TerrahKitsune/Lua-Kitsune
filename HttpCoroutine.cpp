#include "http.h"
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include "Http.h"
#include "networking.h"
#include "HttpMain.h"

#define BUFFER_SIZE 1500
int RecvHttpRequest(lua_State* L, int status, lua_KContext ctx);
int SendHttpRequest(lua_State* L, int status, lua_KContext ctx);

void PushHttpHeader(lua_State* L) {

	LuaHttp* luahttp = lua_tohttp(L, 1);
	int headerSize = lua_tointeger(L, 3);

	lua_createtable(L, 0, 3);

	if (luahttp->membuffer) {
		gff_free(luahttp->membuffer);
	}

	luahttp->membuffer = (char*)gff_malloc(headerSize + 1);

	if (!luahttp->membuffer) {
		luaL_error(L, "Out of memory");
		return;
	}
	else {
		luahttp->membuffer[headerSize] = '\0';
		long pos = btell(luahttp->buffer);
		brewind(luahttp->buffer);
		bread(luahttp->membuffer, headerSize, luahttp->buffer);
		bseek(luahttp->buffer, pos, SEEK_SET);
	}

	char* cursor = luahttp->membuffer;
	char* end = sstrstr(cursor, "\r\n", headerSize);
	char version;
	int code;
	int offset;

	for (size_t n = 0; n < 5; n++) {
		cursor[n] = tolower(cursor[n]);
	}

	if (strncmp(cursor, "http/", 4) != 0) {
		luaL_error(L, "Response malformed");
		gff_free(luahttp->membuffer);
		luahttp->membuffer = NULL;
		return;
	}
	else {
		cursor += 5;
	}

	if (cursor[0] != '1' || cursor[1] != '.') {
		luaL_error(L, "Response malformed");
		return;
	}
	else {
		cursor += 2;
	}

	version = cursor[0];

	if (version != '0' && version != '1' && cursor[1] != ' ') {
		luaL_error(L, "Invalid version");
		return;
	}
	else {
		cursor += 2;
	}

	if (sscanf(cursor, "%d", &code) != 1) {
		luaL_error(L, "Invalid code");
		return;
	}
	else {
		lua_pushstring(L, "Code");
		lua_pushinteger(L, code);
		lua_settable(L, -3);
	}

	offset = 0;

	for (size_t n = 0; n < end - cursor; n++) {
		if (cursor[n] == ' ') {
			offset = n;
			break;
		}
	}

	if (cursor[offset] == '\0') {
		luaL_error(L, "Invalid status");
		return;
	}
	else {
		cursor += offset + 1;
	}

	lua_pushstring(L, "Status");
	lua_pushlstring(L, cursor, end - cursor);
	lua_settable(L, -3);

	cursor = end + 2;

	lua_pushstring(L, "Headers");
	lua_createtable(L, 0, 10);

	char* divider;

	while (cursor != '\0') {

		end = strstr(cursor, "\r\n");

		if (!end) {
			break;
		}

		divider = sstrstr(cursor, ": ", end - cursor);

		if (!divider) {
			luaL_error(L, "Headers malformed");
			return;
		}

		lua_pushlstring(L, cursor, divider - cursor);
		divider += 2;
		lua_pushlstring(L, divider, end - divider);
		lua_settable(L, -3);

		cursor = end + 2;
	}

	lua_settable(L, -3);

	gff_free(luahttp->membuffer);
	luahttp->membuffer = NULL;

	lua_copy(L, -1, 2);
	lua_pop(L, 1);

	HttpBuffer* temp = bopen();

	if (!temp) {
		luaL_error(L, "Out of memory");
		return;
	}

	bwrite(&luahttp->buffer->buf[headerSize + 2], luahttp->buffer->len - headerSize - 2, temp);
	bclose(luahttp->buffer);
	luahttp->buffer = temp;
}

int PushValue(lua_State* L) {

	if (lua_isnil(L, 2)) {

		lua_pushnil(L);
		return FALSE;
	}

	LuaHttp* luahttp = lua_tohttp(L, 1);
	long contentLength = lua_tointeger(L, 4);

	if (luahttp->membuffer) {
		gff_free(luahttp->membuffer);
	}

	long pos = btell(luahttp->buffer);
	bseek(luahttp->buffer, 0, SEEK_END);
	long size = btell(luahttp->buffer);
	luahttp->membuffer = (char*)gff_malloc(size + 1);

	if (!luahttp->membuffer) {

		luaL_error(L, "Out of memory");
		return FALSE;
	}

	luahttp->membuffer[size] = '\0';
	
	brewind(luahttp->buffer);
	size_t read = bread(luahttp->membuffer, size, luahttp->buffer);
	bseek(luahttp->buffer, pos, SEEK_SET);

	if (!luahttp->buffer || read <= 0) {

		lua_pushnil(L);
		return FALSE;
	}
	else if (contentLength >= -1) {

		lua_pushlstring(L, luahttp->membuffer, read);
		gff_free(luahttp->membuffer);
		luahttp->membuffer = NULL;
		bclose(luahttp->buffer);
		luahttp->buffer = bopen();
		return TRUE;
	}
	else {

		long chunksize = strtol(luahttp->membuffer, NULL, 16);

		if (chunksize == 0) {
			lua_pushnil(L);
			gff_free(luahttp->membuffer);
			luahttp->membuffer = NULL;
			return FALSE;
		}
		else if (luahttp->buffer->len < chunksize) {
			lua_pushnil(L);
			gff_free(luahttp->membuffer);
			luahttp->membuffer = NULL;
			return FALSE;
		}

		const char* cursor = sstrstr(luahttp->membuffer, "\r\n", chunksize);

		if (!cursor) {
			lua_pushnil(L);
			gff_free(luahttp->membuffer);
			luahttp->membuffer = NULL;
			return FALSE;
		}
		else {
			cursor += 2;
		}

		long offset = cursor - luahttp->membuffer + 2;
		lua_pushlstring(L, cursor, chunksize);

		HttpBuffer* temp = bopen();
		bwrite(&luahttp->membuffer[chunksize + offset], size - chunksize - offset, temp);
		bclose(luahttp->buffer);
		luahttp->buffer = temp;
		gff_free(luahttp->membuffer);
		luahttp->membuffer = NULL;

		return TRUE;
	}
}

int EndHttpRequest(lua_State* L, int status, lua_KContext ctx) {

	LuaHttp* luahttp = lua_tohttp(L, 1);

	if (luahttp->buffer) {

		lua_pushvalue(L, 2);
		if (PushValue(L)) {

			lua_yieldk(L, 2, 0, EndHttpRequest);
			return 0;
		}
	}

	lua_pop(L, lua_gettop(L) - 1);
	return luahttp_gc(L);
}

int RecvHttpRequest(lua_State* L, int status, lua_KContext ctx) {

	LuaHttp* luahttp = lua_tohttp(L, 1);
	int result;
	char buffer[BUFFER_SIZE + 1] = { 0 };

	if (luahttp->cancel) {
		lua_yieldk(L, 2, 0, EndHttpRequest);
		return 0;
	}

	if (!luahttp->buffer) {
		luahttp->buffer = bopen();

		if (!luahttp->buffer) {

			luaL_error(L, "Out of memory");
			return 0;
		}
	}

	if (lua_type(L, 3) != LUA_TNUMBER) {
		lua_pushnil(L);
		lua_pushinteger(L, -1);
		lua_pushinteger(L, -1);
		lua_pushinteger(L, 0);
	}

	long headerSize = lua_tointeger(L, 3);
	long contentLength = lua_tointeger(L, 4);
	int size = lua_tointeger(L, 5);

	if (headerSize > 0) {
		lua_pushvalue(L, 2);
		PushValue(L);

		if (!lua_isnil(L, -1)) {
			lua_yieldk(L, 2, 0, RecvHttpRequest);
			return 0;
		}

		lua_pop(L, 2);
	}

	do {
		result = luahttp->sslconnection == NULL ? recv(luahttp->socket, buffer, BUFFER_SIZE, 0) : SSL_read(luahttp->sslconnection, buffer, BUFFER_SIZE);

		if (result > 0) {
			result = bwrite(buffer, result, luahttp->buffer);
			size += result;
			lua_pop(L, 1);
			lua_pushinteger(L, size);
			luahttp->recv += result;
		}

		if (headerSize < 0) {
			headerSize = GetHeaderSize(luahttp->buffer);

			if (headerSize > 0) {

				contentLength = GetContentLength(luahttp->buffer, headerSize);

				lua_pop(L, 3);
				lua_pushinteger(L, headerSize);
				lua_pushinteger(L, contentLength);
				lua_pushinteger(L, size);

				PushHttpHeader(L);
			}
		}

		if (IsBlocking(luahttp->sslconnection, result)) {

			if (contentLength == -2 && FileChunkedComplete(luahttp->buffer)) {
				bclose(luahttp->buffer);
				luahttp->buffer = NULL;
				break;
			}
			else if (contentLength > 0 && luahttp->recv >= contentLength + headerSize + 2) {
				break;
			}

			result = 1;
		}
		else if (result == SOCKET_ERROR) {

			if (WSAGetLastError() == ERROR_SUCCESS) {
				break;
			}

			FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, WSAGetLastError(),
				MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), luahttp->status, STATUS_SIZE, NULL);

			luaL_error(L, luahttp->status);
			return 0;
		}

		if (result > 0) {
			lua_pushvalue(L, 2);
			PushValue(L);
			lua_yieldk(L, 2, 0, RecvHttpRequest);
			return 0;
		}

	} while (result > 0);

	lua_pushvalue(L, 2);
	PushValue(L);
	lua_yieldk(L, 2, 0, EndHttpRequest);

	return 0;
}

int SendHttpRequest(lua_State* L, int status, lua_KContext ctx) {

	LuaHttp* luahttp = lua_tohttp(L, 1);

	int result;
	char buffer[BUFFER_SIZE + 1] = { 0 };
	int size = 0;
	int offset = 0;
	HttpBuffer* sendcontent = luahttp->buffer;

	if (lua_type(L, 2) != LUA_TNUMBER)
	{
		lua_pushinteger(L, 0);
		lua_pushinteger(L, 0);
	}

	do {

		size = bread(buffer, BUFFER_SIZE, sendcontent);

		if (size == 0) {

			if (!luahttp->content) {
				break;
			}

			sendcontent = luahttp->content;
			size = bread(buffer, BUFFER_SIZE, sendcontent);

			if (size == 0) {
				break;
			}
		}

		lua_pop(L, 2);
		lua_pushinteger(L, offset);
		lua_pushinteger(L, size);

		do {

			result = luahttp->sslconnection == NULL ? send(luahttp->socket, &buffer[offset], size, 0) : SSL_write(luahttp->sslconnection, &buffer[offset], size);

			if (IsBlocking(luahttp->sslconnection, result)) {

				lua_yieldk(L, 0, 0, SendHttpRequest);
				continue;
			}
			else if (result > 0 && result != size) {
				offset += result;
				size -= result;

				lua_pop(L, 2);
				lua_pushinteger(L, offset);
				lua_pushinteger(L, size);
			}
			else if (result <= 0) {

				if (WSAGetLastError() == ERROR_SUCCESS) {
					break;
				}

				FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, WSAGetLastError(),
					MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), luahttp->status, STATUS_SIZE, NULL);

				luaL_error(L, luahttp->status);
				return 0;
			}

			luahttp->send += result;

		} while (result != size);

	} while (size > 0);

	if (luahttp->content) {
		bclose(luahttp->content);
		luahttp->content = NULL;
	}

	bclose(luahttp->buffer);
	luahttp->buffer = NULL;

	lua_pop(L, 2);

	lua_yieldk(L, 0, 0, RecvHttpRequest);

	return 0;
}

int ConnectHttpRequest(lua_State* L, int status, lua_KContext ctx) {

	LuaHttp* http = lua_tohttp(L, 1);

	http->success = false;
	int result = 0;
	http->socket = Connect(http->ip, http->port);

	if (http->socket == INVALID_SOCKET) {
		strcpy(http->status, "Failed to connect");
		luaL_error(L, http->status);
		return 0;
	}

	if (http->ssl) {

		SSL_CTX* ctx;
		SSL* ssl;
		int result;
		int total = 0;

		ctx = SSL_CTX_new(TLS_client_method());
		if (ctx == NULL) {
			strcpy(http->status, "Unable to open ctx client method");
			return 0;
		}

		ssl = SSL_new(ctx);
		SSL_set_fd(ssl, http->socket);
		result = SSL_connect(ssl);

		if (result != 1) {

			int err = SSL_get_error(ssl, result);

			ShutdownSSL(ssl, ctx);

			sprintf(http->status, "TLS handshake failed %d", err);
			luaL_error(L, http->status);
			return 0;
		}
	}

	brewind(http->buffer);
	u_long flag = 1;
	ioctlsocket(http->socket, FIONBIO, &flag);

	lua_yieldk(L, 0, 0, SendHttpRequest);

	return 0;
}

int HttpStartRequest(lua_State* L) {
	return lua_yieldk(L, 0, 0, ConnectHttpRequest);
}

int StartCoroutineHttp(lua_State* L) {

	char ip[IP_ADDR_SIZE];
	char page[2050];
	char proto[100];
	const char* method = luaL_checkstring(L, 1);
	int port = GetUrls(luaL_checkstring(L, 2), ip, page, proto);
	bool ssl = _stricmp(proto, "https") == 0;
	const char* key;
	const char* content;
	size_t len;
	luaL_Stream* p = NULL;
	int useHttp0 = lua_toboolean(L, 5);

	lua_settop(L, 4);

	if (lua_type(L, 4) != LUA_TTABLE) {
		lua_createtable(L, 0, 5);
		lua_copy(L, -1, 4);
		lua_pop(L, 1);
	}

	if (port <= 0) {
		if (ssl)
			port = 443;
		else
			port = 80;
	}

	if (lua_type(L, 3) == LUA_TUSERDATA && luaL_testudata(L, 3, LUA_FILEHANDLE)) {

		p = (luaL_Stream*)lua_touserdata(L, 3);

		if (p->f) {
			fseek(p->f, 0L, SEEK_END);
			len = (size_t)ftell(p->f);
			rewind(p->f);
		}
		else {
			len = 0;
		}

		content = NULL;
	}
	else {
		content = lua_tolstring(L, 3, &len);
	}

	lua_getfield(L, -1, "Host");
	if (lua_type(L, -1) != LUA_TSTRING) {

		lua_pop(L, 1);
		lua_pushstring(L, "Host");
		lua_pushstring(L, ip);
		lua_settable(L, -3);
	}
	else {
		lua_pop(L, 1);
	}

	lua_getfield(L, -1, "Content-Length");
	if (lua_type(L, -1) != LUA_TSTRING) {

		lua_pop(L, 1);
		lua_pushstring(L, "Content-Length");
		lua_pushfstring(L, "%d", len);
		lua_settable(L, -3);
	}
	else {
		lua_pop(L, 1);
	}

	lua_getfield(L, -1, "Content-Type");
	if (lua_type(L, -1) != LUA_TSTRING) {

		lua_pop(L, 1);
		lua_pushstring(L, "Content-Type");
		lua_pushstring(L, "text/plain");
		lua_settable(L, -3);
	}
	else {
		lua_pop(L, 1);
	}

	LuaHttp* luahttp = lua_pushhttp(L);

	luahttp->ssl = ssl;
	luahttp->buffer = bopen();

	if (p) {
		luahttp->content = bopen(p->f);
		p->f = NULL;
		p->closef = NULL;
	}

	if (!luahttp->buffer) {
		lua_pushnil(L);
		lua_pushstring(L, "Unable to open tempfle");
		return 2;
	}

	memcpy(luahttp->ip, ip, IP_ADDR_SIZE);
	luahttp->port = port;

	if (useHttp0) {
		bprintf(luahttp->buffer, "%s /%s HTTP/1.0\r\n", method, page);
	}
	else {
		bprintf(luahttp->buffer, "%s /%s HTTP/1.1\r\n", method, page);
	}
	lua_pushvalue(L, 4);
	lua_pushnil(L);

	while (lua_next(L, -2) != 0) {
		key = lua_tostring(L, -2);
		bprintf(luahttp->buffer, "%s: %s\r\n", key, lua_tostring(L, -1));
		lua_pop(L, 1);
	}
	bwrite("\r\n", 2, luahttp->buffer);

	lua_pop(L, 1);

	if (content && len > 0) {
		bwrite(content, len, luahttp->buffer);
	}

	lua_State* T = lua_newthread(L);
	lua_pushvalue(L, -2);
	lua_xmove(L, T, 1);

	lua_pushcfunction(T, HttpStartRequest);

	lua_pushvalue(L, -2);
	lua_xmove(L, T, 1);
	lua_resume(T, L, 1);

	lua_pushvalue(L, -2);
	return 2;
}