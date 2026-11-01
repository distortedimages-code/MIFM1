#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Warps' vocoder out of the modulator (MIFM1 plays its six cross-modulations and the frequency shifter only: the
# vocoder cost as much as the four voices). Its object took 64 KB of the modulator's 72: the member goes, its Init
# and its Process call with it; were the algorithm ever to reach it, the output is silent.
#   tools/warps_no_vocoder.py [warps/]   (default: firmware/third_party/warps/, rewritten once)
import os
import sys

root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'firmware', 'third_party',
                                                           'warps')
h = os.path.join(root, 'dsp', 'modulator.h')
c = os.path.join(root, 'dsp', 'modulator.cc')
hs, cs = open(h).read(), open(c).read()
if 'MIFM1: no vocoder' in hs and 'MIFM1: no vocoder' in cs:
    print('warps_no_vocoder: already done')
    sys.exit(0)
edits = [
    (h, '  Vocoder vocoder_;\n', '  // Vocoder vocoder_;  MIFM1: no vocoder (64 KB; not played)\n'),
    (c, '  vocoder_.Init(sample_rate);\n', '  // vocoder_.Init(sample_rate);  MIFM1: no vocoder\n'),
    (c, '''    vocoder_.set_release_time(release_time * (2.0f - release_time));
    vocoder_.set_formant_shift(parameters_.modulation_parameter);
    vocoder_.Process(modulator, carrier, main_output, size);
''', '''    (void)release_time;  // MIFM1: no vocoder: silence
    fill(&main_output[0], &main_output[size], 0.0f);
'''),
]
texts = {h: hs, c: cs}
for path, old, new in edits:
    if texts[path].count(old) != 1:
        print('warps_no_vocoder: %s: not found once: %r' % (path, old[:40]))
        sys.exit(1)
    texts[path] = texts[path].replace(old, new)
for path, text in texts.items():
    open(path, 'w').write(text)
print('warps_no_vocoder: done')
