// Single translation unit that emits the walnut-cgb implementation.
//
// walnut_cgb.h is a single-header library.  Every other translation unit
// includes fe_walnut.h, which defines WALNUT_GB_HEADER_ONLY.  This file is the
// one place where the full implementation is compiled.
#include <stdint.h>

extern "C" {
#include "minigb_apu.h"
}

#include "walnut_cgb.h"
