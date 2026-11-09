// Streaming ROM sector cache and walnut-cgb callback shims.
#pragma once

#include <FS.h>
#include "fe_walnut.h"
#include "fe_config.h"
#include "fe_state.h"

namespace fe {

// 119 KiB of sector storage, a 4096-entry sector -> slot map and per-slot LRU
// ages.  The firmware keeps the same state at 0x3fcba940 (data), 0x3fcb9940
// (sector map), 0x3fcb9650 (sectors), 0x3fcb9850 (ages).
//
// app2.elf has no second "high" cache base and no embedded ROM: gb_rom_read/
// 16/32 all resolve through this one cache, and sector-map values >= 0x77 are
// misses.  app_new.elf added the high window (map values >= 0x77 indexed an
// embedded GTA image at 0x3c09ddb4); that is not part of this build.
struct RomCache {
    uint8_t* base     = nullptr;
    uint32_t used     = 0;
    uint32_t limit    = kCacheLimit;
    uint8_t  sectorMap[kSectorMapSize];
    uint16_t ages[kCacheSlots];
    int      sectors[kCacheSlots];
    int      next     = 0;
    int      victim   = 0;
    int      minAge   = 0x7fffffff;
    uint32_t stats[6] = {};  // fe_rom_stat0..5 (cleared after a load)
};

extern RomCache gRomCache;
extern File     gRomFile;
extern bool     gUseRomCache;
extern uint32_t gRomSize;

// Scratch allocator used by the file picker (grows from the cache base).
void* romScratchAlloc(size_t size);
void  romScratchReset();

// Whole-ROM loader / cache initialiser. Returns the ROM pointer, or nullptr.
uint8_t* readRomToRam(const char* path);

// Per-frame LRU aging (called once per emulated frame).
void romCacheLruScan();

// walnut-cgb callbacks
uint8_t  gbRomRead(gb_s* gb, const uint_fast32_t addr);
uint16_t gbRomRead16(gb_s* gb, const uint_fast32_t addr);
uint32_t gbRomRead32(gb_s* gb, const uint_fast32_t addr);
uint8_t  gbCartRamRead(gb_s* gb, const uint_fast32_t addr);
void     gbCartRamWrite(gb_s* gb, const uint_fast32_t addr, const uint8_t val);

}  // namespace fe
