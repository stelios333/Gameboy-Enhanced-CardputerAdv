# Front-end verification notes

This file records the audit of the reconstructed front-end against the
compiled firmware `.elf` images.  The original audit used `../app_new.elf`
(MD5 `c611c36d58d123d5253d4c3e03de1046`).  The ROM cache / ROM-loading
functions were later re-audited against `../app2.elf` (SHA-256
`611b5df9f2dffbb7c5aefc18523090b369cfc3113dcbe8a14e0fc152578a48f8`), the
build *before* the Grand Theft Auto high-window experiment was armed (app2 has
the re-read/preload gate but no code that writes the high-window pointer).
Where the two builds differ, the reconstruction now follows `app2.elf`.  The ELF
is the source of truth: every entry below was checked against the disassembly
and/or the raw literal/rodata bytes, not against the older `gb_cardputer.ino`.

For `app2.elf` the Ghidra database was seeded with an FID database generated
from `app_new.elf`, so all matching functions kept their names; the four
cache/ROM-loading functions that did not match were identified by their
literal-pool references and renamed (`fe_read_rom_to_ram`,
`fe_gb_rom_read`, `fe_gb_rom_read16`, `fe_gb_rom_read32`).

## Method

* `FUNCTION_MAP.md` supplied the Ghidra names and address ranges.
* `xtensa-esp32-elf-objdump -d --start-address=... --stop-address=...` was used
  for functions where the Ghidra decompiler truncates or loses instruction
  alignment (`fe_file_picker`, `fe_settings_menu`, `fe_emulator_main`,
  `fe_gb_rom_read16/32`, the display task at `0x4200fb00`).
* A small reader (`mem.py`) read `.flash.text` (`0x42000020`),
  `.flash.rodata` (`0x3c070020`) and `.dram0` (`0x3fc95f00`) directly from the
  extracted section images.  Literal-pool entries were resolved to recover
  global addresses, colours, font divisors, delays and table pointers.
* The reconstruction was then built with `pio run` (PlatformIO 8.0.0,
  Arduino-ESP32 3.3.12, `m5stack-cardputer`) after the fixes below.

## Verified identical

These were checked byte-for-byte or instruction-for-instruction and need no
change.

| Area | Evidence |
|---|---|
| SGB palettes (`resources/sgb.h`) | 40 x 12 RGB888 entries at `0x3c0c7364`; exact match. |
| GBC palettes (`resources/gbc.h`) | 12 x 12 entries at `0x3c0c7ae4`; exact match. |
| 12-colour palettes (`resources/palettes_12color.h`) | First 51 x 12 entries at `0x3c0c6674`; exact match. |
| SGB title/profile table | All 54 16-byte titles at `0x3c0c7004`-`0x3c0c7364` and the decoded comparison chain match `src/sgb_profile.cpp`. |
| Config file `/gbeconfig.dat` v5 | Byte order in `settingsLoad`/`settingsSave` matches `fe_settings_load`/`fe_settings_save`. |
| Settings menu strings | 18 entries at `0x3c0705e4` match `kSettingsMenuItems`. |
| Aspect-ratio widths | Jump table at `0x3c07ec48`: `160,165,180,195,210,225,240,255,270,285,300`; matches `setAspectRatio`. |
| ROM cache geometry | 119 slots x 1 KiB at `0x3fcba940`, 4096-byte sector map at `0x3fcb9940`, `int[119]` sectors, `uint16[119]` ages, limit `0x1dbff`. |
| `fe_gb_rom_read` dispatch | `slot = map[addr >> 10]`; `slot < 0x77` is a cache hit (age is reset to `0xffff`), anything else (including `0xff`) goes to `fe_rom_cache_miss`.  No high-window branch in the readers; the loader/re-read gate at `shared_flags+8` exists but is never armed in `app2.elf`. |
| 16/32-bit ROM reads | `fe_gb_rom_read16`/`32` use an aligned loaded-sector fast path (`ages[slot] = 0xffff`, direct `l16ui`/`l32i`); they fall back to byte reads when the value would cross a 1 KiB sector boundary or the sector is not cached. |
| Cache miss / LRU | A miss ages all 119 slots starting at `next`, picks the lowest age, evicts it and leaves `next` unchanged.  The per-frame scan ages 29 slots (hardware loop count 29). |
| Message-box geometry | `box_x = 120 - w/2`, `box_y = (center_x + 0x43) - h/2`, `w = max(w1,w2)+2+padding`, `h = h1+h2+2+padding`, text at `y+padding+1` and `+line1_h`. |
| `rgb888_to_rgb565` | `0x42013ce4` uses the `0x80808081` reciprocal; it is exactly `floor(v*31/255)` / `floor(v*63/255)`, which is what the reconstruction already computes. |
| APU volume conversion | `0x4200e34c`: `(L+R)>>1`, multiply by `volume / 43.3f` (`0x422D3333`), `>>8`, clamp to -128..127, store as unsigned 8-bit with an `0x80` midpoint. |
| SGB border selection | Frame 0 = stereo-TV halves (`0x3c0be7e4`/`0x3c0bbdb4`), frame 1 = Cardputer halves (`0x3c0c3c44`/`0x3c0c1214`). |
| Display task | `0x4200fb00`: `if (fe_display_busy) { fit_frame(); fe_display_busy = 0; } vTaskDelay(6);` while `fe_app_running`. |
| `gb_error` | Messages at `0x3c0702a4`; red address/white message box; 6.2 s delay (`0x5E9AC0` us). |

## Confirmed divergences fixed

### `src/main.cpp`

* `fe_setup` calls `setRotation(1)`.  The current M5GFX Cardputer panel has a
  `rotation_offset` of 1, so `setRotation(0)` in the reconstruction produces
  the same effective landscape orientation as the firmware's
  `setRotation(1)` did with its panel definition.
* It copies the panel bus config, writes `0x04C4B400` (80 MHz write) and
  `0x01312D00` (20 MHz read) and reconfigures the bus.  The reconstruction
  now calls `panel->getBus()->setClock(80000000)` /
  `setReadClock(20000000)`.
* **Framebuffer byte order.**  `fe_display_push_image` (`0x4200f8ac`)
  constructs an LGFX `pixelcopy_t` with `src_depth = rgb565_nonswapped`
  (`0x110`) and `dst_depth = display+0x50` (`rgb565_2Byte`), so LGFX
  byte-swaps the native RGB565 framebuffer as it writes to the panel.  The
  templated `pushImage(uint16_t*)` overload keys off `_swapBytes`, which is
  `false` by default in this M5GFX version; it therefore treated the
  framebuffer as already swapped and performed no conversion, producing
  byte-swapped/wrong colours in every palette mode.  `setup()` now calls
  `Display.setSwapBytes(true)`, which reproduces the firmware's
  `rgb565_nonswapped -> rgb565_2Byte` pixelcopy for the framebuffer, scaled
  output, and both SGB border halves.
* `setFontSize(64)` in `setup()` is not present in `fe_setup` and was removed;
  the splash sets its own sizes.

### `src/ui.cpp`

* `fe_message_box_draw` fills with `rgb888_to_rgb565(0x00666666)`
  (`0x4200011c`), not `0x2104`.  The white outline was already correct.

### `src/emulator_main.cpp`

* **Splash.**  The firmware fades the title in from black to white with a
  `0x00010101` step until `0x01010100` (256 steps; 1 tick + 5 ms each), shows
  the version `"ver %d.%d.%02d"` with args `0, 0x4F, 0x42` for 2.5 s, then
  fades out with a `0xFFFDFDFE` step until the running colour wraps to
  `0xFFFEFEFF` (128 steps, no delay inside the loop).  The old reconstruction
  used a `0x111111` step (about 17x too fast) and faded out with a delay.
* **Forced compatibility.**  `fe_emulator_main` clears
  `fe_emulator_status` bit `0x02` and sets the old core's `gb_s+9` byte to 1.
  The reconstruction now clears `kStatusCompat` (`0x02`) instead of the old
  `0x01` bit and no longer clears `cgbMode`.  The current core has no
  `gb_s+9` equivalent, so `forceCompat` only carries the compatibility state;
  the per-frame core choice is the firmware's fast flag alone (above).
* **Per-frame core choice.**  `0x420125dd`: `p_fe_settings+7 != 0` calls
  `wcgb_gb_run_frame` (`__gb_step_cpu_x`), otherwise
  `wcgb_gb_run_frame_dualfetch` (`__gb_step_cpu`).  That flag is the CPU-MODE
  `FASTEST` state; `fe_settings_load` never rewrites it, so it stays at its
  init value of 1.  The reconstruction now selects on
  `cpuMode == CpuMode::Fastest` (FAST/COMPATIBLE use the dual-fetch core),
  exactly matching the firmware.
* **Volume default.**  `fe_emulator_main` copies the VOLUME START byte
  straight into `apu_global_volume`; a fresh config leaves it at 0 (off).
  The old fallback to `0x14` was removed.
* **Menu item count.**  `fe_settings_menu` checks `fe_app_running`, so the
  EXIT GAME entry exists only while a game is running (18 vs 17 items).  A
  new `gInGame` flag tracks this instead of `gPriv.rom`.
* **Picker bookkeeping.**  After the picker the firmware stores
  `(last_name_byte & 0xDF) == 'B'` in `shared_flags[2]` and clears the u32 at
  `shared_flags+8` (`s32i zero, shared_flags, 8` at `0x42011ebc`).  The
  reconstruction does both.
* **Vestigial high-window re-read.**  `app2.elf` still contains the immediate
  re-read at `0x4201216e`: when `shared_flags+8` is non-null it closes the ROM,
  calls `fe_read_rom_to_ram` again and keeps the new pointer.  Its loader also
  has the per-sector preload branch gated on the same u32 (`0x4200f617`).
  Unlike `app_new.elf`, no profile in this build writes that pointer (the GTA
  entry only sets `shared_flags[4]`) and the picker clears it, so the branch is
  unreachable.  The reconstruction keeps the re-read and preload for fidelity,
  but allocates no high window and ships no `resources/rom_mmap.h`.
* **"Locked ON" message.**  Line 2 uses colour `0x0000FFE0`, not white.
* **Volume hotkeys.**  The audio task reads `gApuVolume`, but the hotkey
  handler only updated `gVolume`, so `;`/`.`/`/`/`,` never changed the audible
  level.  All four hotkeys now update both variables (the firmware writes
  `apu_global_volume` directly).
* **Palette mapping.**  `0x420120c9` maps PALETTE START 0 to B&W
  (`0x3fc95f30`), 1 to original DMG (`0x3fc95f20`), 2 to the
  `fe_palette_default` table (`0x3fc95f10`), and 3 to the
  `fe_palette_pocket` table (`0x3fc95f00`).  The on-screen labels are
  "Gameboy Pocket" for index 2 and "Gameboy Light" for index 3, so the
  firmware's `fe_palette_default`/`fe_palette_pocket` symbol names do not
  match the labels.  The old code returned the `0x3fc95f00` table for both 2
  and 3; index 2 now returns `0x3fc95f10`, matching the firmware.  (The same
  four-way chain is used by the `]` preset cycle and by the initial palette
  after `fe_settings_load`.)
* **Adaptive frame pacing.**  The main loop previously slept for the whole
  remaining 16675 us with a busy-wait.  `fe_emulator_main`
  (`0x42012638`-`0x420127ef`) instead derives a signed budget
  `(int)(16675.0 - elapsed) + sleep_accum`, spends at most 75 % of it with
  `usleep` (which yields to the other tasks), carries the remainder into the
  next pass, and runs an extra undisplayed frame when the budget drops below
  -16675.  The implementation now follows that algorithm exactly, including
  the `0.8` catch-up credit and the `-50025` accumulator floor.

### `src/settings.cpp`

* **Menu chrome.**  Header/footer are filled with `rgb888(0x00032303)`
  (10 px and 12 px high), with a black shadow at `(3,0)`/`(3,124)`, a white
  copy at `(4,1)`/`(4,125)`, and white rules at `y=11` and `y=123`.
* **Carousel.**  The selected entry is centred at text size 2 (font divisor
  `0x3c`) on a `rgb888(0x111111)` highlight in white; the previous/next
  entries use text size 1 (divisor `0x60`), `rgb888(0xAAAAAA)`, and sit
  `+/-3` line heights from the centre.  A selected line wider than the
  240 px panel scrolls as a marquee: it starts at `x = 5`, moves two pixels
  every 50 ms, and bounces between `x = 5` and `x = 235 - width`.
* **VOLUME START.**  `0x42010144`: if the value is `> 126` wrap to 0, else
  add 25 and clamp to 127 (the old code added `0x10` with a modulo mask).
* **I2C Joystick.**  `0x420100f4`: `p_fe_settings+6 ^= 1` (the old code
  forced 0/2).
* **CPU MODE.**  `0x4201002d` and `0x42004626` use two bytes:
  `fe_startup_palette` (`0x3fcb121c`) and the fast flag at
  `p_fe_settings+7`.  The three states are FAST `(0,0)`, FASTEST `(0,1)`,
  COMPATIBLE `(1,0)`; the display is COMPATIBLE when startup palette is
  non-zero, otherwise FASTEST/FAST from the fast flag.  There is no
  "locked" guard in the handler.
* **RESTORE DEFAULTS.**  Sets interlace FULL, palette index 2, SGB border
  MANUAL, SGB auto 0, bass 0/`0x4b`, runtime volume `0x14`; it does not
  touch I2C, CPU mode or the startup compatibility flag.  (The comment was
  corrected: index 2 is displayed as "Pocket" but is the `PalettePreset::
  Default` slot.)

### `src/file_picker.cpp`

* The picker uses the same header/footer chrome as the settings menu.
* It is a five-entry carousel: the selected entry at text size 2 on a
  `0x111111` highlight, `+/-1` entries three line heights out and `+/-2`
  entries five line heights out at text size 1.
* The selected entry uses the same marquee as the settings menu: `+/-2` px
  every 50 ms between `x = 5` and `x = 235 - width`, with the old frame
  erased in the `0x111111` highlight colour.
* Directory entries are yellow (`0xFFFF00` selected, `0xAAAA00` adjacent);
  files are white / `0xAAAAAA`.
* Navigation accepts the configured up/down keys as well as `;` and `.`
  (`fe_file_picker` reads the same key globals).

### `src/marquee.cpp` and `include/fe_marquee.h`

* New shared marquee used by both menus.  The firmware keeps the current
  offset, the limit `width / 2 - 115` and a direction flag; a frame repaints
  the old glyphs in the `0x111111` highlight colour, moves two pixels, and
  redraws in the entry colour (white/grey, or yellow for directories).  The
  offset bounces between `+limit` and `-limit`, so long text travels from
  `x = 5` to `x = 235 - width`.  Frames are spaced by 50 ms
  (`0x4201060b` / `0x42011c27`, `0xC350` us).  Short entries leave the
  marquee idle.

### `src/rom_cache.cpp` (re-audited against `app2.elf`)

The previous code mixed the `app_new.elf` Grand Theft Auto high-window
experiment into the cache.  The high-window reads and the second cache base
are not present in `app2.elf`; only the dormant preload gate survives.  The
following were corrected:

* **High-window reads removed.**  `fe_gb_rom_read` (`0x42003144`),
  `fe_gb_rom_read16` (`0x4200318c`), `fe_gb_rom_read32` (`0x420031fc`) and
  `fe_read_rom_to_ram` (`0x4200f504`) have no `0x77`+ branch to a second ROM
  image.  Any sector-map value >= `0x77` (including `0xff`) is a cache miss.
  `resources/rom_mmap.h` was deleted; `0x3c07edb4` is SGB border data in this
  build, not a sector map.  `app_new.elf`'s high window indexed an embedded
  GTA image at `0x3c09ddb4` (a different table), and `app2.elf` contains
  neither.
* **Correct miss algorithm.**  `fe_rom_cache_miss` (`0x420030b4`) ages and
  scans **all 119** slots (hardware `loop` count 119) starting at
  `fe_rom_cache_next`, picks the lowest age and evicts it.  Because 119 steps
  return to the start, `next` is not advanced by a miss.  The old code aged
  only one round-robin slot.
* **Correct per-frame scan.**  `fe_rom_cache_lru_scan` (`0x42003288`) ages
  **29** slots per frame (`kCacheScanSlots`), not one, and only updates
  `min_age`/`victim` when it finds a lower age.  `fe_emulator_main` calls it
  once per emulated frame (`0x4201255a`).
* **Correct loader bookkeeping.**  The normal path in `fe_read_rom_to_ram`
  reads `min(file_size, 0x1dc00)` bytes and then maps **all 119** slots
  (`sectors[i] = i`, `sectorMap[i] = i`), clears the 119 ages, clears the six
  `fe_rom_stat0..5` counters and resets `next = victim = 0`,
  `min_age = 0x7fffffff`.  The old code mapped only `ceil(size / 1024)`
  entries.  The dormant `shared_flags+8` branch is still mirrored: it
  preloads the first 119 *unmapped* sectors one at a time, exactly like the
  firmware.  `app2.elf` only ever stores zero there (picker clear), so the
  branch cannot run; `app_new.elf` is the build whose GTA profile made it
  live by writing the embedded-image pointer.
* **16/32-bit fast paths.**  `fe_gb_rom_read16`/`32` now do the aligned
  sector-cache load the firmware does, including the `ages[slot] = 0xffff`
  hit and the byte-wise fallback when the value crosses a 1 KiB boundary or
  the sector is not cached.  `fe_gb_rom_read32`'s slow path is
  `read16(addr) | read16(addr+2) << 16`.
* **Scratch allocator.**  `fe_rom_scratch_alloc` (`0x42003034`) does not
  align its bump cursor; the reconstruction now matches.
* **Error dialogs.**  `fe_read_rom_to_ram` shows the firmware's red "ERROR"
  message box for `Cannot open file`, `File size is 0` and `Unexpected EOF`,
  and holds it for `0x5E9AC0` us (6.2 s).

### `src/rom_cache.cpp` (previous audit notes)

### `src/audio_task.cpp`

* Pause/volume selection now follows `apu_task_body` (volume byte, paused
  when `shared_flags[1]` is set).
* `playRaw` is called with length `0x224`, 8-bit unsigned, 32768 Hz,
  `channel = 0`, repeat = 1.  Unlike the firmware it passes **stereo =
  false**, because `apu_volume_convert` produces a mono buffer (see the
  firmware-bug note below).  This restores the intended ~16.7 ms per chunk.
* **PCM buffers.**  `Speaker_Class::playRaw()` only publishes a request to
  the playback task; it does not wait for the buffer to finish.  The old
  two-buffer alternation therefore refilled the buffer that was still
  playing, corrupting roughly half of every chunk.  The M5Unified version in
  this build documents that three rotating buffers are required unless the
  buffer-release callback is used; the task now uses three.

### Speaker configuration (`src/emulator_main.cpp`)

`fe_emulator_main` (`0x4201225a`-`0x420122c3`) copies the board's speaker
config and overrides exactly five fields before `begin()`:
`sample_rate = 32768`, `stereo = 0`, `magnification = 1`,
`dma_buf_len = 548`, `dma_buf_count = 4`, then master volume 255.  The
reconstruction previously omitted the magnification override (the board
default is 16, i.e. 16x / +24 dB louder and clipping) and wrote
`dma_buf_len = 4` (about 0.1 ms per DMA buffer, starving the playback task
and producing constant clicks).  Both now match the firmware, and the
unnecessary `task_priority = 4` override was removed (the firmware leaves the
board priority of 2 in place).

## Remaining known deviations

These are not byte-identical, either because the firmware data cannot be
shipped or because the behaviour is expressed through a different library
ABI.

1. **High window / embedded GTA ROM (app_new only).**  `app_new.elf` embeds a
   ~119 KiB ROM image at `0x3c09ddb4` and its Grand Theft Auto profile writes
   that address to `shared_flags+8`, making the loader's high-window path
   live.  `app2.elf` — the build this reconstruction targets — contains
   neither the image nor that profile write, and its cache readers have no
   high-window branch.  Nothing needs to be shipped for `app2`; reproducing
   the `app_new` behaviour would require adding the image, the `0x3c07edb4`
   map and the profile pointer write.
2. **Save states.**  The original used version 4 with hard-coded offsets
   into the old `gb_s`; the reconstruction serialises the current struct by
   named members.  `.qsv` files are compatible with this build, not with the
   old binary (already documented in `README.md`).
3. **Panel bus configuration.**  `fe_setup` copies a full `Bus_SPI::config_t`,
   sets one extra byte (offset 27) and calls `config()`.  The reconstruction
   uses the public `setClock`/`setReadClock` calls, which update the two
   frequency fields but not the extra byte.  The effective write/read clocks
   are the same.
4. **I2C joystick default.**  The persisted byte is `0x3fcb121e`, while the
   menu handler toggles the runtime flag at `p_fe_settings+6`; the firmware
   clears the persisted byte at startup but the fresh-install value of the
   runtime flag was not conclusively resolved.  The reconstruction keeps the
   previously reconstructed default of 2.  Both toggle correctly with
   `^= 1`.
5. **walnut-cgb ABI.**  The binary used the older six-argument `gb_init`
   (`gb`, ROM read, cart-RAM read/write, error, priv) and a `gb_s+9`
   compatibility byte.  The current header uses eight arguments and has no
   such byte; the reconstruction keeps `startupPalette`/`forceCompat` as the
   compatibility state but has no core-level equivalent for it.  The
   per-frame dispatcher choice itself is unaffected (it uses the fast flag
   exactly as the firmware does).
6. **File picker scratch allocator.**  The firmware uses the ROM-cache base
   as a bump allocator (`fe_rom_scratch_alloc`, 0x1000 units); the
   reconstruction uses `std::vector<String>`.  User-visible behaviour is the
   same, memory use is not.
7. **`too long - cropped` message.**  The firmware shows a message box with
   the typo `"dDrectory listing"` (both strings are present at `0x3c070912`
   and `0x3c070925`) when a directory path is cropped.  The reconstruction
   relies on `snprintf` truncation and does not show it.

## Original-firmware bugs found

* **Audio length/stereo mismatch (fixed).**  `apu_volume_convert` produces
  548 mono bytes from 548 stereo APU frames (16.7 ms at 59.94 Hz), but
  `apu_task_body` passes `stereo = 1` to `_play_raw`.  The speaker task then
  consumes two bytes per output frame, so only 274 frames (about 8.4 ms at
  32768 Hz) are played per period, at twice the intended rate and with the
  mono samples split across the stereo channels.  The reconstruction now
  passes `stereo = false`, trading byte-for-byte fidelity for working audio;
  this is a deliberate deviation from the firmware's bug.
* **Embedded ROM image (app_new only).**  In `app_new.elf` the GTA
  high-window data at `0x3c09ddb4` is a complete ROM image in `.rodata`, not a
  cache window.  `app2.elf` does not contain it (its `.flash.rodata` ends at
  `0x3c09888c`), so this is a note about the later build, not the target.
* **Force-compat bookkeeping.**  The firmware uses `fe_emulator_status` bit
  `0x02` for the forced-compatibility state and bit `0x01` for the
  interlace state; the earlier reconstruction had these swapped.
* **Typo.**  `"dDrectory listing"` at `0x3c070925` (the correctly spelled
  copy at `0x3c070937` is unused by the picker message).

## Build

`pio run` succeeds after these changes:

```
RAM:   77.3% (253284 / 327680 bytes)
Flash: 35.0% (1170655 / 3342336 bytes)
```
