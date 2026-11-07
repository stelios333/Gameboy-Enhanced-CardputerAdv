// Per-game SGB / 12-colour palette profile detection.
#pragma once

#include <stdint.h>
#include "fe_walnut.h"

namespace fe {

// Compares the 16-byte cartridge title at 0x134 with the built-in table.
// Returns 1 when a profile matched, 0 otherwise. Special titles may set
// gSettings.forceCompat and/or gSharedFlags[4].
uint8_t loadSgbProfile(gb_s* gb);

}  // namespace fe
