// Settings menu, persistence and key binding helpers.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "fe_config.h"
#include "fe_state.h"

namespace fe {

// /gbeconfig.dat
void settingsLoad();
bool settingsSave();
void settingsRestoreDefaults();

// Interactive menu (returns when the user closes it).
void settingsMenu();

// Format the current value of menu item `index` into `out`.
void settingsFormatValue(char* out, size_t outSize, const char* name, int index);

// Menu item table (18 entries).
extern const char* const kSettingsMenuItems[18];

// Waits for a key press and returns the key, or 0x13 for Enter.
uint8_t waitForKey();

}  // namespace fe
