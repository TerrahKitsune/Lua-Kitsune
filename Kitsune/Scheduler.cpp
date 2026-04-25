#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "Scheduler.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

void SchedulerInit(SchedulerState* s) {
    s->head = nullptr;
    s->onError = nullptr;
}

void SchedulerSetOnError(SchedulerState* s, KitsuneVariable* onError) {
    if (s->onError)
        KitsuneVariableFree(s->onError);
    s->onError = onError;
}

void SchedulerDrain(SchedulerState* s) {
    ScheduledCall** prev = &s->head;
    ScheduledCall*  call = s->head;
    while (call) {
        if (call->runningId == 0) {
            if (call->argc > 0) {
                KitsuneVariable* values = (KitsuneVariable*)malloc(call->argc * sizeof(KitsuneVariable));
                if (values) {
                    for (int i = 0; i < call->argc; i++)
                        values[i] = *call->argv[i];
                    call->runningId = KitsuneExecuteVariableAsync(call->fn, call->argc, values, false);
                    free(values);
                }
            }
            else {
                call->runningId = KitsuneExecuteVariableAsync(call->fn, 0, nullptr, false);
            }
            for (int i = 0; i < call->argc; i++)
                KitsuneVariableFree(call->argv[i]);
            free(call->argv);
            call->argv = nullptr;
            call->argc = 0;
            KitsuneVariableFree(call->fn);
            call->fn = nullptr;

            if (call->runningId <= 0) {
                *prev = call->next;
                ScheduledCall* next = call->next;
                free(call);
                call = next;
                continue;
            }
        }
        else if (KitsuneHasResult(call->runningId)) {
            KitsuneVariable* result = KitsuneGetResult(call->runningId);
            if (result && result->type == KITSUNE_TERROR) {
                if (s->onError) {
                    KitsuneVariable* ret = KitsuneExecuteVariable(s->onError, 1, result);
                    KitsuneVariableFree(ret);
                }
                else {
                    fprintf(stderr, "[Schedule] error: %.*s\n",
                        (int)result->length, (char*)result->data);
                }
            }
            KitsuneVariableFree(result);
            *prev = call->next;
            ScheduledCall* next = call->next;
            free(call);
            call = next;
            continue;
        }

        prev = &call->next;
        call = call->next;
    }
}

void SchedulerFree(SchedulerState* s) {
    while (s->head) {
        ScheduledCall* call = s->head;
        s->head = call->next;
        if (call->runningId > 0) {
            KitsuneVariable* result = KitsuneGetResult(call->runningId);
            KitsuneVariableFree(result);
        }
        else {
            for (int i = 0; i < call->argc; i++)
                KitsuneVariableFree(call->argv[i]);
            KitsuneVariableFree(call->fn);
            free(call->argv);
        }
        free(call);
    }
    if (s->onError) {
        KitsuneVariableFree(s->onError);
        s->onError = nullptr;
    }
}

bool SchedulerPush(SchedulerState* s, KitsuneVariable* fn, int argc, KitsuneVariable** argv) {
    ScheduledCall* call = (ScheduledCall*)calloc(1, sizeof(ScheduledCall));
    if (!call)
        return false;
    call->fn = fn;
    call->argc = argc;
    call->argv = argv;
    call->next = s->head;
    s->head = call;
    return true;
}

// ---------------------------------------------------------------------------
// Global scheduler used by non-imgui (headless) scripts
// ---------------------------------------------------------------------------

static SchedulerState* g_schedulerState = nullptr;

static int Schedule_Lua(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    SchedulerState* s = (SchedulerState*)userdata;
    if (!s || argc < 1 || argv[0].type != KITSUNE_TFUNCTION)
        return 0;

    KitsuneVariable* fn = KitsuneAnchorVariable(&argv[0]);
    int extraArgc = argc - 1;
    KitsuneVariable** extraArgv = nullptr;
    if (extraArgc > 0) {
        extraArgv = (KitsuneVariable**)malloc(extraArgc * sizeof(KitsuneVariable*));
        if (!extraArgv) {
            KitsuneVariableFree(fn);
            return 0;
        }
        for (int i = 0; i < extraArgc; i++)
            extraArgv[i] = KitsuneAnchorVariable(&argv[i + 1]);
    }

    SchedulerPush(s, fn, extraArgc, extraArgv);
    return 0;
}

static int SchedulerSetOnError_Lua(int argc, const KitsuneVariable* argv,
    const kitsune_ResultSetter setter, void* userdata) {
    SchedulerState* s = (SchedulerState*)userdata;
    if (!s)
        return 0;
    if (argc < 1 || argv[0].type != KITSUNE_TFUNCTION) {
        SchedulerSetOnError(s, nullptr);
        return 0;
    }
    SchedulerSetOnError(s, KitsuneAnchorVariable(&argv[0]));
    return 0;
}

void SchedulerRegister(SchedulerState* s) {
    g_schedulerState = s;
    KitsuneRegisterFunction("Schedule", Schedule_Lua, s);
    KitsuneRegisterFunction("Scheduler.SetOnError", SchedulerSetOnError_Lua, s);
}
