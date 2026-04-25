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

// -- Per-coroutine slot -------------------------------------------------------
struct KitsuneCoroutine {
    int           id;
    int           threadRef;
    lua_State*    thread;
    int           argsRef;
    std::atomic<long> fireAndForget{ 0 };
    std::atomic<long> done{ 0 };
    std::atomic<long> released{ 0 };
    std::atomic<long> interrupted{ 0 };
    char*         error;
    KitsuneVariable result;
    double        sleepUntil;
    int           sleepTokenRef;
    double        startTime;
    int           initialNArgs;
    std::atomic<long> isInline{ 0 };
    std::atomic<long> paused{ 0 };
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
