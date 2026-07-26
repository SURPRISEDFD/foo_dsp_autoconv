#pragma once
#include "preset.h"

// Modal configuration dialog (plain Win32, no WTL/ATL dependency).
// Returns true if the user pressed OK; cfg is updated in-place.
bool run_config_dialog(HWND parent, autoconv_preset & cfg);
