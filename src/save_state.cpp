// Cartridge RAM persistence and save states.
//
// The original firmware used save-state format version 4 and serialised the
// emulator context by hard-coded offsets into the older walnut-cgb struct.
// This reconstruction keeps the same file naming and version byte, but writes
// the current struct by named members.  Save files are therefore self
// consistent with this build rather than byte-compatible with the old binary.

#include <Arduino.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>

#include "fe_walnut.h"
#include "fe_save.h"
#include "fe_state.h"
#include "fe_display.h"
#include "fe_ui.h"

namespace fe {

volatile bool gCartRamDirty = false;

namespace {

size_t saveSize(gb_s* gb)
{
    size_t size = 0;
    if (gb_get_save_size_s(gb, &size) != 0) {
        return 0;
    }
    return size;
}

void constructPath(char* out, size_t outSize, const char* suffix)
{
    snprintf(out, outSize, "%s%s", gRomPath, suffix);
}

template <typename T>
void writeValue(File& f, const T& value)
{
    f.write(reinterpret_cast<const uint8_t*>(&value), sizeof(T));
}

template <typename T>
void readValue(File& f, T& value)
{
    f.read(reinterpret_cast<uint8_t*>(&value), sizeof(T));
}

}  // namespace

// ---------------------------------------------------------------------------
// Cartridge RAM
// ---------------------------------------------------------------------------
void cartRamLoad(gb_s* gb)
{
    char path[kMaxPath];
    constructPath(path, sizeof(path), "sav");

    if (SD.exists(path)) {
        File f = SD.open(path, FILE_READ);
        if (f) {
            const size_t size = saveSize(gb);
            if (gPriv.cartRam && size) {
                f.read(gPriv.cartRam, size);
            }
            f.close();
        }
    }
    gCartRamDirty = false;
}

void cartRamSave(gb_s* gb)
{
    const size_t size = saveSize(gb);
    if (size == 0) {
        messageBoxDraw(-15, "ERROR:", "No cart ram", rgb888ToRgb565(0xFF0000),
                       rgb888ToRgb565(0xFFFFFF), 2, 2, 4);
        delay(kDelayNoCartRamUs / 1000);
        messageBoxClear(-15, "ERROR:", "No cart ram", 2, 2, 4, 0);
        gCartRamDirty = false;
        return;
    }

    char path[kMaxPath];
    constructPath(path, sizeof(path), "sav");

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        messageBoxDraw(-15, "ERROR:", "SDcard write", rgb888ToRgb565(0xFF0000),
                       rgb888ToRgb565(0xFFFFFF), 2, 2, 4);
        delay(kDelayMessageUs / 1000);
        messageBoxClear(-15, "ERROR:", "SDcard write", 2, 2, 4, 0);
        gCartRamDirty = false;
        return;
    }

    if (gPriv.cartRam) {
        f.write(gPriv.cartRam, size);
    }
    f.flush();
    f.close();
    gCartRamDirty = false;
}

// ---------------------------------------------------------------------------
// Save states
// ---------------------------------------------------------------------------
bool saveExists()
{
    char path[kMaxPath];
    constructPath(path, sizeof(path), "qsv");
    return SD.exists(path);
}

void saveStateSave(gb_s* gb)
{
    char path[kMaxPath];
    constructPath(path, sizeof(path), "qsv");

    File f = SD.open(path, FILE_WRITE);
    if (!f) {
        return;
    }

    writeValue<uint8_t>(f, 4);

    // CPU and timing state.
    writeValue(f, gb->cpu_reg);
    writeValue(f, gb->counter);

    // Cartridge banking state.
    writeValue(f, gb->mbc);
    writeValue(f, gb->cart_ram);
    writeValue(f, gb->num_rom_banks_mask);
    writeValue(f, gb->num_ram_banks);
    writeValue(f, gb->selected_rom_bank);
    writeValue(f, gb->cart_ram_bank);
    writeValue(f, gb->enable_cart_ram);
    writeValue(f, gb->cart_mode_select);
    writeValue(f, gb->rtc_latched);
    writeValue(f, gb->rtc_real);

    // Main memory.
    f.write(gb->wram, sizeof(gb->wram));
    f.write(gb->vram, sizeof(gb->vram));
    f.write(gb->oam, sizeof(gb->oam));
    f.write(gb->hram_io, sizeof(gb->hram_io));

    // Display palettes and window state.
    f.write(gb->display.bg_palette, sizeof(gb->display.bg_palette));
    f.write(gb->display.sp_palette, sizeof(gb->display.sp_palette));
    writeValue(f, gb->display.window_clear);
    writeValue(f, gb->display.WY);

    // Front-end joypad latch.
    writeValue(f, gb->direct.joypad);

    // Cartridge RAM.
    const size_t cartSize = saveSize(gb);
    if (gPriv.cartRam && cartSize) {
        f.write(gPriv.cartRam, cartSize);
    }

    // Game Boy Color state.
    writeValue(f, gb->cgb);

    f.flush();
    f.close();
}

void saveStateLoad(gb_s* gb)
{
    char path[kMaxPath];
    constructPath(path, sizeof(path), "qsv");

    File f = SD.open(path, FILE_READ);
    if (!f) {
        return;
    }

    uint8_t version = 0;
    f.read(&version, 1);
    if (version != 4) {
        f.close();
        return;
    }

    readValue(f, gb->cpu_reg);
    readValue(f, gb->counter);

    readValue(f, gb->mbc);
    readValue(f, gb->cart_ram);
    readValue(f, gb->num_rom_banks_mask);
    readValue(f, gb->num_ram_banks);
    readValue(f, gb->selected_rom_bank);
    readValue(f, gb->cart_ram_bank);
    readValue(f, gb->enable_cart_ram);
    readValue(f, gb->cart_mode_select);
    readValue(f, gb->rtc_latched);
    readValue(f, gb->rtc_real);

    f.read(gb->wram, sizeof(gb->wram));
    f.read(gb->vram, sizeof(gb->vram));
    f.read(gb->oam, sizeof(gb->oam));
    f.read(gb->hram_io, sizeof(gb->hram_io));

    f.read(gb->display.bg_palette, sizeof(gb->display.bg_palette));
    f.read(gb->display.sp_palette, sizeof(gb->display.sp_palette));
    readValue(f, gb->display.window_clear);
    readValue(f, gb->display.WY);

    readValue(f, gb->direct.joypad);

    const size_t cartSize = saveSize(gb);
    if (gPriv.cartRam && cartSize) {
        f.read(gPriv.cartRam, cartSize);
    }

    readValue(f, gb->cgb);

    f.close();
}

}  // namespace fe
