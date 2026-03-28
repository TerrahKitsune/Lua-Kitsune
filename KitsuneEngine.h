#pragma once
#include <Windows.h>

#ifdef KITSUNE_ENGINE_EXPORTS
#define KITSUNE_API __declspec(dllexport)
#else
#define KITSUNE_API __declspec(dllimport)
#endif

#define KITSUNE_VERSION "1.0.0"

// KitsuneVariable type constants — match Lua's LUA_T* values for direct comparison.
#define KITSUNE_TNONE    (-1)
#define KITSUNE_TBOOLEAN  (1)
#define KITSUNE_TNUMBER   (3)
#define KITSUNE_TSTRING   (4)

struct KitsuneVariable {
	int            type;     // LUA_TNONE=-1, LUA_TBOOLEAN=1, LUA_TNUMBER=3, LUA_TSTRING=4
	size_t         length;
	union {
		double         number;
		bool           boolean;
		unsigned char* data;  // strings only: C++ allocates on Get; caller-owns on Set
	};
};

extern "C" {
	// Initialise the engine and create the Lua state. If already initialised, returns true immediately.
	// Returns false on failure.
	KITSUNE_API bool KitsuneInit();

	// Frees a KitsuneVariable returned by KitsuneGetResult or KitsuneGetVariable
	// (frees the string data if present, then the struct pointer itself). Safe on NULL.
	KITSUNE_API void KitsuneVariableFree(KitsuneVariable* var);

	// ── Execution ─────────────────────────────────────────────────────────────
	// Both functions start execution as a Lua coroutine managed by the scheduler.
	// Returns a positive coroutine ID on success, or -1 on failure.
	// When fireAndForget is true the slot is freed automatically on completion;
	// do not call KitsuneCoroutineDone / KitsuneGetResult / KitsuneReleaseCoroutine for that id.
	KITSUNE_API int KitsuneExecuteFile(const char* path, int argc, KitsuneVariable* argv, bool fireAndForget = false);
	KITSUNE_API int KitsuneExecuteString(const char* script, int argc, KitsuneVariable* argv, bool fireAndForget = false);
	KITSUNE_API int KitsuneExecuteFunction(const char* functionName, int argc, KitsuneVariable* argv, bool fireAndForget = false);

	// ── Per-coroutine queries (id = value returned by KitsuneExecuteFile/String) ──
	// Returns true once the coroutine has finished (success or error).
	// If len is not NULL it is filled with the byte length of the pending result (0 = no result).
	// Thread-safe.
	KITSUNE_API bool        KitsuneHasResult(int id, size_t* len = nullptr);
	// Returns the error string for this coroutine, or NULL if none. Only valid after KitsuneHasResult.
	// The pointer is invalidated when KitsuneGetResult releases the slot. Thread-safe.
	KITSUNE_API const char* KitsuneGetError(int id);
	// Returns the typed result and releases the slot. Returns NULL on failure.
	// Call KitsuneVariableFree on the result when done. Thread-safe.
	KITSUNE_API KitsuneVariable*      KitsuneGetResult(int id);
	// If the coroutine is still running, signals it to stop and releases the slot immediately. Thread-safe.
	KITSUNE_API void		KitsuneCancel(int id);
	// Returns how long the coroutine has been alive in milliseconds, measured from when it was created.
	// Returns 0.0 if the id is not found. Thread-safe.
	KITSUNE_API double		KitsuneGetRuntime(int id);

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
	// Sets a Vars global. Pass NULL or type == KITSUNE_TNONE to remove the key.
	// String data in var is only read for the duration of the call. Thread-safe.
	KITSUNE_API bool             KitsuneSetVariable(const char* name, const KitsuneVariable* var);
	// Returns the current value of a Vars global as a heap-allocated typed variable.
	// Call KitsuneVariableFree on the result when done. Returns NULL if not found. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneGetVariable(const char* name);

	// Destroy the Lua state and clean up the engine.
	KITSUNE_API void KitsuneCleanup();
}
