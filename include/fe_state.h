// Global front-end state shared between the reconstructed modules.
#pragma once

#include <stdint.h>
#include "fe_config.h"

struct gb_s;

namespace fe {

// Callback private data handed to walnut-cgb (the old `struct priv_t`).
struct Priv {
    uint8_t* rom      = nullptr;  // cache base (streaming) or malloc'd ROM
    uint8_t* cartRam  = nullptr;  // cartridge RAM buffer
};

// Persistent settings, mirroring /gbeconfig.dat version 5.
struct Settings {
    uint8_t keyUp       = kDefaultKeyUp;
    uint8_t keyDown     = kDefaultKeyDown;
    uint8_t keyLeft     = kDefaultKeyLeft;
    uint8_t keyRight    = kDefaultKeyRight;
    uint8_t keyA        = kDefaultKeyA;
    uint8_t keyB        = kDefaultKeyB;
    uint8_t keyStart    = kDefaultKeyStart;
    uint8_t keySelect   = kDefaultKeySelect;

    uint8_t gfxInterlace   = static_cast<uint8_t>(GfxInterlace::Sparse);
    uint8_t i2cJoystick    = 2;
    uint8_t volumeStart    = 0;
    uint8_t paletteStart   = static_cast<uint8_t>(PalettePreset::Default);
    uint8_t sgbBorder      = static_cast<uint8_t>(SgbBorderMode::Auto);
    uint8_t sgbPaletteAuto = 0;
    uint8_t startupPalette = 0;
    float   bassBoostGain  = 0.0f;

    // Runtime-only settings
    uint8_t bassBoostMode     = 0;
    uint8_t bassBoostStrength = 0x4b;
    uint8_t forceCompat       = 0;
    uint8_t cpuMode           = static_cast<uint8_t>(CpuMode::Fastest);
    bool    changed           = false;
};

// The binary keeps a small display state block at 0x3fcb9470.
struct DisplayState {
    uint8_t  mode          = 0;        // 0 = direct chunks, 1 = scaled
    uint8_t  aspectIndex   = 0;        // 0 = 10:9 (native), 1..10 = 11:9..20:9
    uint16_t destWidth     = kLcdWidth; // destination width (160 normal)
    uint16_t scaledWidth   = kLcdWidth; // width recorded by the scaler
    uint8_t  verticalScale = 0;        // 0 fit, 1 centered, 2 scale, 3 bottom
    uint8_t  twelveColour  = 0;        // 12-colour / SGB palette flag
    uint8_t  rowMap[kDisplayWidth] = {}; // destination x -> source x
    uint8_t  dirty[kFbHeight]      = {};
    uint16_t palette[16]           = {};
    uint16_t* fb                   = nullptr;
    uint16_t* fb2                  = nullptr;
};

// Persistent emulator status bits (0x420008d8).
enum EmulatorStatus : uint8_t {
    kStatusSgbPalette = 0x01,
    kStatusFrameSkip  = 0x02,
};

// Shared flags used by the ROM cache and audio task (0x4200005c).
enum SharedFlag : int {
    kSharedRomReads   = 0,
    kSharedAudioPause = 1,
    kSharedSgbCompat  = 2,
    kSharedUnknown3   = 3,
    kSharedHighCache  = 4,
};

// ---------------------------------------------------------------------------
// Globals (defined in src/main.cpp)
// ---------------------------------------------------------------------------
extern gb_s          gGb;
extern Priv          gPriv;
extern Settings      gSettings;
extern DisplayState  gDisplay;

extern bool             gAppRunning;
extern volatile uint8_t gDisplayBusy;
extern uint8_t          gSharedFlags[16];
extern uint8_t          gEmulatorStatus;

// 160 x 135 RGB565 framebuffer and one scanline staging buffer.
extern uint16_t gFramebuffer[kFbHeight][kLcdWidth];
extern uint16_t gLineBuffer[kDisplayWidth];

// Current palette in RGB888 (12 entries) and its RGB565 conversion.
extern uint32_t gActivePalette[12];
extern uint16_t gPalette565[16];

// Palette tables from the original source.
extern const uint32_t kPalettePocket[4];
extern const uint32_t kPaletteDefault[4];
extern const uint32_t kPaletteOriginal[4];
extern const uint32_t kPaletteBlackWhite[4];

// Per-game indices discovered by feLoadSgbProfile().
extern uint8_t gPaletteIndex;
extern uint8_t gSgbPaletteIndex;
extern uint8_t gTwelveColourIndex;
extern uint8_t gCgbPaletteIndex;

extern uint8_t gDisplayMode;
extern uint8_t gSgbBorderEnabled;
extern uint8_t gSgbBorderFrame;
extern uint8_t gSgbBorderFrameCount;

extern uint8_t gVolume;             // APU master volume, 0..0x7f
extern uint8_t gRomExtension[8];
extern char    gRomPath[kMaxPath];  // currently selected ROM

}  // namespace fe
