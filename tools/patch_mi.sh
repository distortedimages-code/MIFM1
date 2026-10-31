#!/bin/sh
# SPDX-License-Identifier: MIT
# The changes made to the vendored Mutable Instruments code (bench/third_party/PATCHES.md says why), after
# tools/vendor_mi.sh. Each one checks it found its line: a new upstream version that moved it stops here.
set -e
T="$(dirname "$0")/../bench/third_party"
sub() {   # file, sed expression, the text that must be there after
    sed -i "$2" "$T/$1"
    grep -q "$3" "$T/$1" || { echo "patch_mi: $1: '$3' not found after the change"; exit 1; }
}
# 1. The FM-1 runs at 44.1 kHz: Plaits' and Rings' rate constants (their tuning and times follow them)
sub plaits/dsp/dsp.h 's/static const float kSampleRate = 48000.0f;/static const float kSampleRate = 44100.0f;  \/\/ MIFM1: the FM-1/' 'kSampleRate = 44100.0f'
sub plaits/dsp/dsp.h 's/static const float kCorrectedSampleRate = 47872.34f;/static const float kCorrectedSampleRate = 44100.0f;  \/\/ MIFM1: the FM-1 codec/' 'kCorrectedSampleRate = 44100.0f'
sub rings/dsp/dsp.h 's/static const float kSampleRate = 48000.0f;/static const float kSampleRate = 44100.0f;  \/\/ MIFM1: the FM-1/' 'kSampleRate = 44100.0f'
# 2. Rings without its reverb's 64 KB when the "string and reverb" model is not wanted (the bench's busy scenes):
#    Part::Init(0) leaves the reverb unset; the bridge never selects that model then (mi_rings.cc)
sub rings/dsp/part.cc 's/^  reverb_.Init(reverb_buffer);/  if (reverb_buffer) reverb_.Init(reverb_buffer);  \/\/ MIFM1: 0 = no reverb/' 'if (reverb_buffer) reverb_.Init'
# 3. Plaits' user data: its TEST build mocks the flash with printf and does not include <cstdio> (its test makefile
#    did); nothing calls the mock here, but it must compile
grep -q "MIFM1: the mock" "$T/plaits/user_data.h" || \
    sub plaits/user_data.h 's/^#ifdef TEST$/#ifdef TEST\n#include <cstdio>  \/\/ MIFM1: the mock below prints/' 'MIFM1: the mock'
echo "patched $T"
