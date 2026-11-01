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
   - `lut_ap_poles`: the frequency shifter's allpass pairs (90 degrees from 10 Hz to 20 kHz);
   - `dsp/sample_rate_conversion_filters.h`, `dsp/modulator.h`: the cross-modulations oversample by 6 through a pair
     of filters made by the recipe written above them, `remez(48, [0, 0.06 / 6, 0.5 / 6, 0.5])`: at 96 kHz flat to
     5.8 kHz, -1 dB at 16 kHz, the images (from 48 kHz) at -68 dB. The same filters at 44.1 kHz would lose 17 dB at
     16 kHz. A pair for 44.1 kHz is added, `remez(72, [0, 12000 / (6 x 44100), 0.5 / 6, 0.5])`: flat to 12 kHz,
     -3.5 dB at 16 kHz, its images (from 22.05 kHz) at -52 dB; the modulator's two converters take it (48 -> 72
     taps). The 48-tap pair stays in the file, unused.
   Warps then runs whole at 44.1 kHz (`firmware/mi/mi_warps.cc`). 0.2.0 and 0.2.1 ran it at 88.2 kHz on the 96 kHz
   tables: twice the cost, too much for the FM-1.

6. `warps/dsp/modulator.h`, `modulator.cc`: the vocoder left out (`tools/warps_no_vocoder.py`): its member (64 KB of
   the modulator's 72), its `Init` and its `Process` call. MIFM1's ALGORITHM never reaches it (0..0.625 of the module's
   knob: the six cross-modulations); were it to, the output would be silent.

7. `warps/dsp/modulator.h` (`tools/warps_fast.py`): `ProcessXmod` plays two neighbouring algorithms and crossfades
   them by ALGORITHM's fraction; when the fraction is 0 over the whole block (on a mark: MIFM1's ALGORITHM selector
   jumps from mark to mark), it plays the first alone: the same samples (the second's weight is 0), half the
   algorithms' work at 6x. (0.3.5 and 0.3.6 tried to run the modulator's loops from RAM: the FM-1's clang has no
   `#pragma clang section`, and its 23-bit jumps do not reach between the flash and the RAM.)

Not changed, but used differently from the modules: Marbles and Tides 2 run at the control rate (Marbles 8 frames
per audio block, Tides one), their time constants given that rate; Clouds' granular processor (written for 32 kHz)
is not played yet (Rings, measured at v0.0, is no longer part of the firmware: Plaits' MODAL and STRING models
carry a lighter version of its code).
