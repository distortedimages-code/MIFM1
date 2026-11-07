#!/bin/sh
# SPDX-License-Identifier: MIT
# The changes made to the vendored Mutable Instruments code (firmware/third_party/PATCHES.md says why), after
# tools/vendor_mi.sh. Each one checks it found its line: a new upstream version that moved it stops here.
set -e
T="$(dirname "$0")/../firmware/third_party"
sub() {   # file, sed expression, the text that must be there after
    sed -i "$2" "$T/$1"
    grep -q "$3" "$T/$1" || { echo "patch_mi: $1: '$3' not found after the change"; exit 1; }
}
# 1. The FM-1 runs at 44.1 kHz: Plaits' rate constants (their tuning and times follow them)
sub plaits/dsp/dsp.h 's/static const float kSampleRate = 48000.0f;/static const float kSampleRate = 44100.0f;  \/\/ MIFM1: the FM-1/' 'kSampleRate = 44100.0f'
sub plaits/dsp/dsp.h 's/static const float kCorrectedSampleRate = 47872.34f;/static const float kCorrectedSampleRate = 44100.0f;  \/\/ MIFM1: the FM-1 codec/' 'kCorrectedSampleRate = 44100.0f'
# 2. Plaits' user data: its TEST build mocks the flash with printf and does not include <cstdio> (its test makefile
#    did); nothing calls the mock here, but it must compile
grep -q "MIFM1: the mock" "$T/plaits/user_data.h" || \
    sub plaits/user_data.h 's/^#ifdef TEST$/#ifdef TEST\n#include <cstdio>  \/\/ MIFM1: the mock below prints/' 'MIFM1: the mock'
# 3. The biggest tables (Marbles' distributions, Plaits' integrated wavetables, Tides' SHAPE curves, the FM drums', and
#    since 0.16.58 Warps' wavefolder: 263 KB) out of
#    the program flash into fm1-base's resource region: on the FM-1 their arrays are not compiled and their symbols
#    are the region's addresses (tools/split_tables.py, which also writes the region's file)
python3 "$(dirname "$0")/split_tables.py"
# 4. Warps' vocoder: a namespace-scope constant computed at start-up (sqrtf), a static initializer fm1-base never runs
#    (the FM-1's old clang may not fold it): its value written out, sqrtf(20) (tools/no_static_init.py checks)
sub warps/dsp/vocoder.h 's/^const float kFollowerGain = sqrtf(kNumBands);/const float kFollowerGain = 4.47213595f;  \/\/ MIFM1: sqrtf(kNumBands), kNumBands 20; no static initializer/' 'kFollowerGain = 4.47213595f'
# 5. Warps at the FM-1's 44.1 kHz instead of 96: the three tables that depend on the rate (the vocoder's filter bank,
#    the oscillators' note-to-increment table, the frequency shifter's allpass poles) made again by the module's own
#    scripts, ported (tools/warps_tables.py: it first checks that the port makes the vendored 96 kHz tables)
#    and the cross-modulations' 6x oversampling filters, by the recipe written above them, flat to 12 kHz (72 taps)
python3 -I "$(dirname "$0")/warps_tables.py" "$T/warps"
grep -q "MIFM1: at 44100 Hz" "$T/warps/resources.cc" || { echo "patch_mi: warps/resources.cc not at 44.1 kHz"; exit 1; }
grep -q "MIFM1: 72 taps" "$T/warps/dsp/modulator.h" || { echo "patch_mi: warps/dsp/modulator.h: 72 taps"; exit 1; }
# 6. Warps' vocoder out (MIFM1 plays its cross-modulations and its frequency shifter only): its 64 KB object, its Init and
#    its Process call (tools/warps_no_vocoder.py)
python3 -I "$(dirname "$0")/warps_no_vocoder.py" "$T/warps"
# 7. Warps lighter, the same sound: on a mark of ALGORITHM, one algorithm played instead of two crossfaded at a weight
#    of 0 (tools/warps_fast.py)
python3 -I "$(dirname "$0")/warps_fast.py" "$T/warps"
grep -q "MIFM1: one algorithm on a mark" "$T/warps/dsp/modulator.h" || { echo "patch_mi: warps: one algorithm"; exit 1; }
# 8. Warps' ECO mode: its cross-modulations oversampled 3 times instead of 6 (a second pair of converters, 3x at
#    44.1 kHz, by the module's recipe), half the work; MIFM1 turns it on (tools/warps_eco.py)
python3 -I "$(dirname "$0")/warps_eco.py" "$T/warps"
grep -q "MIFM1: ECO" "$T/warps/dsp/modulator.cc" || { echo "patch_mi: warps: ECO"; exit 1; }
echo "patched $T"
