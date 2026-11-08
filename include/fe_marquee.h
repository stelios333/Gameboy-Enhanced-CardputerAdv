// Scrolling ("marquee") text for the selected entry of the settings menu and
// the file picker.
//
// The firmware keeps one position, one limit and one direction per menu.  A
// long selection starts at +limit, moves two pixels per frame and bounces
// between +limit and -limit.  Each frame first repaints the glyphs at the old
// position in the highlight colour and then draws them at the new position in
// their normal colour.
#pragma once

#include <stdint.h>

namespace fe {

struct Marquee {
    int    scroll = 0;
    int    limit  = 0;
    int8_t dir    = 1;  // 1 = moving left (-2 px), 0 = moving right (+2 px)

    bool active() const { return limit > 0; }
};

// Recomputes the travel limit for a string of `width` pixels and rewinds the
// animation to its starting position.  Short strings leave the marquee idle.
void marqueeReset(Marquee& marquee, int width);

// Advances one frame: erase the previous frame, move two pixels, redraw in
// `colorRgb888`, then wait 50 ms (the firmware's per-frame delay).  When the
// string is short this only yields.
void marqueeStep(Marquee& marquee, const char* text, uint32_t colorRgb888);

}  // namespace fe
