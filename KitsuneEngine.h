#pragma once
#include <Windows.h>

#ifdef KITSUNE_ENGINE_EXPORTS
#define KITSUNE_API __declspec(dllexport)
#else
#define KITSUNE_API __declspec(dllimport)
#endif

#define KITSUNE_VERSION "1.0.0"

typedef struct lua_State lua_State;

extern "C" {
	// Initialise the engine and create the Lua state. If already initialised, returns the existing state.
	// Returns NULL on failure (check KitsuneGetError).
	KITSUNE_API lua_State*  KitsuneInit();
	KITSUNE_API int         KitsuneExecuteFile(const char* path, int argc, const char** argv);
	KITSUNE_API int         KitsuneExecuteString(const char* script, int argc, const char** argv);
	KITSUNE_API const char* KitsuneGetError();
	// Returns non-zero if the engine is currently executing a script (thread-safe)
	KITSUNE_API int         KitsuneIsRunning();
	// Signals the running script to stop at the next instruction boundary (thread-safe)
	KITSUNE_API void        KitsuneInterrupt();
	// Blocks the calling thread until the engine is no longer running
	KITSUNE_API void        KitsuneWait();
	// Sets a pending global variable. Applied before the next pcall or at the next ticker fire. Overwrites any unconsumed pending set (thread-safe)
	KITSUNE_API bool        KitsuneSetVariable(const char* name, const char* value, size_t length);
	// Fills buffer with the string representation of a global variable. Returns the actual string length
	// (may exceed bufferSize-1 if truncated). Returns 0 for nil/not found. Thread-safe.
	KITSUNE_API size_t      KitsuneGetVariable(const char* name, char* buffer, size_t bufferSize);
	// Returns the byte length of the pending result without consuming it. Returns 0 if no result is available.
	KITSUNE_API size_t      KitsuneHasResult();
	// Fills buffer with the string representation of the most recent result from a script. Returns the actual string length
	// Waits for a result to be available if the engine is currently running. Consumes the result, so subsequent calls will return 0 until another result is produced.
	// (may exceed bufferSize-1 if truncated). Returns 0 for nil/not found. Thread-safe.
	KITSUNE_API size_t      KitsuneGetResult(char* buffer, size_t bufferSize);
	// Destroy the Lua state and clean up the engine
	KITSUNE_API void        KitsuneCleanup();
}
