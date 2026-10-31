# Changes to the vendored code

`tools/patch_mi.sh` applies them after `tools/vendor_mi.sh`; each changed line carries an `MIFM1:` comment.

1. `plaits/dsp/dsp.h`, `rings/dsp/dsp.h`: `kSampleRate` (and Plaits' `kCorrectedSampleRate`) 44100 instead of
   48000 (Plaits' STM32F373 ran at 47872 Hz). The FM-1's codec runs at 44.1 kHz; the modules' tuning and times
   follow these constants.
2. `rings/dsp/part.cc`: `Part::Init(0)` leaves the reverb unset, so a part that will not play the "string and
   reverb" model does without its 64 KB buffer (the bench's bridge never selects that model then).
3. `plaits/user_data.h`: `#include <cstdio>` in its TEST branch, whose flash mock prints (its test makefile
   included it); nothing calls the mock here.

Not changed, but used differently from the modules: Clouds (written for 32 kHz) and Warps (96 kHz) get the FM-1's
44.1 kHz frames as they are in the bench, which measures the work per frame; Tides 2 and Marbles are called at
44.1 kHz with their own rate constants (62.5 and 32 kHz).
