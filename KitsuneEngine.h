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
#define KITSUNE_TUINT          (-9) // Kitsune extension: unsigned 64-bit integer (LuaUInt userdata). The raw uint64_t bit pattern is stored in the integer union field. Values <= INT64_MAX round-trip via KITSUNE_TINTEGER; values > INT64_MAX require this type to avoid sign loss.
#define KITSUNE_TDATETIME     (-10) // Kitsune extension: DateTime userdata. datetime → heap-allocated KitsuneDateTime { int64_t ticks; int16_t offset_minutes; }. Free via KitsuneVariableFree.
#define KITSUNE_TTIMESPAN     (-11) // Kitsune extension: TimeSpan userdata. timespan → heap-allocated KitsuneTimeSpan { int64_t ticks; }. Free via KitsuneVariableFree.
#define KITSUNE_TDECIMAL      (-12) // Kitsune extension: Decimal userdata. decimal → heap-allocated KitsuneDecimal { uint64_t lo; uint64_t hi; int16_t scale; uint8_t negative; }. Free via KitsuneVariableFree.
#define KITSUNE_TIDENTIFIER   (-13) // Kitsune extension: Identifier userdata. identifier → heap-allocated KitsuneIdentifier { uint8_t type; uint8_t bytes[16]; } where type 0=UUID, 1=OID. Free via KitsuneVariableFree.
#define KITSUNE_TTABLECONTENTS  (-8) // Kitsune extension: snapshot of a Lua table's key-value pairs; table field points to a KitsuneKeyValuePairVariableNode linked list. Produced by KitsuneGetTableContents; consumed by KitsuneSetTableContents. KitsuneVariableFree recursively releases the list.
#define KITSUNE_TITERATOR      (-7) // Kitsune extension: iterator type; data is a pointer to a kitsune_Iterator struct containing the iteration state. Not a value returned by lua_type() — only used in KitsuneVariable for iterating tables with KitsuneGetAll, and never appears in Lua or in a variable returned by the engine. Data should be a pointer to a kitsune_Iterator.
#define KITSUNE_TCFUNCTION     (-6) // Kitsune extension: C function pointer type; data is a pointer to a kitsune_CFunctionData struct containing the function pointer and its userdata. Not a value returned by lua_type() — only used in KitsuneVariable for passing C function pointers to Lua, and never appears in Lua or in a variable returned by the engine. Data should be a pointer to a kitsune_CFunctionData.
#define KITSUNE_TJSON          (-5) // Kitsune extension: JSON string type; data is a UTF-8 char* and length is in bytes (excluding null terminator). Not a value returned by lua_type().
#define KITSUNE_TCHAR16        (-4) // Kitsune extension: UTF-16 string type; data is a char16_t* and length is in char16_t code units (excluding null terminator). Not a value returned by lua_type().
#define KITSUNE_TINTEGER       (-3) // Kitsune extension: Lua 5.3+ integer subtype (lua_isinteger); not a value returned by lua_type()
#define KITSUNE_TERROR         (-2) // Kitsune extension: The variable represents an error message (UTF-8 char*); length is in bytes (excluding null terminator). Not a value returned by lua_type(); Used to signal an error when passed too and from Lua via kitsune_ResultSetter and KitsuneVariableReturnFromLua.
#define KITSUNE_TNONE          (-1)
#define KITSUNE_TNIL            (0)
#define KITSUNE_TBOOLEAN        (1)
#define KITSUNE_TLIGHTUSERDATA  (2)
#define KITSUNE_TNUMBER         (3)
#define KITSUNE_TSTRING         (4)
#define KITSUNE_TTABLE          (5)  // When returned from the engine, ref holds a luaL_ref registry reference
							// anchoring the live table. Same lifecycle as KITSUNE_TFUNCTION: release via
							// KitsuneVariableFree, push back to Lua via PushKitsuneVariable (lua_rawgeti).
							// Use KitsuneGetTableContents to snapshot contents; KitsuneSetTableContents to replace them.
#define KITSUNE_TFUNCTION       (6)  // When returned from the engine, ref holds a luaL_ref registry reference
								// anchoring the function. Release via KitsuneVariableFree, which calls
								// luaL_unref. Push back to Lua via PushKitsuneVariable (lua_rawgeti).
								// Cannot be constructed from scratch on the host side in a meaningful way.
#define KITSUNE_TUSERDATA       (7)
#define KITSUNE_TTHREAD         (8)

// Return values for KitsuneGetStatus.
#define KITSUNE_STATUS_NONE      (0)  // id not found (never existed, already released, or compacted)
#define KITSUNE_STATUS_IDLE      (1)  // alive and queued; waiting to be resumed by the scheduler
#define KITSUNE_STATUS_SLEEPING  (2)  // alive but waiting out a Sleep() deadline; can be force-woken via KitsuneResume(id, NULL) — value discarded
#define KITSUNE_STATUS_RUNNING   (3)  // currently executing inside lua_resume
#define KITSUNE_STATUS_DONE      (4)  // finished successfully; result not yet consumed
#define KITSUNE_STATUS_FAULTED   (5)  // finished with a runtime or Lua error; call KitsuneGetError
#define KITSUNE_STATUS_CANCELLED (6)  // stopped by an explicit KitsuneCancel(id) call, or cancel is pending
#define KITSUNE_STATUS_INLINE    (7)  // inline sync call paused in cooperative yield window; calling thread will resume imminently
#define KITSUNE_STATUS_PAUSED    (8)  // suspended inside the coroutine via Pause(); waiting for KitsuneResume(id, value) / task:Resume(value)
#define KITSUNE_STATUS_WAITING   (9)  // suspended inside the coroutine via task:Wait(); can be force-woken via KitsuneResume(id, NULL) — value discarded

// Forward declaration
struct KitsuneKeyValuePairVariableNode;
// Forward declaration required so KitsuneVariable can hold a kitsune_CFunctionData* in its union;
// the full definition follows after kitsune_CFunction is declared.
struct kitsune_CFunctionData;
// Forward declaration required so KitsuneVariable can hold a KitsuneIterator* in its union;
// the full definition follows after kitsune_CFunction is declared.
struct KitsuneIterator;
// Forward declaration required so KitsuneVariable can hold a KitsuneUserData* in its union;
// the full definition follows below.
struct KitsuneUserData;
// Forward declarations for typed structured value types stored in the union.
struct KitsuneDateTime;
struct KitsuneTimeSpan;
struct KitsuneDecimal;
struct KitsuneIdentifier;

struct KitsuneVariable {
	int type; // see KITSUNE_T* constants above
	size_t length; // byte count for KITSUNE_TSTRING and KITSUNE_TUSERDATA __name; char16_t count for KITSUNE_TCHAR16; entry count for KITSUNE_TTABLE; 0 for all other types
	union {
		int ref;                                   // LUA_TTHREAD, LUA_TFUNCTION, LUA_TTABLE: Lua registry reference
		double number;                             // LUA_TNUMBER
		long long integer;                         // KITSUNE_TINTEGER, KITSUNE_TUINT
		bool boolean;                              // LUA_TBOOLEAN
		unsigned char* data;                       // KITSUNE_TSTRING, KITSUNE_TJSON, KITSUNE_TERROR: heap-allocated UTF-8 bytes
		char16_t* char16data;                      // KITSUNE_TCHAR16: heap-allocated char16_t string
		KitsuneKeyValuePairVariableNode* table;    // KITSUNE_TTABLECONTENTS: head of linked list
		kitsune_CFunctionData* cfunction;          // KITSUNE_TCFUNCTION
		KitsuneIterator* iterator;                 // KITSUNE_TITERATOR
		void* lightuserdata;                       // LUA_TLIGHTUSERDATA
		KitsuneUserData* userdata;                 // LUA_TUSERDATA
		KitsuneDateTime* datetime;                 // KITSUNE_TDATETIME
		KitsuneTimeSpan* timespan;                 // KITSUNE_TTIMESPAN
		KitsuneDecimal* decimal;                   // KITSUNE_TDECIMAL
		KitsuneIdentifier* identifier;             // KITSUNE_TIDENTIFIER
	};
};

struct KitsuneUserData {
	char* name; // Name of the userdata
	int ref; // luaL_ref registry reference anchoring the userdata in Lua; release with luaL_unref in KitsuneVariableFree when type == KITSUNE_TUSERDATA
	void* userdata; // Opaque pointer passed to C functions registered in this userdata's metatable; null if it its userdata that was not registered by KitsuneRegisterUserdata
};

// 100-nanosecond ticks since 0001-01-01 (same epoch as .NET DateTime), plus a signed UTC offset.
struct KitsuneDateTime {
	int64_t ticks;          // 100-ns intervals since 0001-01-01 00:00:00 UTC
	int16_t offset_minutes; // UTC offset in minutes; 0 = UTC
};

// Signed duration in 100-nanosecond ticks (same representation as .NET TimeSpan).
struct KitsuneTimeSpan {
	int64_t ticks; // 100-ns signed duration; negative = directed backwards
};

// 128-bit decimal value matching the LuaDecimal internal layout.
// Coefficient = hi:lo (unsigned 128-bit); value = (-1)^negative * (hi:lo) * 10^(-scale).
struct KitsuneDecimal {
	uint64_t lo;       // Low 64 bits of coefficient
	uint64_t hi;       // High 64 bits of coefficient
	int16_t  scale;    // Decimal digits after the point
	uint8_t  negative; // 1 = negative, 0 = positive
};

// Identifier (UUID or MongoDB ObjectID).
struct KitsuneIdentifier {
	uint8_t type;      // 0 = UUID (16 bytes used), 1 = OID (12 bytes used)
	uint8_t bytes[16]; // Raw identifier bytes; for OID only bytes[0..11] are meaningful
};

// Iterator struct for KITSUNE_TITERATOR values.
// First time the it calls first, subsequent it calls next.
// When Lua GC's the iterator (e.g. after a for-in loop), finalized is called to free any remaining resources.
// Iteration should break if first or next returns a KitsuneVariable with type == KITSUNE_TNONE.
struct KitsuneIterator {
	kitsune_CFunctionData* first; // First time the iterator is called
	kitsune_CFunctionData* next; // Subsequent calls after the first
	kitsune_CFunctionData* finalized; // Called when garbage collected
	void* userdata;
};

// Intrusive linked list node for KITSUNE_TTABLECONTENTS values.
// When a KitsuneVariable has type == KITSUNE_TTABLECONTENTS, its table field points to the head of this list.
// Keys and values are bridged the same way as FillKitsuneVariableFromStack: nested tables appear as
// live KITSUNE_TTABLE registry refs. Call KitsuneVariableFree to recursively release the list.
struct KitsuneKeyValuePairVariableNode {
	KitsuneVariable key;
	KitsuneVariable value;
	KitsuneKeyValuePairVariableNode* next;
};

// Called once per key-value entry during a KitsuneGetAll traversal.
// key and value are temporary copies valid only for the duration of this call.
// Copy any data you need before returning; do not store the data pointers beyond the callback.
// userdata is the opaque pointer passed to KitsuneGetAll.
// CONSTRAINT: KitsuneGetAll holds accessLock for the entire traversal. From a non-scheduler
// thread any function that also acquires accessLock (KitsuneSetVariable, KitsuneGetVariable,
// KitsuneCallMethod, etc.) will deadlock. Only lock-free functions are safe in that context:
// KitsuneVariableFree, KitsuneGetStatus, KitsuneCancel, KitsuneGetRuntime, etc.
// Exception: if KitsuneGetAll is itself called from within a kitsune_CFunction (scheduler
// thread), accessLock is not held and the same safe/unsafe set as kitsune_CFunction applies.
typedef void (*kitsune_KeyValuePairCallback)(const KitsuneVariable* key, const KitsuneVariable* value, void* userdata);

// Callback passed to kitsune_CFunction to return one or more values to Lua.
// Call once per return value; each call pushes one value onto the Lua stack.
// The number of calls determines the number of values Lua receives (e.g. call twice to
// return two values the way a regular Lua function does with 'return a, b').
// Pass KITSUNE_TERROR with an optional error message in data to raise a Lua error instead.
// Returns non-zero if the value was stored, 0 on failure (e.g. allocation error for KITSUNE_TERROR).
// The caller retains ownership of the KitsuneVariable and any data it points to for the duration of the call.
typedef int (*kitsune_ResultSetter) (const KitsuneVariable* result);

// Signature for C functions registered via KitsuneRegisterFunction.
// Parameters:
//   argc         — number of arguments passed from Lua (may be 0).
//   argv         — read-only array of argc KitsuneVariable arguments, each valid ONLY for the
//                  duration of this call. The engine owns the array and all values inside it;
//                  do not store pointers into it or modify it.
//   resultSetter — call once per return value to push a result onto the Lua stack.
//                  Passing a KITSUNE_TERROR variable raises a Lua error instead.
//                  Valid only within this function call; do not cache or call after returning.
//   userdata     — the opaque pointer supplied to KitsuneRegisterFunction; lifetime is
//                  caller-managed and may be NULL.
// Return > 0 on success, <= 0 to raise a generic "delegate function error" in Lua.
// CONSTRAINTS — the following are NOT safe to call from within this callback:
//   KitsuneExecuteFileAsync / StringAsync / FunctionAsync / VariableAsync:
//     Explicitly return -1 on the scheduler thread.
//   KitsuneGC:
//     Calls AcquireLuaAccess directly. Sets pauseFlag then waits for pausedEvent, which is
//     only signalled by the Ticker between Lua bytecode instructions — it never fires while
//     a C function is executing. Deadlock.
//   KitsuneWait:
//     Waits for runningCount to reach 0 via doneCV. The scheduler thread is occupied
//     running this callback so the count never drops. Deadlock.
//   KitsuneCleanup:
//     Destroys the engine while it is in use.
// Everything else is safe:
//   KitsuneSetVariable, KitsuneGetVariable, KitsuneGetAll, KitsuneRegisterFunction,
//   KitsuneRegisterUserdata, KitsuneCallMethod, KitsuneCallMetamethod, and all
//   coroutine-query functions (KitsuneGetStatus, KitsuneCancel, KitsuneHasResult, etc.)
//   use LuaAccessGuard, which is a no-op on the scheduler thread.
//   The synchronous KitsuneExecuteFile / String / Function / Variable detect the
//   scheduler-thread context and run via a re-entrant tight-loop path; Sleep() and
//   Yield() inside the called script are no-ops in this case.
// lua_State* is intentionally not exposed.
typedef int (*kitsune_CFunction) (int argc, const KitsuneVariable* argv, const kitsune_ResultSetter resultSetter, void* userdata);

typedef void (*kitsune_Finalizer) (void* userdata);

// Holds the function pointer and userdata for a KITSUNE_TCFUNCTION variable.
// Set KitsuneVariable.data to a pointer to one of these to pass an anonymous C function to Lua
// without registering it in the global table. The struct only needs to be alive for the duration
// of the PushKitsuneVariable call; after that the func and userdata values are captured by value
// in Lua closure upvalues and the struct itself is no longer referenced.
struct kitsune_CFunctionData {
	kitsune_CFunction func; // C function to wrap as a Lua closure
	void* userdata; // opaque userdata passed to func on each call
	kitsune_Finalizer finalizer; // optional finalizer called when the Lua closure is garbage collected; receives the same userdata pointer. Set to NULL if no finalizer is needed.
};

// A named entry in a userdata's method or metamethod table.
struct KitsuneNamedFunction {
	char* name;
	kitsune_CFunction func;
	void* userdata; // Opaque pointer passed to func when this method is called
	kitsune_Finalizer finalizer; // Optional: called with userdata when the Lua closure is GC'd. Set to NULL if not needed.
	KitsuneNamedFunction* Next;
};

// Passed to KitsuneRegisterUserdata to describe the methods and metamethods of a userdata type.
struct KitsuneUserDataRegistration {
	KitsuneNamedFunction* MetaTableFunctions; // Meta functions names should match lua https://www.lua.org/pil/13.html
	KitsuneNamedFunction* Functions; // Functions added to the userdata metatable
};

struct KitsuneMemoryAllocator {
	void* (*malloc)(size_t);
	void* (*realloc)(void*, size_t);
	void  (*free)(void*);
};

struct KitsuneInternals {
	KitsuneMemoryAllocator Allocator; // The *current* memory allocator in use by the engine. Initially set to the standard malloc/realloc/free, but can be overridden by passing a custom allocator to KitsuneInit. The engine does not take ownership of the function pointers; they must remain valid for the duration of the engine's lifecycle.
	void(*MongoDbInit)(); // Holds the internal function pointer to mongodb init, if this is called from the outside you must also call MongoDbCleanUp when cleaning up the engine, otherwise you will leak memory and other resources used by the MongoDB client.
	void(*MongoDbCleanUp)();
};

extern "C" {

	// Returns a pointer to the engine's internal state, including the active memory allocator
	// and optional module lifecycle hooks. The returned pointer is to a static struct and is
	// always valid — it does not require the engine to be initialised first and never needs
	// to be freed.
	//
	// Usage is entirely optional. The most common reasons to call this are:
	//
	//   1. Sharing the engine's allocator with your own code:
	//        KitsuneInternals* internals = KitsuneGetInternals();
	//        void* buf = internals->memoryAllocator.malloc(size);
	//        internals->memoryAllocator.free(buf);
	//      This ensures your allocations come from the same heap as the engine, which matters
	//      when USEMEMORYMANAGER or USEHEAPALLOC is defined or a custom allocator was provided.
	//
	//   2. Overriding the allocator before KitsuneInit:
	//        KitsuneMemoryAllocator alloc = { my_malloc, my_realloc, my_free };
	//        KitsuneGetInternals(&alloc); // installs alloc; call KitsuneInit as normal afterwards
	//      Equivalent to passing the allocator directly to KitsuneInit, but lets you install it
	//      before any other initialisation (e.g. before MongoDB's one-time init runs).
	//
	//   3. Explicit MongoDB lifecycle management (debug / hosted-process scenarios):
	//      In a single-session executable the CRT debug heap will report MongoDB's 9 one-time
	//      init allocations as leaks because mongoc_cleanup() normally runs via DLL atexit, after
	//      _CrtDumpMemoryLeaks. Calling MongoDbInit / MongoDbCleanUp manually brackets the
	//      allocations inside the CRT diff window so the check stays clean:
	//
	//        KitsuneInternals* internals = KitsuneGetInternals();
	//        // ... KitsuneInit, run, KitsuneCleanup ...
	//        if (internals->MongoDbCleanUp) internals->MongoDbCleanUp();
	//        // now take the sNew CRT checkpoint — MongoDB allocs are already freed
	//
	//      Multi-session hosts (e.g. the test runner) should NOT call MongoDbCleanUp between
	//      sessions; MongoDB cannot be re-initialised after cleanup. Omitting the call is safe —
	//      the engine's own allocator exempts the init allocations from its leak counter.
	KITSUNE_API KitsuneInternals* KitsuneGetInternals(KitsuneMemoryAllocator* KitsuneMemoryAllocator = nullptr);

	// Initialise the engine and create the Lua state.
	// Returns true if the engine was just initialised by this call (the caller owns the lifecycle).
	// Returns false if the engine was already initialised by another caller, or on failure.
	// Callers that receive false must not call KitsuneCleanup.
	KITSUNE_API bool KitsuneInit(KitsuneMemoryAllocator* KitsuneMemoryAllocator = nullptr);

	// Destroy the Lua state and clean up the engine.
	KITSUNE_API size_t KitsuneCleanup();

	// Perform garbage collection or query memory useage.
	// Mode = 0: do no garbage collection. Only return the current usage.
	// Mode = 1: perform a full garbage collection cycle and return the usage after collection.
	// Mode = 2: perform an incremental step of garbage collection and return the current usage.
	// Mode = 3: Pause garbage collection. Returns the current usage. Warning: if you pause garbage collection and never restart it, memory usage will grow without bound until the process runs out of memory and crashes or you mantually call KitsuneGC with mode 1 or 2 or resume it with mode 4.
	// Mode = 4: Restart garbage collection. Returns the current usage.
	KITSUNE_API long KitsuneGC(int mode = 1);

	// Frees a KitsuneVariable returned by KitsuneGetResult or KitsuneGetVariable
	// (frees the string data if present, then the struct pointer itself). Safe on NULL.
	KITSUNE_API void KitsuneVariableFree(KitsuneVariable* var);

	// Registers a C function as a global callable from Lua.
	// name is a dot-separated path (e.g. "Foo" or "Ns.Foo"); intermediate tables are created.
	// See kitsune_CFunction for the full list of constraints on what may be called from within func.
	// finalizer is optional: called with userdata when the Lua closure is garbage collected.
	KITSUNE_API void KitsuneRegisterFunction(const char* name, kitsune_CFunction func, void* userdata = nullptr, kitsune_Finalizer finalizer = nullptr);

	// Registers a userdata type with the engine, allowing Lua to recognise it and call its metamethods.
	// Fails if the name is already taken, or if registration->MetaTableFunctions is missing
	// a __gc entry (releases the managed GCHandle) or a __tostring entry (enables tostring()).
	KITSUNE_API bool KitsuneRegisterUserdata(const char* name, const KitsuneUserDataRegistration* registration);

	// -- Execution
	// All four functions execute synchronously: they block the calling thread until
	// the script finishes and return the typed result directly. Returns NULL on start
	// failure (e.g. engine not initialised, no slots available). On success the caller
	// MUST free the returned pointer with KitsuneVariableFree.
	// A result with type KITSUNE_TNONE means the script returned no value.
	// A result with type KITSUNE_TERROR means the script raised a Lua error;
	// the error message is in result->data (UTF-8, length in result->length).
	// Calling from within a kitsune_CFunction IS supported via a re-entrant tight-loop
	// path; Sleep() and Yield() inside the called script are no-ops in that case.
	// Do not call recursively from the same non-scheduler thread while already blocking
	// on another sync Execute call on that thread.
	KITSUNE_API KitsuneVariable* KitsuneExecuteFile(const char* path, int argc, const KitsuneVariable* argv);
	KITSUNE_API KitsuneVariable* KitsuneExecuteString(const char* script, int argc, const KitsuneVariable* argv);
	KITSUNE_API KitsuneVariable* KitsuneExecuteFunction(const char* functionName, int argc, const KitsuneVariable* argv);
	KITSUNE_API KitsuneVariable* KitsuneExecuteVariable(const KitsuneVariable* var, int argc, const KitsuneVariable* argv);

	// -- Async Execution --------------------------------------------------------
	// All four functions start execution as a Lua coroutine managed by the scheduler.
	// Returns a positive coroutine ID on success, or -1 on failure.
	// KitsuneExecuteFunctionAsync still returns a positive ID when the named function does not exist;
	// in that case KitsuneHasResult will return true immediately with error "function not found".
	// When fireAndForget is true the slot is freed automatically on completion;
	// do not call KitsuneHasResult / KitsuneGetResult for that id.
	KITSUNE_API int KitsuneExecuteFileAsync(const char* path, int argc, const KitsuneVariable* argv, bool fireAndForget = false);
	KITSUNE_API int KitsuneExecuteStringAsync(const char* script, int argc, const KitsuneVariable* argv, bool fireAndForget = false);
	KITSUNE_API int KitsuneExecuteFunctionAsync(const char* functionName, int argc, const KitsuneVariable* argv, bool fireAndForget = false);
	KITSUNE_API int KitsuneExecuteVariableAsync(const KitsuneVariable* var, int argc, const KitsuneVariable* argv, bool fireAndForget = false);

	// -- Per-coroutine queries (id = value returned by KitsuneExecuteFileAsync/StringAsync) --
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
	// Un-pauses a coroutine that was suspended by Pause() or that was created by Tasks.New
	// but not yet started (task:Start()). The optional `value` (may be NULL) is delivered as
	// the return value of Pause() inside the coroutine on its next resume. Returns true if the
	// coroutine was found and was in a paused state and was successfully resumed. Returns false
	// if it was not found, not paused, or already running. Thread-safe.
	KITSUNE_API bool KitsuneResume(int id, const KitsuneVariable* value);
	// Returns how long the coroutine has been alive in milliseconds, measured from when it was created.
	// Returns 0.0 if the id is not found. Thread-safe.
	KITSUNE_API double KitsuneGetRuntime(int id);
	// Returns the current status of the coroutine identified by id. See KITSUNE_STATUS_* constants.
	// Thread-safe.
	KITSUNE_API int KitsuneGetStatus(int id);

	// -- Global control --------------------------------------------------------
	// Returns true if any coroutine slot exists (running, sleeping, idle, or finished but not yet released). Thread-safe.
	KITSUNE_API bool KitsuneIsRunning();
	// Returns the ID of the first coroutine that is still running, or 0 if none are active. Thread-safe.
	KITSUNE_API int  KitsuneGetRunningId();
	// Signals all running coroutines to stop at the next instruction boundary. Thread-safe.
	KITSUNE_API void KitsuneInterrupt();
	// Blocks the calling thread until all coroutine slots are gone (finished and released). Thread-safe.
	KITSUNE_API void KitsuneWait();
	// Returns all unreleased coroutine IDs (running or awaiting result consumption).
	// Fills buffer with up to bufferSize IDs and returns the total count.
	// Pass a NULL buffer to query the count without filling. Thread-safe.
	KITSUNE_API int KitsuneGetActiveIds(int* buffer, int bufferSize);

	// -- Coroutine naming ------------------------------------------------------
	// Associates a human-readable name with the coroutine identified by id.
	// Returns false if the name is already in use by another coroutine, or if id is not found.
	// Pass NULL to clear the name. Thread-safe.
	KITSUNE_API bool KitsuneSetName(int id, const char* name);
	// Returns the name associated with the coroutine identified by id, copied into buf
	// (always null-terminated, truncated to bufSize-1 bytes if needed).
	// Pass buf=NULL / bufSize=0 to query the required byte length (excluding null terminator).
	// Returns 0 if the id is not found or has no name. Thread-safe.
	KITSUNE_API size_t KitsuneGetName(int id, char* buf, size_t bufSize);
	// Returns the coroutine id whose name matches the given name, or 0 if not found. Thread-safe.
	KITSUNE_API int KitsuneGetId(const char* name);

	// -- Variable bridge -------------------------------------------------------
	// Sets a Lua global at the given dot-separated path (e.g. "foo" or "foo.bar.baz").
	// The Lua global environment is the root; intermediate tables are created automatically.
	// Pass NULL or KITSUNE_TNONE to remove the key. Pass type==KITSUNE_TTABLE to set an empty table.
	// Key components must not contain '.'. String data is only read for the duration of the call.
	// Thread-safe.
	KITSUNE_API bool KitsuneSetVariable(const char* path, const KitsuneVariable* var);
	// Returns the Lua global at the given dot-separated path as a heap-allocated typed variable.
	// If path is NULL or "", returns the global table (_G) itself as a KITSUNE_TTABLE registry ref,
	// which can be used directly with KitsuneGetIndex, KitsuneSetIndex, KitsuneGetAll, etc.
	// Returns NULL if not found or if any intermediate component is not a table.
	// Call KitsuneVariableFree on the result when done. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneGetVariable(const char* path);
	// Iterates all entries at the given dot-separated path in the Lua global environment,
	// invoking callback once per key-value pair. Pass NULL or "" to iterate _G itself.
	// key and value are temporary — valid only for the duration of each call. Thread-safe.
	KITSUNE_API void KitsuneGetAll(const char* path, kitsune_KeyValuePairCallback callback, void* userdata);
	// Registers a KitsuneVariable on the lua registry and returns an integer reference. Thread-safe.
	KITSUNE_API int KitsuneRegister(const KitsuneVariable* var);
	// Retrieves a registered KitsuneVariable from the lua registry by its integer reference.
	// Call KitsuneVariableFree on the result when done. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneGetByReference(int ref);
	// Unregisters a KitsuneVariable from the lua registry by its integer reference and returns it.
	// Call KitsuneVariableFree on the result when done. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneUnregister(int ref);
	// Pushes var onto the Lua stack (converting to a native Lua value), anchors the result
	// in the registry, and returns a new heap-allocated KitsuneVariable that owns that anchor.
	// The returned variable remains valid until freed with KitsuneVariableFree, regardless of
	// the lifetime of the original var.
	//
	// Common uses:
	//   1. Extend the lifetime of a temporary variable from a callback or tight scope:
	//        KitsuneVariable* kept = KitsuneAnchorVariable(argv[0]);
	//   2. Create a deep copy of a value-type variable (string, number, etc.) that
	//      can be freed independently of the original.
	//   3. Create a second handle to a reference-type variable (table, function, userdata)
	//      that points to the same Lua object but can be freed independently.
	//   4. Create a new empty anchored table — pass a KITSUNE_TTABLECONTENTS variable
	//      with table == NULL; PushKitsuneVariable creates a fresh lua_newtable and
	//      FillKitsuneVariableFromStack anchors it:
	//        KitsuneVariable cv = { .type = KITSUNE_TTABLECONTENTS };
	//        KitsuneVariable* emptyTable = KitsuneAnchorVariable(&cv); // type == LUA_TTABLE
	//   5. Create a populated table from a KITSUNE_TTABLECONTENTS snapshot (same mechanism
	//      as above, with a non-NULL linked list).
	// Returns NULL on failure. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneAnchorVariable(const KitsuneVariable* var);
	// Snapshots the contents of a live KITSUNE_TTABLE variable into a heap-allocated KITSUNE_TTABLECONTENTS.
	// tableVar must have type KITSUNE_TTABLE with a valid registry ref (ref != LUA_NOREF).
	// Returns NULL on failure. Call KitsuneVariableFree on the result when done. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneGetTableContents(const KitsuneVariable* tableVar);
	// Replaces the contents of a live Lua table (tableVar, type KITSUNE_TTABLE) with the snapshot
	// from contentsVar (type KITSUNE_TTABLECONTENTS). All existing keys are removed first (replace,
	// not merge). Integer keys restore the array part naturally. Returns false on invalid arguments.
	// Thread-safe.
	KITSUNE_API bool KitsuneSetTableContents(const KitsuneVariable* tableVar, const KitsuneVariable* contentsVar);
	// Gets obj[key] in Lua, firing the __index metamethod if present.
	// obj must be KITSUNE_TTABLE (ref > 0) or KITSUNE_TUSERDATA (non-null userdata).
	// The lookup runs inside a protected call so any __index error surfaces as KITSUNE_TERROR.
	// Return semantics (always non-NULL on a valid obj):
	//   KITSUNE_TNIL   — key is absent or is nil (indistinguishable, matching Lua semantics)
	//   KITSUNE_TERROR — __index raised a Lua error (message in .data)
	//   anything else  — the value at obj[key]
	// Returns NULL only on OOM or invalid obj. Heap-allocated; free with KitsuneVariableFree. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneGetIndex(const KitsuneVariable* obj, const KitsuneVariable* key);
	// Sets obj[key] = value in Lua, firing the __newindex metamethod if present.
	// obj must be KITSUNE_TTABLE (ref > 0) or KITSUNE_TUSERDATA (non-null userdata).
	// Returns false on invalid arguments or if __newindex raised an error. Thread-safe.
	KITSUNE_API bool KitsuneSetIndex(const KitsuneVariable* obj, const KitsuneVariable* key, const KitsuneVariable* value);
	// Returns the result of #obj, firing the __len metamethod if present.
	// For plain tables without __len, returns the raw sequence length.
	// obj must be KITSUNE_TTABLE (ref > 0) or KITSUNE_TUSERDATA (non-null userdata).
	// Return semantics (always non-NULL on a valid obj):
	//   KITSUNE_TINTEGER or KITSUNE_TNUMBER — the length value
	//   KITSUNE_TERROR — __len raised a Lua error (message in .data)
	// Returns NULL only on OOM or invalid obj. Heap-allocated; free with KitsuneVariableFree. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneGetLength(const KitsuneVariable* obj);
	// Advances iteration over a live KITSUNE_TTABLE by one raw step (no metamethods; mirrors lua_next).
	// tableVar: must be KITSUNE_TTABLE (ref > 0).
	// key ownership/cursor rules:
	//   NULL or type != KITSUNE_TTABLECONTENTS ? start from the beginning; key is not consumed.
	//   KITSUNE_TTABLECONTENTS (result of a prior KitsuneNext call) ? advance one step; key is
	//     consumed (freed by this call). Do NOT use or free key after passing it here.
	// Return semantics:
	//   KITSUNE_TTABLECONTENTS (1 entry, node->next == NULL) — next key-value pair. The embedded
	//     key and value are valid until this result is consumed by the next KitsuneNext call or freed
	//     with KitsuneVariableFree. To hold the key/value independently (e.g. in a collection),
	//     call KitsuneNextGetEntry first.
	//   KITSUNE_TNONE  — table exhausted (no more entries).
	//   KITSUNE_TERROR — lua_next raised an error (key was invalidated by concurrent modification).
	//   NULL — OOM or invalid tableVar.
	// Thread safety: if the table is modified between calls, behavior mirrors Lua's own next(t, k).
	//   For guaranteed-atomic traversal, use KitsuneGetAll which holds accessLock for the full walk.
	// Heap-allocated; free the final TNONE/TERROR result with KitsuneVariableFree. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneNext(const KitsuneVariable* tableVar, KitsuneVariable* key);
	// Calls a named metamethod directly from obj's metatable: getmetatable(obj).__name(obj, args...).
	// Return semantics (always non-NULL on a valid obj):
	//   KITSUNE_TNONE  — metamethod is absent from the metatable
	//   KITSUNE_TNIL   — metamethod ran but returned nothing (or explicitly returned nil)
	//   KITSUNE_TERROR — metamethod raised a Lua error (message in .data)
	//   anything else  — the metamethod's first return value
	// Returns NULL only on OOM or invalid obj. Heap-allocated; free with KitsuneVariableFree. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneCallMetamethod(const KitsuneVariable* obj, const char* metamethod, int argc, const KitsuneVariable* argv);
	// Looks up method on obj via __index and calls it with obj as self: obj:method(args...).
	// obj must be KITSUNE_TTABLE (ref > 0) or KITSUNE_TUSERDATA (non-null userdata).
	// The __index lookup and the call each run in their own protected call so any error surfaces
	// cleanly without bypassing LuaAccessGuard.
	// Return semantics (always non-NULL on a valid obj):
	//   KITSUNE_TNONE  — method is absent or not callable (nil/non-function from __index)
	//   KITSUNE_TNIL   — method ran but returned nothing (or explicitly returned nil)
	//   KITSUNE_TERROR — __index or the method raised a Lua error (message in .data)
	//   anything else  — the method's first return value
	// Returns NULL only on OOM or invalid obj. Heap-allocated; free with KitsuneVariableFree. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneCallMethod(const KitsuneVariable* obj, const char* method, int argc, const KitsuneVariable* argv);
	// Returns the contents of a live KITSUNE_TTABLE variable as a KITSUNE_TJSON.
	// The JSON string is UTF-8 encoded and null-terminated, with byte length in .length (excluding null terminator).
	// KITSUNE_TNONE = var is not a table; KITSUNE_TERROR = serialization error (message in .data). Returns NULL on OOM. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneGetTableContentsAsJson(const KitsuneVariable* var);
	// Returns the kitsune variable as a string. For tables, this is the same as luaL_tolstring (invoking metamethod) or the same as passing a value through luas tostring().
	// For other types, it's a reasonable string representation (e.g. numbers are converted to their literal string form).
	// Returns NULL on OOM. Thread-safe.
	KITSUNE_API KitsuneVariable* KitsuneToString(const KitsuneVariable* var);
}

// ---------------------------------------------------------------------------
// Numeric coercion helpers — safe to call from any context including kitsune_CFunction.
// These mirror Lua's own coercion rules: integer beats number beats string.
// ---------------------------------------------------------------------------
#include <cstdlib>

// Coerce to float. Integer?cast, number?cast, string?strtof, else?fallback.
static inline float KitsuneAsFloat(const KitsuneVariable* v, float fallback) {
	if (!v) return fallback;
	if (v->type == KITSUNE_TINTEGER) return (float)v->integer;
	if (v->type == KITSUNE_TUINT)    return (float)(unsigned long long)v->integer;
	if (v->type == KITSUNE_TNUMBER)  return (float)v->number;
	if (v->type == KITSUNE_TSTRING && v->data && v->length > 0) {
		char* end = nullptr;
		float f = strtof((const char*)v->data, &end);
		return (end != (const char*)v->data) ? f : fallback;
	}
	return fallback;
}

// Coerce to double. Integer?cast, number?pass-through, string?strtod, else?fallback.
static inline double KitsuneAsDouble(const KitsuneVariable* v, double fallback) {
	if (!v) return fallback;
	if (v->type == KITSUNE_TINTEGER) return (double)v->integer;
	if (v->type == KITSUNE_TUINT)    return (double)(unsigned long long)v->integer;
	if (v->type == KITSUNE_TNUMBER)  return v->number;
	if (v->type == KITSUNE_TSTRING && v->data && v->length > 0) {
		char* end = nullptr;
		double d = strtod((const char*)v->data, &end);
		return (end != (const char*)v->data) ? d : fallback;
	}
	return fallback;
}

// Coerce to long long. Integer?pass-through, number?truncate, boolean?0/1,
// string?strtoll base-10, else?fallback.
static inline long long KitsuneAsInt(const KitsuneVariable* v, long long fallback) {
	if (!v) return fallback;
	if (v->type == KITSUNE_TINTEGER) return v->integer;
	if (v->type == KITSUNE_TUINT)    return v->integer;  // same bit pattern — caller interprets as signed
	if (v->type == KITSUNE_TNUMBER)  return (long long)v->number;
	if (v->type == KITSUNE_TBOOLEAN) return v->boolean ? 1LL : 0LL;
	if (v->type == KITSUNE_TSTRING && v->data && v->length > 0) {
		char* end = nullptr;
		long long i = strtoll((const char*)v->data, &end, 10);
		return (end != (const char*)v->data) ? i : fallback;
	}
	return fallback;
}

// Coerce to bool. Mirrors Lua truthiness exactly: only nil and false are falsy.
// Unlike C, 0 / 0.0 / "" are all truthy — matching Lua's own rules.
//   nil / none            -> false
//   boolean               -> as-is
//   anything else         -> true
static inline bool KitsuneAsBool(const KitsuneVariable* v) {
	if (!v) return false;
	if (v->type == KITSUNE_TNIL || v->type == KITSUNE_TNONE) return false;
	if (v->type == KITSUNE_TBOOLEAN) return v->boolean;
	return true;
}
