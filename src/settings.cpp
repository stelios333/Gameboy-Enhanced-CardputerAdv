// Settings persistence, value formatting and the interactive settings menu.

#include <Arduino.h>
#include <M5Cardputer.h>
#include <SD.h>
#include <stdio.h>
#include <string.h>

#include "fe_settings.h"
#include "fe_state.h"
#include "fe_ui.h"
#include "fe_marquee.h"
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
    // interlace setting, palette index 2 (Default) for PALETTE START, MANUAL
    // for the SGB border, and resets the legacy flags / bass boost plus the
    // runtime APU volume to 0x14.  I2C joystick, CPU mode and the startup
    // compatibility flag are left untouched.
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
        // fe_settings_format_value: startup palette != 0 -> COMPATIBLE,
        // otherwise the p_fe_settings+7 fast flag selects FASTEST, else FAST.
        const char* value = "FAST";
        if (gSettings.startupPalette) {
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

void drawMenu(int selected, int itemCount, Marquee& marquee)
{
    auto& d = M5Cardputer.Display;

    // fe_settings_menu clears the panel, paints a 10 px header and a 12 px
    // footer in rgb888(0x00032303), gives both a black drop shadow at (3,0)
    // / (3,124) plus a white copy at (4,1) / (4,125), and separates them with
    // white 240 px rules at y=11 and y=123.
    d.fillRect(0, 0, kDisplayWidth, kFbHeight, 0x0000);

    d.fillRect(0, 0, kDisplayWidth, 10, rgb888ToRgb565(0x00032303));
    setFontSize(0x80);
    d.setTextColor(0x0000);
    d.drawString(" /\\ \\/ TO NAVIGATE | ENTER TO SELECT", 3, 0);
    d.setTextColor(0xFFFF);
    d.drawString(" /\\ \\/ TO NAVIGATE | ENTER TO SELECT", 4, 1);
    d.drawFastHLine(0, 11, kDisplayWidth, 0xFFFF);

    d.fillRect(0, 123, kDisplayWidth, 12, rgb888ToRgb565(0x00032303));
    setFontSize(0x80);
    d.setTextColor(0x0000);
    d.drawString(" PRESS ESC OR ' KEY TO CLOSE SETTINGS ", 3, 124);
    d.setTextColor(0xFFFF);
    d.drawString(" PRESS ESC OR ' KEY TO CLOSE SETTINGS ", 4, 125);
    d.drawFastHLine(0, 123, kDisplayWidth, 0xFFFF);

    const int height = d.height();
    char value[80];

    // The firmware shows exactly three entries: the selection in the centre
    // (setFontSize(0x3c), white, on a rgb888(0x111111) highlight) and its
    // immediate neighbours (setFontSize(0x60), rgb888(0xAAAAAA)) at +/- 3
    // line heights.
    if (selected > 0) {
        settingsFormatValue(value, sizeof(value),
                            kSettingsMenuItems[selected - 1], selected - 1);
        setFontSize(0x60);
        const int w = textWidth(value);
        const int h = d.fontHeight();
        const int y = height / 2 - h / 2 - 3 * h;
        d.setTextColor(rgb888ToRgb565(0xAAAAAA));
        d.drawString(value, 120 - w / 2, y);
    }

    settingsFormatValue(value, sizeof(value), kSettingsMenuItems[selected], selected);
    setFontSize(0x3c);
    {
        const int w = textWidth(value);
        const int h = d.fontHeight();
        const int y = height / 2 - h / 2;
        // The firmware rewinds the marquee whenever the menu is redrawn and
        // starts long selections at their left-most position, scrolling left.
        marqueeReset(marquee, w);
        d.fillRect(0, y - 2, kDisplayWidth, h + 4, rgb888ToRgb565(0x00111111));
        d.setTextColor(0xFFFF);
        d.drawString(value, 120 + marquee.scroll - w / 2, y);
    }

    if (selected + 1 < itemCount) {
        settingsFormatValue(value, sizeof(value),
                            kSettingsMenuItems[selected + 1], selected + 1);
        setFontSize(0x60);
        const int w = textWidth(value);
        const int h = d.fontHeight();
        const int y = height / 2 - h / 2 + 3 * h;
        d.setTextColor(rgb888ToRgb565(0xAAAAAA));
        d.drawString(value, 120 - w / 2, y);
    }
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
    // fe_settings_menu checks fe_app_running: the EXIT GAME entry only exists
    // while a game is actually running (18 items); from the picker there are
    // 17 items.
    const int itemCount = gInGame ? 18 : 17;
    int selected = 0;
    bool redraw = true;
    Marquee marquee;
    char marqueeText[80];

    while (true) {
        if (redraw) {
            drawMenu(selected, itemCount, marquee);
            redraw = false;
        }

        M5Cardputer.update();
        if (!M5Cardputer.Keyboard.isPressed()) {
            // While the menu is idle, animate a long selected entry at the
            // firmware's two-pixels-per-50-ms rate.  Short entries only yield.
            settingsFormatValue(marqueeText, sizeof(marqueeText),
                                kSettingsMenuItems[selected], selected);
            marqueeStep(marquee, marqueeText, 0x00FFFFFF);
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
                // p_fe_settings+6 ^= 1 (the stored value is 2 or 3).
                gSettings.i2cJoystick ^= 1;
                gSettings.changed = true;
            } else if (selected == 10) {
                // VOLUME START: +25, wrapping to 0 at 127 and clamping there.
                if (gSettings.volumeStart > 126) {
                    gSettings.volumeStart = 0;
                } else {
                    const int v = gSettings.volumeStart + 25;
                    gSettings.volumeStart = static_cast<uint8_t>(v > 127 ? 127 : v);
                }
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
                // The firmware cycles two bytes: fe_startup_palette (0x3fcb121c,
                // the core compatibility flag) and the fast flag at
                // p_fe_settings+7.  The three states are FAST (0,0),
                // FASTEST (0,1), COMPATIBLE (1,0).  No lock is applied when a
                // profile forced compatibility.
                if (gSettings.startupPalette) {
                    gSettings.startupPalette = 0;
                    gSettings.cpuMode = static_cast<uint8_t>(CpuMode::Fast);
                } else if (gSettings.cpuMode != static_cast<uint8_t>(CpuMode::Fastest)) {
                    gSettings.cpuMode = static_cast<uint8_t>(CpuMode::Fastest);
                } else {
                    gSettings.startupPalette = 1;
                    gSettings.cpuMode = static_cast<uint8_t>(CpuMode::Fast);
                }
                gSettings.changed = true;
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
