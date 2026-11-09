// Per-game SGB / 12-colour / GBC palette profile detection.
//
// The original fe_load_sgbprofile() was an unrolled chain of 16-byte title
// comparisons.  This table is the equivalent, reconstructed from that chain.
// Entries with kNoChange leave the corresponding index unchanged.

#include <string.h>

#include "fe_walnut.h"
#include "fe_sgb.h"
#include "fe_state.h"
#include "fe_rom_cache.h"

namespace fe {

namespace {

constexpr uint8_t kNoChange = 0xff;

enum ProfileFlags : uint8_t {
    kFlagForceCompat  = 0x01,
    kFlagShared2Clear = 0x02,
    kFlagShared4      = 0x04,
    kFlagReturnZero   = 0x08,
};

struct SgbProfile {
    char    title[17];  // 16 significant bytes plus the C terminator
    uint8_t sgb;
    uint8_t twelve;
    uint8_t cgb;
    uint8_t flags;
};

// 16-byte cartridge titles, exactly as stored at 0x134..0x143.
// Order follows the comparison chain in fe_load_sgbprofile().
const SgbProfile kProfiles[] = {
    {"BOMBER MAN GB",        0x27, 0x32, kNoChange, 0},
    {"GAME&WATCH",           0x26, 0x12, kNoChange, 0},
    {"KEN GRIFFEY JR",       0x22, 0x31, kNoChange, 0},
    {"KILLERINSTINCT95",     0x22, 0x30, kNoChange, 0},
    {"MEGAMAN3",             0x24, 0x19, kNoChange, 0},
    {"G&W GALLERY",          0x22, 0x13, kNoChange, 0},
    {"BOY AND BLOB GB1",     0x25, 0x0a, kNoChange, 0},
    {"JAMES  BOND  007",     0x22, 0x15, kNoChange, 0},
    {"MILLI/CENTI/PEDE",     0x22, 0x03, kNoChange, 0},
    {"LOLO2",                0x24, 0x00, kNoChange, 0},
    {"MEGA MAN 2",           0x24, 0x18, kNoChange, 0},
    {"MEGAMAN",              0x24, 0x17, kNoChange, 0},
    {"MYSTIC QUEST",         0x22, 0x1b, kNoChange, 0},
    {"BZONE SBREAKOUT",      0x23, kNoChange, kNoChange, kFlagForceCompat},
    {"BALLOON KID",          0x00, 0x06, kNoChange, 0},
    {"SUPERMARIOLAND3",      0x01, 0x2a, kNoChange, 0},
    {"KIRBY'S PINBALL",      0x01, 0x2f, kNoChange, 0},
    {"ZELDA",                0x04, 0x16, kNoChange, 0},
    {"SUPER MARIOLAND",      0x05, 0x23, kNoChange, 0},
    {"TETRIS",               0x10, 0x26, kNoChange, 0},
    {"MARIOLAND2",           0x13, 0x24, kNoChange, 0},
    {"F1RACE",               0x1d, 0x12, kNoChange, 0},
    {"DONKEY KONG",          0x07, 0x0c, kNoChange, 0},
    {"DONKEYKONGLAND95",     0x07, 0x0d, kNoChange, 0},
    {"DW1&2\x00\x00\x00\x00\x00\x00" "AEDE\x80",
                             0x09, 0x06, 0x03, 0},
    {"POKEMON RED",          0x07, 0x1c, 0x03, 0},
    {"POKEMON BLUE",         0x0c, 0x22, 0x01, 0},
    {"POKEMON YELLOW\x00\x80",
                             0x21, 0x2d, 0x00, kFlagShared2Clear},
    {"POKEMON_GLDAAUE\x80",  0x05, 0x23, 0x02, kFlagShared2Clear},
    {"POKEMON_SLVAAXE\x80",  0x05, 0x23, 0x02, 0},
    {"METROID2",             0x1e, 0x1a, kNoChange, 0},
    {"GOLF",                 0x17, 0x14, kNoChange, 0},
    {"SOLARSTRIKER",         0x06, 0x1f, kNoChange, 0},
    {"SPACE INVADERS",       0x06, 0x20, kNoChange, 0},
    {"ASTEROIDS/MISCMD",     0x22, 0x02, kNoChange, 0},
    {"SAGA",                 0x20, 0x2c, kNoChange, 0},
    {"SAGA2",                0x20, 0x2c, kNoChange, 0},
    {"ALLEY WAY",            0x15, 0x01, kNoChange, 0},
    {"BASEBALL",             0x0e, 0x01, kNoChange, 0},
    {"SRJ DMG",              0x26, 0x21, kNoChange, 0},
    {"GBWARS",               0x14, kNoChange, kNoChange, 0},
    {"DR.MARIO",             0x11, 0x10, kNoChange, 0},
    {"KAERUNOTAMENI",        0x08, kNoChange, kNoChange, 0},
    {"KID ICARUS",           0x0d, kNoChange, kNoChange, 0},
    {"KIRBY DREAM LAND",     0x0a, kNoChange, kNoChange, 0},
    {"MARIO & YOSHI",        0x0b, kNoChange, kNoChange, 0},
    {"QIX",                  0x18, kNoChange, kNoChange, 0},
    {"TENNIS",               0x16, kNoChange, kNoChange, 0},
    {"X",                    0x1b, kNoChange, kNoChange, 0},
    {"YAKUMAN",              0x12, kNoChange, kNoChange, 0},
    {"YOSSY NO TAMAGO",      0x0b, kNoChange, kNoChange, 0},
    {"DONKEYKONGLAND 3",     kNoChange, kNoChange, kNoChange, kFlagShared4},
    {"DONKEYKONGLAND 2",     kNoChange, kNoChange, kNoChange, kFlagShared4},
    // app2.elf only sets shared_flags[4] and returns 0 for this title.  It
    // does not store a high-window pointer at shared_flags+8; that write (and
    // the embedded GTA ROM it pointed at) was added in app_new.elf.
    {"GRAND THEFTAOAE\x80",  kNoChange, kNoChange, kNoChange,
     kFlagShared4 | kFlagReturnZero},
};

}  // namespace

uint8_t loadSgbProfile(gb_s* gb)
{
    gSettings.forceCompat = 0;

    char title[16];
    for (int i = 0; i < 16; i++) {
        title[i] = static_cast<char>(gbRomRead(gb, 0x134 + i));
    }

    for (const SgbProfile& p : kProfiles) {
        if (memcmp(title, p.title, 16) != 0) {
            continue;
        }

        if (p.sgb != kNoChange) {
            gSgbPaletteIndex = p.sgb;
        }
        if (p.twelve != kNoChange) {
            gTwelveColourIndex = p.twelve;
        }
        if (p.cgb != kNoChange) {
            gCgbPaletteIndex = p.cgb;
        }
        if (p.flags & kFlagForceCompat) {
            gSettings.forceCompat = 1;
        }
        if (p.flags & kFlagShared2Clear) {
            gSharedFlags[kSharedSgbCompat] = 0;
        }
        if (p.flags & kFlagShared4) {
            gSharedFlags[kSharedHighCache] = 1;
        }
        if (p.flags & kFlagReturnZero) {
            return 0;
        }
        return 1;
    }

    return 0;
}

}  // namespace fe
