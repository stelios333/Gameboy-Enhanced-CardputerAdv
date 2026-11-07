// Compatibility include for walnut-cgb.
//
// walnut_cgb.h is a single-header library: including it normally emits the
// whole emulator, so exactly one translation unit (walnut_cgb_impl.cpp) does
// that.  Every other translation unit includes this wrapper, which asks the
// header for declarations only.
//
// The bundled header has two quirks this wrapper works around:
//   * it emits __gb_step_cpu_x() after the header-only guard, and that
//     function needs the IO_* constants from inside the guard;
//   * gb_run_frame_dualfetch() is defined but not declared in the prototype
//     section.
#pragma once

#include <stdint.h>

extern "C" {
#include "minigb_apu.h"
}

struct gb_s;

#define WALNUT_GB_HEADER_ONLY

/* Constants used by the out-of-guard __gb_step_cpu_x() definition. */
#define IO_JOYP 0x00
#define IO_SB 0x01
#define IO_SC 0x02
#define IO_DIV 0x04
#define IO_TIMA 0x05
#define IO_TMA 0x06
#define IO_TAC 0x07
#define IO_IF 0x0F
#define IO_LCDC 0x40
#define IO_STAT 0x41
#define IO_SCY 0x42
#define IO_SCX 0x43
#define IO_LY 0x44
#define IO_LYC 0x45
#define IO_DMA 0x46
#define IO_BGP 0x47
#define IO_OBP0 0x48
#define IO_OBP1 0x49
#define IO_WY 0x4A
#define IO_WX 0x4B
#define IO_BOOT 0x50
#define IO_IE 0xFF

#define IO_TAC_RATE_MASK 0x3
#define IO_TAC_ENABLE_MASK 0x4

#define IO_STAT_MODE_HBLANK 0
#define IO_STAT_MODE_VBLANK 1
#define IO_STAT_MODE_OAM_SCAN 2
#define IO_STAT_MODE_LCD_DRAW 3
#define IO_STAT_MODE_VBLANK_OR_TRANSFER_MASK 0x1

/* Internal helpers called by the out-of-guard step function. */
uint16_t __gb_read16(struct gb_s *gb, uint16_t addr);
uint32_t __gb_read32(struct gb_s *gb, uint16_t addr);
uint8_t __gb_execute_cb(struct gb_s *gb);
void __gb_draw_line(struct gb_s *gb);

/* The out-of-guard definition would otherwise be duplicated in every TU. */
#define __gb_step_cpu_x __attribute__((weak)) __gb_step_cpu_x
/* walnut_cgb.h declares __gb_step_cpu() as static inline but only defines it in
 * the implementation TU, so header-only TUs warn about it. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "walnut_cgb.h"
#pragma GCC diagnostic pop
#undef __gb_step_cpu_x

/* Declared in the header but missing from the prototype block. */
void gb_run_frame_dualfetch(struct gb_s *gb);
