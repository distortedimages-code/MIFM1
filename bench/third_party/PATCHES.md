# Changes to the vendored code

`tools/patch_mi.sh` applies them after `tools/vendor_mi.sh`; each changed line carries an `MIFM1:` comment.

1. `plaits/dsp/dsp.h`, `rings/dsp/dsp.h`: `kSampleRate` (and Plaits' `kCorrectedSampleRate`) 44100 instead of
   48000 (Plaits' STM32F373 ran at 47872 Hz). The FM-1's codec runs at 44.1 kHz; the modules' tuning and times
   follow these constants.
2. `rings/dsp/part.cc`: `Part::Init(0)` leaves the reverb unset, so a part that will not play the "string and
   reverb" model does without its 64 KB buffer (the bench's bridge never selects that model then).
3. `plaits/user_data.h`: `#include <cstdio>` in its TEST branch, whose flash mock prints (its test makefile
   included it); nothing calls the mock here.
4. `marbles/resources.cc` (`dist_icdf_*`), `plaits/resources.cc` (`wav_integrated_waves`), `tides2/resources.cc`
   (`lut_wavetable`): `#ifdef PLAT_HOST` around these arrays (`tools/split_tables.py`). With them the six modules
   do not fit the FM-1's 581 KB of program flash, so on the FM-1 these 145 KB live in fm1-base's resource region
   (`bench/res/mi_tables.bin`, written by the installer after the firmware), and their symbols are the addresses
   where the region reads (`bench/mi/res_symbols.cc`, generated). The values are the same bytes: on a computer the
   arrays stay in the build and `make test` compares the region's file with them. No code or value is changed.
5. `warps/dsp/vocoder.h`: `kFollowerGain = 4.47213595f` instead of `sqrtf(kNumBands)` (kNumBands is 20: the same
   float). Computed, it is a static initializer, and fm1-base never runs those: unless the compiler folds it, the
   vocoder's gain would stay 0 on the FM-1 (`tools/no_static_init.py` checks every object).

Not changed, but used differently from the modules: Clouds (written for 32 kHz) and Warps (96 kHz) get the FM-1's
44.1 kHz frames as they are in the bench, which measures the work per frame; Tides 2 and Marbles are called at
44.1 kHz with their own rate constants (62.5 and 32 kHz).
