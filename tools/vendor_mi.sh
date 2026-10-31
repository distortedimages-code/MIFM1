#!/bin/sh
# SPDX-License-Identifier: MIT
# Copies the parts of Mutable Instruments' code this firmware uses into firmware/third_party/ (MIT, Emilie Gillet),
# from a checkout of https://github.com/pichenettes/eurorack with its stmlib submodule:
#   tools/vendor_mi.sh /path/to/eurorack
# Pinned: eurorack f4cfa7697095a28d292bff758beb51deb49b5a86, stmlib e3bd7c9cc00e4364166f9905c0509b6ffd0535ec.
# The few changes made to the copies are listed in firmware/third_party/PATCHES.md (tools/patch_mi.sh applies them).
set -e
SRC="$1"
DST="$(dirname "$0")/../firmware/third_party"
[ -f "$SRC/plaits/dsp/voice.cc" ] && [ -f "$SRC/stmlib/stmlib.h" ] || { echo "usage: $0 EURORACK_CHECKOUT (with stmlib)"; exit 1; }
rm -rf "$DST/stmlib" "$DST/plaits" "$DST/clouds" "$DST/warps" "$DST/tides2" "$DST/marbles"
mkdir -p "$DST"
cp "$SRC/stmlib/LICENSE" "$DST/LICENSE-stmlib"
copy() { mkdir -p "$DST/$(dirname "$1")"; cp -R "$SRC/$1" "$DST/$1"; }
copy stmlib/stmlib.h; copy stmlib/dsp; copy stmlib/utils; copy stmlib/fft
copy plaits/dsp; copy plaits/resources.cc; copy plaits/resources.h; copy plaits/user_data.h
copy clouds/dsp; copy clouds/resources.cc; copy clouds/resources.h; copy clouds/drivers/debug_pin.h
copy warps/dsp; copy warps/resources.cc; copy warps/resources.h; copy warps/drivers/debug_pin.h
for f in poly_slope_generator.cc poly_slope_generator.h ramp_generator.h ramp_shaper.h resources.cc resources.h ramp/ratio.h; do copy tides2/$f; done
copy marbles/random; copy marbles/ramp; copy marbles/resources.cc; copy marbles/resources.h; copy marbles/scale_recorder.h
# Marbles' preset scales: only that table of marbles/settings.cc (the rest is the module's flash storage), with its
# licence header, as marbles/preset_scales.h
{ sed -n '1,/^\/\/ Settings storage\./p' "$SRC/marbles/settings.cc" | sed '$d'
  printf '%s\n' "// Marbles' six preset scales, from marbles/settings.cc (only this table: the rest of that file is the module's" \
    "// flash storage). MIFM1: extracted by tools/vendor_mi.sh." "" "#ifndef MARBLES_PRESET_SCALES_H_" \
    "#define MARBLES_PRESET_SCALES_H_" "" '#include "marbles/random/quantizer.h"' "" "namespace marbles {" ""
  sed -n '/^const Scale preset_scales\[6\] = {/,/^};/p' "$SRC/marbles/settings.cc" | sed 's/^const Scale/static const Scale/'
  printf '%s\n' "" "}  // namespace marbles" "" "#endif  // MARBLES_PRESET_SCALES_H_"
} > "$DST/marbles/preset_scales.h"
# what is not built here: python generators, tests, unused stmlib parts
find "$DST" -name '*.py' -delete
find "$DST" -name '*.pyc' -delete
rm -rf "$DST/stmlib/fft/kiss_fft"* "$DST/stmlib/utils/murmurhash3.h" 2>/dev/null || true
echo "vendored into $DST"
