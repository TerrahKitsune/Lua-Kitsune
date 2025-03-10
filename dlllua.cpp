#include <windows.h>
#include "objbase.h"
#include "dlllua.h"
#include "LuaArchiveMain.h"
#include "mem.h"
#include "StreamMain.h"
#include "wcharmain.h"
#include "luajsonmain.h"
#include "LuaAesMain.h"
#include "LuaCsvMain.h"
#include "LuaFileSystemMain.h"
#include "LuaFTPMain.h"
#include "HttpMain.h"
#include "luakafkamain.h"
#include "MD5Main.h"
#include "LuaMutexMain.h"
#include "MySQLMain.h"
#include "ODBCMain.h"
#include "RedisMain.h"
#include "LuaSQLiteMain.h"
#include "Sha256Main.h"
#include "TimerMain.h"
#include "NamedPipeMain.h"
#include "base64.h"
//#include "LuaBinaryTreeMain.h"
#include "OpenSSL/include/openssl/ssl.h"

lua_State* OpenLuaState(lua_Alloc memoryAllocator) {

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		return NULL;
	}

	SSL_load_error_strings();
	SSL_library_init();
	OpenSSL_add_all_algorithms();

	lua_State* L = lua_newstate(memoryAllocator, NULL);
	luaL_openlibs(L);

	luaopen_archive(L);
	lua_setglobal(L, "Archive");
	luaopen_wchar(L);
	lua_setglobal(L, "Wchar");
	luaopen_stream(L);
	lua_setglobal(L, "Stream");
	luaopen_json(L);
	lua_setglobal(L, "Json");
	luaopen_luaaes(L);
	lua_setglobal(L, "Aes");
	luaopen_csv(L);
	lua_setglobal(L, "CSV");
	luaopen_filesystem(L);
	lua_setglobal(L, "FileSystem");
	luaopen_ftp(L);
	lua_setglobal(L, "FTP");
	luaopen_http(L);
	lua_setglobal(L, "Http");
	luaopen_kafka(L);
	lua_setglobal(L, "Kafka");
	luaopen_md5(L);
	lua_setglobal(L, "MD5");
	luaopen_odbc(L);
	lua_setglobal(L, "ODBC");
	luaopen_mutex(L);
	lua_setglobal(L, "Mutex");
	luaopen_mysql(L);
	lua_setglobal(L, "MySQL");
	luaopen_sqlite(L);
	lua_setglobal(L, "SQLite");
	luaopen_redis(L);
	lua_setglobal(L, "Redis");
	luaopen_sha256(L);
	lua_setglobal(L, "SHA256");
	luaopen_timer(L);
	lua_setglobal(L, "Timer");
	luaopen_namedpipe(L);
	lua_setglobal(L, "Pipe");
	luaopen_base64(L);
	lua_setglobal(L, "Base64");
	//luaopen_binarytree(L);
	//lua_setglobal(L, "BinaryTree");

	return L;
}