// On-screen UI helpers: message boxes, text, file picker.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "fe_walnut.h"
#include "fe_config.h"

namespace fe {

void setFontSize(int divisor);
void setTextScale(int size);
int  textWidth(const char* text);
void debugPrint(const char* message);

// Message box with one or two lines. `centerX` is kept for compatibility with
// the original API (the firmware passed -15).
void messageBoxDraw(int centerX, const char* line1, const char* line2,
                    uint16_t line1Color, uint16_t line2Color,
                    int line1Size, int line2Size, int padding);
void messageBoxClear(int centerX, const char* line1, const char* line2,
                     int line1Size, int line2Size, int padding,
                     uint32_t background);

// SD directory browser. Writes the selected absolute path into `out`.
void filePicker(char* out, size_t outSize);

void waitKeyRelease();
void waitYield();

// gb_error callback
void gbError(gb_s* gb, const enum gb_error_e errorCode, const uint16_t address);

}  // namespace fe
