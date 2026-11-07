// Save states and cartridge RAM persistence.
#pragma once

#include "fe_walnut.h"

namespace fe {

// .sav (cartridge RAM)
void cartRamLoad(gb_s* gb);
void cartRamSave(gb_s* gb);

// .qsv (save state, format version 4)
void saveStateSave(gb_s* gb);
void saveStateLoad(gb_s* gb);

// True when "<rom>.qsv" exists.
bool saveExists();

extern volatile bool gCartRamDirty;

}  // namespace fe
