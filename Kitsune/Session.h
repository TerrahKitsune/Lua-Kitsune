#pragma once
#include "KitsuneEngine.h"

// Registers Session.Console, Session.Display and Session.Clipboard into Lua.
// Intermediate tables are created automatically by KitsuneRegisterFunction.
void RegisterSessionFunctions();
