// Display / scanline rendering.
#pragma once

#include <stdint.h>
#include "fe_walnut.h"
#include "fe_config.h"
#include "fe_state.h"

namespace fe {

// Called by walnut-cgb for every visible scanline.
void lcdDrawLine(gb_s* gb, const uint8_t* pixels, const uint_fast8_t line);

// Double-buffered scanline writer used by the display task.
void scanlineDraw(gb_s* gb, const uint8_t* pixels, uint_fast8_t line);

// Push the framebuffer to the panel.
void fitFrame();
void fitFrameRedrawEverything();

// SGB border halves (40 x 135 RGB565 each).
void drawSgbBorder();

// Convert a 24-bit RGB value to RGB565 using the same 5/6/5 scaling as the
// original fe_rgb888_to_rgb565().
uint16_t rgb888ToRgb565(uint32_t rgb);

// Rebuild gDisplay.palette from gActivePalette.
void updatePalette();

// Returns the current left/right border image.
const uint16_t* sgbBorderLeft();
const uint16_t* sgbBorderRight();

// Apply a vertical scale / aspect ratio mode.
void setAspectRatio(uint8_t mode);
void setVerticalScale(uint8_t mode);

}  // namespace fe
