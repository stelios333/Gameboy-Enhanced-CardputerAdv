// On-screen UI helpers: text and message boxes.

#include <Arduino.h>
#include <M5Cardputer.h>
#include <stdio.h>

#include "fe_ui.h"
#include "fe_state.h"
#include "fe_display.h"

namespace fe {

void waitYield()
{
    while (gDisplayBusy) {
        taskYIELD();
    }
}

void waitKeyRelease()
{
    do {
        M5Cardputer.update();
    } while (M5Cardputer.Keyboard.isPressed());
}

void setFontSize(int divisor)
{
    int textSize = M5Cardputer.Display.height() / divisor;
    if (textSize == 0) {
        textSize = 1;
    }
    M5Cardputer.Display.setTextSize(textSize);
}

void setTextScale(int size)
{
    if (size < 1) {
        size = 1;
    }
    M5Cardputer.Display.setTextSize(size);
}

int textWidth(const char* text)
{
    return M5Cardputer.Display.textWidth(text);
}

void debugPrint(const char* message)
{
    M5Cardputer.Display.clearDisplay();
    M5Cardputer.Display.setTextColor(0xFFFF);
    M5Cardputer.Display.setCursor(0, 0);
    M5Cardputer.Display.print(message);
}

// ---------------------------------------------------------------------------
// Message boxes (fe_message_box_draw / fe_message_box_clear)
// ---------------------------------------------------------------------------
namespace {

struct BoxGeometry {
    int x;
    int y;
    int w;
    int h;
    int line1Height;
    int line2Height;
    int line1Width;
    int line2Width;
};

BoxGeometry computeBox(const char* line1, const char* line2, int line1Size,
                       int line2Size, int padding, int centerX)
{
    setTextScale(line1Size);
    const int w1 = textWidth(line1);
    const int h1 = M5Cardputer.Display.fontHeight();

    int w2 = 0;
    int h2 = 0;
    if (line2 && line2[0]) {
        setTextScale(line2Size);
        w2 = textWidth(line2);
        h2 = M5Cardputer.Display.fontHeight();
    }

    const int boxW = std::max(w1, w2) + 2 + padding;
    const int boxH = h1 + h2 + 2 + padding;

    BoxGeometry g;
    g.w = boxW;
    g.h = boxH;
    g.x = 120 - boxW / 2;
    g.y = (centerX + 0x43) - boxH / 2;
    g.line1Height = h1;
    g.line2Height = h2;
    g.line1Width = w1;
    g.line2Width = w2;
    return g;
}

}  // namespace

void messageBoxDraw(int centerX, const char* line1, const char* line2,
                    uint16_t line1Color, uint16_t line2Color,
                    int line1Size, int line2Size, int padding)
{
    waitYield();

    const BoxGeometry g = computeBox(line1, line2, line1Size, line2Size,
                                     padding, centerX);

    M5Cardputer.Display.fillRect(g.x, g.y, g.w, g.h, 0x2104);
    M5Cardputer.Display.drawRect(g.x, g.y, g.w, g.h, 0xFFFF);

    setTextScale(line1Size);
    M5Cardputer.Display.setTextColor(line1Color);
    const int line1Y = g.y + 1 + padding;
    M5Cardputer.Display.drawString(line1, 120 - g.line1Width / 2, line1Y);

    if (line2 && line2[0]) {
        setTextScale(line2Size);
        M5Cardputer.Display.setTextColor(line2Color);
        M5Cardputer.Display.drawString(line2, 120 - g.line2Width / 2,
                                       line1Y + g.line1Height);
    }
}

void messageBoxClear(int centerX, const char* line1, const char* line2,
                     int line1Size, int line2Size, int padding,
                     uint32_t background)
{
    waitYield();
    const BoxGeometry g = computeBox(line1, line2, line1Size, line2Size,
                                     padding, centerX);
    M5Cardputer.Display.fillRect(g.x, g.y, g.w, g.h,
                                 rgb888ToRgb565(background));
}

// ---------------------------------------------------------------------------
// gb_error
// ---------------------------------------------------------------------------
void gbError(gb_s* /*gb*/, const enum gb_error_e errorCode, const uint16_t address)
{
    // gb_error_messages at 0x3c0702a4; the strings already carry "ERROR:".
    static const char* const messages[] = {
        "ERROR:UNKNOWN",
        "ERROR:INVALID OPCODE",
        "ERROR:INVALID READ",
        "ERROR:INVALID WRITE",
        "ERROR:HATL FOREVER",
    };

    const int index = (errorCode >= 0 && errorCode < 5) ? errorCode : 0;
    char addr[44];
    snprintf(addr, sizeof(addr), "%04x", address & 0xffff);

    messageBoxDraw(-15, messages[index], addr, rgb888ToRgb565(0xFF0000),
                   rgb888ToRgb565(0xFFFFFF), 2, 2, 4);
    delay(kDelayGbErrorUs / 1000);
    messageBoxClear(-15, messages[index], addr, 2, 2, 4, 0);
}

}  // namespace fe
