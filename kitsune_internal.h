#pragma once
// Internal engine types and helpers shared between KitsuneEngine.cpp and sibling modules
// (e.g. luatask.cpp). Not part of the public KitsuneEngine.h API surface.

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <unordered_map>
#include <cstdint>
#include "platform.h"
#include "mem.h"
#include "lua_main_incl.h"
#include "KitsuneEngine.h"

#define KITSUNE_MAX_COROUTINES 256

// -- KitsuneCoroutine state values --------------------------------------------
#define KITSUNE_COROUTINE_STATE_NOT_USED    0  // slot is zeroed and ready for reuse
#define KITSUNE_COROUTINE_STATE_WORKING     1  // alive and being scheduled normally
#define KITSUNE_COROUTINE_STATE_DONE        2  // finished; result/error can be collected
#define KITSUNE_COROUTINE_STATE_RELEASED    3  // done and consumed; scheduler will free the slot
#define KITSUNE_COROUTINE_STATE_ABORTED     4  // condemned; scheduler will cancel and free it at the next tick
#define KITSUNE_COROUTINE_STATE_SLEEPING    5  // sleeping until a deadline or token expires
#define KITSUNE_COROUTINE_STATE_PAUSED      6  // suspended until KitsuneResume is called
#define KITSUNE_COROUTINE_STATE_WAITING     7  // suspended until a target coroutine finishes (or timeout)

// -- Per-coroutine slot -------------------------------------------------------
struct KitsuneCoroutine {
    int           id;
    int           threadRef;
    lua_State*    thread;
    std::atomic<int>  fireAndForget{ 0 };
    std::atomic<long> state{ 0 };
    char*         error;
    KitsuneVariable result;
    int           resumeValueRef;  // luaL_ref of value passed by task:Resume(value); LUA_NOREF if none
    int           onErrorRef;      // luaL_ref of per-task error handler fn(id, err); LUA_NOREF if none
    double        sleepUntil;
    int           sleepTokenRef;
    int           waitingForId;
    double        startTime;
    int           initialNArgs;
    std::atomic<int>  isInline{ 0 };
    char*         name;
    int           luaRefCount;
    bool          apiOwned;
    bool          didWork;  // set by Yield(true) or cooperative C yields with data; cleared by scheduler each tick
};

// -- Engine state -------------------------------------------------------------
struct KitsuneState {
    lua_State*    L;
    double        PCFreq;
    int64_t       CounterStart;
    lua_State*    DelegateState;
    char*         lastCallError;

    std::atomic<long> interrupt{ 0 };
    std::atomic<long> pauseFlag{ 0 };
    PlatformEvent pausedEvent;
    PlatformEvent resumeEvent;

    std::mutex    accessLock;

    std::thread           schedulerThread;
    std::atomic<long>     schedulerStop{ 0 };
    PlatformEvent         workEvent;
    PlatformEvent         schedulerDoneEvent;

    KitsuneCoroutine* slots[KITSUNE_MAX_COROUTINES];
    int               slotCount;
    std::mutex        slotsLock;

    std::mutex              doneMtx;
    std::condition_variable doneCV;

    std::atomic<long> nextId{ 0 };
    std::atomic<long> runningCount{ 0 };
    std::atomic<long> currentCoroutineId{ 0 };
    int               taskErrorHandlerRef{ 0 };

    // Dual-hook registry: one entry per tracked lua_State*.
    // Owned here so there are no dangling globals and cleanup is trivially
    // tied to KitsuneState lifetime.
    std::mutex                                        hookMtx;
    std::unordered_map<lua_State*, KitsuneHookState> hookMap;
};

// -- Global state (defined in KitsuneEngine.cpp) ------------------------------
extern KitsuneState* g_state;

// -- Internal helpers (defined in KitsuneEngine.cpp) --------------------------
KitsuneCoroutine* FindSlot(KitsuneState* state, int id);
int               GetSlotStatus(KitsuneState* state, KitsuneCoroutine* slot);
double            GetCounter(KitsuneState* state);
lua_State*        CreateCoroutineThread(KitsuneState* state, KitsuneCoroutine* slot);
void              PushKitsuneVariable(lua_State* L, const KitsuneVariable* v);
// Frees all owned heap/registry data inside var and zeroes the pointer fields.
// Pass a valid lua_State* to unref Lua registry refs (tables, functions, threads).
// Pass NULL for L only when no registry refs can be present.
void              FreeVariableData(KitsuneVariable* var, lua_State* L);
// Acquires a slot, assigns the next id, increments runningCount, and adds it to slots[] under
// slotsLock. Sets isInline and fireAndForget as requested. This is the only function allowed to
// set id to a non-zero value and elevate state to WORKING. Returns NULL if at capacity or OOM.
KitsuneCoroutine* AcquireSlot(KitsuneState* state, bool isInline, bool fireAndForget);
// Releases all resources held by slot and memsets it back to the zeroed NOT_USED state.
// Caller MUST hold slotsLock. This is the only function that sets id back to 0.
void              FreeSlot(KitsuneState* state, KitsuneCoroutine* slot);

// Finalises a Tasks.New slot after fn+args are on the thread stack.
// Wakes the scheduler. Defined in KitsuneEngine.cpp.
void LaunchTaskSlot(KitsuneState* state);

// Like KitsuneResume but takes a pre-created Lua registry ref as the resume value.
// Pass LUA_NOREF for no value. Takes ownership of the ref (will luaL_unref it on next resume
// or when the slot is freed). Returns false if the slot was not found or not paused.
bool KitsuneResumeRef(int id, int luaRef);
