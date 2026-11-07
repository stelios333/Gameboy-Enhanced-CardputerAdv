// Settings persistence, value formatting and the interactive settings menu.

#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>

#include "fe_settings.h"
#include "fe_state.h"
#include "fe_ui.h"
#include "fe_display.h"
#include "fe_audio.h"

namespace fe {

const char* const kSettingsMenuItems[18] = {
    "CONTROL UP = ",
    "CONTROL DOWN = ",
    "CONTROL LEFT = ",
    "CONTROL RIGHT = ",
    "CONTROL A BTN = ",
    "CONTROL B BTN = ",
    "CONTROL START = ",
    "CONTROL SELECT = ",
    "GFX INTERLACE ",
    "I2C Joystick ",
    "VOLUME START ",
    "PALETTE START ",
    "SGB COLOR ",
    "SGB BORDER ",
    "CPU MODE ",
    "BASS BOOST ",
    "RESTORE DEFAULTS",
    "EXIT GAME",
};

namespace {

uint8_t readByte(File& f)
{
    uint8_t v = 0;
    f.read(&v, 1);
    return v;
}

void readBytes(File& f, void* dst, size_t n)
{
    f.read(static_cast<uint8_t*>(dst), n);
}

void writeByte(File& f, uint8_t v)
{
    f.write(&v, 1);
}

void writeBytes(File& f, const void* src, size_t n)
{
    f.write(static_cast<const uint8_t*>(src), n);
}

}  // namespace

void settingsRestoreDefaults()
{
    gSettings.keyUp       = kDefaultKeyUp;
    gSettings.keyDown     = kDefaultKeyDown;
    gSettings.keyLeft     = kDefaultKeyLeft;
    gSettings.keyRight    = kDefaultKeyRight;
    gSettings.keyA        = kDefaultKeyA;
    gSettings.keyB        = kDefaultKeyB;
    gSettings.keyStart    = kDefaultKeyStart;
    gSettings.keySelect   = kDefaultKeySelect;
    // The firmware's RESTORE DEFAULTS handler (0x4201019d) writes FULL for the
    // interlace setting, Pocket for the palette, MANUAL for the SGB border,
    // and resets legacy flags / bass boost plus the runtime APU volume to 0x14.
    // I2C joystick and the startup 12-colour flag are left untouched.
    gSettings.gfxInterlace = static_cast<uint8_t>(GfxInterlace::Full);
    gSettings.paletteStart = static_cast<uint8_t>(PalettePreset::Default);
    gSettings.sgbBorder = static_cast<uint8_t>(SgbBorderMode::Manual);
    gSettings.sgbPaletteAuto = 0;
    gSettings.bassBoostGain = 0.0f;
    gSettings.bassBoostMode = 0;
    gSettings.bassBoostStrength = 0x4b;
    gVolume = kDefaultVolume;
    gApuVolume = kDefaultVolume;
    gSettings.changed = true;
}

// ---------------------------------------------------------------------------
// /gbeconfig.dat
// ---------------------------------------------------------------------------
void settingsLoad()
{
    File f = SD.open(kConfigPath, FILE_READ);
    if (!f) {
        return;
    }

    const uint8_t version = readByte(f);

    auto readKeys = [&]() {
        gSettings.keyUp     = readByte(f);
        gSettings.keyDown   = readByte(f);
        gSettings.keyLeft   = readByte(f);
        gSettings.keyRight  = readByte(f);
        gSettings.keyA      = readByte(f);
        gSettings.keyB      = readByte(f);
        gSettings.keyStart  = readByte(f);
        gSettings.keySelect = readByte(f);
    };

    auto readCommon = [&]() {
        gSettings.gfxInterlace   = readByte(f);
        gSettings.i2cJoystick    = readByte(f);
        gSettings.volumeStart    = readByte(f);
        gSettings.paletteStart   = readByte(f);
        gSettings.sgbBorder      = readByte(f);
        gSettings.sgbPaletteAuto = readByte(f);
    };

    if (version == 0) {
        readKeys();
    } else if (version == 1) {
        readKeys();
        gSettings.gfxInterlace = readByte(f);
        gSettings.i2cJoystick  = readByte(f);
        gSettings.volumeStart  = readByte(f);
    } else if (version >= 2 && version <= 4) {
        readKeys();
        readCommon();

        // Versions 2-4 stored two obsolete bytes here.
        (void)readByte(f);
        (void)readByte(f);
        gSettings.bassBoostMode = readByte(f);

        if (version >= 3) {
            gSettings.startupPalette = readByte(f);
        }
        if (version >= 4) {
            readBytes(f, &gSettings.bassBoostGain, sizeof(float));
        }
    } else if (version == 5) {
        readKeys();
        readCommon();
        gSettings.startupPalette = readByte(f);
        readBytes(f, &gSettings.bassBoostGain, sizeof(float));
    }

    switch (gSettings.bassBoostMode) {
    case 1:  gSettings.bassBoostStrength = 0x96; break;
    case 2:  gSettings.bassBoostStrength = 200;  break;
    default: gSettings.bassBoostStrength = 0x4b; break;
    }

    f.close();
}

bool settingsSave()
{
    File f = SD.open(kConfigPath, FILE_WRITE);
    if (!f) {
        return false;
    }

    writeByte(f, 5);
    writeByte(f, gSettings.keyUp);
    writeByte(f, gSettings.keyDown);
    writeByte(f, gSettings.keyLeft);
    writeByte(f, gSettings.keyRight);
    writeByte(f, gSettings.keyA);
    writeByte(f, gSettings.keyB);
    writeByte(f, gSettings.keyStart);
    writeByte(f, gSettings.keySelect);
    writeByte(f, gSettings.gfxInterlace);
    writeByte(f, gSettings.i2cJoystick);
    writeByte(f, gSettings.volumeStart);
    writeByte(f, gSettings.paletteStart);
    writeByte(f, gSettings.sgbBorder);
    writeByte(f, gSettings.sgbPaletteAuto);
    writeByte(f, gSettings.startupPalette);
    writeBytes(f, &gSettings.bassBoostGain, sizeof(float));
    f.close();
    return true;
}

// ---------------------------------------------------------------------------
// Value formatting
// ---------------------------------------------------------------------------
void settingsFormatValue(char* out, size_t outSize, const char* name, int index)
{
    auto keyText = [&](uint8_t key) {
        snprintf(out, outSize, "%s%c", name, static_cast<char>(key));
    };

    switch (index) {
    case 0: keyText(gSettings.keyUp); break;
    case 1: keyText(gSettings.keyDown); break;
    case 2: keyText(gSettings.keyLeft); break;
    case 3: keyText(gSettings.keyRight); break;
    case 4: keyText(gSettings.keyA); break;
    case 5: keyText(gSettings.keyB); break;
    case 6: keyText(gSettings.keyStart); break;
    case 7: keyText(gSettings.keySelect); break;

    case 8: {
        const char* value = "OFF";
        if (gSettings.gfxInterlace == static_cast<uint8_t>(GfxInterlace::Sparse)) {
            value = "SPARSE";
        } else if (gSettings.gfxInterlace == static_cast<uint8_t>(GfxInterlace::Full)) {
            value = "FULL";
        }
        snprintf(out, outSize, "%s%s", name, value);
        break;
    }
    case 9:
        snprintf(out, outSize, "%s%s", name,
                 gSettings.i2cJoystick ? "JoyV2" : "OFF");
        break;
    case 10:
        if (gSettings.volumeStart == 0) {
            snprintf(out, outSize, "%sOFF", name);
        } else if (gSettings.volumeStart == 0x7f) {
            snprintf(out, outSize, "%sMAX", name);
        } else {
            snprintf(out, outSize, "%s%d%%", name,
                     (gSettings.volumeStart * 100) / 0x7f);
        }
        break;
    case 11: {
        const char* value = "Pocket";
        switch (gSettings.paletteStart) {
        case 0: value = "B&W"; break;
        case 1: value = "OrigGB"; break;
        case 2: value = "Pocket"; break;
        default: value = "Light"; break;
        }
        snprintf(out, outSize, "%s%s", name, value);
        break;
    }
    case 12:
        snprintf(out, outSize, "%s%s", name,
                 gSettings.sgbPaletteAuto ? "AUTO" : "MANUAL");
        break;
    case 13: {
        const char* value = "MANUAL";
        if (gSettings.sgbBorder == static_cast<uint8_t>(SgbBorderMode::Auto)) {
            value = "AUTO";
        } else if (gSettings.sgbBorder == static_cast<uint8_t>(SgbBorderMode::Tv)) {
            value = "TV";
        }
        snprintf(out, outSize, "%s%s", name, value);
        break;
    }
    case 14: {
        // fe_settings_format_value: COMPATIBLE when the per-game profile
        // forced compatibility, otherwise FASTEST when the context flag at
        // gb_s+? (p_fe_settings+7) is 0, else FAST.
        const char* value = "FAST";
        if (gSettings.forceCompat) {
            value = "COMPATIBLE";
        } else if (gSettings.cpuMode == static_cast<uint8_t>(CpuMode::Fastest)) {
            value = "FASTEST";
        }
        snprintf(out, outSize, "%s%s", name, value);
        break;
    }
    case 15:
        if (gSettings.bassBoostGain != 0.0f) {
            snprintf(out, outSize, "%s+%.1f", name, gSettings.bassBoostGain);
        } else {
            snprintf(out, outSize, "%sOFF", name);
        }
        break;
    default:
        snprintf(out, outSize, "%s", name);
        break;
    }
}

// ---------------------------------------------------------------------------
// Interactive menu
// ---------------------------------------------------------------------------
namespace {

void drawMenu(int selected, int itemCount)
{
    M5Cardputer.Display.clearDisplay();
    M5Cardputer.Display.fillRect(0, 0, 240, 12, 0x2104);
    M5Cardputer.Display.setTextColor(0xFFFF);
    setFontSize(128);
    M5Cardputer.Display.drawString(" /\\ \\/ TO NAVIGATE | ENTER TO SELECT", 2, 1);

    setFontSize(0x3c);

    const int visible = 5;
    int first = selected - visible / 2;
    if (first < 0) first = 0;
    if (first + visible > itemCount) first = itemCount - visible;

    for (int i = 0; i < visible; i++) {
        const int index = first + i;
        const int y = 24 + i * 20;
        char value[64];
        settingsFormatValue(value, sizeof(value), kSettingsMenuItems[index], index);

        if (index == selected) {
            M5Cardputer.Display.fillRect(0, y - 2, 240, 18, 0x8410);
            M5Cardputer.Display.setTextColor(0x0000);
        } else {
            M5Cardputer.Display.setTextColor(0xFFFF);
        }
        const int textX = 120 - textWidth(value) / 2;
        M5Cardputer.Display.drawString(value, textX, y);
    }

    M5Cardputer.Display.setTextColor(0xFFFF);
    M5Cardputer.Display.drawString(" PRESS ESC OR ' KEY TO CLOSE SETTINGS ", 2, 124);
}

void rebindKey(uint8_t& target)
{
    messageBoxDraw(-15, "Press the new key", "a-z,0-9 and ,./;'", 0xFFFF,
                   0xFFFF, 2, 2, 4);
    const uint8_t key = waitForKey();
    messageBoxClear(-15, "Press the new key", "a-z,0-9 and ,./;'", 2, 2, 4, 0);

    // The firmware accepts a-z plus '-' and ':' (mask 0xffff4002 over the
    // ',':';' range, plus the a-z range check).  Anything else leaves the
    // binding untouched.
    const bool lower = (key >= 'a' && key <= 'z');
    const bool punct = (key == '-' || key == ':');
    if (lower || punct) {
        target = key;
        gSettings.changed = true;
    } else {
        messageBoxDraw(-15, "Invalid Selection", "No Changes", 0xFFFF, 0xFFFF, 2, 2, 4);
        delay(kDelayMessageUs / 1000);
        messageBoxClear(-15, "Invalid Selection", "No Changes", 2, 2, 4, 0);
    }
}

}  // namespace

void settingsMenu()
{
    waitKeyRelease();
    const int itemCount = gPriv.rom ? 18 : 17;
    int selected = 0;
    bool redraw = true;

    while (true) {
        if (redraw) {
            drawMenu(selected, itemCount);
            redraw = false;
        }

        M5Cardputer.update();
        if (!M5Cardputer.Keyboard.isPressed()) {
            delay(10);
            continue;
        }

        auto& state = M5Cardputer.Keyboard.keysState();
        bool acted = false;

        if (state.enter) {
            if (selected <= 7) {
                uint8_t* keys[] = {
                    &gSettings.keyUp, &gSettings.keyDown, &gSettings.keyLeft,
                    &gSettings.keyRight, &gSettings.keyA, &gSettings.keyB,
                    &gSettings.keyStart, &gSettings.keySelect,
                };
                rebindKey(*keys[selected]);
            } else if (selected == 8) {
                gSettings.gfxInterlace = (gSettings.gfxInterlace + 1) % 3;
                gSettings.changed = true;
            } else if (selected == 9) {
                gSettings.i2cJoystick = gSettings.i2cJoystick ? 0 : 2;
                gSettings.changed = true;
            } else if (selected == 10) {
                gSettings.volumeStart = (gSettings.volumeStart + 0x10) & 0x7f;
                gSettings.changed = true;
            } else if (selected == 11) {
                gSettings.paletteStart = (gSettings.paletteStart + 1) & 3;
                gSettings.changed = true;
            } else if (selected == 12) {
                gSettings.sgbPaletteAuto ^= 1;
                gSettings.changed = true;
            } else if (selected == 13) {
                gSettings.sgbBorder = (gSettings.sgbBorder + 1) % 3;
                gSettings.changed = true;
            } else if (selected == 14) {
                // The firmware cycles CPU MODE as FASTEST -> FAST ->
                // COMPATIBLE.  A per-game forced-compatibility profile locks
                // the item, so the value does not change.
                if (!gSettings.forceCompat) {
                    if (gSettings.cpuMode == static_cast<uint8_t>(CpuMode::Fastest)) {
                        gSettings.cpuMode = static_cast<uint8_t>(CpuMode::Fast);
                    } else if (gSettings.cpuMode == static_cast<uint8_t>(CpuMode::Fast)) {
                        gSettings.cpuMode = static_cast<uint8_t>(CpuMode::Compatible);
                    } else {
                        gSettings.cpuMode = static_cast<uint8_t>(CpuMode::Fastest);
                    }
                    gSettings.changed = true;
                }
            } else if (selected == 15) {
                gSettings.bassBoostGain += 0.5f;
                if (gSettings.bassBoostGain > 6.0f) gSettings.bassBoostGain = 0.0f;
                gSettings.changed = true;
            } else if (selected == 16) {
                settingsRestoreDefaults();
                messageBoxDraw(-15, "Restored", "Defaults", 0xFFFF, 0xFFFF, 2, 2, 4);
                delay(kDelayMessageUs / 1000);
                messageBoxClear(-15, "Restored", "Defaults", 2, 2, 4, 0);
            } else if (selected == 17) {
                gAppRunning = false;
                return;
            }
            acted = true;
            redraw = true;
            delay(kDelayKeyRepeatUs / 1000);
        }

        for (char key : state.word) {
            if (key == '`' || key == 0x1b) {
                return;
            }
            if (key == ';') {
                selected = (selected + itemCount - 1) % itemCount;
                acted = true;
                redraw = true;
            } else if (key == '.') {
                selected = (selected + 1) % itemCount;
                acted = true;
                redraw = true;
            }
        }

        if (acted) {
            waitKeyRelease();
        }
    }
}

// ---------------------------------------------------------------------------
// Key wait helpers
// ---------------------------------------------------------------------------
uint8_t waitForKey()
{
    delay(kDelayKeyRepeatUs / 1000);
    while (true) {
        M5Cardputer.update();
        if (!M5Cardputer.Keyboard.isPressed()) {
            continue;
        }
        auto& state = M5Cardputer.Keyboard.keysState();
        if (state.enter) {
            waitKeyRelease();
            return 0x13;
        }
        if (!state.word.empty()) {
            const uint8_t key = static_cast<uint8_t>(state.word[0]);
            waitKeyRelease();
            return key;
        }
        waitKeyRelease();
    }
}

}  // namespace fe
