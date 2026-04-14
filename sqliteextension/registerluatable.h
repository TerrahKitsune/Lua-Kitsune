#pragma once
#include "dllmain.h"

// userdata = KitsuneExtState* — register_table_cb reads db via extState->db.
int register_table_cb(int argc, const KitsuneVariable* argv, kitsune_ResultSetter resultSetter, void* userdata);
