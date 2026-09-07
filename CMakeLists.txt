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
	# ── Lua debug/hook bridge ─────────────────────────────────────────────────
	KitsuneLuaDebug.cpp

	# ── Core scheduler ─────────────────────────────────────────────────────
	KitsuneEngine.cpp
	kitsuneuserdata.cpp
	luatask.cpp
	mem.cpp
	LuaEngineBuiltins.cpp
	lua_misc.cpp

	# ── MCP (Model Context Protocol server) ─────────────────────────────────
	luamcp.cpp

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

	# ── MsgPack ─────────────────────────────────────────────────────────────
	luamsgpack.cpp
	luamsgpackmain.cpp
	msgpack/src/objectc.c
	msgpack/src/unpack.c
	msgpack/src/version.c
	msgpack/src/vrefbuffer.c
	msgpack/src/zone.c

	# ── WChar (wide-string type) ────────────────────────────────────────────
	luawchar.cpp
	wcharmain.cpp

	# ── UInt (unsigned 64-bit integer type) ──────────────────────────────────
	luauint.cpp
	uintmain.cpp

	# ── Identifier (UUID/OID type) ───────────────────────────────────────────
	luaidentifier.cpp
	identifiermain.cpp

	# ── DateTime (UTC/offset date-time type) ─────────────────────────────────
	luadatetime.cpp
	datetimemain.cpp

	# ── Decimal (128-bit fixed-point decimal type) ───────────────────────────
	luadecimal.cpp
	decimalmain.cpp

	# ── TimeSpan (signed duration type) ──────────────────────────────────────
	luatimespan.cpp
	timespanmain.cpp

	# ── CSV ────────────────────────────────────────────────────────────────
	luacsv.cpp
	LuaCsvMain.cpp

	# ── XML (pugixml, bundled) ───────────────────────────────────────────────
	luaxml.cpp
	luaxmlmain.cpp
	pugixml/pugixml.cpp

	# ── YAML (libyaml, bundled) ──────────────────────────────────────────────
	luayaml.cpp
	luayamlmain.cpp
	libyaml/src/api.c
	libyaml/src/dumper.c
	libyaml/src/emitter.c
	libyaml/src/loader.c
	libyaml/src/parser.c
	libyaml/src/reader.c
	libyaml/src/scanner.c
	libyaml/src/writer.c

	# ── TOML (tomlc99, bundled) ───────────────────────────────────────────────
	luatoml.cpp
	luatomlmain.cpp
	tomlc99/toml.c

	# ── INI (pure C, no library) ─────────────────────────────────────────────
	luaini.cpp
	luainimain.cpp
	luaalivetoken.cpp
	luaalivtokenmain.cpp

	# ── Stream infrastructure (used by the variable bridge) ─────────────────
	stream.cpp
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

	# ── Hardware sensors (CPU temp, fan, memory — WMI on Windows, hwmon on Linux) ─────
	LuaHardware.cpp
	LuaHardwareMain.cpp
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
		PROPERTIES COMPILE_DEFINITIONS "${_sqlite_hidden};SQLITE_ENABLE_MATH_FUNCTIONS")
else()
	set_source_files_properties(
		SQLite/sqlite3.c
		PROPERTIES COMPILE_DEFINITIONS "SQLITE_ENABLE_MATH_FUNCTIONS")
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
	set(KITSUNE_MONGO    ON CACHE BOOL "Enabled by KITSUNE_ALL" FORCE)
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

# ── Optional HTTP + WebSocket support (libcurl + libevent 2.2) ────────────────
option(KITSUNE_HTTP "Build with HTTP/WebSocket support (requires libcurl >= 7.86.0 with --enable-websockets)" OFF)

if(KITSUNE_HTTP)
	find_package(CURL 7.86.0 REQUIRED)

	# ── libevent ──────────────────────────────────────────────────────────────
	# Always use the bundled libevent so WebSocket (event2/ws.h, libevent 2.2)
	# is available on every platform.
	# Windows: pre-built .lib files in libevent/lib (or libevent/debug/lib).
	# Linux:   static .a files built by build-linux-and-test.ps1 and placed in
	#          libevent/linux/lib.  The build script clones libevent 2.2 from
	#          GitHub and builds it with -DEVENT__DISABLE_OPENSSL=ON.
	set(LIBEVENT_BUNDLED_INCLUDE "${CMAKE_SOURCE_DIR}/libevent/include")

	if(WIN32)
		if(CMAKE_BUILD_TYPE STREQUAL "Debug")
			set(LIBEVENT_BUNDLED_LIBDIR "${CMAKE_SOURCE_DIR}/libevent/debug/lib")
			set(LIBEVENT_LIBS eventd event_cored event_extrad)
		else()
			set(LIBEVENT_BUNDLED_LIBDIR "${CMAKE_SOURCE_DIR}/libevent/lib")
			set(LIBEVENT_LIBS event event_core event_extra)
		endif()
		link_directories(${LIBEVENT_BUNDLED_LIBDIR})
		set(LIBEVENT_LINK_LIBRARIES ${LIBEVENT_LIBS})
	else()
		set(LIBEVENT_BUNDLED_LIBDIR "${CMAKE_SOURCE_DIR}/libevent/linux/lib")
		if(NOT EXISTS "${LIBEVENT_BUNDLED_LIBDIR}/libevent.a")
			message(FATAL_ERROR
				"Bundled libevent 2.2 static libs not found at ${LIBEVENT_BUNDLED_LIBDIR}.\n"
				"Run build-linux-and-test.ps1 once to build them, or run:\n"
				"  bash build-libevent-linux.sh\n"
				"from the repo root inside WSL.")
		endif()
		# The Linux-specific event-config.h (generated by cmake during the libevent
		# build) must come before the bundled Windows headers so that platform types
		# like ev_ssize_t resolve correctly on Linux.
		set(LIBEVENT_BUNDLED_INCLUDE
			"${CMAKE_SOURCE_DIR}/libevent/linux/include"
			"${CMAKE_SOURCE_DIR}/libevent/include")
		set(LIBEVENT_LINK_LIBRARIES
			"${LIBEVENT_BUNDLED_LIBDIR}/libevent.a"
			"${LIBEVENT_BUNDLED_LIBDIR}/libevent_core.a"
			"${LIBEVENT_BUNDLED_LIBDIR}/libevent_extra.a")
	endif()

		target_sources(KitsuneEngine PRIVATE
		HttpCurl.cpp HttpCurlMain.cpp
		LuaHttpRequest.cpp LuaHttpResponse.cpp
		LuaHttpServer.cpp LuaHttpServerMain.cpp
		LuaWebSocket.cpp
		LuaTcpListener.cpp LuaTcpMain.cpp)

	target_compile_definitions(KitsuneEngine PRIVATE KITSUNE_HTTP KITSUNE_WEBSOCKET)
	# Suppress _strdup deprecation on Linux — consistent with Windows build flags
	target_compile_definitions(KitsuneEngine PRIVATE _CRT_SECURE_NO_WARNINGS)

	target_include_directories(KitsuneEngine PRIVATE ${LIBEVENT_BUNDLED_INCLUDE})
	target_link_libraries(KitsuneEngine PRIVATE CURL::libcurl ${LIBEVENT_LINK_LIBRARIES})

	message(STATUS "WebSocket support: enabled (bundled libevent 2.2)")
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

option(KITSUNE_MONGO "Build with MongoDB support (requires libmongoc-1.0 >= 1.17)" OFF)

if(KITSUNE_MONGO)
	find_package(PkgConfig REQUIRED)
	pkg_check_modules(MONGOC REQUIRED libmongoc-1.0)

	target_sources(KitsuneEngine PRIVATE
		LuaMongo.cpp
		MongoMain.cpp
	)

	target_compile_definitions(KitsuneEngine PRIVATE KITSUNE_MONGO)
	target_include_directories(KitsuneEngine PRIVATE ${MONGOC_INCLUDE_DIRS})
	target_link_libraries(KitsuneEngine PRIVATE ${MONGOC_LIBRARIES})
endif()

# ── Optional llama.cpp support (local LLM inference) ─────────────────────────
# Windows-only for now (CUDA prebuilt binaries). Run fetch-llama-binaries.ps1 first.
if(WIN32)
	option(KITSUNE_LLAMA "Build with llama.cpp support (run fetch-llama-binaries.ps1 first)" OFF)

	if(KITSUNE_LLAMA)
		set(LLAMA_VENDOR "${CMAKE_SOURCE_DIR}/llama")
		if(NOT EXISTS "${LLAMA_VENDOR}/include/llama.h")
			message(FATAL_ERROR "llama.cpp vendor files missing. Run: powershell fetch-llama-binaries.ps1")
		endif()

		target_sources(KitsuneEngine PRIVATE
			LlamaContext.cpp
			luallama.cpp
			luallamamain.cpp
			luallama_prompt.cpp
		)

		target_compile_definitions(KitsuneEngine PRIVATE KITSUNE_LLAMA)
		target_include_directories(KitsuneEngine PRIVATE "${LLAMA_VENDOR}/include" "${LLAMA_VENDOR}/include/ggml")
		target_link_libraries(KitsuneEngine PRIVATE
			"${LLAMA_VENDOR}/lib/llama.lib"
			"${LLAMA_VENDOR}/lib/ggml.lib"
		)
	endif()
endif()

target_compile_definitions(KitsuneEngine PRIVATE
	KITSUNE_ENGINE_EXPORTS
)

target_include_directories(KitsuneEngine PRIVATE
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/Lua"
	"${CMAKE_SOURCE_DIR}/msgpack/include"
	"${CMAKE_SOURCE_DIR}/libyaml/include"
	"${CMAKE_SOURCE_DIR}/tomlc99"
)

# Export all symbols by default on Linux (mirrors Windows DLL behaviour).
# Individual symbols that need hiding can be marked explicitly later.
set_target_properties(KitsuneEngine PROPERTIES
	CXX_VISIBILITY_PRESET default
	C_VISIBILITY_PRESET   default
)

target_link_libraries(KitsuneEngine PRIVATE lua_core pthread dl)

# ── kitsune host executable ───────────────────────────────────────────────────
if(EXISTS "${CMAKE_SOURCE_DIR}/Kitsune/Scheduler.cpp")
add_executable(kitsune Kitsune/Program.cpp Kitsune/Session.cpp Kitsune/Scheduler.cpp)

target_include_directories(kitsune PRIVATE
	"${CMAKE_SOURCE_DIR}"
	"${CMAKE_SOURCE_DIR}/Lua"
	"${CMAKE_SOURCE_DIR}/Kitsune"
)

target_link_libraries(kitsune PRIVATE KitsuneEngine)
endif()

# ── ImGui (SDL2 + OpenGL3, Windows and Linux) ─────────────────────────────────
option(KITSUNE_IMGUI "Build with ImGui support (requires SDL2 and OpenGL)" OFF)
if(KITSUNE_IMGUI)
	find_package(OpenGL REQUIRED)

	# ── litehtml (bundled) ────────────────────────────────────────────────────
	set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
	add_subdirectory("${CMAKE_SOURCE_DIR}/Kitsune/litehtml" litehtml EXCLUDE_FROM_ALL)
	set_target_properties(litehtml PROPERTIES POSITION_INDEPENDENT_CODE ON)

	# SDL2: use the pre-built binaries in SDL2/ (mirrors the hiredis/postgres pattern).
	# On Linux, fall back to system SDL2 via pkg-config if the local folder is absent.
	if(EXISTS "${CMAKE_SOURCE_DIR}/Kitsune/SDL2/include/SDL.h")
		set(SDL2_INCLUDE_DIRS "${CMAKE_SOURCE_DIR}/Kitsune/SDL2/include")
		if(WIN32)
			set(SDL2_LIBRARIES "${CMAKE_SOURCE_DIR}/Kitsune/SDL2/lib/x64/SDL2.lib"
							   "${CMAKE_SOURCE_DIR}/Kitsune/SDL2/lib/x64/SDL2main.lib")
		else()
			find_library(SDL2_LIB SDL2 PATHS "${CMAKE_SOURCE_DIR}/Kitsune/SDL2/lib" NO_DEFAULT_PATH)
			set(SDL2_LIBRARIES ${SDL2_LIB})
		endif()
	else()
		find_package(PkgConfig REQUIRED)
		pkg_check_modules(SDL2 REQUIRED sdl2)
	endif()

	target_sources(kitsune PRIVATE
		Kitsune/ImguiSession.cpp
		Kitsune/ImguiRenderer.cpp
		Kitsune/ImguiEnums.cpp
		Kitsune/ImguiRenderer_generated.cpp
		Kitsune/ImguiEnums_generated.cpp
		Kitsune/Font.cpp
		Kitsune/ImguiHtml.cpp
		Kitsune/Imgui/imgui.cpp
		Kitsune/Imgui/imgui_draw.cpp
		Kitsune/Imgui/imgui_tables.cpp
		Kitsune/Imgui/imgui_widgets.cpp
		Kitsune/Imgui/imgui_demo.cpp
		Kitsune/Imgui/imgui_impl_sdl2.cpp
		Kitsune/Imgui/imgui_impl_opengl3.cpp
	)

	target_compile_definitions(kitsune PRIVATE KITSUNE_IMGUI)

	target_include_directories(kitsune PRIVATE
		"${CMAKE_SOURCE_DIR}/Kitsune/Imgui"
		"${CMAKE_SOURCE_DIR}/Kitsune/litehtml/include"
		${SDL2_INCLUDE_DIRS}
	)

	target_link_libraries(kitsune PRIVATE
		${SDL2_LIBRARIES}
		OpenGL::GL
		litehtml
	)

	# ── Optional SDL_mixer audio support (requires KITSUNE_IMGUI) ─────────────
	option(KITSUNE_AUDIO "Build with audio support via SDL_mixer (requires KITSUNE_IMGUI)" OFF)
	if(KITSUNE_AUDIO)
		target_sources(kitsune PRIVATE Kitsune/SDLAudio.cpp)
		target_compile_definitions(kitsune PRIVATE KITSUNE_IMGUI)

		if(WIN32)
			# Expect pre-built SDL2_mixer binaries mirroring the SDL2 layout:
			#   Kitsune/SDL2_mixer/include/SDL_mixer.h
			#   Kitsune/SDL2_mixer/lib/x64/SDL2_mixer.lib
			if(EXISTS "${CMAKE_SOURCE_DIR}/Kitsune/SDL2_mixer/include/SDL_mixer.h")
				target_include_directories(kitsune PRIVATE
					"${CMAKE_SOURCE_DIR}/Kitsune/SDL2_mixer/include")
				target_link_libraries(kitsune PRIVATE
					"${CMAKE_SOURCE_DIR}/Kitsune/SDL2_mixer/lib/x64/SDL2_mixer.lib")
			else()
				message(FATAL_ERROR
					"KITSUNE_AUDIO requires SDL2_mixer. "
					"Place SDL_mixer.h under Kitsune/SDL2_mixer/include/ and "
					"SDL2_mixer.lib under Kitsune/SDL2_mixer/lib/x64/.")
			endif()
		else()
			find_package(PkgConfig REQUIRED)
			pkg_check_modules(SDL2_MIXER REQUIRED SDL2_mixer)
			target_include_directories(kitsune PRIVATE ${SDL2_MIXER_INCLUDE_DIRS})
			target_link_libraries(kitsune PRIVATE ${SDL2_MIXER_LINK_LIBRARIES})
		endif()
	endif()
endif()
