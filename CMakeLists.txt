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
	LuaSHA1.cpp
	SHA1Main.cpp

	# ── JSON ───────────────────────────────────────────────────────────────
	luajson.cpp
	luajsonmain.cpp
	jsondecode.cpp
	jsonencode.cpp
	jsonutil.cpp
	lua_json.cpp

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
)

add_library(KitsuneEngine SHARED ${KITSUNE_ENGINE_SOURCES})

target_compile_definitions(KitsuneEngine PRIVATE
	KITSUNE_ENGINE_EXPORTS
	KITSUNE_BAREBONES       # gates out external-dependency module registrations
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
