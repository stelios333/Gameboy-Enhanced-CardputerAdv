// Extracted graphics / palette data tables.
//
// These headers are kept in resources/ and are included exactly once here to
// avoid duplicate definitions.

#include <stdint.h>

#include "fe_palettes.h"

#include "../resources/sgb.h"
#include "../resources/gbc.h"
#include "../resources/palettes_12color.h"
#include "../resources/dmg.h"

namespace fe {

const uint32_t* sgbPalette(int index)
{
    if (index < 0 || index >= kSgbPaletteCount) {
        index = 0;
    }
    return &default32sgb_palettes[index * 12];
}

const uint32_t* gbcPalette(int index)
{
    if (index < 0 || index >= kGbcPaletteCount) {
        index = 0;
    }
    return &GBC_PALETTES[index * 12];
}

const uint32_t* twelveColourPalette(int index)
{
    if (index < 0 || index >= kTwelveColourCount) {
        index = 0;
    }
    return &kPalettes12Colour[index][0];
}

const uint8_t* dmgBootRom()
{
    return dmg_boot_rom;
}

}  // namespace fe
