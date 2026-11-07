// Main emulator front-end: splash, ROM loading, task creation and input loop.
//
// Reconstructed from fe_emulator_main (0x42011c70) and the surrounding helper
// functions in app_new.elf.

#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <sys/time.h>
#include <stdio.h>
#include <string.h>

#include "fe_walnut.h"
#include "minigb_apu.h"

#include "fe_config.h"
#include "fe_state.h"
#include "fe_emulator.h"
#include "fe_rom_cache.h"
#include "fe_display.h"
#include "fe_settings.h"
#include "fe_save.h"
#include "fe_sgb.h"
#include "fe_palettes.h"
#include "fe_audio.h"
#include "fe_ui.h"

namespace fe {

namespace {

TaskHandle_t sAudioTask = nullptr;
TaskHandle_t sDisplayTask = nullptr;

uint64_t nowUs()
{
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return static_cast<uint64_t>(tv.tv_sec) * 1000000u + tv.tv_usec;
}

const uint32_t* presetPalette(uint8_t index)
{
    // 0 = B&W, 1 = original DMG, 2 = "Pocket" (the firmware's default),
    // 3 = "Light" (the firmware's pocket table).
    switch (index & 3) {
    case 0:  return kPaletteBlackWhite;
    case 1:  return kPaletteOriginal;
    case 2:  return kPaletteDefault;
    default: return kPalettePocket;
    }
}

void copyPalette(const uint32_t* src, int count)
{
    memset(gActivePalette, 0, sizeof(gActivePalette));
    memcpy(gActivePalette, src, sizeof(uint32_t) * count);
    updatePalette();
}

void drawSplash()
{
    auto& d = M5Cardputer.Display;
    d.clearDisplay();

    // Fade the title in from black.
    for (uint32_t c = 0; c <= 0xFFFFFF; c += 0x111111) {
        d.setTextColor(rgb888ToRgb565(c));
        setFontSize(64);
        d.drawString("Gameboy Emulator", 0x16, 0x2D);
        delay(5);
    }

    d.setTextColor(0xFFFF);
    char version[20];
    snprintf(version, sizeof(version), "ver %d.%d.%02d", 0, 0x4F, 0x42);
    setFontSize(72);
    d.drawString(version, 0xA0, 0x5A);
    delay(kDelaySplashHoldUs / 1000);

    // Fade back out to white.
    for (int c = 0xFFFFFF; c >= 0; c -= 0x111111) {
        d.setTextColor(rgb888ToRgb565(c));
        setFontSize(64);
        d.drawString("Gameboy Emulator", 0x16, 0x2D);
        setFontSize(72);
        d.drawString(version, 0xA0, 0x5A);
        delay(5);
    }
}

void applyPalettePreset(uint8_t index)
{
    gPaletteIndex = index & 3;
    copyPalette(presetPalette(gPaletteIndex), 4);
}

void showVolumeMessage()
{
    char text[24];
    if (gVolume == 0) {
        messageBoxDraw(-15, " Volume ", " OFF ", 0xFFFF,
                       rgb888ToRgb565(0xFF0000), 2, 2, 4);
    } else {
        // The firmware prints the percentage in colour_menu_text (0xFFE0).
        snprintf(text, sizeof(text), "%d%%", (gVolume * 100) / 0x7F);
        messageBoxDraw(-15, " Volume ", text, 0xFFFF, 0xFFE0, 2, 2, 4);
    }
    delay(kDelayVolumeMsgUs / 1000);
    messageBoxClear(-15, " Volume ", nullptr, 2, 2, 4, 0);
}

void setDisplayMode(uint8_t mode)
{
    gDisplayMode = mode & 3;

    // The original front-end never touches the cartridge's CGB flag here; it
    // only selects how the already-emulated pixels are mapped to colours.
    // fe_display_state+7 (`twelveColour`) is cleared for the two 4-shade modes
    // and set for the GBC / Analogue Pocket 12-entry BG/OBJ0/OBJ1 mapping.
    // fe_color_mode_flag (0x3fcb1241) is only set in Super Game Boy mode;
    // nothing in the rendering path reads it, so it is not modelled here.
    switch (gDisplayMode) {
    case 0:  // Classic Game Boy palette
        gDisplay.twelveColour = 0;
        copyPalette(presetPalette(gPaletteIndex), 4);
        break;
    case 1:  // Super Game Boy palette
        gDisplay.twelveColour = 0;
        copyPalette(sgbPalette(gSgbPaletteIndex), 12);
        break;
    case 2:  // Game Boy Color palettes
        gDisplay.twelveColour = 1;
        gb_run_frame_dualfetch(&gGb);
        copyPalette(gbcPalette(gCgbPaletteIndex), 12);
        break;
    default:  // 12-colour / Analogue Pocket
        gDisplay.twelveColour = 1;
        gb_run_frame_dualfetch(&gGb);
        copyPalette(twelveColourPalette(gTwelveColourIndex), 12);
        break;
    }

    fitFrameRedrawEverything();
    drawSgbBorder();
}

void cycleDisplayMode()
{
    if (gGb.cgb.cgbMode) {
        messageBoxDraw(-15, " n/a in CGB mode ", nullptr, 0xFFFF, 0xFFFF, 2, 1, 4);
        delay(kDelayNaMessageUs / 1000);
        messageBoxClear(-15, " n/a in CGB mode ", nullptr, 2, 1, 4, 0);
        return;
    }

    setDisplayMode(gDisplayMode + 1);

    // The firmware labels the mode as "<name>" / "Mode", and groups the two
    // colour modes under the "12 COLOUR MODE" heading.
    const char* line1 = "Classic Gameboy";
    const char* line2 = "Mode";
    switch (gDisplayMode) {
    case 1: line1 = " Super Gameboy "; line2 = "Mode"; break;
    case 2: line1 = "12 COLOUR MODE";  line2 = "GBC PALETTES"; break;
    case 3: line1 = "12 COLOUR MODE";  line2 = "ANALOGUE POCKET"; break;
    default: break;
    }

    messageBoxDraw(-15, line1, line2, 0xFFFF, 0xFFFF, 2, 2, 4);
    delay(kDelayMessageUs / 1000);
    messageBoxClear(-15, line1, line2, 2, 2, 4, 0);
    drawSgbBorder();
}

void cyclePalettePreset()
{
    if (gGb.cgb.cgbMode) {
        messageBoxDraw(-15, " n/a in CGB mode ", nullptr, 0xFFFF, 0xFFFF, 2, 1, 4);
        delay(kDelayNaMessageUs / 1000);
        messageBoxClear(-15, " n/a in CGB mode ", nullptr, 2, 1, 4, 0);
        return;
    }

    if (gDisplayMode == 2) {
        gCgbPaletteIndex = (gCgbPaletteIndex + 1) % kGbcPaletteCount;
        copyPalette(gbcPalette(gCgbPaletteIndex), 12);
        char text[24];
        snprintf(text, sizeof(text), "Preset %d", gCgbPaletteIndex);
        messageBoxDraw(-15, " Gameboy Colour ", text, 0xFFFF, 0xFFFF, 2, 2, 4);
        delay(kDelayMessageUs / 1000);
        messageBoxClear(-15, " Gameboy Colour ", text, 2, 2, 4, 0);
    } else if (gDisplayMode == 3) {
        gTwelveColourIndex = (gTwelveColourIndex + 1) % kTwelveColourCycle;
        copyPalette(twelveColourPalette(gTwelveColourIndex), 12);
        char text[24];
        snprintf(text, sizeof(text), "Preset %d", gTwelveColourIndex);
        messageBoxDraw(-15, " Analogue Pocket ", text, 0xFFFF, 0xFFFF, 2, 2, 4);
        delay(kDelayMessageUs / 1000);
        messageBoxClear(-15, " Analogue Pocket ", text, 2, 2, 4, 0);
    } else if (gDisplayMode == 1) {
        // The firmware masks with fe_sgb_palette_mask (0x1f), so the user
        // cycle wraps at 32 even though the table has 40 entries.
        gSgbPaletteIndex = (gSgbPaletteIndex + 1) & 0x1F;
        copyPalette(sgbPalette(gSgbPaletteIndex), 12);
        char text[24];
        snprintf(text, sizeof(text), "%d", gSgbPaletteIndex);
        messageBoxDraw(-15, " Super GB Preset ", text, 0xFFFF, 0xFFFF, 2, 3, 4);
        delay(kDelayMessageUs / 1000);
        messageBoxClear(-15, " Super GB Preset ", text, 2, 3, 4, 0);
    } else {
        applyPalettePreset(gPaletteIndex + 1);
        static const char* const names[] = {
            " Black & White ", " Original Gameboy ", " Gameboy Pocket ", " Gameboy Light ",
        };
        messageBoxDraw(-15, names[gPaletteIndex & 3], nullptr, 0xFFFF, 0xFFFF, 2, 1, 4);
        delay(kDelayNaMessageUs / 1000);
        messageBoxClear(-15, names[gPaletteIndex & 3], nullptr, 2, 1, 4, 0);
    }

    fitFrameRedrawEverything();
    drawSgbBorder();
}

void toggleSgbBorder()
{
    gSgbBorderEnabled = !gSgbBorderEnabled;
    drawSgbBorder();
    fitFrameRedrawEverything();
    messageBoxDraw(-15, "Super Gameboy",
                   gSgbBorderEnabled ? "Border ON" : "Border OFF",
                   0xFFFF, 0xFFFF, 2, 2, 4);
    delay(kDelayMessageUs / 1000);
    messageBoxClear(-15, "Super Gameboy",
                    gSgbBorderEnabled ? "Border ON" : "Border OFF", 2, 2, 4, 0);
}

void loadSaveState()
{
    waitYield();
    if (!saveExists()) {
        messageBoxDraw(-15, "game state", "doesn't exist", 0xFFFF, 0xFFFF, 2, 2, 4);
        delay(kDelayMessageUs / 1000);
        messageBoxClear(-15, "game state", "doesn't exist", 2, 2, 4, 0);
    } else {
        messageBoxDraw(-15, "Loading", "game state", 0xFFFF, 0xFFFF, 2, 2, 4);
        delay(kDelayMessageUs / 1000);
        messageBoxClear(-15, "Loading", "game state", 2, 2, 4, 0);
        saveStateLoad(&gGb);
        gb_run_frame_dualfetch(&gGb);
    }
    fitFrameRedrawEverything();
}

void saveCartRamWithMessage()
{
    waitYield();
    messageBoxDraw(-15, "saving data", nullptr, 0xFFFF, 0xFFFF, 2, 2, 4);
    cartRamSave(&gGb);
    delay(kDelayMessageUs / 1000);
    messageBoxClear(-15, "saving data", nullptr, 2, 2, 4, 0);
    fitFrameRedrawEverything();
    drawSgbBorder();
}

void openSettingsMenu()
{
    waitYield();
    gSharedFlags[kSharedAudioPause] = 1;

    size_t saveSize = 0;
    gb_get_save_size_s(&gGb, &saveSize);
    if (saveSize != 0) {
        messageBoxDraw(-15, "saving data", nullptr, 0xFFFF, 0xFFFF, 2, 2, 4);
        cartRamSave(&gGb);
        delay(kDelayKeyRepeatUs / 1000);
        messageBoxClear(-15, "saving data", nullptr, 2, 2, 4, 0);
        gCartRamDirty = false;
    }

    settingsMenu();
    settingsSave();

    // `CPU Compatibility / Locked ON` is only shown when the per-game profile
    // forced compatibility (fe_force_compat != 0).
    if (gSettings.forceCompat) {
        messageBoxDraw(-15, "CPU Compatibility", "Locked ON", 0xFFFF, 0xFFFF, 2, 2, 4);
        delay(kDelayRomErrorUs / 1000);
        messageBoxClear(-15, "CPU Compatibility", "Locked ON", 2, 2, 4, 0);
    }

    // gb->direct.interlace tracks GFX INTERLACE == FULL; frame_skip is kept.
    gGb.direct.interlace = (gSettings.gfxInterlace ==
                            static_cast<uint8_t>(GfxInterlace::Full));

    if (!gAppRunning) {
        // EXIT GAME: persist cartridge RAM and reset the palette state.
        delay(1);
        if (gCartRamDirty) {
            cartRamSave(&gGb);
        }
        gDisplayMode = 0;
        gSgbBorderEnabled = 0;
        gSgbBorderFrame = 0;
        gSgbBorderFrameCount = 0;
        gSharedFlags[kSharedHighCache] = 0;
        gSgbPaletteIndex = 0;
        gTwelveColourIndex = 0;
        gCgbPaletteIndex = 0;
        gPaletteIndex = gSettings.paletteStart;
        applyPalettePreset(gPaletteIndex);
    } else {
        // Still running: clear the panel and repaint in the new settings.
        M5Cardputer.Display.clearDisplay();
        fitFrameRedrawEverything();
        drawSgbBorder();
        delay(kDelayKeyRepeatUs / 1000);
    }

    gSettings.changed = false;
    gSharedFlags[kSharedAudioPause] = 0;
}

void handleHotkey(char key, bool modifier)
{
    switch (key) {
    case '-':
        loadSaveState();
        break;

    case '=':
        if (!modifier) {
            saveCartRamWithMessage();
        }
        break;

    case ';':
        if (modifier) {
            gVolume = (gVolume < 0x7A) ? gVolume + 5 : 0x7F;
            showVolumeMessage();
        }
        break;

    case '.':
        if (modifier) {
            gVolume = (gVolume > 5) ? gVolume - 5 : 0;
            showVolumeMessage();
        }
        break;

    case '/':
        if (modifier) {
            gVolume = (gVolume < 0x40) ? 0x40 : 0x7F;
            showVolumeMessage();
        }
        break;

    case ',':
        if (modifier) {
            gVolume = 0;
            showVolumeMessage();
        }
        break;

    case '[':
        if (modifier) {
            toggleSgbBorder();
        } else {
            cycleDisplayMode();
        }
        break;

    case '\\':
        if (modifier) {
            gDisplay.verticalScale = (gDisplay.verticalScale + 1) & 3;
            setVerticalScale(gDisplay.verticalScale);
            static const char* const names[] = {
                "Fit to screen", "Centered", "Top", "Bottom",
            };
            const char* value = names[gDisplay.verticalScale & 3];
            messageBoxDraw(-15, " Vertical Scale ", value, 0xFFFF,
                           0xFFFF, 2, 3, 4);
            delay(kDelayMessageUs / 1000);
            messageBoxClear(-15, " Vertical Scale ", value, 2, 3, 4, 0);
        } else {
            gDisplay.aspectIndex = (gDisplay.aspectIndex + 1) % 11;
            setAspectRatio(gDisplay.aspectIndex);
            static const char* const ratios[11] = {
                "10:9", "11:9", "12:9", "13:9", "14:9", "15:9",
                "16:9", "17:9", "18:9", "19:9", "20:9",
            };
            const char* value = ratios[gDisplay.aspectIndex];
            messageBoxDraw(-15, " Aspect Ratio ", value, 0xFFFF, 0xFFFF, 2, 3, 4);
            delay(kDelayMessageUs / 1000);
            messageBoxClear(-15, " Aspect Ratio ", value, 2, 3, 4, 0);
        }
        fitFrameRedrawEverything();
        drawSgbBorder();
        break;

    case ']':
        if (modifier) {
            // Advance the animated border frame (modulo frame_count + 2).
            if (gSgbBorderEnabled) {
                gSgbBorderFrame = static_cast<uint8_t>(
                    (gSgbBorderFrame + 1) % (gSgbBorderFrameCount + 2));
                drawSgbBorder();
            }
        } else {
            cyclePalettePreset();
        }
        break;

    case '`':
        openSettingsMenu();
        break;

    default:
        break;
    }
}

void updateGamepad(const Keyboard_Class::KeysState& state)
{
    for (char key : state.word) {
        if (key == static_cast<char>(gSettings.keyUp))    gGb.direct.joypad &= ~JOYPAD_UP;
        if (key == static_cast<char>(gSettings.keyDown))  gGb.direct.joypad &= ~JOYPAD_DOWN;
        if (key == static_cast<char>(gSettings.keyLeft))  gGb.direct.joypad &= ~JOYPAD_LEFT;
        if (key == static_cast<char>(gSettings.keyRight)) gGb.direct.joypad &= ~JOYPAD_RIGHT;
        if (key == static_cast<char>(gSettings.keyA))     gGb.direct.joypad &= ~JOYPAD_A;
        if (key == static_cast<char>(gSettings.keyB))     gGb.direct.joypad &= ~JOYPAD_B;
        if (key == static_cast<char>(gSettings.keyStart)) gGb.direct.joypad &= ~JOYPAD_START;
        if (key == static_cast<char>(gSettings.keySelect)) gGb.direct.joypad &= ~JOYPAD_SELECT;
    }
}

void displayTaskEntry(void* /*arg*/)
{
    while (gAppRunning) {
        if (gDisplayBusy) {
            fitFrame();
            gDisplayBusy = 0;
        }
        vTaskDelay(6);
    }
    vTaskDelete(nullptr);
}

void runMainLoop()
{
    uint64_t lastFrame = nowUs();

    while (gAppRunning) {
        const uint64_t frameStart = nowUs();

        gGb.direct.joypad = 0xFF;

        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isPressed()) {
            auto& state = M5Cardputer.Keyboard.keysState();
            updateGamepad(state);

            // The firmware's key_flags byte is KeysState.fn (offset 1 of the
            // copied KeysState), i.e. hotkey alternatives use the fn key.
            const bool modifier = state.fn;
            for (char key : state.word) {
                handleHotkey(key, modifier);
            }
        }

        if (gAppRunning) {
            // COMPATIBLE uses the single-fetch core (p_wcgb_gb_run_frame at
            // 0x403764b0); FASTEST and FAST use the dual-fetch dispatcher.
            if (gSettings.cpuMode == static_cast<uint8_t>(CpuMode::Compatible)) {
                gb_run_frame(&gGb);
            } else {
                gb_run_frame_dualfetch(&gGb);
            }
            romCacheLruScan();
        }

        if (gAppRunning) {
            waitYield();
            gDisplayBusy = 1;
            taskYIELD();
        }

        const uint64_t elapsed = nowUs() - frameStart;
        if (elapsed < static_cast<uint64_t>(kFramePeriodUs)) {
            delayMicroseconds(static_cast<uint32_t>(kFramePeriodUs - elapsed));
        }
        lastFrame = nowUs();
        (void)lastFrame;
    }
}

}  // namespace

// ---------------------------------------------------------------------------
// emulatorMain()
// ---------------------------------------------------------------------------
void emulatorMain()
{
    Serial.println("emulator_main() beginning splash screen sequence\n");
    drawSplash();
    Serial.println("emulator_main() finished splash screen sequence\n");

    // Defaults (fe_emulator_main's initialisation block).
    gDisplayMode = 0;
    gSgbBorderEnabled = 1;
    gSgbBorderFrame = 0;
    gSgbBorderFrameCount = 0;
    gPaletteIndex = 2;
    gSgbPaletteIndex = 0;
    gTwelveColourIndex = 0;
    gCgbPaletteIndex = 0;
    gDisplay.twelveColour = 0;
    // The firmware initialises fe_display_state with the aspect index 6
    // (16:9, 240 px wide) and vertical scale 0.
    gDisplay.verticalScale = 0;
    gDisplay.fb = &gFramebuffer[0][0];
    gDisplay.fb2 = &gFramebuffer[0][0];
    setAspectRatio(6);
    copyPalette(kPaletteDefault, 4);

    gSettings = Settings{};
    gSettings.volumeStart = 0;
    gSettings.paletteStart = static_cast<uint8_t>(PalettePreset::Default);
    gSettings.sgbBorder = static_cast<uint8_t>(SgbBorderMode::Auto);
    gSettings.sgbPaletteAuto = 0;
    gSettings.startupPalette = 0;
    gSettings.forceCompat = 0;
    gSettings.cpuMode = static_cast<uint8_t>(CpuMode::Fastest);
    gSettings.changed = false;

    settingsLoad();
    gRomExtension[0] = '.';
    strncpy(reinterpret_cast<char*>(gRomExtension), ".gbc", sizeof(gRomExtension) - 1);

    gAppRunning = true;

    while (gAppRunning) {
        char path[kMaxPath] = {};
        filePicker(path, sizeof(path));

        if (path[0] == '\0') {
            // The picker was closed with the settings key.
            settingsMenu();
            settingsSave();
            continue;
        }

        strncpy(gRomPath, path, sizeof(gRomPath) - 1);
        gRomPath[sizeof(gRomPath) - 1] = '\0';

        if (gSettings.changed) {
            Serial.println("settings changed... calling settings_save()\n");
            settingsSave();
            gSettings.changed = false;
        }

        Serial.println("calling read_rom_to_ram()\n");
        uint8_t* rom = readRomToRam(path);
        gPriv.rom = rom;
        if (!rom) {
            debugPrint("Error at read_rom_to_ram!!");
            delay(kDelayRomErrorUs / 1000);
            continue;
        }

        Serial.println("calling gb_init()\n");
        const enum gb_init_error_e initRet =
            gb_init(&gGb, gbRomRead, gbRomRead16, gbRomRead32,
                    gbCartRamRead, gbCartRamWrite, gbError, &gPriv);
        if (initRet != GB_INIT_NO_ERROR) {
            if (initRet == GB_INIT_INVALID_CHECKSUM) {
                debugPrint("GB_INIT chksum!!!");
                setFontSize(0x60);
            } else {
                debugPrint("GB_INIT unsupported cart");
            }
            delay(kDelayInitErrorUs / 1000);
            gRomFile.close();
            if (!gUseRomCache) {
                free(gPriv.rom);
            }
            gPriv.rom = nullptr;
            continue;
        }

        size_t saveSize = 0;
        gb_get_save_size_s(&gGb, &saveSize);
        if (saveSize == 0) {
            saveSize = 1;
        }
        gPriv.cartRam = static_cast<uint8_t*>(malloc(saveSize));
        if (gPriv.cartRam) {
            memset(gPriv.cartRam, 0, saveSize);
        }

        gb_reset(&gGb);

        if (saveSize > 0x8000) {
            messageBoxDraw(-15, "ERROR >32kb SRAM", "PLEASE REPORT", 0xFFFF, 0xFFFF, 2, 2, 4);
            delay(kDelayMessageUs / 1000);
            messageBoxClear(-15, "ERROR >32kb SRAM", "PLEASE REPORT", 2, 2, 4, 0);
        }

        Serial.println("calling gb_cart_ram_load()\n");
        cartRamLoad(&gGb);
        Serial.println("gb_cart_ram_load() returned.\n");

        gb_init_lcd(&gGb, lcdDrawLine);
        gGb.direct.interlace = 0;

        Serial.println("setting startup 4-colour palette\n");
        gDisplay.twelveColour = 0;
        applyPalettePreset(gSettings.paletteStart);

        Serial.println("calling load_sgbprofile()\n");
        gSharedFlags[kSharedHighCache] = 0;
        const uint8_t profile = loadSgbProfile(&gGb);
        if (profile) {
            // Message selection mirrors fe_emulator_main: twelve == 0 means an
            // SGB-only profile, twelve != 0 with sgb > 0 means both tables
            // exist, otherwise only an Analogue Pocket profile exists.
            if (gTwelveColourIndex == 0) {
                messageBoxDraw(-15, "This game has a", "SGB profile", 0xFFFF, 0xFFFF, 2, 2, 6);
                delay(kDelaySgbProfileUs / 1000);
                messageBoxClear(-15, "This game has a", "SGB profile", 2, 2, 6, 0);
            } else if (gSgbPaletteIndex > 0) {
                messageBoxDraw(-15, "This game has a", "SGB+AP profile", 0xFFFF, 0xFFFF, 2, 2, 6);
                delay(kDelaySgbProfileUs / 1000);
                messageBoxClear(-15, "This game has a", "SGB+AP profile", 2, 2, 6, 0);

                // Auto SGB is applied right after the message is cleared.
                if (gSettings.sgbPaletteAuto) {
                    gDisplay.twelveColour = 0;
                    gDisplayMode = static_cast<uint8_t>(DisplayMode::SuperGameboy);
                    copyPalette(sgbPalette(gSgbPaletteIndex), 12);
                }
            } else {
                messageBoxDraw(-15, "This game has a", "AP profile", 0xFFFF, 0xFFFF, 2, 2, 6);
                delay(kDelaySgbProfileUs / 1000);
                messageBoxClear(-15, "This game has a", "AP profile", 2, 2, 6, 0);
            }
        }

        if (gSettings.forceCompat) {
            Serial.println("forcing compatibility mode...\n");
            gGb.cgb.cgbMode = 0;
            gEmulatorStatus &= ~kStatusSgbPalette;
            gSettings.cpuMode = static_cast<uint8_t>(CpuMode::Compatible);
        }

        gVolume = gSettings.volumeStart ? gSettings.volumeStart : kDefaultVolume;

        // Speaker configuration and APU initialisation.
        auto spkCfg = M5Cardputer.Speaker.config();
        spkCfg.sample_rate = kSampleRate;
        spkCfg.task_priority = 4;
        spkCfg.dma_buf_count = 4;
        spkCfg.dma_buf_len = 4;
        M5Cardputer.Speaker.config(spkCfg);
        M5Cardputer.Speaker.begin();
        M5Cardputer.Speaker.setVolume(255);

        audioInit();
        gApuVolume = gVolume;

        // Generate the first frame and draw it.
        gGb.direct.joypad = 0xFF;
        gb_run_frame_dualfetch(&gGb);
        M5Cardputer.Display.clearDisplay();
        fitFrameRedrawEverything();
        // fe_emulator_main: AUTO (1) draws only when a border profile was
        // loaded (frame_count > 0; in practice never), TV (2) always draws
        // frame 0, MANUAL (0) leaves the border off.
        if (gSettings.sgbBorder == static_cast<uint8_t>(SgbBorderMode::Auto)) {
            if (gSgbBorderFrameCount > 0) {
                gSgbBorderEnabled = 1;
                drawSgbBorder();
            }
        } else if (gSettings.sgbBorder == static_cast<uint8_t>(SgbBorderMode::Tv)) {
            gSgbBorderEnabled = 1;
            gSgbBorderFrame = 0;
            drawSgbBorder();
        }

        gDisplayBusy = 0;
        gSharedFlags[kSharedAudioPause] = 0;
        gAppRunning = true;

        xTaskCreatePinnedToCore(audioTaskEntry, "gbAPU", kAudioTaskStack, &gGb,
                                kAudioTaskPriority, &sAudioTask, 0);
        xTaskCreatePinnedToCore(displayTaskEntry, "gbDISP", kDisplayTaskStack, &gGb,
                                kDisplayTaskPriority, &sDisplayTask, 0);

        Serial.println("entering emulator main loop...\n");
        runMainLoop();

        // The settings menu's EXIT GAME item clears gAppRunning. Pause the
        // APU and let both worker tasks notice the flag and self-delete.
        gAppRunning = false;
        gSharedFlags[kSharedAudioPause] = 1;
        vTaskDelay(30);
        sAudioTask = nullptr;
        sDisplayTask = nullptr;

        if (gCartRamDirty) {
            cartRamSave(&gGb);
        }

        free(gPriv.cartRam);
        gPriv.cartRam = nullptr;
        gRomFile.close();
        if (!gUseRomCache) {
            free(gPriv.rom);
        }
        gPriv.rom = nullptr;
        gSgbBorderEnabled = 0;
        gSgbBorderFrame = 0;
        gSgbBorderFrameCount = 0;
        gSgbPaletteIndex = 0;
        gTwelveColourIndex = 0;
        gCgbPaletteIndex = 0;
        gPaletteIndex = gSettings.paletteStart;
        applyPalettePreset(gPaletteIndex);

        gAppRunning = true;  // return to the picker
    }
}

}  // namespace fe
