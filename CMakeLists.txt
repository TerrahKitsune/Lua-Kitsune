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
add_executable(kitsune Program.cpp)

target_include_directories(kitsune PRIVATE
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/Lua"
)

target_link_libraries(kitsune PRIVATE KitsuneEngine)
