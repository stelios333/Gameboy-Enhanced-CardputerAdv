// Gameboy Enhanced Firmware for M5Stack Cardputer
// Reconstructed entry point and global definitions.
//
// The original firmware was an Arduino sketch.  This file provides the
// equivalent of setup()/loop() and owns the large global objects that the
// binary kept in .dram0.data / .bss.

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <M5Cardputer.h>

#include "fe_walnut.h"
#include "minigb_apu.h"

#include "fe_config.h"
#include "fe_state.h"
#include "fe_emulator.h"
#include "fe_ui.h"

namespace fe {

// ---------------------------------------------------------------------------
// Global objects
// ---------------------------------------------------------------------------
gb_s          gGb;
Priv          gPriv;
Settings      gSettings;
DisplayState  gDisplay;

bool             gAppRunning    = false;
volatile uint8_t gDisplayBusy   = 0;
uint8_t          gSharedFlags[16] = {};
uint8_t          gEmulatorStatus  = 0;

uint16_t gFramebuffer[kFbHeight][kLcdWidth] = {};
uint16_t gLineBuffer[kDisplayWidth] = {};

uint32_t gActivePalette[12] = {};
uint16_t gPalette565[16]   = {};

const uint32_t kPalettePocket[4] = {
    0x00B582, 0x009A70, 0x00694A, 0x004F3A,
};
const uint32_t kPaletteDefault[4] = {
    0xC5CAA4, 0x8C926B, 0x4A5138, 0x181818,
};
const uint32_t kPaletteOriginal[4] = {
    0x7B8210, 0x5A7942, 0x39594A, 0x294139,
};
const uint32_t kPaletteBlackWhite[4] = {
    0xFFFFFF, 0xA5A5A5, 0x525252, 0x000000,
};

uint8_t gPaletteIndex      = 2;
uint8_t gSgbPaletteIndex   = 0;
uint8_t gTwelveColourIndex = 0;
uint8_t gCgbPaletteIndex   = 0;

uint8_t gDisplayMode         = 0;
uint8_t gSgbBorderEnabled    = 1;
uint8_t gSgbBorderFrame      = 0;
uint8_t gSgbBorderFrameCount = 0;

uint8_t gVolume = kDefaultVolume;
uint8_t gRomExtension[8] = ".gbc";
char    gRomPath[kMaxPath] = {};

}  // namespace fe

// ---------------------------------------------------------------------------
// SD card initialisation (fe_sd_init)
// ---------------------------------------------------------------------------
namespace {

SPIClass sdSpi;

}  // namespace

namespace fe {

void sdInit()
{
    Serial.println("setup() Initializing SD card\n");
    vTaskDelay(1);
    usleep(1);

    sdSpi.begin(M5.getPin(m5::pin_name_t::sd_spi_sclk),
                M5.getPin(m5::pin_name_t::sd_spi_miso),
                M5.getPin(m5::pin_name_t::sd_spi_mosi),
                M5.getPin(m5::pin_name_t::sd_spi_ss));

    bool mounted = SD.begin(M5.getPin(m5::pin_name_t::sd_spi_ss), sdSpi,
                            40000000, kMountPoint, 2);

    int retry = 0;
    while (retry != 10 && !mounted) {
        mounted = SD.begin(M5.getPin(m5::pin_name_t::sd_spi_ss), sdSpi,
                           25000000, kMountPoint, 3);
        usleep(200);
        vTaskDelay(200);
        Serial.print(".");
        vTaskDelay(200);
        retry++;
    }

    Serial.println(mounted ? "Done!\n" : "Failed!\n");
}

}  // namespace fe

// ---------------------------------------------------------------------------
// Arduino entry points
// ---------------------------------------------------------------------------
void setup()
{
    // The original firmware disables both watchdogs before doing any I/O.
    disableCore0WDT();
    disableCore1WDT();

    Serial.begin(115200);
    Serial.println("Gameboy Enhanced Firmware setup() called.\n");

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    fe::sdInit();

    M5Cardputer.Display.setRotation(0);
    M5Cardputer.Display.setColorDepth(16);
    fe::setFontSize(64);

    // Never returns: emulatorMain() owns the rest of the program.
    fe::emulatorMain();
}

void loop()
{
    // All work happens in emulatorMain() and the FreeRTOS tasks it creates.
    delay(1000);
}
