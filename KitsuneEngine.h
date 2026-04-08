#pragma once
#include "platform.h"

#ifdef _WIN32
#ifdef KITSUNE_ENGINE_EXPORTS
#define KITSUNE_API __declspec(dllexport)
#else
#define KITSUNE_API __declspec(dllimport)
#endif
#endif

#define KITSUNE_VERSION "1.0.0"

// KitsuneVariable type constants — values 0–8 match Lua's LUA_T* constants for direct comparison.
// KITSUNE_TNONE (-1) matches LUA_TNONE. KITSUNE_TERROR (-2) is a Kitsune extension not present
// in Lua; it is used exclusively with kitsune_ResultSetter to signal a Lua error from a
#define KITSUNE_TSTREAM        (-6) // Kitsune extension: pointer to a SharedMemoryBlock that Lua always owns.
									// The block MUST have been obtained via KitsuneCreateMemoryBlock.
									// Passing a block not created by KitsuneCreateMemoryBlock is an error
									// and will push nil to Lua.
#define KITSUNE_TJSON          (-5) // Kitsune extension: JSON string type; data is a UTF-8 char* and length is in bytes (excluding null terminator). Not a value returned by lua_type().
#define KITSUNE_TCHAR16        (-4) // Kitsune extension: UTF-16 string type; data is a char16_t* and length is in char16_t code units (excluding null terminator). Not a value returned by lua_type().
#define KITSUNE_TINTEGER       (-3) // Kitsune extension: Lua 5.3+ integer subtype (lua_isinteger); not a value returned by lua_type()
#define KITSUNE_TERROR         (-2) // registered C function and will never appear in a KitsuneVariable returned by the engine.
#define KITSUNE_TNONE          (-1)
#define KITSUNE_TNIL            (0)
#define KITSUNE_TBOOLEAN        (1)
#define KITSUNE_TLIGHTUSERDATA  (2)
#define KITSUNE_TNUMBER         (3)
#define KITSUNE_TSTRING         (4)
#define KITSUNE_TTABLE          (5)
#define KITSUNE_TFUNCTION       (6)  // When returned from the engine, integer holds a luaL_ref registry reference
									// anchoring the function. Release via KitsuneVariableFree, which calls
									// luaL_unref. Push back to Lua via PushKitsuneVariable (lua_rawgeti).
									// Cannot be constructed from scratch on the host side in a meaningful way.
#define KITSUNE_TUSERDATA       (7)
#define KITSUNE_TTHREAD         (8)

// Return values for KitsuneGetStatus.
#define KITSUNE_STATUS_NONE      (0)  // id not found (never existed, already released, or compacted)
#define KITSUNE_STATUS_IDLE      (1)  // alive and queued; waiting to be resumed by the scheduler
#define KITSUNE_STATUS_SLEEPING  (2)  // alive but waiting out a Sleep() deadline
#define KITSUNE_STATUS_RUNNING   (3)  // currently executing inside lua_resume
#define KITSUNE_STATUS_DONE      (4)  // finished successfully; result not yet consumed
#define KITSUNE_STATUS_FAULTED   (5)  // finished with a runtime or Lua error; call KitsuneGetError
#define KITSUNE_STATUS_CANCELLED (6)  // stopped by an explicit KitsuneCancel(id) call, or cancel is pending

#define KITSUNE_SHARED_MEMORY_FLAG_LOCKED (1 << 0) // Set by an accessor while it is reading or writing the block to signal concurrent usage.
														// Other accessors should check this flag and wait or retry before accessing the block.
														// The Lua stream vtable sets and clears this flag automatically around each read/write.
#define KITSUNE_SHARED_MEMORY_FLAG_READONLY (1 << 2) // The block is read-only; write operations are rejected by the stream vtable.
#define KITSUNE_SHARED_MEMORY_FLAG_KITSUNE_OWNED    (1 << 3) // Set by KitsuneCreateMemoryBlock; required for a block to be accepted as KITSUNE_TSTREAM.
#define KITSUNE_SHARED_MEMORY_FLAG_OWNER_DISPOSED    (1 << 4) // Set when all Lua streams referencing this block have been GC'd. Once set, never cleared.
#define KITSUNE_SHARED_MEMORY_FLAG_ACCESSOR_DISPOSED (1 << 5) // Cleared when C# takes ownership (LuaStream constructor); set when C# disposes. Starts at 1 (no accessor).
#define KITSUNE_SHARED_MEMORY_FLAG_LUA_REFERENCED    (1 << 6) // Set when any Lua stream is created from this block. Once set, never cleared.

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4200) // nonstandard extension: zero-sized array; intentional flexible array member
#endif
struct SharedMemoryBlock {
	uint8_t flags; // Bitfield of KITSUNE_SHARED_MEMORY_FLAG_* values.
	// KITSUNE_SHARED_MEMORY_FLAG_LOCKED:        set by an accessor during a read or write; other accessors should wait.
	// KITSUNE_SHARED_MEMORY_FLAG_READONLY:      data must not be modified; write operations are rejected.
	// KITSUNE_SHARED_MEMORY_FLAG_KITSUNE_OWNED: block was created by KitsuneCreateMemoryBlock.
	void* userdata;          // Reserved; not used by the engine.
	SharedMemoryBlock* next; // Intrusive linked-list link for the global block registry. Written only under g_shmem_lock.
	size_t size;             // Size of the data region in bytes. The data block immediately follows the header in memory.
	uint8_t data[]; // Continous data block of the specified size. The entire struct is allocated as a single block on the heap, so freeing the struct pointer also frees the data block.
};
#ifdef _MSC_VER
#pragma warning(pop)
#endif

// Forward declaration required so KitsuneVariable can hold a pointer to the node in its union.
struct KeyValuePairKitsuneVariableNode;

struct KitsuneVariable {
	int type; // see KITSUNE_T* constants above
	size_t length; // byte count for KITSUNE_TSTRING and KITSUNE_TUSERDATA __name; char16_t count for KITSUNE_TCHAR16; entry count for KITSUNE_TTABLE; 0 for all other types
	union {
		double number;                         // KITSUNE_TNUMBER
		long long integer;                     // KITSUNE_TINTEGER
		bool boolean;                          // KITSUNE_TBOOLEAN
		unsigned char* data;                   // KITSUNE_TSTRING: heap-allocated UTF-8 bytes; caller-owned on Set
		// KITSUNE_TUSERDATA: heap-allocated UTF-8 __name from metatable (NULL if no __name); length = byte count
		char16_t* char16data;                  // KITSUNE_TCHAR16: heap-allocated char16_t string; length = number of char16_t code units (excl. null terminator)
		KeyValuePairKitsuneVariableNode* table; // KITSUNE_TTABLE: head of linked list (NULL = empty table)
		SharedMemoryBlock* stream; // KITSUNE_TSTREAM: pointer to a SharedMemoryBlock representing the stream; caller-owned on Set
	};
};

// Intrusive linked list node for KITSUNE_TTABLE values.
// When a KitsuneVariable has type == KITSUNE_TTABLE, its table field points to the head of this list.
// Keys and values may themselves be KITSUNE_TTABLE nodes, enabling nested tables up to
// KITSUNE_MAX_TABLE_DEPTH levels deep. Call KitsuneVariableFree to recursively release the list.
struct KeyValuePairKitsuneVariableNode {
	KitsuneVariable key;
	KitsuneVariable value;
	KeyValuePairKitsuneVariableNode* next;
};

// Called once per key-value entry during a KitsuneGetAll traversal.
// key and value are temporary copies valid only for the duration of this call.
// Copy any data you need before returning; do not store the data pointers beyond the callback.
// userdata is the opaque pointer passed to KitsuneGetAll.
// CONSTRAINT: do not call any Kitsune API that calls AcquireLuaAccess from within this callback
// (the same deadlock constraint as kitsune_CFunction — see above).
typedef void (*kitsune_KeyValuePairCallback)(const KitsuneVariable* key, const KitsuneVariable* value, void* userdata);

// Callback passed to kitsune_CFunction to return a value to Lua.
// Pass a KitsuneVariable with any type to return a value; pass KITSUNE_TERROR with an optional
// error message in data to raise a Lua error. Returns non-zero if the value was stored, 0 if
// the store failed (e.g. allocation error for KITSUNE_TERROR). The caller retains ownership of
// the KitsuneVariable and any data it points to for the duration of the call.
typedef int (*kitsune_ResultSetter) (const KitsuneVariable* result);

// Signature for C functions registered via RegisterFunction.
// argc/argv are the Lua call arguments; call resultSetter to return a result or raise an error.
// Return > 0 on success, <= 0 to raise a generic "delegate function error" in Lua.
// CONSTRAINTS: do NOT call KitsuneSetVariable, KitsuneGetVariable, KitsuneExecuteStringAsync/FileAsync/FunctionAsync,
// KitsuneGetAll, or KitsuneRegisterFunction from within this callback — the scheduler
// thread owns the Lua state for the duration of the call, so any function that calls
// AcquireLuaAccess will deadlock permanently. lua_State* is intentionally not exposed.
typedef int (*kitsune_CFunction) (int argc, KitsuneVariable* argv, const kitsune_ResultSetter resultSetter, void* userdata);

// Initialisation callback passed to KitsuneInit. L is a pointer to the Lua state after the engine has created it and loaded the standard libraries, but before any scripts have been executed. This is intended for advanced users who need to perform custom setup on the Lua state (e.g. load additional libraries, set up a custom panic handler, etc.) before the engine starts running scripts. The callback must not call any Kitsune API that calls AcquireLuaAccess (see above) — the scheduler thread owns the Lua state for the duration of this call.
typedef void (*kitsune_Init) (const void* L);

extern "C" {
	// Initialise the engine and create the Lua state. If already initialised, returns true immediately.
	// Returns false on failure.
	KITSUNE_API bool KitsuneInit(kitsune_Init initFunc = nullptr);

	// Frees a KitsuneVariable returned by KitsuneGetResult or KitsuneGetVariable
	// (frees the string data if present, then the struct pointer itself). Safe on NULL.
	KITSUNE_API void KitsuneVariableFree(KitsuneVariable* var);

	// Registers a C function as a global callable from Lua.
	// name is a dot-separated path (e.g. "Foo" or "Ns.Foo"); intermediate tables are created.
	// See kitsune_CFunction for the full list of constraints on what may be called from within func.
	KITSUNE_API void KitsuneRegisterFunction(const char* name, kitsune_CFunction func, void* userdata = nullptr);

	// Allocates a Lua-owned memory block of the given size, anchors it in the Lua registry,
	// and returns a pointer to the block for the host to read from or write into.
	// block->data[] is zero-initialised and its length is block->size.
	// The block MUST be released exactly once by one of:
	//   a) Passing it to Lua as KITSUNE_TSTREAM — Lua takes ownership and calls block->close on GC.
	//   b) Calling block->close(block) directly — releases the Lua anchor and frees the block.
	// Do NOT call free() or any other allocator on the block.
	// Cannot be called from the Lua scheduler thread inside a registered function (will return NULL).
	// Returns NULL on failure.
	KITSUNE_API SharedMemoryBlock* KitsuneCreateMemoryBlock(size_t size);

	// ── Execution ──────────────────────────────────────────────────────────────
	// All three functions execute synchronously: they block the calling thread until
	// the script finishes and return the typed result directly. Returns NULL on start
	// failure (e.g. engine not initialised, no slots available). On success the caller
	// MUST free the returned pointer with KitsuneVariableFree. A result with type
	// KITSUNE_TNONE means the script returned nothing or raised a Lua error; use the
	// Async API with KitsuneGetError to obtain error details. Cannot be called from
	// within a kitsune_CFunction — the scheduler owns the Lua state and will deadlock.
	KITSUNE_API KitsuneVariable* KitsuneExecuteFile(const char* path, int argc, const KitsuneVariable* argv);
	KITSUNE_API KitsuneVariable* KitsuneExecuteString(const char* script, int argc, const KitsuneVariable* argv);
	KITSUNE_API KitsuneVariable* KitsuneExecuteFunction(const char* functionName, int argc, const KitsuneVariable* argv);
	KITSUNE_API KitsuneVariable* KitsuneExecuteVariable(const KitsuneVariable* var, int argc, const KitsuneVariable* argv);

	// ── Async Execution ────────────────────────────────────────────────────────
	// All three functions start execution as a Lua coroutine managed by the scheduler.
	// Returns a positive coroutine ID on success, or -1 on failure.
	// KitsuneExecuteFunctionAsync still returns a positive ID when the named function does not exist;
	// in that case KitsuneHasResult will return true immediately with error "function not found".
	// When fireAndForget is true the slot is freed automatically on completion;
	// do not call KitsuneHasResult / KitsuneGetResult for that id.
	KITSUNE_API int KitsuneExecuteFileAsync(const char* path, int argc, const KitsuneVariable* argv, bool fireAndForget = false);
	KITSUNE_API int KitsuneExecuteStringAsync(const char* script, int argc, const KitsuneVariable* argv, bool fireAndForget = false);
	KITSUNE_API int KitsuneExecuteFunctionAsync(const char* functionName, int argc, const KitsuneVariable* argv, bool fireAndForget = false);
	KITSUNE_API int KitsuneExecuteVariableAsync(const KitsuneVariable* var, int argc, const KitsuneVariable* argv, bool fireAndForget = false);

	// ── Per-coroutine queries (id = value returned by KitsuneExecuteFileAsync/StringAsync) ──
	// Returns true once the coroutine has finished (success or error).
	// If len is not NULL it is set to the byte length of the string result, or 0 if the result
	// is absent or is not a string type (number, boolean, table, etc. all yield len == 0).
	// Thread-safe.
	KITSUNE_API bool KitsuneHasResult(int id, size_t* len = nullptr);
	// Returns the byte length of the error string for this coroutine (excluding null terminator),
	// or 0 if there is no error or the id is not found. If buf is non-NULL and bufSize > 0 the
	// error is copied into buf (always null-terminated, truncated to bufSize-1 bytes if needed).
	// Pass buf=NULL / bufSize=0 to query the required size before allocating.
	// Only call after KitsuneHasResult returns true. Invalid after KitsuneCancel. Thread-safe.
	KITSUNE_API size_t KitsuneGetError(int id, char* buf, size_t bufSize);
	// Returns the typed result and releases the slot. Returns NULL if the id is not found,
	// the coroutine is not yet done, or memory allocation fails. A coroutine that returned
	// nothing (no return statement, or finished with error) yields a non-NULL variable with
	// type == KITSUNE_TNONE — use KitsuneGetError to distinguish the two cases.
	// Call KitsuneVariableFree on the result when done. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneGetResult(int id);
	// Releases the slot of a finished coroutine without consuming its result value.
	// Use when you have already read the error via KitsuneGetError and do not need the result.
	// No-op if the coroutine is not yet done; use KitsuneCancel for running coroutines. Thread-safe.
	KITSUNE_API void KitsuneReleaseResult(int id);
	// Signals the coroutine to stop and marks its slot for release. If the coroutine is still
	// running, it is interrupted at the next instruction boundary; if it has already finished,
	// the slot is released immediately. Slot compaction is asynchronous — the scheduler performs
	// it on the next cycle. After KitsuneCancel the id is invalid: do not call KitsuneGetError,
	// KitsuneGetResult, or KitsuneHasResult for it. Thread-safe.
	KITSUNE_API void KitsuneCancel(int id);
	// Returns how long the coroutine has been alive in milliseconds, measured from when it was created.
	// Returns 0.0 if the id is not found. Thread-safe.
	KITSUNE_API double KitsuneGetRuntime(int id);
	// Returns the current status of the coroutine identified by id. See KITSUNE_STATUS_* constants.
	// Thread-safe.
	KITSUNE_API int KitsuneGetStatus(int id);

	// ── Global control ────────────────────────────────────────────────────────
	// Returns true if any coroutine is currently running or yielded. Thread-safe.
	KITSUNE_API bool KitsuneIsRunning();
	// Returns the ID of the first coroutine that is still running, or 0 if none are active. Thread-safe.
	KITSUNE_API int  KitsuneGetRunningId();
	// Signals all running coroutines to stop at the next instruction boundary. Thread-safe.
	KITSUNE_API void KitsuneInterrupt();
	// Blocks the calling thread until all coroutines have finished. Thread-safe.
	KITSUNE_API void KitsuneWait();
	// Returns all unreleased coroutine IDs (running or awaiting result consumption).
	// Fills buffer with up to bufferSize IDs and returns the total count.
	// Pass a NULL buffer to query the count without filling. Thread-safe.
	KITSUNE_API int KitsuneGetActiveIds(int* buffer, int bufferSize);

	// ── Variable bridge ───────────────────────────────────────────────────────
	// Sets a Lua global at the given dot-separated path (e.g. "foo" or "foo.bar.baz").
	// The Lua global environment is the root; intermediate tables are created automatically.
	// Pass NULL or KITSUNE_TNONE to remove the key. Pass type==KITSUNE_TTABLE to set an empty table.
	// Key components must not contain '.'. String data is only read for the duration of the call.
	// Thread-safe.
	KITSUNE_API bool KitsuneSetVariable(const char* path, const KitsuneVariable* var);
	// Returns the Lua global at the given dot-separated path as a heap-allocated typed variable.
	// Returns NULL if not found or if any intermediate component is not a table.
	// Call KitsuneVariableFree on the result when done. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneGetVariable(const char* path);
	// Iterates all entries at the given dot-separated path in the Lua global environment,
	// invoking callback once per key-value pair. Pass NULL or "" to iterate _G itself.
	// key and value are temporary — valid only for the duration of each call. Thread-safe.
	KITSUNE_API void KitsuneGetAll(const char* path, kitsune_KeyValuePairCallback callback, void* userdata);
	// Destroy the Lua state and clean up the engine.
	KITSUNE_API size_t KitsuneCleanup();
	// Registers the Session table (Session.Console, Session.Clipboard) into the Lua global
	// environment. Call once from the host after KitsuneInit() to enable interactive session
	// functions. No-op if called before KitsuneInit(). Thread-safe.
	KITSUNE_API void KitsuneRegisterSession();
}