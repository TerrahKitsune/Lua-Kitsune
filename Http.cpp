#include "http.h"
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include "Http.h"
#include "networking.h"
#include "HttpMain.h"

#define BUFFER_SIZE 1500

static int io_fclose(lua_State* L) {
	luaL_Stream* p = (luaL_Stream*)luaL_checkudata(L, 1, LUA_FILEHANDLE);
	int res = fclose(p->f);
	return luaL_fileresult(L, (res == 0), NULL);
}

static void _dumpbuffer(FILE* f) {

	char b[1000];
	int n;

	rewind(f);
	do {

		n = fread(b, 1, 1000, f);
		b[n] = '\0';
		puts(b);

	} while (b[0] != '\0');
}

static void DumpFile(HttpBuffer* fp) {

#if DEBUG
	return;
#endif

	FILE* f = fopen("http.txt", "wb");
	long pos = btell(fp);
	int c;
	brewind(fp);
	c = bgetc(fp);
	while (c != EOF) {
		fputc(c, f);
		c = bgetc(fp);
	}
	bseek(fp, pos, SEEK_SET);
	fflush(f);
	fclose(f);
}

char* sstrstr(char* haystack, const char* needle, size_t length)
{
	size_t needle_length = strlen(needle);
	size_t i;
	for (i = 0; i < length; i++) {
		if (i + needle_length > length) {
			return NULL;
		}
		else if (haystack[i] == needle[0] && memcmp(&haystack[i], needle, needle_length) == 0) {
			return &haystack[i];
		}
	}
	return NULL;
}

int GetUrls(const char* url, char* ip, char* page, char* proto) {

	int port = -1;
	int iResult;
	proto[0] = '\0';
	page[0] = '\0';
	ip[0] = '\0';
	iResult = sscanf(url, "%99[^:]://%99[^:]:%99d/%2048[^\n]", proto, ip, &port, page);
	if (iResult != 4) {
		iResult = sscanf(url, "%99[^:]://%99[^/]/%2048[^\n]", proto, ip, page);
		if (iResult != 3) {
			iResult = sscanf(url, "%99[^:]://%99[^:]:%i[^\n]", proto, ip, &port);
		}
	}

	if (ip[strlen(ip) - 1] == '/')
		ip[strlen(ip) - 1] = '\0';

	return port;
}

SOCKET Connect(const char* ip, int port) {

	int iResult;
	struct addrinfo* result = NULL,
		* ptr = NULL,
		hints;
	SOCKET ConnectSocket = INVALID_SOCKET;

	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	char str[15];
	sprintf(str, "%d", port);

	iResult = getaddrinfo(ip, str, &hints, &result);
	if (iResult != 0) {

		return INVALID_SOCKET;
	}

	for (ptr = result; ptr != NULL; ptr = ptr->ai_next) {

		// Create a SOCKET for connecting to server
		ConnectSocket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);

		if (ConnectSocket == INVALID_SOCKET) {

			freeaddrinfo(result);
			return INVALID_SOCKET;
		}

		// Connect to server.
		iResult = connect(ConnectSocket, ptr->ai_addr, (int)ptr->ai_addrlen);
		if (iResult == SOCKET_ERROR) {
			closesocket(ConnectSocket);
			ConnectSocket = INVALID_SOCKET;
			continue;
		}
		break;
	}

	freeaddrinfo(result);

	if (ConnectSocket == INVALID_SOCKET) {

		return INVALID_SOCKET;
	}

	return ConnectSocket;
}

bool IsBlocking(SSL* ssl, int ret) {

	if (ssl) {
		return SSL_get_error(ssl, ret) == SSL_ERROR_WANT_READ;
	}

	return ret == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK;
}

char* stristr(const char* str1, const char* str2) {
	size_t len1 = strlen(str1);
	size_t len2 = strlen(str2);
	size_t i;

	for (i = 0; i <= len1 - len2; i++) {
		if (_strnicmp(str1 + i, str2, len2) == 0) {
			return (char*)str1 + i;
		}
	}

	return NULL;
}

#define CHUNK_SIZE 1024

long GetHeaderSize(HttpBuffer* fp) {

	long pos = btell(fp);
	brewind(fp);
	int size = 0;
	char buf[5] = { 0 };
	long headerSize = -1;

	int cursor = bgetc(fp);
	while (cursor != EOF) {

		if (size < 4) {
			buf[size++] = (char)cursor;
		}
		else {
			buf[0] = buf[1];
			buf[1] = buf[2];
			buf[2] = buf[3];
			buf[3] = (char)cursor;
		}

		if (strcmp(buf, "\r\n\r\n") == 0) {
			headerSize = btell(fp) - 2;
			break;
		}

		cursor = bgetc(fp);
	}

	bseek(fp, pos, SEEK_SET);
	return headerSize;
}

long GetContentLength(HttpBuffer* fp, long headerSize) {

	long pos = btell(fp);
	char* buffer = (char*)gff_malloc(headerSize + 1);
	size_t nread;
	char* header_value;
	long content_length = -1;

	if (!buffer) {
		return -1;
	}
	else {
		buffer[headerSize] = '\0';
	}

	brewind(fp);
	if ((nread = bread(buffer, headerSize, fp)) != headerSize) {
		gff_free(buffer);
		bseek(fp, pos, SEEK_SET);
		return content_length;
	}

	char* content_length_header = stristr(buffer, "Content-Length:");

	if (!content_length_header) {

		content_length_header = stristr(buffer, "Transfer-Encoding:");

		if (content_length_header) {

			header_value = strtok(content_length_header, " ");

			if (!header_value) {
				gff_free(buffer);
				bseek(fp, pos, SEEK_SET);
				return content_length;
			}

			header_value = strtok(NULL, "\r\n");

			if (!header_value) {
				gff_free(buffer);
				bseek(fp, pos, SEEK_SET);
				return content_length;
			}

			if (_strnicmp(header_value, "chunked", 7) == 0) {
				content_length = -2;
			}

			gff_free(buffer);
			bseek(fp, pos, SEEK_SET);
			return content_length;

		}
		else {
			gff_free(buffer);
			bseek(fp, pos, SEEK_SET);
			return content_length;
		}
	}

	header_value = strtok(content_length_header, " ");

	if (!header_value) {
		gff_free(buffer);
		bseek(fp, pos, SEEK_SET);
		return content_length;
	}

	header_value = strtok(NULL, "\r\n");

	if (!header_value) {
		gff_free(buffer);
		bseek(fp, pos, SEEK_SET);
		return content_length;
	}

	content_length = atol(header_value);

	gff_free(buffer);
	bseek(fp, pos, SEEK_SET);
	return content_length;
}

int FileEndsWithCRLF(HttpBuffer* fp) {

	int found = 0;
	long pos;
	int ch1, ch2;

	pos = btell(fp);
	bseek(fp, 0, SEEK_END);
	bseek(fp, btell(fp) - 2, SEEK_SET);

	ch1 = bgetc(fp);
	ch2 = bgetc(fp);

	bseek(fp, pos, SEEK_SET);

	return ch1 == '\r' && ch2 == '\n';
}

int FileChunkedComplete(HttpBuffer* fp) {

	long pos = btell(fp);
	int result = FALSE;

	bseek(fp, 0, SEEK_END);
	bseek(fp, btell(fp) - 7, SEEK_SET);

	if (bgetc(fp) == '\r' && bgetc(fp) == '\n' &&
		bgetc(fp) == '0' &&
		bgetc(fp) == '\r' && bgetc(fp) == '\n' &&
		bgetc(fp) == '\r' && bgetc(fp) == '\n') {
		result = TRUE;
	}

	bseek(fp, pos, SEEK_SET);
	return result;
}

int CopyToFile(HttpBuffer* src, HttpBuffer* dst, int size) {

	char buf[CHUNK_SIZE + 1] = { 0 };
	size_t read;

	while (size > 0) {

		read = bread(buf, MIN(CHUNK_SIZE, size), src);

		if (read == 0) {
			return FALSE;
		}

		bwrite(buf, read, dst);

		size -= read;
	}

	return TRUE;
}

HttpBuffer* AssembleChunks(HttpBuffer* fp, long headersize) {

	char buf[CHUNK_SIZE + 1] = { 0 };
	long pos;
	char* cursor;
	size_t read;
	int chunksize;
	HttpBuffer* temp = bopen();

	brewind(fp);
	CopyToFile(fp, temp, headersize + 2);
	bseek(fp, headersize + 2, SEEK_SET);

	while (true) {

		pos = btell(fp);
		read = bread(buf, CHUNK_SIZE, fp);

		if (read == 0) {
			bclose(temp);
			return fp;
		}

		cursor = strtok(buf, "\r\n");

		if (!cursor) {
			bclose(temp);
			return fp;
		}

		chunksize = strtol(buf, NULL, 16);

		if (chunksize <= 0) {
			break;
		}

		bseek(fp, pos + strlen(cursor) + 2, SEEK_SET);

		if (!CopyToFile(fp, temp, chunksize)) {
			bclose(temp);
			return fp;
		}
		else {
			bseek(fp, btell(fp) + 2, SEEK_SET);
		}
	}

	bclose(fp);

	return temp;
}

int SendRecv(LuaHttp* luahttp, SOCKET ConnectSocket, SSL* ssl) {

	int result;
	char buffer[BUFFER_SIZE + 1] = { 0 };
	int size;
	int offset;
	u_long flag = 1;

	HttpBuffer* sendcontent = luahttp->buffer;
	brewind(sendcontent);
	ioctlsocket(ConnectSocket, FIONBIO, &flag);

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

		offset = 0;

		do {
			if (luahttp->cancel) {
				return -3;
			}

			result = ssl == NULL ? send(ConnectSocket, &buffer[offset], size, 0) : SSL_write(ssl, &buffer[offset], size);

			if (IsBlocking(ssl, result)) {

				if (luahttp->timeout != 0 && (time(NULL) - luahttp->start) >= luahttp->timeout) {
					return -2;
				}

				Sleep(10);
				continue;
			}
			else if (result > 0 && result != size) {
				offset += result;
				size -= result;
			}
			else if (result <= 0) {

				if (WSAGetLastError() == ERROR_SUCCESS) {
					break;
				}

				return -1;
			}

			luahttp->send += result;

		} while (result != size);

	} while (size > 0);

	if (luahttp->content) {
		bclose(luahttp->content);
		luahttp->content = NULL;
	}

	bclose(luahttp->buffer);
	luahttp->buffer = bopen();

	if (!luahttp->buffer) {
		return -1;
	}

	long headerSize = -1;
	long contentLength = -1;
	long sleeps = 0;

	do {

		if (luahttp->cancel) {
			return -3;
		}

		result = ssl == NULL ? recv(ConnectSocket, buffer, BUFFER_SIZE, 0) : SSL_read(ssl, buffer, BUFFER_SIZE);

		if (result > 0) {
			result = bwrite(buffer, result, luahttp->buffer);
			size += result;
			luahttp->recv += result;
			sleeps = 0;
		}
		else if (IsBlocking(ssl, result)) {

			if (luahttp->timeout != 0 && (time(NULL) - luahttp->start) >= luahttp->timeout) {
				return -2;
			}

			if (headerSize < 0) {
				headerSize = GetHeaderSize(luahttp->buffer);

				if (headerSize > 0) {
					contentLength = GetContentLength(luahttp->buffer, headerSize);
				}
			}
			else if (contentLength == -2 && FileChunkedComplete(luahttp->buffer)) {
				break;
			}
			else if (contentLength > 0 && luahttp->recv >= contentLength + headerSize + 2) {
				break;
			}
			else if (++sleeps >= 100 && FileEndsWithCRLF(luahttp->buffer)) {
				break;
			}

			Sleep(10);
			result = 1;
		}
		else if (result == SOCKET_ERROR) {

			if (WSAGetLastError() == ERROR_SUCCESS) {
				break;
			}

			return -1;
		}

	} while (result > 0);

	if (headerSize < 0) {
		headerSize = GetHeaderSize(luahttp->buffer);

		if (headerSize > 0) {
			contentLength = GetContentLength(luahttp->buffer, headerSize);
		}
	}

	if (contentLength == -2) {
		luahttp->buffer = AssembleChunks(luahttp->buffer, headerSize);
	}

	luahttp->success = true;

	return size;
}

int DoHttp(SOCKET socket, LuaHttp* http) {

	int result = SendRecv(http, socket, NULL);

	if (result == -1) {

		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, WSAGetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), http->status, STATUS_SIZE, NULL);
	}

	return result;
}

void ShutdownSSL(SSL* ssl, SSL_CTX* ctx)
{
	if (ctx)
		SSL_CTX_free(ctx);

	if (ssl) {
		SSL_shutdown(ssl);
		SSL_free(ssl);
	}
}

int DoHttps(SOCKET socket, LuaHttp* http) {

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
	SSL_set_fd(ssl, socket);
	result = SSL_connect(ssl);

	if (result != 1) {

		int err = SSL_get_error(ssl, result);

		ShutdownSSL(ssl, ctx);

		sprintf(http->status, "TLS handshake failed %d", err);
		return -1;
	}

	result = SendRecv(http, socket, ssl);

	ShutdownSSL(ssl, ctx);

	if (result == -1) {

		FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, WSAGetLastError(),
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), http->status, STATUS_SIZE, NULL);
	}

	return result;
}

int httpprocess(LuaHttp* http) {

	http->success = false;
	int result = 0;
	SOCKET socket = Connect(http->ip, http->port);

	if (socket == INVALID_SOCKET) {
		strcpy(http->status, "Failed to connect");
		return result;
	}

	result = http->ssl ? DoHttps(socket, http) : DoHttp(socket, http);

	closesocket(socket);

	if (result == -2) {
		strcpy(http->status, "Timeout");
	}
	else if (result == -3) {
		strcpy(http->status, "Cancel");
	}
	else if (result >= 0) {
		strcpy(http->status, "Finished");
	}

	return result >= 0;
}

int GetResult(lua_State* L) {

	LuaHttp* luahttp = luaL_checkhttp(L, 1);

	if (luahttp->thread == INVALID_HANDLE_VALUE) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushstring(L, "Request not alive");
		return 2;
	}

	WaitForSingleObject(luahttp->thread, INFINITE);

	lua_pop(L, lua_gettop(L));

	if (!luahttp->success) {
		lua_pushnil(L);
		if (strlen(luahttp->status) > 0) {
			lua_pushstring(L, luahttp->status);
		}
		else {
			lua_pushstring(L, "Request failed");
		}
		return 2;
	}

	bseek(luahttp->buffer, 0L, SEEK_END);
	long size = btell(luahttp->buffer);
	brewind(luahttp->buffer);

	if (luahttp->membuffer) {
		gff_free(luahttp->membuffer);
	}

	luahttp->membuffer = (char*)gff_malloc(size + 1);

	if (!luahttp->membuffer) {
		luaL_error(L, "Not enough memory");
		return 0;
	}

	luahttp->membuffer[size] = '\0';
	size = bread(luahttp->membuffer, size, luahttp->buffer);
	char* content = sstrstr(luahttp->membuffer, "\r\n\r\n", size);
	size_t headerLength = (content - luahttp->membuffer);

	if (!content) {
		luaL_error(L, "Response malformed");
		return 0;
	}
	else {
		content += 4;
	}

	char* cursor = luahttp->membuffer;
	char* end = sstrstr(cursor, "\r\n", headerLength);
	char version;
	int code;
	int offset;

	for (size_t n = 0; n < 5; n++) {
		cursor[n] = tolower(cursor[n]);
	}

	if (strncmp(cursor, "http/", 4) != 0) {
		luaL_error(L, "Response malformed");
		return 0;
	}
	else {
		cursor += 5;
	}

	if (cursor[0] != '1' || cursor[1] != '.') {
		luaL_error(L, "Response malformed");
		return 0;
	}
	else {
		cursor += 2;
	}

	version = cursor[0];

	if (version != '0' && version != '1' && cursor[1] != ' ') {
		luaL_error(L, "Invalid version");
		return 0;
	}
	else {
		cursor += 2;
	}

	if (sscanf(cursor, "%d", &code) != 1) {
		luaL_error(L, "Invalid code");
		return 0;
	}
	else {
		lua_pop(L, lua_gettop(L));
		lua_pushinteger(L, code);
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
		return 0;
	}
	else {
		cursor += offset + 1;
	}

	lua_pushlstring(L, cursor, end - cursor);

	cursor = end + 2;

	size_t contentLength = size - (content - luahttp->membuffer);

	lua_pushlstring(L, content, contentLength);

	lua_createtable(L, 0, 10);

	char* divider;

	while (cursor < (content - 4)) {

		end = sstrstr(cursor, "\r\n", (content - cursor));

		if (!end) {
			luaL_error(L, "Headers malformed");
			return 0;
		}

		divider = sstrstr(cursor, ": ", end - cursor);

		if (!divider) {
			luaL_error(L, "Headers malformed");
			return 0;
		}

		lua_pushlstring(L, cursor, divider - cursor);
		divider += 2;
		lua_pushlstring(L, divider, end - divider);
		lua_settable(L, -3);

		cursor = end + 2;
	}

	if (luahttp->membuffer) {
		gff_free(luahttp->membuffer);
		luahttp->membuffer = NULL;
	}

	return 4;
}

int StartHttp(lua_State* L) {

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

	//lua_getfield(L, -1, "Connection");
	//if (lua_type(L, -1) != LUA_TSTRING) {

	//	lua_pop(L, 1);
	//	lua_pushstring(L, "Connection");
	//	lua_pushstring(L, "close");
	//	lua_settable(L, -3);
	//}
	//else {
	//	lua_pop(L, 1);
	//}

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

	luahttp->start = time(NULL);
	luahttp->thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)httpprocess, luahttp, 0, NULL);

	return 1;
}

int GetStatus(lua_State* L) {

	LuaHttp* luahttp = luaL_checkhttp(L, 1);
	DWORD code = 0;

	if (luahttp->thread == INVALID_HANDLE_VALUE) {
		lua_pushnil(L);
		lua_pushstring(L, "Thread closed");
		return 2;
	}
	else {
		GetExitCodeThread(luahttp->thread, &code);
	}

	lua_pop(L, lua_gettop(L));

	if (code == STILL_ACTIVE) {

		lua_pushboolean(L, TRUE);
		lua_pushstring(L, "Running");
	}
	else {
		lua_pushboolean(L, FALSE);
		lua_pushstring(L, luahttp->status);
	}

	lua_pushinteger(L, time(NULL) - luahttp->start);
	lua_pushinteger(L, luahttp->recv);
	lua_pushinteger(L, luahttp->send);

	return 5;
}

int SetHttpTimeout(lua_State* L) {

	LuaHttp* luahttp = luaL_checkhttp(L, 1);
	lua_Integer timeout = luaL_optinteger(L, 2, 0);

	if (timeout < 0) {
		timeout = 0;
	}

	luahttp->timeout = (time_t)timeout;

	return 0;
}

int GetRaw(lua_State* L) {

	LuaHttp* luahttp = luaL_checkhttp(L, 1);

	if (luahttp->thread == INVALID_HANDLE_VALUE) {
		lua_pop(L, lua_gettop(L));
		lua_pushnil(L);
		lua_pushstring(L, "Request not alive");
		return 2;
	}

	WaitForSingleObject(luahttp->thread, INFINITE);

	lua_pop(L, lua_gettop(L));

	if (!luahttp->success) {
		lua_pushnil(L);
		lua_pushstring(L, "Request failed");
		return 2;
	}

	luaL_Stream* p = (luaL_Stream*)lua_newuserdata(L, sizeof(luaL_Stream));
	p->closef = &io_fclose;
	p->f = bdumptmp(luahttp->buffer);
	luahttp->buffer = NULL;

	luaL_setmetatable(L, LUA_FILEHANDLE);

	CloseHandle(luahttp->thread);
	luahttp->thread = INVALID_HANDLE_VALUE;

	return 1;
}

int WaitForFinish(lua_State* L) {

	LuaHttp* luahttp = luaL_checkhttp(L, 1);
	lua_Integer param = luaL_optinteger(L, 2, 0);
	DWORD timeout;

	if (param <= 0) {
		timeout = INFINITE;
	}
	else {
		timeout = (DWORD)param;
	}

	if (luahttp->thread == INVALID_HANDLE_VALUE) {
		lua_pushboolean(L, true);
		return 2;
	}

	DWORD result = WaitForSingleObject(luahttp->thread, timeout);

	lua_pushboolean(L, result != WAIT_TIMEOUT);

	return 1;
}

inline int ishex(int x)
{
	return	(x >= '0' && x <= '9') ||
		(x >= 'a' && x <= 'f') ||
		(x >= 'A' && x <= 'F');
}

int UrlDecode(lua_State* L) {

	size_t len;
	const char* s = lua_tolstring(L, -1, &len);
	char* o;
	const char* end = s + len;
	int c;

	char* dec = GetHttpBuffer(len + 1);

	if (!dec) {
		luaL_error(L, "Out of memory");
		return 0;
	}

	dec[len] = '\0';

	for (o = dec; s <= end; o++) {
		c = *s++;
		if (c == '+') c = ' ';
		else if (c == '%' && (!ishex(*s++) ||
			!ishex(*s++) ||
			!sscanf(s - 2, "%2x", &c)))
			return -1;

		if (dec) *o = c;
	}

	lua_pop(L, 1);
	lua_pushlstring(L, dec, o - dec);

	if (len + 1 > 1024) {
		GetHttpBuffer(0);
	}

	return 1;
}

int UrlEncode(lua_State* L) {
	size_t len;
	const char* data = lua_tolstring(L, -1, &len);

	size_t allocSize = sizeof(char) * len * 3 + 1;
	char* buffer = (char*)GetHttpBuffer(allocSize);
	if (!buffer) {
		luaL_error(L, "Not enough memory");
		return 0;
	}
	const char* hex = "0123456789abcdef";

	int pos = 0;
	for (int i = 0; i < len; i++) {
		if (('a' <= data[i] && data[i] <= 'z')
			|| ('A' <= data[i] && data[i] <= 'Z')
			|| ('0' <= data[i] && data[i] <= '9')
			|| data[i] == '-'
			|| data[i] == '_'
			|| data[i] == '.'
			|| data[i] == '~') {
			buffer[pos++] = data[i];
		}
		else {
			buffer[pos++] = '%';
			buffer[pos++] = hex[data[i] >> 4];
			buffer[pos++] = hex[data[i] & 15];
		}
	}
	buffer[pos] = '\0';

	lua_pop(L, 1);
	lua_pushlstring(L, buffer, pos);

	if (allocSize > 1024) {
		GetHttpBuffer(0);
	}

	return 1;
}

LuaHttp* lua_tohttp(lua_State* L, int index) {

	LuaHttp* luahttp = (LuaHttp*)lua_touserdata(L, index);
	if (luahttp == NULL) {
		luaL_error(L, "parameter is not a %s", LUAHTTP);
		return NULL;
	}
	return luahttp;
}

LuaHttp* luaL_checkhttp(lua_State* L, int index) {

	LuaHttp* luahttp = (LuaHttp*)luaL_checkudata(L, index, LUAHTTP);
	if (luahttp == NULL) {
		luaL_error(L, "parameter is not a %s", LUAHTTP);
		return NULL;
	}

	return luahttp;
}

LuaHttp* lua_pushhttp(lua_State* L) {

	LuaHttp* luahttp = (LuaHttp*)lua_newuserdata(L, sizeof(LuaHttp));
	if (luahttp == NULL) {
		luaL_error(L, "Unable to create http");
		return NULL;
	}

	luaL_getmetatable(L, LUAHTTP);
	lua_setmetatable(L, -2);
	memset(luahttp, 0, sizeof(LuaHttp));

	luahttp->thread = INVALID_HANDLE_VALUE;

	return luahttp;
}

int luahttp_gc(lua_State* L) {

	LuaHttp* luahttp = (LuaHttp*)lua_tohttp(L, 1);

	luahttp->cancel = true;

	if (luahttp->thread != INVALID_HANDLE_VALUE) {
		while (WaitForSingleObject(luahttp->thread, 1000) == WAIT_TIMEOUT) {
			luahttp->cancel = true;
		}

		CloseHandle(luahttp->thread);
		luahttp->thread = INVALID_HANDLE_VALUE;
	}

	if (luahttp->buffer) {

		bclose(luahttp->buffer);
		luahttp->buffer = NULL;
	}

	if (luahttp->content) {

		bclose(luahttp->content);
		luahttp->content = NULL;
	}

	if (luahttp->membuffer) {
		gff_free(luahttp->membuffer);
		luahttp->membuffer = NULL;
	}

	return 0;
}

int luahttp_tostring(lua_State* L) {

	LuaHttp* sq = lua_tohttp(L, 1);
	char my[500];
	sprintf(my, "Http: 0x%08X", (unsigned int)sq);
	lua_pushstring(L, my);
	return 1;
}

int btotempfile(HttpBuffer* buf) {

	if (buf->fp) {
		return TRUE;
	}

	FILE* fp = tmpfile();

	if (!fp) {
		return FALSE;
	}

	if (buf->buf) {
		fwrite(buf->buf, 1, buf->len, fp);
		fflush(fp);
		gff_free(buf->buf);
	}

	ZeroMemory(buf, sizeof(HttpBuffer));

	return TRUE;
}

void brewind(HttpBuffer* buf) {

	if (buf->fp) {
		rewind(buf->fp);
		return;
	}

	buf->pos = 0;
}

size_t bread(void* dest, size_t bytes, HttpBuffer* buf) {

	if (buf->fp) {
		return fread(dest, 1, bytes, buf->fp);
	}

	bytes = MIN(bytes, buf->len - buf->pos);
	memcpy(dest, &buf->buf[buf->pos], bytes);
	buf->pos += bytes;
	return bytes;
}

void bclose(HttpBuffer* buf) {

	if (buf) {

		if (buf->buf) {
			gff_free(buf->buf);
		}

		if (buf->fp) {
			fclose(buf->fp);
		}

		gff_free(buf);
	}
}

HttpBuffer* bopen() {

	HttpBuffer* buf = (HttpBuffer*)gff_malloc(sizeof(HttpBuffer));

	if (buf) {
		ZeroMemory(buf, sizeof(HttpBuffer));
	}

	return buf;
}

HttpBuffer* bopen(FILE* fp) {

	HttpBuffer* buf = bopen();

	if (buf) {
		buf->fp = fp;
	}

	return buf;
}

size_t bwrite(const void* src, size_t bytes, HttpBuffer* buf) {

	if (buf->fp) {
		size_t result = fwrite(src, 1, bytes, buf->fp);
		fflush(buf->fp);
		return result;
	}

	if (buf->pos + bytes > buf->alloc) {

		size_t new_alloc = buf->pos + bytes + 1500;

		char* new_buf = (char*)gff_realloc(buf->buf, new_alloc);

		if (new_buf == NULL) {
			btotempfile(buf);
			return bwrite(src, bytes, buf);
		}

		buf->buf = new_buf;
		buf->alloc = new_alloc;
	}

	memcpy(buf->buf + buf->pos, src, bytes);

	buf->pos += bytes;

	if (buf->pos > buf->len) {
		buf->len = buf->pos;
	}

	return bytes;
}

long btell(HttpBuffer* buf) {

	if (buf->fp) {
		return ftell(buf->fp);
	}

	return buf->pos;
}

int bgetc(HttpBuffer* buf) {

	if (buf->fp) {
		return fgetc(buf->fp);
	}

	if (buf->pos >= buf->len) {
		return EOF;
	}

	return buf->buf[buf->pos++];
}

int bseek(HttpBuffer* buf, long offset, int origin) {

	if (buf->fp) {
		return fseek(buf->fp, offset, origin);
	}

	long newpos = 0;

	switch (origin) {

	case SEEK_SET:
		newpos = offset;
		break;
	case SEEK_CUR:
		newpos = buf->pos + offset;
		break;
	case SEEK_END:
		newpos = buf->len + offset;
		break;
	default:
		return -1;
	}
	if (newpos < 0 || newpos > buf->len) {
		return -1;
	}

	buf->pos = newpos;

	return 0;
}

int bprintf(HttpBuffer* buf, const char* format, ...) {

	va_list args;

	if (buf->fp) {
		va_start(args, format);
		int result = vfprintf(buf->fp, format, args);
		va_end(args);

		fflush(buf->fp);

		return result;
	}

	va_start(args, format);
	int size_needed = vsnprintf(NULL, 0, format, args);
	va_end(args);

	if (size_needed < 0) {
		return -1;
	}

	// Expand the buffer if necessary
	if (buf->pos + size_needed >= buf->alloc) {

		size_t new_alloc = buf->pos + size_needed + 1500;

		char* new_buf = (char*)gff_realloc(buf->buf, new_alloc);
		if (new_buf == NULL) {

			btotempfile(buf);
			va_start(args, format);
			int result = vfprintf(buf->fp, format, args);
			va_end(args);
			fflush(buf->fp);

			return result;
		}

		buf->buf = new_buf;
		buf->alloc = new_alloc;
	}

	va_start(args, format);
	int result = vsnprintf(buf->buf + buf->pos, size_needed + 1, format, args);
	va_end(args);

	if (result < 0 || result > size_needed) {
		return -1;
	}

	buf->pos += result;
	if (buf->pos > buf->len) {
		buf->len = buf->pos;
	}

	return result;
}

FILE* bdumptmp(HttpBuffer* buf) {

	FILE* file = buf->fp;

	if (file) {
		buf->fp = NULL;
		bclose(buf);
		rewind(file);
		return file;
	}

	file = tmpfile();

	if (file) {
		fwrite(buf->buf, 1, buf->len, file);
		fflush(file);
		rewind(file);
		buf->fp = NULL;
	}

	bclose(buf);

	return file;
}