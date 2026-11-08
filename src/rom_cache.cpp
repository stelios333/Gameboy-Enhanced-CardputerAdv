// Streaming ROM sector cache.
//
// The original firmware does not keep the whole ROM in RAM.  It streams 1 KiB
// sectors through a 119-slot cache and keeps a sector -> slot map.  A per-frame
// LRU scan ages one slot; a miss evicts the oldest slot.

#include <Arduino.h>
#include <SD.h>
#include <string.h>
#include <esp_heap_caps.h>

#include "fe_rom_cache.h"
#include "fe_save.h"
#include "fe_ui.h"

#include "../resources/rom_mmap.h"

namespace fe {

RomCache gRomCache;
File     gRomFile;
bool     gUseRomCache = true;
uint32_t gRomSize     = 0;

namespace {

uint8_t  sCacheStorage[kCacheSlots * kSectorSize];

// Fill one cache slot from the open ROM file.
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

// Return a pointer to the byte in the cache, loading a sector if necessary.
// Returns nullptr when the address is outside the supported 4 MiB map.
uint8_t* cacheByte(uint_fast32_t address)
{
    if (address >= gRomSize) {
        return nullptr;
    }

    const uint32_t sector = address >> 10;
    if (sector >= kSectorMapSize) {
        return nullptr;
    }

    const uint32_t slot = gRomCache.sectorMap[sector];
    if (slot < kCacheSlots) {
        gRomCache.ages[slot] = 0xffff;
        return gRomCache.base + slot * kSectorSize + (address & (kSectorSize - 1));
    }
    if (slot == 0xff) {
        // Miss: age the round-robin slot, advance the cursor and evict the
        // slot currently holding the lowest age.
        int aged = gRomCache.next;
        if (aged < 0 || aged >= kCacheSlots) {
            aged = 0;
        }
        uint32_t age = 0;
        if (gRomCache.ages[aged] != 0) {
            gRomCache.ages[aged]--;
            age = gRomCache.ages[aged];
        }
        if (static_cast<int>(age) < gRomCache.minAge) {
            gRomCache.minAge = static_cast<int>(age);
            gRomCache.victim = aged;
        }
        gRomCache.next = (aged + 1) % kCacheSlots;

        int victim = gRomCache.victim;
        if (victim < 0 || victim >= kCacheSlots) {
            victim = 0;
        }
        const int oldSector = gRomCache.sectors[victim];
        if (oldSector >= 0 && oldSector < kSectorMapSize) {
            gRomCache.sectorMap[oldSector] = 0xff;
        }
        gRomCache.sectorMap[sector] = static_cast<uint8_t>(victim);
        gRomCache.minAge = 0x7fffffff;
        loadSector(victim, static_cast<int>(sector));
        return gRomCache.base + victim * kSectorSize + (address & (kSectorSize - 1));
    }

    // High window (slot values 0x77..0xfe).
    const uint32_t highSlot = slot - 0x77;
    if (highSlot >= static_cast<uint32_t>(kCacheSlots)) {
        return nullptr;
    }
    return gRomCache.highBase + highSlot * kSectorSize + (address & (kSectorSize - 1));
}

}  // namespace

// ---------------------------------------------------------------------------
// Scratch allocator used by the file picker
// ---------------------------------------------------------------------------
void romScratchReset()
{
    gRomCache.used = 0;
}

void* romScratchAlloc(size_t size)
{
    // Keep the cursor 4-byte aligned.
    uint32_t used = (gRomCache.used + 3u) & ~3u;
    if (used + size > gRomCache.limit) {
        return nullptr;
    }
    uint8_t* p = gRomCache.base + used;
    memset(p, 0, size);
    gRomCache.used = used + size;
    return p;
}

// ---------------------------------------------------------------------------
// Whole-ROM loader / cache initialiser (fe_read_rom_to_ram)
// ---------------------------------------------------------------------------
uint8_t* readRomToRam(const char* path)
{
    gRomCache.base     = sCacheStorage;
    // The original firmware kept the high window in flash rodata.  Allocate
    // the equivalent buffer from PSRAM so it does not consume DRAM.
    if (!gRomCache.highBase) {
        gRomCache.highBase = static_cast<uint8_t*>(
            heap_caps_malloc(kCacheSlots * kSectorSize, MALLOC_CAP_SPIRAM));
    }
    gRomCache.used     = 0;
    gRomCache.limit    = kCacheLimit;

    gRomFile = SD.open(path, FILE_READ);
    if (!gRomFile) {
        debugPrint("Cannot open file");
        return nullptr;
    }

    const uint32_t fileSize = gRomFile.size();
    gRomSize = fileSize;
    if (fileSize == 0) {
        debugPrint("File size is 0");
        gRomFile.close();
        return nullptr;
    }

    // Save files replace the ROM extension: "game.gbc" -> "game.sav"/"game.qsv".
    const char* dot = strrchr(path, '.');
    size_t baseLen = dot ? static_cast<size_t>(dot - path + 1) : strlen(path);
    if (baseLen >= sizeof(gRomPath)) {
        baseLen = sizeof(gRomPath) - 1;
    }
    memcpy(gRomPath, path, baseLen);
    gRomPath[baseLen] = '\0';

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
        gRomFile.close();
        return rom;
    }

    // Keep the ROM extension for the debug banner.
    if (dot) {
        strncpy(reinterpret_cast<char*>(gRomExtension), dot, sizeof(gRomExtension) - 1);
        gRomExtension[sizeof(gRomExtension) - 1] = '\0';
    }

    // High-window remap mode.  fe_load_sgbprofile stores a non-null pointer
    // at shared_flags+8 for the built-in Grand Theft Auto profile; the
    // firmware then re-runs this loader with the 0x3c07edb4 sector map and
    // reads the selected sectors into the high window instead of the normal
    // 119-slot cache.  The reconstruction uses the PSRAM window and fills it
    // from the selected file so the same sector map is honoured.
    uint8_t* highWindow = nullptr;
    memcpy(&highWindow, &gSharedFlags[8], sizeof(highWindow));
    if (highWindow) {
        if (!gRomCache.highBase) {
            gRomCache.highBase = static_cast<uint8_t*>(
                heap_caps_malloc(kCacheSlots * kSectorSize, MALLOC_CAP_SPIRAM));
            if (!gRomCache.highBase) {
                gRomFile.close();
                return nullptr;
            }
        }
        highWindow = gRomCache.highBase;
        gRomCache.base = highWindow;

        memset(gRomCache.sectorMap, 0xff, sizeof(gRomCache.sectorMap));
        memset(gRomCache.ages, 0xff, sizeof(gRomCache.ages));
        for (int i = 0; i < kCacheSlots; i++) {
            gRomCache.sectors[i] = -1;
        }

        for (uint32_t sector = 0; sector < kSectorMapSize; sector++) {
            const uint8_t slot = kRomHighMmap[sector];
            if (slot == 0xff) {
                continue;
            }
            // Values below 0x77 index the normal cache base; values 0x77 and
            // above use the high window exactly like fe_gb_rom_read does.
            uint8_t* dest = (slot < kCacheSlots)
                                ? highWindow + slot * kSectorSize
                                : highWindow + (slot - 0x77) * kSectorSize;
            const uint32_t offset = sector * kSectorSize;
            if (offset >= fileSize) {
                continue;
            }
            uint32_t want = fileSize - offset;
            if (want > kSectorSize) {
                want = kSectorSize;
            }
            memset(dest, 0xff, kSectorSize);
            gRomFile.seek(offset);
            gRomFile.read(dest, want);
            gRomCache.sectorMap[sector] = slot;
            gRomCache.sectors[slot < kCacheSlots ? slot : slot - 0x77] =
                static_cast<int>(sector);
            gRomCache.ages[slot < kCacheSlots ? slot : slot - 0x77] = 0xffff;
        }

        gRomCache.next   = 0;
        gRomCache.victim = 0;
        gRomCache.minAge = 0x7fffffff;
        return highWindow;
    }

    memset(gRomCache.sectorMap, 0xff, sizeof(gRomCache.sectorMap));
    memset(gRomCache.ages, 0xff, sizeof(gRomCache.ages));
    for (int i = 0; i < kCacheSlots; i++) {
        gRomCache.sectors[i] = -1;
    }

    const uint32_t toRead = fileSize < static_cast<uint32_t>(kCacheSlots * kSectorSize)
                                ? fileSize
                                : static_cast<uint32_t>(kCacheSlots * kSectorSize);
    if (gRomFile.read(sCacheStorage, toRead) != toRead) {
        debugPrint("Unexpected EOF");
        gRomFile.close();
        return nullptr;
    }

    const int sectorsRead = static_cast<int>((toRead + kSectorSize - 1) / kSectorSize);
    for (int i = 0; i < sectorsRead; i++) {
        gRomCache.sectors[i]   = i;
        gRomCache.ages[i]      = 0xffff;
        gRomCache.sectorMap[i] = static_cast<uint8_t>(i);
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
    const int slot = gRomCache.next;
    if (slot < 0 || slot >= kCacheSlots) {
        return;
    }

    uint32_t age = 0;
    if (gRomCache.ages[slot] != 0) {
        gRomCache.ages[slot]--;
        age = gRomCache.ages[slot];
    }

    if (static_cast<int>(age) < gRomCache.minAge) {
        gRomCache.minAge = age;
        gRomCache.victim = slot;
    }

    gRomCache.next = (slot + 1) % kCacheSlots;
}

// ---------------------------------------------------------------------------
// walnut-cgb callbacks
// ---------------------------------------------------------------------------
uint8_t gbRomRead(gb_s* /*gb*/, const uint_fast32_t address)
{
    if (!gUseRomCache) {
        return (gPriv.rom && address < gRomSize) ? gPriv.rom[address] : 0xff;
    }

    uint8_t* p = cacheByte(address);
    return p ? *p : 0xff;
}

uint16_t gbRomRead16(gb_s* gb, const uint_fast32_t address)
{
    // The ESP32 flash/PSRAM path requires alignment-safe access, so read the
    // two bytes individually.
    return static_cast<uint16_t>(gbRomRead(gb, address))
         | static_cast<uint16_t>(gbRomRead(gb, address + 1) << 8);
}

uint32_t gbRomRead32(gb_s* gb, const uint_fast32_t address)
{
    return static_cast<uint32_t>(gbRomRead(gb, address))
         | (static_cast<uint32_t>(gbRomRead(gb, address + 1)) << 8)
         | (static_cast<uint32_t>(gbRomRead(gb, address + 2)) << 16)
         | (static_cast<uint32_t>(gbRomRead(gb, address + 3)) << 24);
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
