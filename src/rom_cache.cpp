// Streaming ROM sector cache.
//
// Reconstructed from app2.elf (the build before the Grand Theft Auto
// high-window experiment):
//
//   fe_read_rom_to_ram        0x4200f504
//   fe_rom_scratch_alloc      0x42003034
//   fe_rom_cache_load_sector  0x42003064
//   fe_rom_cache_miss         0x420030b4
//   fe_gb_rom_read            0x42003144
//   fe_gb_rom_read16          0x4200318c
//   fe_gb_rom_read32          0x420031fc
//   fe_rom_cache_lru_scan     0x42003288
//
// The original firmware does not keep the whole ROM in RAM.  It streams 1 KiB
// sectors through a 119-slot cache and keeps a sector -> slot map.  A miss
// walks all 119 slots, ages them and evicts the oldest one.  In addition the
// main loop ages 29 slots per frame so that the running LRU minimum survives
// between misses.
//
// app2.elf has no high-window/cache-base pair: every read goes through this
// 119-slot cache.  The only trace of the later GTA experiment is a dormant
// preload branch gated on shared_flags+8 (see readRomToRam), which nothing in
// this build ever sets.

#include <Arduino.h>
#include <SD.h>
#include <string.h>

#include "fe_rom_cache.h"
#include "fe_save.h"
#include "fe_ui.h"
#include "fe_config.h"

namespace fe {

RomCache gRomCache;
File     gRomFile;
bool     gUseRomCache = true;
uint32_t gRomSize     = 0;

namespace {

alignas(16) uint8_t sCacheStorage[kCacheSlots * kSectorSize];

constexpr uint32_t kUnexpectedEofDelayUs = kDelayGbErrorUs;

// Fill one cache slot from the open ROM file (fe_rom_cache_load_sector).
void loadSector(int slot, int sector)
{
    if (gSharedFlags[kSharedRomReads] < 99) {
        gSharedFlags[kSharedRomReads]++;
    }
    gRomFile.seek(static_cast<uint32_t>(sector) * kSectorSize);
    gRomFile.read(sCacheStorage + slot * kSectorSize, kSectorSize);
    gRomCache.sectors[slot] = sector;
    gRomCache.ages[slot]    = 0xffff;
}

// Show the same error box that fe_read_rom_to_ram uses (line 1 red, line 2
// white) and hold it for 6.2 s.
void romLoadError(const char* line2)
{
    messageBoxDraw(-15, "ERROR", line2, 0xF800, 0xFFFF, 2, 2, 4);
    delay(kUnexpectedEofDelayUs / 1000);
    messageBoxClear(-15, "ERROR", line2, 2, 2, 4, 0);
}

// 119-slot LRU scan performed on a miss (fe_rom_cache_miss).  The hardware
// loop in the firmware runs the body 119 times, i.e. one complete pass over
// the cache starting at gRomCache.next.  That many steps also leave `next`
// where it was.
uint8_t cacheMiss(uint32_t byteOffset, uint32_t sector, uint32_t address)
{
    int scanSlot  = gRomCache.next;
    int scanMin   = gRomCache.minAge;
    int bestSlot  = gRomCache.victim;
    bool found    = false;

    for (int i = 0; i < kCacheSlots; i++) {
        uint16_t age = gRomCache.ages[scanSlot];
        if (age != 0) {
            age--;
            gRomCache.ages[scanSlot] = age;
        }
        if (static_cast<int>(age) < scanMin) {
            bestSlot = scanSlot;
            scanMin  = static_cast<int>(age);
            found    = true;
        }
        scanSlot++;
        if (scanSlot == kCacheSlots) {
            scanSlot = 0;
        }
    }

    gRomCache.next = scanSlot;  // 119 steps: back to the starting slot
    if (found) {
        gRomCache.victim = bestSlot;
    }

    const int victim    = gRomCache.victim;
    const int oldSector = gRomCache.sectors[victim];
    gRomCache.minAge    = 0x7fffffff;
    gRomCache.sectorMap[oldSector] = 0xff;
    gRomCache.sectorMap[sector]    = static_cast<uint8_t>(victim);
    loadSector(victim, static_cast<int>(address >> 10));

    return gRomCache.base[victim * kSectorSize + byteOffset];
}

// Return the cached byte for a mapped sector, refreshing its age.  Returns
// nullptr when the sector is not in the cache.
inline uint8_t* cachedByte(uint_fast32_t address)
{
    const uint8_t slot = gRomCache.sectorMap[address >> 10];
    if (slot >= kCacheSlots) {
        return nullptr;
    }
    gRomCache.ages[slot] = 0xffff;
    return gRomCache.base + slot * kSectorSize + (address & (kSectorSize - 1));
}

}  // namespace

// ---------------------------------------------------------------------------
// Scratch allocator used by the firmware's file picker
// ---------------------------------------------------------------------------
void romScratchReset()
{
    gRomCache.used = 0;
}

void* romScratchAlloc(size_t size)
{
    // fe_rom_scratch_alloc does not align the cursor.
    if (gRomCache.used + size > gRomCache.limit) {
        return nullptr;
    }
    uint8_t* p = gRomCache.base + gRomCache.used;
    memset(p, 0, size);
    gRomCache.used += size;
    return p;
}

// ---------------------------------------------------------------------------
// ROM loader / cache initialiser (fe_read_rom_to_ram)
// ---------------------------------------------------------------------------
uint8_t* readRomToRam(const char* path)
{
    gRomCache.base = sCacheStorage;

    gRomFile = SD.open(path, FILE_READ);
    if (!gRomFile) {
        romLoadError("Cannot open file");
        return nullptr;
    }

    const uint32_t fileSize = gRomFile.size();
    gRomSize = fileSize;
    if (fileSize == 0) {
        romLoadError("File size is 0");
        gRomFile.close();
        return nullptr;
    }

    if (!gUseRomCache) {
        uint8_t* rom = static_cast<uint8_t*>(malloc(fileSize));
        if (!rom) {
            gRomFile.close();
            return nullptr;
        }
        if (gRomFile.read(rom, fileSize) != fileSize) {
            free(rom);
            gRomFile.close();
            return nullptr;
        }
        // The firmware prints the first and last byte in this mode.
        char banner[32];
        snprintf(banner, sizeof(banner), "f: %02x | l: %02x",
                 rom[0], rom[fileSize - 1]);
        debugPrint(banner);
        gRomFile.close();
        return rom;
    }

    // Save files replace the ROM extension: "game.gbc" -> "game.sav"/"game.qsv".
    const char* dot = strrchr(path, '.');
    size_t baseLen = dot ? static_cast<size_t>(dot - path + 1) : strlen(path);
    if (baseLen >= sizeof(gRomPath)) {
        baseLen = sizeof(gRomPath) - 1;
    }
    memcpy(gRomPath, path, baseLen);
    gRomPath[baseLen] = '\0';

    memset(gRomCache.sectorMap, 0xff, sizeof(gRomCache.sectorMap));

    // The firmware has a dormant "preload every sector individually" path
    // gated on shared_flags+8.  app2 only ever stores zero there (the picker
    // clears it), but mirror the branch so the cache behaves identically if a
    // non-zero value appears.
    uint32_t preloadFlag = 0;
    memcpy(&preloadFlag, &gSharedFlags[8], sizeof(preloadFlag));
    if (preloadFlag != 0) {
        int slot = 0;
        uint32_t sector = 0;
        for (;;) {
            if (gRomCache.sectorMap[sector] == 0xff) {
                gRomFile.seek(sector * kSectorSize);
                gRomFile.read(sCacheStorage + slot * kSectorSize, kSectorSize);
                gRomCache.sectors[slot]   = static_cast<int>(sector);
                gRomCache.sectorMap[sector] = static_cast<uint8_t>(slot);
                gRomCache.ages[slot]      = 0xffff;
                if (++slot == kCacheSlots) {
                    break;
                }
            }
            sector++;
        }
    } else {
        const uint32_t readSize =
            fileSize < static_cast<uint32_t>(kCacheSlots * kSectorSize)
                ? fileSize
                : static_cast<uint32_t>(kCacheSlots * kSectorSize);
        if (gRomFile.read(sCacheStorage, readSize) != readSize) {
            romLoadError("Unexpected EOF");
            gRomFile.close();
            return nullptr;
        }

        // The firmware maps all 119 slots, not just the sectors covered by
        // the file.  Reads beyond EOF therefore return whatever the sector
        // buffer contains, exactly like the original.
        for (int i = 0; i < kCacheSlots; i++) {
            gRomCache.sectors[i]   = i;
            gRomCache.sectorMap[i] = static_cast<uint8_t>(i);
        }
        memset(gRomCache.ages, 0xff, sizeof(gRomCache.ages));
    }

    for (uint32_t& stat : gRomCache.stats) {
        stat = 0;
    }
    gRomCache.next   = 0;
    gRomCache.victim = 0;
    gRomCache.minAge = 0x7fffffff;

    return sCacheStorage;
}

// ---------------------------------------------------------------------------
// Per-frame LRU scan (fe_rom_cache_lru_scan)
// ---------------------------------------------------------------------------
void romCacheLruScan()
{
    int scanSlot = gRomCache.next;
    int scanMin  = gRomCache.minAge;
    int bestSlot = gRomCache.victim;
    bool found   = false;

    for (int i = 0; i < kCacheScanSlots; i++) {
        uint16_t age = gRomCache.ages[scanSlot];
        if (age != 0) {
            age--;
            gRomCache.ages[scanSlot] = age;
        }
        if (static_cast<int>(age) < scanMin) {
            bestSlot = scanSlot;
            scanMin  = static_cast<int>(age);
            found    = true;
        }
        scanSlot++;
        if (scanSlot == kCacheSlots) {
            scanSlot = 0;
        }
    }

    gRomCache.next = scanSlot;
    if (found) {
        gRomCache.minAge = scanMin;
        gRomCache.victim = bestSlot;
    }
}

// ---------------------------------------------------------------------------
// walnut-cgb callbacks
// ---------------------------------------------------------------------------
uint8_t gbRomRead(gb_s* /*gb*/, const uint_fast32_t address)
{
    if (!gUseRomCache) {
        return (gPriv.rom && address < gRomSize) ? gPriv.rom[address] : 0xff;
    }
    if (uint8_t* p = cachedByte(address)) {
        return *p;
    }
    return cacheMiss(address & (kSectorSize - 1), address >> 10, address);
}

uint16_t gbRomRead16(gb_s* gb, const uint_fast32_t address)
{
    if (gUseRomCache) {
        const uint32_t offset = address & (kSectorSize - 1);
        const uint8_t  slot   = gRomCache.sectorMap[address >> 10];
        // Fast path only when both bytes live in the same cached sector.
        if (offset != kSectorSize - 1 && slot < kCacheSlots) {
            gRomCache.ages[slot] = 0xffff;
            uint8_t* p = gRomCache.base + slot * kSectorSize + offset;
            if ((reinterpret_cast<uintptr_t>(p) & 1u) == 0) {
                return *reinterpret_cast<const uint16_t*>(p);
            }
            return static_cast<uint16_t>(p[0] | (p[1] << 8));
        }
    }
    return static_cast<uint16_t>(gbRomRead(gb, address))
         | static_cast<uint16_t>(gbRomRead(gb, address + 1) << 8);
}

uint32_t gbRomRead32(gb_s* gb, const uint_fast32_t address)
{
    if (gUseRomCache) {
        const uint32_t offset = address & (kSectorSize - 1);
        const uint8_t  slot   = gRomCache.sectorMap[address >> 10];
        // Fast path only when all four bytes live in the same cached sector.
        if (offset <= kSectorSize - 4 && slot < kCacheSlots) {
            gRomCache.ages[slot] = 0xffff;
            uint8_t* p = gRomCache.base + slot * kSectorSize + offset;
            if ((reinterpret_cast<uintptr_t>(p) & 3u) == 0) {
                return *reinterpret_cast<const uint32_t*>(p);
            }
            return static_cast<uint32_t>(p[0])
                 | (static_cast<uint32_t>(p[1]) << 8)
                 | (static_cast<uint32_t>(p[2]) << 16)
                 | (static_cast<uint32_t>(p[3]) << 24);
        }
    }
    // The firmware's slow path re-uses the 16-bit reader for both halves.
    return static_cast<uint32_t>(gbRomRead16(gb, address))
         | (static_cast<uint32_t>(gbRomRead16(gb, address + 2)) << 16);
}

uint8_t gbCartRamRead(gb_s* /*gb*/, const uint_fast32_t address)
{
    return gPriv.cartRam ? gPriv.cartRam[address] : 0xff;
}

void gbCartRamWrite(gb_s* /*gb*/, const uint_fast32_t address, const uint8_t val)
{
    if (gPriv.cartRam) {
        gPriv.cartRam[address] = val;
        gCartRamDirty = true;
    }
}

}  // namespace fe
