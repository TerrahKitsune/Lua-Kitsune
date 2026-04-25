#pragma once
#include "KitsuneEngine.h"

// ---------------------------------------------------------------------------
// Generic coroutine scheduler
// ---------------------------------------------------------------------------
// A ScheduledCall is a Lua function (plus optional args) that is submitted
// as an async coroutine and polled until it finishes.  Any error is forwarded
// to the optional onError handler stored in the owning SchedulerState.

struct ScheduledCall {
    KitsuneVariable*  fn;
    int               runningId;  // 0 = not yet submitted, >0 = in-flight
    int               argc;
    KitsuneVariable** argv;
    ScheduledCall*    next;
};

struct SchedulerState {
    ScheduledCall*   head;
    KitsuneVariable* onError;  // nullable; owned by the state
};

// Initialise a SchedulerState to zero.
void SchedulerInit(SchedulerState* s);

// Set (or replace) the onError handler; takes ownership of an anchored var.
void SchedulerSetOnError(SchedulerState* s, KitsuneVariable* onError);

// Submit-or-poll all pending calls; call once per frame / loop tick.
void SchedulerDrain(SchedulerState* s);

// Cancel all calls (blocks on in-flight ones to collect their results).
void SchedulerFree(SchedulerState* s);

// Enqueue a new call.  fn and each argv[i] must be anchored variables;
// ownership is transferred to the scheduler.
bool SchedulerPush(SchedulerState* s, KitsuneVariable* fn, int argc, KitsuneVariable** argv);

// Registers the global "Schedule" and "Scheduler.SetOnError" Lua functions
// that operate on *s. Call once after KitsuneInit().
void SchedulerRegister(SchedulerState* s);
