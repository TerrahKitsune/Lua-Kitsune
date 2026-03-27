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
	// Returns NULL on failure.
	KITSUNE_API lua_State* KitsuneInit();

	// ── Execution ─────────────────────────────────────────────────────────────
	// Both functions start execution as a Lua coroutine managed by the scheduler.
	// Returns a positive coroutine ID on success, or -1 on failure.
	// When fireAndForget is true the slot is freed automatically on completion;
	// do not call KitsuneCoroutineDone / KitsuneGetResult / KitsuneReleaseCoroutine for that id.
	KITSUNE_API int KitsuneExecuteFile(const char* path, int argc, const char** argv, bool fireAndForget = false);
	KITSUNE_API int KitsuneExecuteString(const char* script, int argc, const char** argv, bool fireAndForget = false);

	// ── Per-coroutine queries (id = value returned by KitsuneExecuteFile/String) ──
	// Returns true once the coroutine has finished (success or error).
	// If len is not NULL it is filled with the byte length of the pending result (0 = no result).
	// Thread-safe.
	KITSUNE_API bool        KitsuneHasResult(int id, size_t* len = nullptr);
	// Returns the error string for this coroutine, or NULL if none. Only valid after KitsuneHasResult.
	// The pointer is invalidated when KitsuneGetResult releases the slot. Thread-safe.
	KITSUNE_API const char* KitsuneGetError(int id);
	// Fills buffer with the result for this coroutine. Returns the actual byte length
	// (may exceed bufferSize-1 if truncated). Consumes the result and releases the slot;
	// call once after KitsuneHasResult returns true, even with a NULL buffer, to free the slot. Thread-safe.
	KITSUNE_API size_t      KitsuneGetResult(int id, char* buffer, size_t bufferSize);

	// ── Global control ────────────────────────────────────────────────────────
	// Returns the ID of the first coroutine that is still running, or 0 if none are active. Thread-safe.
	KITSUNE_API int  KitsuneIsRunning();
	// Signals all running coroutines to stop at the next instruction boundary. Thread-safe.
	KITSUNE_API void KitsuneInterrupt();
	// Blocks the calling thread until all coroutines have finished. Thread-safe.
	KITSUNE_API void KitsuneWait();
	// Returns all unreleased coroutine IDs (running or awaiting result consumption).
	// Fills buffer with up to bufferSize IDs and returns the total count.
	// Pass a NULL buffer to query the count without filling. Thread-safe.
	KITSUNE_API int  KitsuneGetActiveIds(int* buffer, int bufferSize);

	// ── Variable bridge ───────────────────────────────────────────────────────
	// Sets a global variable in the Vars table. Thread-safe.
	KITSUNE_API bool   KitsuneSetVariable(const char* name, const char* value, size_t length);
	// Fills buffer with the string representation of a Vars global. Returns the actual string length
	// (may exceed bufferSize-1 if truncated). Returns 0 for nil/not found. Thread-safe.
	KITSUNE_API size_t KitsuneGetVariable(const char* name, char* buffer, size_t bufferSize);

	// Destroy the Lua state and clean up the engine.
	KITSUNE_API void KitsuneCleanup();
}
