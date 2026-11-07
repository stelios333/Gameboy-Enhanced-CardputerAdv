// Streaming ROM sector cache and walnut-cgb callback shims.
#pragma once

#include <FS.h>
#include "fe_walnut.h"
#include "fe_config.h"
#include "fe_state.h"

namespace fe {

// 119 KiB of sector storage, a 4096-entry sector -> slot map and per-slot LRU
// ages. This mirrors the firmware's cache at 0x3fcba940 / 0x3fcb9940.
struct RomCache {
    uint8_t* base     = nullptr;
    uint8_t* highBase = nullptr;
    uint32_t used     = 0;
    uint32_t limit    = kCacheLimit;
    uint8_t  sectorMap[kSectorMapSize];
    uint16_t ages[kCacheSlots];
    int      sectors[kCacheSlots];
    int      next     = 0;
    int      victim   = 0;
    int      minAge   = 0x7fffffff;
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
