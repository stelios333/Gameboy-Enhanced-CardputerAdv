// Scanline rendering, framebuffer push and SGB borders.

#include <Arduino.h>
#include <M5Cardputer.h>
#include <esp_random.h>
#include <string.h>

#include "fe_walnut.h"
#include "fe_display.h"
#include "fe_state.h"
#include "fe_ui.h"

// Extracted RGB565 border halves.
#include "../resources/sgb_borders.h"

namespace fe {

namespace {

inline uint16_t scale5(uint32_t v) { return static_cast<uint16_t>((v * 31u) / 255u); }
inline uint16_t scale6(uint32_t v) { return static_cast<uint16_t>((v * 63u) / 255u); }

const uint16_t* framebufferAt(int row)
{
    return &gFramebuffer[row][0];
}

void pushChunk(int y, int height, const uint16_t* src)
{
    M5Cardputer.Display.pushImage(kDisplayX, y, kLcdWidth, height, src);
}

}  // namespace

uint16_t rgb888ToRgb565(uint32_t rgb)
{
    const uint16_t r = scale5((rgb >> 16) & 0xff);
    const uint16_t g = scale6((rgb >> 8) & 0xff);
    const uint16_t b = scale5(rgb & 0xff);
    return static_cast<uint16_t>((r << 11) | (g << 5) | b);
}

void updatePalette()
{
    for (int i = 0; i < 12; i++) {
        gPalette565[i] = rgb888ToRgb565(gActivePalette[i]);
    }
    // Keep the display state's active palette in sync.
    memcpy(gDisplay.palette, gPalette565, sizeof(gPalette565));
}

// ---------------------------------------------------------------------------
// Scanline callbacks
// ---------------------------------------------------------------------------
void lcdDrawLine(gb_s* gb, const uint8_t* pixels, const uint_fast8_t line)
{
    const int y = (line * kFbHeight) / kLcdHeight;
    if (y < 0 || y >= kFbHeight) {
        return;
    }

    uint16_t* dst = &gFramebuffer[y][0];

    if (gb && gb->cgb.cgbMode) {
        for (int x = 0; x < kLcdWidth; x++) {
            dst[x] = gb->cgb.fixPalette[pixels[x]];
        }
    } else if (gDisplay.twelveColour) {
        for (int x = 0; x < kLcdWidth; x++) {
            const uint8_t p = pixels[x];
            const uint8_t index = static_cast<uint8_t>(((p >> 1) & 9) | (p & 3));
            dst[x] = gPalette565[index];
        }
    } else {
        for (int x = 0; x < kLcdWidth; x++) {
            dst[x] = gPalette565[pixels[x] & 3];
        }
    }

    gDisplay.dirty[y] = 1;
}

void scanlineDraw(gb_s* gb, const uint8_t* pixels, uint_fast8_t line)
{
    if (line >= kLcdHeight) {
        return;
    }

    const int y = (line * kFbHeight) / kLcdHeight;

    // Wait until the display task has pushed the previous frame's line.
    while (gDisplay.dirty[y]) {
        taskYIELD();
    }

    lcdDrawLine(gb, pixels, line);
    gDisplay.dirty[y] = 1;
}

// ---------------------------------------------------------------------------
// Framebuffer push
// ---------------------------------------------------------------------------
namespace {

void rebuildRowMap(int destWidth)
{
    if (destWidth < 1) {
        destWidth = 1;
    }

    if (destWidth <= kDisplayWidth) {
        for (int x = 0; x < destWidth; x++) {
            int src = (x * kLcdWidth) / destWidth;
            if (src >= kLcdWidth) {
                src = kLcdWidth - 1;
            }
            gDisplay.rowMap[x] = static_cast<uint8_t>(src);
        }
        return;
    }

    // Wider than the panel: the firmware crops the middle of the stretched
    // image to 240 columns.  Mirrors the `dest_height > 240` branch of
    // fe_display_scale_blit().
    const int start = kLcdWidth * ((destWidth / 2) - (kDisplayWidth / 2));
    for (int x = 0; x < kDisplayWidth; x++) {
        int src = (start + x * kLcdWidth) / destWidth;
        if (src >= kLcdWidth) {
            src = kLcdWidth - 1;
        }
        gDisplay.rowMap[x] = static_cast<uint8_t>(src);
    }
}

int scaledOutputWidth(int destWidth)
{
    return (destWidth <= kDisplayWidth) ? destWidth : kDisplayWidth;
}

void displayScaleBlit(int destWidth)
{
    if (destWidth < 1) {
        destWidth = 1;
    }

    rebuildRowMap(destWidth);
    gDisplay.scaledWidth = static_cast<uint16_t>(destWidth);

    const int outWidth = scaledOutputWidth(destWidth);
    const int xOffset  = (kDisplayWidth - outWidth) / 2;

    for (int y = 0; y < kFbHeight; y++) {
        if (!gDisplay.dirty[y]) {
            continue;
        }

        const uint16_t* src = framebufferAt(y);
        for (int x = 0; x < outWidth; x++) {
            gLineBuffer[x] = src[gDisplay.rowMap[x]];
        }

        M5Cardputer.Display.pushImage(xOffset, y, outWidth, 1, gLineBuffer);
        gDisplay.dirty[y] = 0;
    }
}

}  // namespace

void fitFrame()
{
    if (gDisplay.mode == 0) {
        if (gSettings.gfxInterlace == static_cast<uint8_t>(GfxInterlace::Sparse)) {
            // Sparse interlace pushes random 1..3 line chunks so that the SPI
            // traffic is spread over several frames.  The firmware computes
            // `esp_random() % 2` for the start row and `esp_random() % 3 + 1`
            // for the chunk length (via div3 magic).
            int row = static_cast<int>(esp_random() % 2);
            while (row < kFbHeight) {
                int count = static_cast<int>(esp_random() % 3) + 1;
                if (row + count > kFbHeight) {
                    count = kFbHeight - row;
                }
                pushChunk(row, count, framebufferAt(row));
                row += count;
            }
        } else if (gSettings.gfxInterlace == static_cast<uint8_t>(GfxInterlace::Full)) {
            for (int row = 0; row < kFbHeight; row++) {
                if (gDisplay.dirty[row]) {
                    pushChunk(row, 1, framebufferAt(row));
                    gDisplay.dirty[row] = 0;
                }
                if ((row & 3) != 0) {
                    taskYIELD();
                }
            }
        } else {
            for (int i = 0; i < kChunkCount; i++) {
                pushChunk(i * kChunkLines, kChunkLines, framebufferAt(i * kChunkLines));
                if ((i & 1) != 0) {
                    taskYIELD();
                }
            }
            memset(gDisplay.dirty, 0, sizeof(gDisplay.dirty));
        }
    } else {
        displayScaleBlit(gDisplay.destWidth);
    }
}

void fitFrameRedrawEverything()
{
    waitYield();

    if (gDisplay.mode == 0) {
        if (gSettings.gfxInterlace == static_cast<uint8_t>(GfxInterlace::Sparse)) {
            int row = static_cast<int>(esp_random() % 2);
            while (row < kFbHeight) {
                int count = static_cast<int>(esp_random() % 3) + 1;
                if (row + count > kFbHeight) {
                    count = kFbHeight - row;
                }
                pushChunk(row, count, framebufferAt(row));
                row += count;
            }
        } else if (gSettings.gfxInterlace == static_cast<uint8_t>(GfxInterlace::Full)) {
            // Interlaced mode redraws the whole panel in one transfer.
            pushChunk(0, kFbHeight, framebufferAt(0));
        } else {
            for (int i = 0; i < kChunkCount; i++) {
                pushChunk(i * kChunkLines, kChunkLines, framebufferAt(i * kChunkLines));
            }
        }
    } else {
        memset(gDisplay.dirty, 1, sizeof(gDisplay.dirty));
        displayScaleBlit(gDisplay.destWidth);
    }

    memset(gDisplay.dirty, 0, sizeof(gDisplay.dirty));
}

// ---------------------------------------------------------------------------
// SGB border
// ---------------------------------------------------------------------------
// fe_sgb_border_left/right (0x420052f8 / 0x4200530c): frame 0 uses the
// stereo-TV border at 0x3c0be7e4 / 0x3c0bbdb4, frame 1 uses the Cardputer
// arcade border at 0x3c0c3c44 / 0x3c0c1214.
const uint16_t* sgbBorderLeft()
{
    return (gSgbBorderFrame == 1) ? sgb_borders_cardputer_left
                                  : sgb_borders_tvborder_left;
}

const uint16_t* sgbBorderRight()
{
    return (gSgbBorderFrame == 1) ? sgb_borders_cardputer_right
                                  : sgb_borders_tvborder_right;
}

void drawSgbBorder()
{
    const int width = gDisplay.destWidth;
    if (width >= kDisplayWidth) {
        return;
    }

    const int leftX  = 80 - (width / 2);
    const int rightX = 120 - (width / 2) + width;

    if (!gSgbBorderEnabled) {
        M5Cardputer.Display.fillRect(leftX, 0, kBorderWidth, kBorderHeight, 0x0000);
        M5Cardputer.Display.fillRect(rightX, 0, kBorderWidth, kBorderHeight, 0x0000);
        return;
    }

    M5Cardputer.Display.pushImage(leftX, 0, kBorderWidth, kBorderHeight, sgbBorderLeft());
    M5Cardputer.Display.pushImage(rightX, 0, kBorderWidth, kBorderHeight, sgbBorderRight());
}

// ---------------------------------------------------------------------------
// Aspect / vertical scaling
// ---------------------------------------------------------------------------
void setAspectRatio(uint8_t mode)
{
    // 10:9 is the native Game Boy aspect; the remaining ratios stretch the
    // image horizontally.  Mirrors the eleven jump-table cases at 0x420131ba.
    static const uint16_t widths[11] = {
        160, 165, 180, 195, 210, 225, 240, 255, 270, 285, 300,
    };

    gDisplay.aspectIndex = mode % 11;
    gDisplay.destWidth = widths[gDisplay.aspectIndex];
    gDisplay.mode = (gDisplay.aspectIndex == 0) ? 0 : 1;
    rebuildRowMap(gDisplay.destWidth);
    gDisplay.scaledWidth = gDisplay.destWidth;
    memset(gDisplay.dirty, 1, sizeof(gDisplay.dirty));
}

void setVerticalScale(uint8_t mode)
{
    // Vertical alignment: the Game Boy image already fills the 135-line
    // panel, so these modes only affect how the border area is positioned.
    gDisplay.verticalScale = mode & 3;
    memset(gDisplay.dirty, 1, sizeof(gDisplay.dirty));
}

}  // namespace fe
