// Access to the extracted palette and boot-ROM tables.
#pragma once

#include <stdint.h>

namespace fe {

// Each returns 12 RGB888 entries (SGB / GBC / 12-colour palettes).
const uint32_t* sgbPalette(int index);
const uint32_t* gbcPalette(int index);
const uint32_t* twelveColourPalette(int index);

constexpr int kSgbPaletteCount     = 40;
constexpr int kGbcPaletteCount     = 12;
// The table has 51 entries (profile indices reach 0x32), but the firmware's
// user-facing palette cycle wraps at 47.
constexpr int kTwelveColourCount   = 51;
constexpr int kTwelveColourCycle   = 47;

const uint8_t* dmgBootRom();
constexpr int kDmgBootRomSize = 256;

}  // namespace fe
