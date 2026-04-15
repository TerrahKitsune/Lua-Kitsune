cmake_minimum_required(VERSION 3.20)
project(LuaKitsune CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

# ── Compiler flags ────────────────────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
	add_compile_options(-Wall -Wno-unused-function -Wno-unused-variable)
endif()

# ── Lua core static library ───────────────────────────────────────────────────
# Compile all Lua C files into a static lib that is linked into KitsuneEngine.
# lua.c (standalone interpreter) and luac.c (compiler) are not present in this
# repo so no exclusion is needed.
file(GLOB LUA_SOURCES "${CMAKE_SOURCE_DIR}/Lua/*.c")
add_library(lua_core STATIC ${LUA_SOURCES})

target_compile_definitions(lua_core PRIVATE
	LUA_CORE          # compiling the Lua core itself (exposes private headers)
)
target_include_directories(lua_core PUBLIC "${CMAKE_SOURCE_DIR}/Lua")

# Lua objects are linked into KitsuneEngine.so — must be position-independent.
set_target_properties(lua_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ── KitsuneEngine shared library ─────────────────────────────────────────────
# Barebones build: external-dependency modules (MySQL, PostgreSQL, Redis, ImGui,
# etc.) are compiled out via KITSUNE_BAREBONES.  Only in-memory modules and the
# core scheduler are compiled.
set(KITSUNE_ENGINE_SOURCES
	# ── Core scheduler ─────────────────────────────────────────────────────
	KitsuneEngine.cpp
	kitsuneuserdata.cpp
	mem.cpp
	LuaEngineBuiltins.cpp
	lua_misc.cpp

	# ── Hashing / encoding ─────────────────────────────────────────────────
	base64.cpp
	LuaMD5.cpp
	MD5Main.cpp
	md5.c
	LuaSHA1.cpp
	SHA1Main.cpp
	sha1.c
	LuaSha256.cpp
	Sha256Main.cpp
	sha256.c
	Bencode.cpp
	MemoryManager.cpp

	# ── JSON ───────────────────────────────────────────────────────────────
	luajson.cpp
	luajsonmain.cpp

	# ── WChar (wide-string type) ────────────────────────────────────────────
	luawchar.cpp
	wcharmain.cpp

	# ── Identifier (UUID/OID type) ───────────────────────────────────────────
	luaidentifier.cpp
	identifiermain.cpp

	# ── DateTime (UTC/offset date-time type) ─────────────────────────────────
	luadatetime.cpp
	datetimemain.cpp

	# ── Decimal (128-bit fixed-point decimal type) ───────────────────────────
	luadecimal.cpp
	decimalmain.cpp

	# ── CSV ────────────────────────────────────────────────────────────────
	luacsv.cpp
	LuaCsvMain.cpp

	# ── Stream infrastructure (used by the variable bridge) ─────────────────
	stream.cpp
	streamshmemory.cpp
	streammemory.cpp
	streamfile.cpp
	StreamMain.cpp

	# ── Compression (miniz, bundled — same library on all platforms) ──────────────────
	miniz.c

	# ── AES encryption ────────────────────────────────────────────────────────────────
	luaaes.cpp
	LuaAesMain.cpp
	aes.c

	# ── Timer (std::chrono::steady_clock — same on all platforms) ──────────────────────
	Timer.cpp
	TimerMain.cpp

	# ── FileSystem ─────────────────────────────────────────────────────────────────────
	LuaFileSystem.cpp
	LuaFileSystemMain.cpp

	# ── Mutex (POSIX named semaphores on Linux) ────────────────────────────────────────
	LuaMutex.cpp
	LuaMutexMain.cpp

	# ── SQLite (bundled amalgamation) ─────────────────────────────────────────────────
	LuaSQLite.cpp
	LuaSQLiteMain.cpp
	SQLite/sqlite3.c

	# ── Process management (cross-platform: POSIX fork/exec on Linux) ─────────────────
	LuaProcess.cpp
	ProcessMain.cpp
)

# On Linux, hide all bundled SQLite symbols from the global dynamic-symbol table so
# they cannot clash with any system libsqlite3 that dotnet or a test dependency loads.
# PLT resolution to the wrong SQLite copy would cause allocator mismatches and
# "free(): invalid pointer" crashes.  SQLITE_API is checked in sqlite3.h and is
# only set there if it is not already defined, so defining it here wins cleanly.
if(NOT WIN32)
	set(_sqlite_hidden "SQLITE_API=__attribute__((visibility(\"hidden\")))")
	set_source_files_properties(
		SQLite/sqlite3.c LuaSQLite.cpp LuaSQLiteMain.cpp
		PROPERTIES COMPILE_DEFINITIONS "${_sqlite_hidden}")
endif()

add_library(KitsuneEngine SHARED ${KITSUNE_ENGINE_SOURCES})

# ── KITSUNE_ALL umbrella option ───────────────────────────────────────────────
# Mirrors the Windows KITSUNE_ALL preprocessor flag: enables every optional
# module in a single switch and passes -DKITSUNE_ALL to the compiler so that
# platform.h's cascade activates identically to the Windows vcxproj build.
option(KITSUNE_ALL "Enable all optional modules (MySQL, PostgreSQL, …)" OFF)

if(KITSUNE_ALL)
	set(KITSUNE_MYSQL    ON CACHE BOOL "Enabled by KITSUNE_ALL" FORCE)
	set(KITSUNE_POSTGRES ON CACHE BOOL "Enabled by KITSUNE_ALL" FORCE)
	set(KITSUNE_KAFKA    ON CACHE BOOL "Enabled by KITSUNE_ALL" FORCE)
	set(KITSUNE_ARCHIVE  ON CACHE BOOL "Enabled by KITSUNE_ALL" FORCE)
	set(KITSUNE_REDIS    ON CACHE BOOL "Enabled by KITSUNE_ALL" FORCE)
	set(KITSUNE_HTTP     ON CACHE BOOL "Enabled by KITSUNE_ALL" FORCE)
	target_compile_definitions(KitsuneEngine PRIVATE KITSUNE_ALL)
endif()

# ── Optional MySQL support ────────────────────────────────────────────────────
option(KITSUNE_MYSQL "Build with MySQL support (requires libmysqlclient >= 8.0)" OFF)

if(KITSUNE_MYSQL)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(MYSQLCLIENT REQUIRED mysqlclient>=8.0)

	target_sources(KitsuneEngine PRIVATE LuaMySQL.cpp MySQLMain.cpp)

	target_compile_definitions(KitsuneEngine PRIVATE KITSUNE_MYSQL)
	target_include_directories(KitsuneEngine PRIVATE ${MYSQLCLIENT_INCLUDE_DIRS})
	target_link_libraries(KitsuneEngine PRIVATE ${MYSQLCLIENT_LINK_LIBRARIES})
endif()

# ── Optional HTTP + WebSocket support (libcurl) ───────────────────────────────
option(KITSUNE_HTTP "Build with HTTP/WebSocket support (requires libcurl >= 7.86.0 with --enable-websockets)" OFF)

if(KITSUNE_HTTP)
	find_package(CURL 7.86.0 REQUIRED)
	target_sources(KitsuneEngine PRIVATE HttpCurl.cpp HttpCurlMain.cpp)
	target_compile_definitions(KitsuneEngine PRIVATE KITSUNE_HTTP)
	target_link_libraries(KitsuneEngine PRIVATE CURL::libcurl)
endif()

# ── Optional PostgreSQL support ───────────────────────────────────────────────
option(KITSUNE_POSTGRES "Build with PostgreSQL support (requires libpq)" OFF)

if(KITSUNE_POSTGRES)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(LIBPQ REQUIRED libpq)

	target_sources(KitsuneEngine PRIVATE LuaPostgres.cpp PostgresMain.cpp)

	target_compile_definitions(KitsuneEngine PRIVATE KITSUNE_POSTGRES)
	target_include_directories(KitsuneEngine PRIVATE ${LIBPQ_INCLUDE_DIRS})
	target_link_libraries(KitsuneEngine PRIVATE ${LIBPQ_LINK_LIBRARIES})
endif()

# ── Optional Kafka support ────────────────────────────────────────────────────
option(KITSUNE_KAFKA "Build with Kafka support (requires librdkafka >= 1.9)" OFF)

if(KITSUNE_KAFKA)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(RDKAFKA REQUIRED rdkafka>=1.9)

	target_sources(KitsuneEngine PRIVATE
		luakafka.cpp
		luakafkaconsumer.cpp
		luakafkaproducer.cpp
		kafkahelpers.cpp
		luakafkamain.cpp
	)

	target_compile_definitions(KitsuneEngine PRIVATE KITSUNE_KAFKA)
	target_include_directories(KitsuneEngine PRIVATE ${RDKAFKA_INCLUDE_DIRS})
	target_link_libraries(KitsuneEngine PRIVATE ${RDKAFKA_LINK_LIBRARIES})
endif()

# ── Optional Archive support ──────────────────────────────────────────────────
option(KITSUNE_ARCHIVE "Build with Archive support (requires libarchive >= 3.2)" OFF)

if(KITSUNE_ARCHIVE)
	find_package(LibArchive 3.2 REQUIRED)

	target_sources(KitsuneEngine PRIVATE LuaArchive.cpp LuaArchiveMain.cpp)

	target_compile_definitions(KitsuneEngine PRIVATE KITSUNE_ARCHIVE)
	target_include_directories(KitsuneEngine PRIVATE ${LibArchive_INCLUDE_DIRS})
	target_link_libraries(KitsuneEngine PRIVATE ${LibArchive_LIBRARIES})
endif()

# ── Optional Redis support ─────────────────────────────────────────────
option(KITSUNE_REDIS "Build with Redis support (requires hiredis >= 1.0 with SSL)" OFF)

if(KITSUNE_REDIS)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(HIREDIS REQUIRED hiredis)
	find_package(OpenSSL REQUIRED)

	target_sources(KitsuneEngine PRIVATE
		Redis.cpp
		RedisMain.cpp
		RedisPubSub.cpp
		RedisKey.cpp
		RedisString.cpp
		RedisValue.cpp
		RedisStream.cpp
		RedisJson.cpp
	)

	target_compile_definitions(KitsuneEngine PRIVATE KITSUNE_REDIS)
	target_link_libraries(KitsuneEngine PRIVATE
		${HIREDIS_LIBRARIES}
		hiredis_ssl
		OpenSSL::SSL
		OpenSSL::Crypto
	)
endif()

target_compile_definitions(KitsuneEngine PRIVATE
	KITSUNE_ENGINE_EXPORTS
)

target_include_directories(KitsuneEngine PRIVATE
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/Lua"
)

# Export all symbols by default on Linux (mirrors Windows DLL behaviour).
# Individual symbols that need hiding can be marked explicitly later.
set_target_properties(KitsuneEngine PROPERTIES
	CXX_VISIBILITY_PRESET default
	C_VISIBILITY_PRESET   default
)

target_link_libraries(KitsuneEngine PRIVATE lua_core pthread dl)

# ── kitsune host executable ───────────────────────────────────────────────────
add_executable(kitsune Kitsune/Program.cpp Kitsune/Session.cpp)

target_include_directories(kitsune PRIVATE
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/Lua"
	"${CMAKE_SOURCE_DIR}/Kitsune"
)

target_link_libraries(kitsune PRIVATE KitsuneEngine)
