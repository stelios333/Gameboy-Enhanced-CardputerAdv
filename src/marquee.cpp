// Marquee animation shared by the settings menu and the file picker.

#include <Arduino.h>
#include <M5Cardputer.h>
#include <stdlib.h>

#include "fe_marquee.h"
#include "fe_ui.h"
#include "fe_config.h"
#include "fe_display.h"

namespace fe {

namespace {

// fe_settings_menu / fe_file_picker: two pixels per animation frame; the
// travel range is width / 2 - 115, so a long line runs from x = 5 to
// x = 235 - width.
constexpr int      kMarqueeStepPx   = 2;
constexpr int      kMarqueeMargin   = 115;
constexpr uint32_t kMarqueeEraseRgb = 0x00111111;
constexpr uint32_t kMarqueeFrameUs  = 50000;

}  // namespace

void marqueeReset(Marquee& marquee, int width)
{
    if (width > kDisplayWidth) {
        marquee.limit  = width / 2 - kMarqueeMargin;
        marquee.scroll = marquee.limit;
        marquee.dir    = 1;
    } else {
        marquee.limit  = 0;
        marquee.scroll = 0;
        marquee.dir    = 1;
    }
}

void marqueeStep(Marquee& marquee, const char* text, uint32_t colorRgb888)
{
    if (!marquee.active()) {
        delay(10);
        return;
    }

    auto& d = M5Cardputer.Display;
    setFontSize(0x3c);
    const int width  = textWidth(text);
    const int height = d.fontHeight();
    const int y      = d.height() / 2 - height / 2;

    // Erase the previous frame with the highlight colour, exactly as the
    // firmware does before advancing the position.
    d.setTextColor(rgb888ToRgb565(kMarqueeEraseRgb));
    d.drawString(text, 120 + marquee.scroll - width / 2, y);

    marquee.scroll += marquee.dir ? -kMarqueeStepPx : kMarqueeStepPx;
    if (abs(marquee.scroll) >= marquee.limit) {
        if (marquee.dir) {
            marquee.scroll = -marquee.limit;
            marquee.dir    = 0;
        } else {
            marquee.scroll = marquee.limit;
            marquee.dir    = 1;
        }
    }

    d.setTextColor(rgb888ToRgb565(colorRgb888));
    d.drawString(text, 120 + marquee.scroll - width / 2, y);

    // 0xC350 us (50 ms) in the firmware; delay() keeps the same cadence while
    // yielding to the other tasks.
    delay(kMarqueeFrameUs / 1000);
}

}  // namespace fe
