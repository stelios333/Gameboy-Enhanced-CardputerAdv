// Gameboy Enhanced Firmware for M5Stack Cardputer
// Reconstructed front-end configuration constants.
#pragma once

#include <stdint.h>

namespace fe {

// ---------------------------------------------------------------------------
// Display geometry
// ---------------------------------------------------------------------------
constexpr int kLcdWidth       = 160;   // Game Boy visible width
constexpr int kLcdHeight      = 144;   // Game Boy visible height
constexpr int kFbHeight       = 135;   // Cardputer display height
constexpr int kDisplayX       = 40;    // emulator image X offset
constexpr int kBorderWidth    = 40;    // SGB border half width
constexpr int kBorderHeight   = 135;   // SGB border height
constexpr int kDisplayWidth   = 240;   // Cardputer display width

// The firmware draws the 144-line Game Boy image into a 135-line buffer,
// scaling Y by 135/144. The buffer is pushed to the panel in nine 15-line
// chunks (the `fe_fb_seg1..8` pointers in the binary).
constexpr int kChunkLines     = 15;
constexpr int kChunkCount     = 9;

// ---------------------------------------------------------------------------
// ROM sector cache
// ---------------------------------------------------------------------------
constexpr int kSectorSize     = 1024;
constexpr int kCacheSlots     = 119;   // 0x77
constexpr int kCacheLimit     = kCacheSlots * kSectorSize - 1;
constexpr int kSectorMapSize  = 4096;  // address >> 10 (4 MiB of ROM)

// ---------------------------------------------------------------------------
// SD / file picker
// ---------------------------------------------------------------------------
constexpr int kMaxFiles       = 1024;
constexpr int kMaxPath        = 256;

// ---------------------------------------------------------------------------
// Timing (microseconds)
// ---------------------------------------------------------------------------
constexpr uint32_t kDelaySplashStepUs   = 5000;
constexpr uint32_t kDelaySplashHoldUs   = 2500000;
constexpr uint32_t kDelayMessageUs      = 1000000;
constexpr uint32_t kDelayRomErrorUs     = 500000;
constexpr uint32_t kDelayInitErrorUs    = 2000000;
constexpr uint32_t kDelaySgbProfileUs   = 2200000;
constexpr uint32_t kDelayKeyRepeatUs    = 300000;
constexpr uint32_t kDelayFileListingUs  = 400000;
constexpr uint32_t kDelayVolumeMsgUs    = 200000;
constexpr uint32_t kDelayMenuScrollUs   = 50000;
constexpr uint32_t kDelayNaMessageUs    = 750000;
constexpr uint32_t kDelayNoCartRamUs    = 1200000;
// gb_error_delay_us at 0x42000128 (0x5E9AC0).
constexpr uint32_t kDelayGbErrorUs      = 6200000;

// Frame period: 1e6 / 59.94 = 16675 us. The original firmware also applies
// a 0.8 sleep scale when the measured frame time is high.
constexpr double   kFramePeriodUs       = 16675.0;
constexpr double   kSleepScale          = 0.8;
constexpr uint32_t kFrameTimeThreshold  = 1000;

// ---------------------------------------------------------------------------
// Audio
// ---------------------------------------------------------------------------
constexpr uint32_t kSampleRate          = 32768;
constexpr int      kAudioSamples        = 548;  // AUDIO_SAMPLES (0x224)
constexpr int      kAudioTaskStack      = 2048;
constexpr int      kDisplayTaskStack    = 2048;
constexpr int      kAudioTaskPriority   = 6;
constexpr int      kDisplayTaskPriority = 5;
constexpr uint8_t  kDefaultVolume       = 0x14;

// ---------------------------------------------------------------------------
// Defaults
// ---------------------------------------------------------------------------
constexpr char kDefaultKeyUp      = 'e';
constexpr char kDefaultKeyDown    = 's';
constexpr char kDefaultKeyLeft    = 'a';
constexpr char kDefaultKeyRight   = 'd';
constexpr char kDefaultKeyA       = 'l';
constexpr char kDefaultKeyB       = 'k';
constexpr char kDefaultKeyStart   = '1';
constexpr char kDefaultKeySelect  = '2';

constexpr uint8_t kDefaultVolumeUpKey   = 0x38;
constexpr uint8_t kDefaultVolumeDownKey = 0x30;

constexpr const char* kConfigPath = "/gbeconfig.dat";
constexpr const char* kMountPoint = "/sd";

// ---------------------------------------------------------------------------
// Enumerations (numeric values match the firmware's globals)
// ---------------------------------------------------------------------------
enum class DisplayMode : uint8_t {
    ClassicGameboy = 0,
    SuperGameboy   = 1,
    GbcPalettes    = 2,
    TwelveColour   = 3,
};

enum class PalettePreset : uint8_t {
    BlackAndWhite = 0,
    Original      = 1,
    Default       = 2,
    Pocket        = 3,
};

enum class SgbBorderMode : uint8_t {
    Manual = 0,
    Auto   = 1,
    Tv     = 2,
};

enum class GfxInterlace : uint8_t {
    Off    = 0,
    Sparse = 1,
    Full   = 2,
};

enum class CpuMode : uint8_t {
    Compatible = 0,
    Fastest    = 1,
    Fast       = 2,
};

}  // namespace fe
