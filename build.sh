#!/usr/bin/env bash
#
# Build the reconstructed front-end in place.
#
# Uses the bundled copies under lib/:
#   lib/Walnut-CGB/            walnut_cgb.h (single-header emulator core)
#   lib/minigb_apu_cardputer/  minigb_apu.c/.h (+ toolchain-compatible glue.h)
#
# PlatformIO's build directory (.pio) is deliberately kept between runs so that
# only changed translation units are recompiled.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

exec pio run "$@"
