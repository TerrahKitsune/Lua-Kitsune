#pragma once
// Internal engine types and helpers shared between KitsuneEngine.cpp and sibling modules
// (e.g. luatask.cpp). Not part of the public KitsuneEngine.h API surface.

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <thread>
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
#define KITSUNE_COROUTINE_STATE_INTERRUPTED 4  // will be cancelled on the next scheduler tick
#define KITSUNE_COROUTINE_STATE_SLEEPING    5  // sleeping until a deadline or token expires
#define KITSUNE_COROUTINE_STATE_PAUSED      6  // suspended until KitsuneResume is called

// -- Per-coroutine slot -------------------------------------------------------
struct KitsuneCoroutine {
    int           id;
    int           threadRef;
    lua_State*    thread;
    int           argsRef;
    std::atomic<long> fireAndForget{ 0 };
    std::atomic<long> state{ 0 };
    char*         error;
    KitsuneVariable result;
    double        sleepUntil;
    int           sleepTokenRef;
    double        startTime;
    int           initialNArgs;
    std::atomic<long> isInline{ 0 };
    char*         name;
    int           luaRefCount;
    bool          apiOwned;
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
    int               taskErrorHandlerRef{ LUA_NOREF };
};

// -- Global state (defined in KitsuneEngine.cpp) ------------------------------
extern KitsuneState* g_state;

// -- Internal helpers (defined in KitsuneEngine.cpp) --------------------------
KitsuneCoroutine* FindSlot(KitsuneState* state, int id);
int               GetSlotStatus(KitsuneState* state, KitsuneCoroutine* slot);
double            GetCounter(KitsuneState* state);
lua_State*        CreateCoroutineThread(KitsuneState* state, KitsuneCoroutine* slot);
void              PushKitsuneVariable(lua_State* L, const KitsuneVariable* v);
KitsuneCoroutine* AcquireAsyncSlot(KitsuneState* state, bool fireAndForget, bool& isNewSlot);

// Finalises a Tasks.New slot after fn+args are on the thread stack.
// Increments runningCount, adds to slots[], wakes the scheduler.
// Defined in KitsuneEngine.cpp.
void LaunchTaskSlot(KitsuneState* state, KitsuneCoroutine* slot, bool isNewSlot);
