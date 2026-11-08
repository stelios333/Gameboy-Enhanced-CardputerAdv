# Gameboy Enhanced Firmware — reconstructed front-end

This folder contains a source-level reconstruction of the M5Stack Cardputer
Game Boy emulator front-end found in `app_new.elf`.  It is not a byte-for-byte
decompilation: the code has been rewritten as normal C++/Arduino modules with
the same control flow, constants, tables and user-visible behaviour as the
firmware.

See `VERIFICATION_NOTES.md` for the line-by-line audit against the ELF, the
list of confirmed divergences that were fixed, and the remaining known
deviations.

## What is included

```
src/
  main.cpp              setup(), SD init, globals
  emulator_main.cpp     splash, ROM load, task creation, input/main loop
  display.cpp           scanline conversion, framebuffer push, SGB borders
  rom_cache.cpp         119-sector streaming ROM cache + walnut callbacks
  file_picker.cpp       SD browser
  marquee.cpp           scrolling selected text shared by both menus
  ui.cpp                text helpers, message boxes, file picker
  settings.cpp          /gbeconfig.dat load/save, settings menu
  save_state.cpp        .sav cartridge RAM and .qsv save states
  sgb_profile.cpp       per-game SGB / 12-colour / GBC palette profiles
  audio_task.cpp        minigb_apu -> M5Unified speaker task
  palettes.cpp          accessors for the extracted palette tables
  walnut_cgb_impl.cpp   the single translation unit that emits walnut-cgb

include/                public headers for the modules above
resources/              extracted data tables (palettes, borders, boot ROM)
lib/
  Walnut-CGB/           Walnut-CGB v1.2.4 emulator core (single header)
  minigb_apu_cardputer/ minigb_apu.c/.h (APU) + glue.h shim
```

M5Unified, M5GFX, LGFX, SD and the ESP-IDF are intentionally **not** copied
here; PlatformIO pulls them in from `lib_deps`.  The `walnut-cgb/` folder at
the workspace root is the previous 1.2.3 copy and is no longer referenced —
everything builds against `lib/Walnut-CGB/`.

## Building

The reconstruction was validated with:

* PlatformIO `espressif32` 8.0.0
* board `m5stack-cardputer` (ESP32-S3)
* Arduino framework 3.3.12
* Walnut-CGB 1.2.4

```sh
./build.sh          # or: pio run
```

`build.sh` is a thin wrapper around `pio run` in this directory.  The build
tree (`.pio/`) is kept between runs so only changed translation units are
recompiled — do not delete it unless you really want a full rebuild.

### Dependencies

`platformio.ini` points the include paths at the vendored copies:

```ini
lib_dir = lib
build_flags =
    -Ilib/Walnut-CGB
    -Ilib/minigb_apu_cardputer
```

* **`lib/Walnut-CGB/walnut_cgb.h`** is the unmodified upstream single-header
  emulator (upstream v1.2.4).  `src/walnut_cgb_impl.cpp` is the only file that
  includes it without `WALNUT_GB_HEADER_ONLY`, so exactly one translation unit
  emits the implementation.  All other files go through `include/fe_walnut.h`.
* **`lib/minigb_apu_cardputer/minigb_apu.c`** includes `"../glue.h"`, which
  resolves to **`lib/glue.h`**.  Upstream's `glue.h` predates the current
  ESP32-S3 toolchain (it re-typedefs `int32_t`/`uint32_t`/`bool` over the
  system headers and `#define`s `bool` to `char`), so the copy here is:

  ```c
  #pragma once
  #include <stdint.h>
  #include <stdbool.h>
  ```

  This is the only deviation from upstream sources and it is required for the
  Arduino 3.x toolchain.

## Source of the extracted data

All large tables in `resources/` were extracted from `app_new.elf`:

| Table | Address | Size |
|---|---|---|
| 12-colour palettes | `0x3c0c6674` | 51 × 12 × RGB888 |
| SGB palettes | `0x3c0c7364` | 40 × 12 × RGB888 |
| GBC palettes | `0x3c0c7ae4` | 12 × 12 × RGB888 |
| SGB border halves | `0x3c0c...` | 4 × 40 × 135 RGB565 |
| DMG boot ROM | `dmg.h` | 256 bytes |

The 12-colour table has 51 entries because per-game profile indices reach
`0x32`; the firmware indexes it without a bounds check. The user-facing
palette cycle still wraps at 47 (`fe_div47_magic`), and the SGB preset cycle
wraps at 32 (`fe_sgb_palette_mask == 0x1f`) even though the SGB table has 40
entries.

## Compatibility notes

* **walnut-cgb ABI.**  The binary called the older six-argument `gb_init`
  (`gb`, `rom_read`, `cart_ram_read`, `cart_ram_write`, `gb_error`, `priv`).
  The bundled header exposes the current eight-argument API with separate
  16-bit and 32-bit ROM readers.  The reconstruction uses the current API;
  `gbRomRead16`/`gbRomRead32` are byte-wise readers in `rom_cache.cpp`.
* **Save states.**  The original wrote format version 4 using hard-coded
  offsets into the old `gb_s`.  The reconstruction keeps the `.qsv` naming and
  version byte but serialises the current struct by named members.  Files are
  therefore compatible with this build, not with the old binary.
* **SGB profile table.**  The 54 title entries and their SGB / 12-colour /
  GBC indices were decoded mechanically from the comparison chain in
  `fe_load_sgbprofile` and verified byte-for-byte against the firmware's title
  pointer table at `0x42000254`.
* **High ROM window.**  Special titles (Donkey Kong Land 2/3, Grand Theft Auto)
  are detected and set `shared_flags[4]`; Grand Theft Auto additionally stores
  the high-window base pointer at `shared_flags+8`.  The original preloaded a
  high ROM window from flash; the reconstruction allocates the equivalent
  window from PSRAM and loads sectors on demand.
* **Display scaling.**  Aspect ratios are 10:9 through 20:9 in steps of 1
  (`160,165,180,...,300` px destination width); widths above 240 are cropped
  to the panel centre, mirroring `fe_display_scale_blit`.  `10:9` uses the
  fast chunked push path, the others use the per-line scaler.  The vertical
  scale selector (`Fit to screen` / `Centered` / `Top` / `Bottom`) matches the
  four `fe_display_state+6` values.
* **Settings.**  `/gbeconfig.dat` version 5 is written and versions 0–5 are
  read.  CPU mode is runtime-only, as in the original; RESTORE DEFAULTS sets
  GFX INTERLACE = FULL, PALETTE START = Pocket, SGB BORDER = MANUAL and the
  runtime volume to `0x14`.
* **Audio.**  `apu_volume_convert` divides by `apu_volume_curve`
  (`0x422D3333` = 43.3f), mixes the stereo pair with `(L+R)/2`, shifts right by
  8 and clamps to −128..127 before adding the unsigned 8-bit midpoint
  (`0x80`), exactly as the firmware does.
* **gb_error.**  Messages are the firmware's `gb_error_messages` strings
  (`ERROR:UNKNOWN`, `ERROR:INVALID OPCODE`, …) shown with a `%04x` address and
  a 6.2 s delay.
* **SGB borders.**  `fe_sgb_border_left`/`_right` select by
  `fe_sgb_border_frame`: frame 0 is the stereo-TV border
  (`0x3c0be7e4` / `0x3c0bbdb4`) and frame 1 the Cardputer arcade border
  (`0x3c0c3c44` / `0x3c0c1214`).  The animation advances with
  `(frame + 1) % (frame_count + 2)`.
* **Startup display state.**  `fe_emulator_main` initialises
  `fe_display_state` with aspect index 6 (16:9 → 240 px wide), vertical scale
  0 and the 12-colour flag clear, which is what `emulatorMain()` reproduces.

## Address map

The reconstruction follows the Ghidra symbol names in `FUNCTION_MAP.md`.
Front-end code spans `0x42002b38`–`0x42013ce4`; the ROM cache lives at
`0x3fcba940` and the framebuffer at `0x3fc96ef0` in the original image.
