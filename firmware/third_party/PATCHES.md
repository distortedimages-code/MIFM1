# Changes to the vendored code

`tools/patch_mi.sh` applies them after `tools/vendor_mi.sh`; each changed line carries an `MIFM1:` comment.

1. `plaits/dsp/dsp.h`: `kSampleRate` and `kCorrectedSampleRate` 44100 instead of 48000 (Plaits' STM32F373 ran at
   47872 Hz). The FM-1's codec runs at 44.1 kHz; Plaits' tuning and times follow these constants.
2. `plaits/user_data.h`: `#include <cstdio>` in its TEST branch, whose flash mock prints (its test makefile
   included it); nothing calls the mock here.
3. `marbles/resources.cc` (`dist_icdf_*`), `plaits/resources.cc` (`wav_integrated_waves`), `tides2/resources.cc`
   (`lut_wavetable`): `#ifdef PLAT_HOST` around these arrays (`tools/split_tables.py`). With them the six modules
   do not fit the FM-1's 581 KB of program flash, so on the FM-1 these 145 KB live in fm1-base's resource region
   (`firmware/res/mi_tables.bin`, written by the installer after the firmware), and their symbols are the addresses
   where the region reads (`firmware/mi/res_symbols.cc`, generated). The values are the same bytes: on a computer the
   arrays stay in the build and `make test` compares the region's file with them. No code or value is changed.
4. `warps/dsp/vocoder.h`: `kFollowerGain = 4.47213595f` instead of `sqrtf(kNumBands)` (kNumBands is 20: the same
   float). Computed, it is a static initializer, and fm1-base never runs those: unless the compiler folds it, the
   vocoder's gain would stay 0 on the FM-1 (`tools/no_static_init.py` checks every object).

5. `warps/resources.cc`: the tables that depend on Warps' rate made for 44.1 kHz instead of 96 kHz, by
   `tools/warps_tables.py`, a Python 3 port of the module's own `warps/resources/filter_bank.py` and
   `lookup_tables.py` (it first checks that it makes the vendored 96 kHz tables, to float precision):
   - the vocoder's filter bank (`fb_*`, `filter_bank_table`): the same 20 bands (87 Hz to 7 kHz, third of an octave
     apart), the same filters and the same rule for their groups (decimated by 12, by 3, or not: each band under 0.35
     of its group's Nyquist), which at 44.1 kHz gives 9, 6 and 5 bands instead of 13, 6 and 1. The arrays keep their
     names (`fb_1397_8000` is now the 1397 Hz band at 14700 Hz), the comment on each says what it is. Two things the
     script fitted at 96 kHz are kept as what they do: the high pass's gain (its pass band, measured against the
     96 kHz design: 4.97 instead of 21 x its frequency) and the last band's 4 extra samples of delay (as a time);
   - `lut_midi_to_f_high`: the oscillators' note to frequency;
   - `lut_ap_poles`: the frequency shifter's allpass pairs (90 degrees from 10 Hz to 20 kHz).
   The vocoder and the frequency shifter run at 44.1 kHz on them; the cross-modulations at 88.2 kHz (their 6x
   oversampling filters are cut at a fixed part of the rate: at 44.1 kHz they would pass only ~8 kHz), the
   oscillators an octave down there (`firmware/mi/mi_warps.cc`).

Not changed, but used differently from the modules: Marbles and Tides 2 run at the control rate (Marbles 8 frames
per audio block, Tides one), their time constants given that rate; Clouds' granular processor (written for 32 kHz)
is not played yet (Rings, measured at v0.0, is no longer part of the firmware: Plaits' MODAL and STRING models
carry a lighter version of its code).
